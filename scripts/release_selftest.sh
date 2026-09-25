#!/usr/bin/env bash
# release.py's own test: archives are byte-reproducible and carry data/, what ships passes the gate
# and smoke while a broken plugin fails them, and a channel's index and manifest cover exactly the
# pinned plugin releases and refuse an incomplete set, and staging never replaces a released file.
#
#   scripts/release_selftest.sh
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

if ! ./build.sh stub release > /dev/null; then
    echo "release selftest: the build failed; run ./build.sh stub release to see why" >&2
    exit 1
fi

release=scripts/release.py
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

passed=0
failed=0

# check <what it proves> <pass|fail> <substring the output must contain> <command...>
check() {
    local label="$1" expected="$2" needle="$3"
    shift 3
    local output status=0
    output="$("$@" 2>&1)" || status=$?

    if [[ $expected == pass && $status -eq 0 || $expected == fail && $status -ne 0 ]] && [[ $output == *"$needle"* ]]; then
        echo "ok   - ${label}"
        passed=$((passed + 1))
    else
        echo "FAIL - ${label} (exit ${status}, expected to ${expected} saying '${needle}')"
        sed 's/^/       /' <<< "$output"
        failed=$((failed + 1))
    fi
}

listing() {
    zstd -d -q -c "$1" | tar -tv --numeric-owner | awk '{print $1, $2, $6}'
}

check "every roster pins plugins in this repository" pass "stub 1.0.0" "$release" roster dev
check "stable has a roster"                         pass "stub 1.0.0" "$release" roster stable
check "a channel with no roster is refused"         fail "no roster" "$release" roster nope
check "a plugin tag names the plugin and version"   pass "vamiga 1.2.0" "$release" tag vamiga/v1.2.0
check "a channel tag is not a plugin tag"           fail "is not <plugin>/vX.Y.Z" "$release" tag dev/v3
check "a tag naming no plugin here is refused"      fail "not a plugin in this repository" "$release" tag nope/v1.0.0

# A copy of this repository's scripts and stub, which release.py reads as its repository, for the
# ways the source tree itself can be wrong.
fake="$scratch/repo"
mkdir -p "$fake/scripts" "$fake/plugins" "$fake/channels/bad" "$fake/channels/test"
cp scripts/release.py scripts/publish_gate.py scripts/check_abi_floor.sh "$fake/scripts/"
ln -s "$repo_dir/sdk" "$fake/sdk"
cp -r plugins/stub "$fake/plugins/"
printf '[plugins]\nstubb = "1.0.0"\n' > "$fake/channels/bad/roster.toml"
check "a roster pinning no plugin here is refused" fail "not a plugin in this repository" "$fake/scripts/release.py" roster bad
printf '[plugins]\nstub = "1.0"\n' > "$fake/channels/bad/roster.toml"
check "a roster pin that is not X.Y.Z is refused" fail "is not a version" "$fake/scripts/release.py" roster bad
printf 'plugins = ["stub"]\n' > "$fake/channels/bad/roster.toml"
check "a roster listing plugins without versions is refused" fail "" "$fake/scripts/release.py" roster bad

# The stub as built, and a copy of it with a data/ folder, different times and different modes.
first="$scratch/first"
second="$scratch/second"
mkdir -p "$first/plugins/stub/data/sub"
cp build/release/plugins/stub/stub.so "$first/plugins/stub/"
printf 'engine data' > "$first/plugins/stub/data/sub/engine.dat"
cp -r "$first" "$second"
touch -d '2001-02-03' "$second/plugins/stub/stub.so" "$second/plugins/stub/data/sub/engine.dat"
chmod 600 "$second/plugins/stub/data/sub/engine.dat"

check "a plugin packs" pass "stub-linux-x86_64.tar.zst" \
    "$release" pack --build-dir "$first" --target x86_64 --out "$scratch/packed-first" stub
