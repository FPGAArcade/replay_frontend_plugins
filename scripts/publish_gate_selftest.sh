#!/usr/bin/env bash
# The publish gate's own test: the stub passes and gets a valid index entry, every way a plugin can
# fail the gate is shown to fail for its stated reason, and two clean builds of the stub agree.
#
#   scripts/publish_gate_selftest.sh
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

if ! ./build.sh stub release > /dev/null; then
    echo "gate selftest: the build failed; run ./build.sh stub release to see why" >&2
    exit 1
fi

gate=scripts/publish_gate.py
build=build/release
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

    if [[ $expected == pass && $status -eq 0 || $expected == fail && $status -eq 1 ]] && [[ $output == *"$needle"* ]]; then
        echo "ok   - ${label}"
        passed=$((passed + 1))
    else
        echo "FAIL - ${label} (exit ${status}, expected to ${expected} saying '${needle}')"
        sed 's/^/       /' <<< "$output"
        failed=$((failed + 1))
    fi
}

# A fixture tree the gate reads the way it reads this repository: plugins/<name>/<template> and
# <build>/plugins/<name>/<name>.so, with the smoke host beside them.
fixtures="$scratch/src"
fixture_build="$scratch/build"
mkdir -p "$fixture_build/smoke"
ln -s "$repo_dir/$build/smoke/replay_smoke" "$fixture_build/smoke/replay_smoke"

# fixture <plugin> <artifact to publish as it> <template text>
fixture() {
    mkdir -p "$fixtures/plugins/$1" "$fixture_build/plugins/$1"
    cp "$2" "$fixture_build/plugins/$1/$1.so"
    printf '%s\n' "$3" > "$fixtures/plugins/$1/$1.json5"
}

# A template the gate accepts, for the plugin named.
good_template() {
    printf 'name: "%s", // what the frontend shows\nplugin: "%s.so",\nmetadata: { info: "A fake" },\nextensions: ["fake",],\n' \
        "${1^^}" "$1"
}

gate_fixture() {
    "$gate" check --source-dir "$fixtures" --build-dir "$fixture_build" --out "$scratch/out" "$1"
}

# What a published plugin must do, and that its entry is one the frontend's index reads.
check "the stub passes"                          pass "gate: OK - stub" \
    "$gate" check --build-dir "$build" --out "$scratch/out" stub
check "the stub's entry is a plugin-index entry" pass "entry ok" python3 -c '
import json, sys
entry = json.load(open(sys.argv[1]))
assert entry["id"] == "stub" and entry["name"] == "Stub" and entry["kind"] == "emulator", entry
assert entry["abi_version"] == 1 and entry["recommended"] is False and entry["data_artifacts"] == [], entry
media = entry["media_support"]
assert media == [{"extensions": ["stub"], "action": "run_directly", "priority": 50, "description": ""}], media
print("entry ok")' "$scratch/out/stub.json"

fixture fake_silent "$build/smoke/fake_silent.so" "$(good_template fake_silent)"
check "a well-formed fake passes"                pass "gate: OK - fake_silent" gate_fixture fake_silent

# The binary checks, one broken artifact each.
fixture fake_abi "$build/smoke/fake_abi.so" "$(good_template fake_abi)"
check "a mismatched ABI version fails"           fail "reports ABI version 2" gate_fixture fake_abi
fixture fake_noabi "$build/smoke/fake_noabi.so" "$(good_template fake_noabi)"
check "a missing ABI version fails"              fail "exports no rp_emu_plugin_abi_version" gate_fixture fake_noabi
fixture fake_export "$build/smoke/fake_export.so" "$(good_template fake_export)"
check "a stray exported symbol fails"            fail "exports rp_stray_plugin_get" gate_fixture fake_export
fixture fake_import "$build/smoke/fake_import.so" "$(good_template fake_import)"
check "an unresolvable import fails"             fail "rp_fake_missing_host_function" gate_fixture fake_import
# Under imports, not load: the load is lazy, so a host function it never calls cannot fail it.
check "an unresolvable import still loads"       fail "fake_import: imports" gate_fixture fake_import
check "...and is not reported as a load failure" pass "" bash -c '! "$@" 2>&1 | grep -q "fake_import: load"' _ \
    "$gate" check --source-dir "$fixtures" --build-dir "$fixture_build" --out "$scratch/out" fake_import