"$release" pack --build-dir "$second" --target x86_64 --out "$scratch/packed-second" stub > /dev/null
check "times and modes do not reach the archive" pass "" \
    cmp "$scratch/packed-first/stub-linux-x86_64.tar.zst" "$scratch/packed-second/stub-linux-x86_64.tar.zst"

expected_listing="-rw-r--r-- 0/0 Stub.json5
drwxr-xr-x 0/0 data/
drwxr-xr-x 0/0 data/sub/
-rw-r--r-- 0/0 data/sub/engine.dat
-rwxr-xr-x 0/0 stub.so"
check "the archive holds the .so, the template and data/, owned by nobody" pass "" \
    test "$(listing "$scratch/packed-first/stub-linux-x86_64.tar.zst")" = "$expected_listing"

"$release" pack --build-dir build/release --target aarch64 --out "$scratch/plain" stub > /dev/null
check "a plugin without data/ ships only the .so and its template" pass "" \
    test "$(listing "$scratch/plain/stub-linux-arm64.tar.zst" | awk '{print $3}' | xargs)" = "Stub.json5 stub.so"

linked="$scratch/linked"
cp -r "$first" "$linked"
ln -s /etc/passwd "$linked/plugins/stub/data/passwd"
check "a link under data/ is refused" fail "is a link" \
    "$release" pack --build-dir "$linked" --target x86_64 --out "$scratch/packed-linked" stub
check "a plugin that was not built is refused" fail "no built plugin" \
    "$release" pack --build-dir "$scratch/nothing" --target x86_64 --out "$scratch/packed-nothing" stub
cp plugins/stub/Stub.json5 "$fake/plugins/stub/Second.json5"
check "a plugin with two templates is refused" fail "exactly one .json5" \
    "$fake/scripts/release.py" pack --build-dir "$first" --target x86_64 --out "$scratch/packed-two" stub
rm "$fake/plugins/stub/"*.json5
check "a plugin with no template is refused" fail "exactly one .json5" \
    "$fake/scripts/release.py" pack --build-dir "$first" --target x86_64 --out "$scratch/packed-none" stub
cp plugins/stub/Stub.json5 "$fake/plugins/stub/"

# What ships, laid out as a build tree again.
stage="$scratch/stage"
mkdir -p "$stage/smoke"
cp build/release/smoke/replay_smoke "$stage/smoke/"
check "an archive unpacks to what was packed" pass "" \
    "$release" unpack --artifacts "$scratch/packed-first" --target x86_64 --into "$stage" stub
check "the unpacked plugin is the built one" pass "" cmp build/release/plugins/stub/stub.so "$stage/plugins/stub/stub.so"
check "the unpacked data/ is the built one" pass "" \
    cmp "$first/plugins/stub/data/sub/engine.dat" "$stage/plugins/stub/data/sub/engine.dat"
check "what ships passes the gate and the smoke" pass "1 plugins passed" \
    "$release" validate --build-dir "$stage" --out "$scratch/entries-x86_64" stub

rm "$fake/plugins/stub/smoke.toml"
check "a plugin with no smoke.toml fails validation" fail "no smoke.toml" \
    "$fake/scripts/release.py" validate --build-dir "$stage" --out "$scratch/entries-unsmoked" stub

broken="$scratch/broken"
mkdir -p "$broken/smoke" "$broken/plugins/stub"
cp build/release/smoke/replay_smoke "$broken/smoke/"
printf 'not a plugin' > "$broken/plugins/stub/stub.so"
check "a broken plugin fails validation" fail "the publish gate" \
    "$release" validate --build-dir "$broken" --out "$scratch/entries-broken" stub

# hostile <what it proves> <entry name> <file|link>: an archive of that one entry is refused.
hostile() {
    local dir
    dir="$(mktemp -d -p "$scratch")"
    python3 - "$dir/raw.tar" "$2" "$3" <<'EOF'
import io, sys, tarfile
with tarfile.open(sys.argv[1], "w") as tar:
    info = tarfile.TarInfo(sys.argv[2])
    if sys.argv[3] == "link":
        info.type = tarfile.SYMTYPE
        info.linkname = "/etc/passwd"
    else:
        info.size = 1
    tar.addfile(info, io.BytesIO(b"x"))
EOF
    zstd -q "$dir/raw.tar" -o "$dir/stub-linux-x86_64.tar.zst"
    check "$1" fail "is not allowed" "$release" unpack --artifacts "$dir" --target x86_64 --into "$dir/stage" stub
}
hostile "an archive whose path leaves the directory is refused" ../outside file
hostile "an archive with an absolute path is refused"           /tmp/outside file
hostile "an archive holding a link is refused"                  passwd link

# A plugin release keeps one index entry, which every target must agree on.
cp -r "$scratch/entries-x86_64" "$scratch/entries-aarch64"
check "an entry both targets agree on is kept" pass "" "$release" entry --out "$scratch/kept" \
    --entries "$scratch/entries-x86_64" --entries "$scratch/entries-aarch64" stub
check "the kept entry is the gate's" pass "" cmp "$scratch/entries-x86_64/stub.json" "$scratch/kept/stub.json"
cp -r "$scratch/entries-aarch64" "$scratch/entries-differing"
sed -i 's/"recommended": false/"recommended": true/' "$scratch/entries-differing/stub.json"
check "entries that differ between targets are refused" fail "differ" "$release" entry --out "$scratch/refused" \
    --entries "$scratch/entries-x86_64" --entries "$scratch/entries-differing" stub
check "a target with no entry is refused" fail "no gate entry" "$release" entry --out "$scratch/refused" \
    --entries "$scratch/entries-x86_64" --entries "$scratch/nothing" stub

# A channel set gathered from the fake repository, where stub/v1.0.0 is a commit before the
# channel's own, the way a pin outlives the commits after it.
git_fake() { git -C "$fake" -c user.name=selftest -c user.email=selftest@localhost "$@"; }
printf '[plugins]\nstub = "1.0.0"\n' > "$fake/channels/test/roster.toml"
git_fake init --quiet
git_fake add -A
git_fake commit --quiet -m plugin
git_fake tag stub/v1.0.0
git_fake commit --quiet --allow-empty -m channel
set_dir="$scratch/set"
mkdir -p "$set_dir"
cp "$scratch/packed-first/stub-linux-x86_64.tar.zst" "$scratch/plain/stub-linux-arm64.tar.zst" "$set_dir/"
check "the index joins the pinned plugins' entries" pass "" \
    "$fake/scripts/release.py" index --channel test --entries "$scratch/kept" --out "$set_dir/plugin-index.json"
check "the index is schema 1 over the rostered plugin" pass "" \
    python3 -c 'import json, sys; d = json.load(open(sys.argv[1])); assert d["schema"] == 1 and [p["id"] for p in d["plugins"]] == ["stub"]' \
    "$set_dir/plugin-index.json"
check "a pinned plugin with no entry is refused" fail "no gate entry" \
    "$fake/scripts/release.py" index --channel test --entries "$scratch/nothing" --out "$scratch/index"

digest="$("$fake/scripts/release.py" manifest --channel test --version 7 --artifacts "$set_dir" --out "$set_dir/manifest.json")"
check "the printed digest is the manifest's" pass "" test "$digest" = "$(sha256sum "$set_dir/manifest.json" | cut -d' ' -f1)"
check "a second manifest of the same commit is identical" pass "$digest" \
    "$fake/scripts/release.py" manifest --channel test --version 7 --artifacts "$set_dir" --out "$scratch/manifest-again.json"
check "the manifest names every artifact, each at the commit it was built from" pass "" \
    python3 - "$set_dir/manifest.json" "$set_dir" "$(git_fake rev-parse stub/v1.0.0)" "$(git_fake rev-parse HEAD)" <<'EOF'