check "a failing plugin gets no index entry"     pass "" test ! -e "$scratch/out/fake_import.json"
fixture regressed "$build/smoke/fake_silent.so" "$(good_template regressed)"
gate_fixture regressed > /dev/null
cp "$build/smoke/fake_abi.so" "$fixture_build/plugins/regressed/regressed.so"
check "a plugin that regresses loses its entry"  fail "reports ABI version 2" gate_fixture regressed
check "...from an earlier passing run"           pass "" test ! -e "$scratch/out/regressed.json"
check "a missing artifact fails"                 fail "no built plugin" \
    "$gate" check --source-dir "$fixtures" --build-dir "$fixture_build" --out "$scratch/out" no_such_plugin

# Malformed templates, on an artifact that is otherwise fine.
malformed() {
    fixture "$1" "$build/smoke/fake_silent.so" "$2"
    check "$3" fail "$4" gate_fixture "$1"
}
malformed bad_syntax  'name: "BAD_SYNTAX", plugin: "bad_syntax.so" extensions: ["x"]' \
    "a template that does not parse fails"      "unexpected text after the document"
malformed bad_name    'name: "Other", plugin: "bad_name.so", extensions: ["x"]' \
    "a name that is not the plugin's id fails"  "gives the id 'other'"
malformed bad_plugin  'name: "BAD_PLUGIN", plugin: "elsewhere.so", extensions: ["x"]' \
    "a plugin field naming another file fails"  "'plugin' must name the built artifact"
malformed bad_noext   'name: "BAD_NOEXT", plugin: "bad_noext.so", extensions: []' \
    "a template claiming nothing fails"         "'extensions' claims nothing"
malformed bad_dotext  'name: "BAD_DOTEXT", plugin: "bad_dotext.so", extensions: [".adf"]' \
    "an extension with a dot fails"             "without a leading dot: .adf"
malformed bad_type    'name: "BAD_TYPE", plugin: "bad_type.so", extensions: ["x"], recommended: "yes"' \
    "a field of the wrong type fails"           "'recommended' must be true or false"
malformed bad_noname  'plugin: "bad_noname.so", extensions: ["x"]' \
    "a template without a name fails"           "no 'name'"
malformed bad_twice   'name: "BAD_TWICE", name: "BAD_TWICE", plugin: "bad_twice.so", extensions: ["x"]' \
    "a key set twice fails"                     "key 'name' set twice"

fixture two_templates "$build/smoke/fake_silent.so" "$(good_template two_templates)"
cp "$fixtures/plugins/two_templates/two_templates.json5" "$fixtures/plugins/two_templates/other.json5"
check "two templates fail"                       fail "exactly one .json5 template, not 2" gate_fixture two_templates
fixture no_template "$build/smoke/fake_silent.so" ""
rm "$fixtures/plugins/no_template/no_template.json5"
check "no template fails"                        fail "exactly one .json5 template, not 0" gate_fixture no_template

# Every real template parses, so a failure above is the fixture's and not the parser's.
for template in plugins/*/*.json5; do
    check "$template parses"                     pass "" python3 -B -c '
import pathlib, sys
sys.path.insert(0, "scripts")
import publish_gate
publish_gate.Json5Parser(pathlib.Path(sys.argv[1]).read_text()).document()' "$template"
done

check "two clean builds of the stub are identical" pass "are identical" "$gate" reproduce stub

# A commit whose stub embeds a value chosen at configure time, which no two builds share. It is
# committed from a throwaway worktree and never named by a branch.
unstable="$scratch/unstable"
git worktree add --quiet --detach "$unstable" HEAD
printf '\nstring(RANDOM LENGTH 16 unstable)\ntarget_compile_definitions(stub PRIVATE UNSTABLE="${unstable}")\n' \
    >> "$unstable/plugins/stub/CMakeLists.txt"
printf '\n__attribute__((used)) static const char s_unstable[] = UNSTABLE;\n' >> "$unstable/plugins/stub/stub_plugin.c"
git -C "$unstable" -c user.name=selftest -c user.email=selftest@localhost commit --quiet -am unstable
unstable_commit="$(git -C "$unstable" rev-parse HEAD)"
git worktree remove --force "$unstable"
check "two builds that differ fail"              fail "differ" "$gate" reproduce --ref "$unstable_commit" stub

echo "gate selftest: ${passed} passed, ${failed} failed"
((failed == 0))