import hashlib, json, pathlib, sys
m = json.load(open(sys.argv[1]))
plugin_commit, channel_commit = sys.argv[3], sys.argv[4]
assert m["schema"] == 1 and m["version"] == 7 and m["published"].endswith("Z")
assert m["source_revision"] == channel_commit
pairs = [(a["name"], a.get("target"), a.get("version")) for a in m["artifacts"]]
assert pairs == [("plugin-index", None, None), ("stub", "linux-arm64", "1.0.0"), ("stub", "linux-x86_64", "1.0.0")], pairs
for a in m["artifacts"]:
    data = (pathlib.Path(sys.argv[2]) / a["path"]).read_bytes()
    assert a["sha256"] == hashlib.sha256(data).hexdigest() and a["size"] == len(data)
    assert a["revision"] == (channel_commit if a["name"] == "plugin-index" else plugin_commit)
EOF

printf '[plugins]\nstub = "1.0.1"\n' > "$fake/channels/test/roster.toml"
check "a pin with no tag is refused" fail "there is no tag stub/v1.0.1" \
    "$fake/scripts/release.py" manifest --channel test --version 7 --artifacts "$set_dir" --out "$scratch/manifest-untagged.json"
printf '[plugins]\nstub = "1.0.0"\n' > "$fake/channels/test/roster.toml"

rm "$set_dir/stub-linux-arm64.tar.zst"
check "a set missing a target is refused" fail "no stub-linux-arm64.tar.zst" \
    "$fake/scripts/release.py" manifest --channel test --version 7 --artifacts "$set_dir" --out "$scratch/manifest-missing.json"

rm "$set_dir/plugin-index.json"
check "a set missing its index is refused" fail "no plugin-index.json" \
    "$fake/scripts/release.py" manifest --channel test --version 7 --artifacts "$set_dir" --out "$scratch/manifest-noindex.json"

# stage_release.sh against a gh that keeps releases as directories under $releases.
releases="$scratch/releases"
mkdir -p "$scratch/bin" "$releases"
cat > "$scratch/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ $1 == release ]] || exit 2
command="$2" tag="$3"
shift 3
dir="$GH_RELEASES/${tag//\//_}"
case "$command" in
    view)
        [[ -d $dir ]] || exit 1
        if [[ ${1:-} == --json ]]; then ls "$dir"; fi ;;
    create) mkdir "$dir"; echo "created $tag" ;;
    upload) cp "$1" "$dir/"; echo "uploaded $(basename "$1")" ;;
    download) cp "$dir/$2" "$4/" ;;
esac
EOF
chmod +x "$scratch/bin/gh"
stage() { PATH="$scratch/bin:$PATH" GH_RELEASES="$releases" scripts/stage_release.sh "$@"; }

mkdir -p "$scratch/staging"
printf 'one' > "$scratch/staging/a.tar.zst"
printf 'two' > "$scratch/staging/b.json"
check "a release that does not exist is created and filled" pass "created vamiga/v1.2.0" \
    stage vamiga/v1.2.0 notes "$scratch/staging/a.tar.zst" "$scratch/staging/b.json"
check "the staged files are the ones given" pass "" cmp "$scratch/staging/b.json" "$releases/vamiga_v1.2.0/b.json"
printf 'three' > "$scratch/staging/c.tar.zst"
check "a re-run skips what is already staged identically" pass "a.tar.zst already on vamiga/v1.2.0" \
    stage vamiga/v1.2.0 notes "$scratch/staging/a.tar.zst" "$scratch/staging/c.tar.zst"
check "a re-run uploads what is missing" pass "" cmp "$scratch/staging/c.tar.zst" "$releases/vamiga_v1.2.0/c.tar.zst"
printf 'changed' > "$scratch/staging/a.tar.zst"
check "a staged file is never replaced by different bytes" fail "a.tar.zst is already on vamiga/v1.2.0 with different bytes" \
    stage vamiga/v1.2.0 notes "$scratch/staging/a.tar.zst"
check "the staged file is left as it was" pass "" test "$(cat "$releases/vamiga_v1.2.0/a.tar.zst")" = one

echo "release selftest: ${passed} passed, ${failed} failed"
((failed == 0))
