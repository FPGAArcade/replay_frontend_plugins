#!/usr/bin/env bash
# fetch_verified's own test: a file is fetched once, a rebuild uses the cache offline, and a tampered,
# mismatched or missing download stops the build with the reason. vice_c64 fetches its ROMs through
# it, so run this after changing cmake/FetchVerified.cmake.
#
#   scripts/fetch_verified_selftest.sh
#
# The source is a file:// directory under /tmp. Nothing here touches the network.
set -uo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

passed=0
failed=0

# check <what it proves> <the assertion, as a command>
check() {
    local label="$1"
    shift
    if "$@"; then
        echo "ok   - ${label}"
        passed=$((passed + 1))
    else
        echo "FAIL - ${label}"
        failed=$((failed + 1))
    fi
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/src/sub" "$tmp/cache"
printf 'kernal' > "$tmp/src/sub/a.bin"
printf 'basic' > "$tmp/src/b.bin"
(cd "$tmp/src" && sha256sum sub/a.bin b.bin) > "$tmp/manifest"

cat > "$tmp/driver.cmake" <<EOF
list(APPEND CMAKE_MODULE_PATH "${source_dir}/cmake")
include(FetchVerified)
fetch_verified("\${MANIFEST}" "file://${tmp}/src" "${tmp}/cache" files)
message(STATUS "got \${files}")
EOF

# fetch <manifest>: runs the driver, output in $tmp/out.
fetch() {
    cmake -DMANIFEST="$1" -P "$tmp/driver.cmake" > "$tmp/out" 2>&1
}

# CMake wraps a long message across lines, so match it with the whitespace collapsed.
output_has() { tr -s ' \n' '  ' < "$tmp/out" | grep -q -- "$1"; }

check "the first run fetches every file" fetch "$tmp/manifest"
check "  and says so" output_has "fetching sub/a.bin"
check "  and caches them byte for byte" cmp -s "$tmp/src/sub/a.bin" "$tmp/cache/sub/a.bin"
check "  and returns their paths" output_has "got ${tmp}/cache/sub/a.bin;${tmp}/cache/b.bin"

mv "$tmp/src" "$tmp/src.away"
check "a second run needs no source" fetch "$tmp/manifest"
check "  and fetches nothing" bash -c "! grep -q fetching '$tmp/out'"
mv "$tmp/src.away" "$tmp/src"

printf 'X' >> "$tmp/cache/b.bin"
check "a tampered cached file fails" bash -c "! cmake -DMANIFEST='$tmp/manifest' -P '$tmp/driver.cmake' > '$tmp/out' 2>&1"
check "  naming the file and the fix" output_has "b.bin has sha256 .* Delete it to fetch it again"
rm "$tmp/cache/b.bin"

sed 's/^[0-9a-f]\{8\}/00000000/' "$tmp/manifest" > "$tmp/wrong"
rm -rf "$tmp/cache"
check "a download that does not match its hash fails" bash -c "! cmake -DMANIFEST='$tmp/wrong' -P '$tmp/driver.cmake' > '$tmp/out' 2>&1"
check "  with the expected hash" output_has "expects 00000000"

rm -rf "$tmp/cache"
echo "0000  missing.bin" > "$tmp/missing"
check "a missing download fails" bash -c "! cmake -DMANIFEST='$tmp/missing' -P '$tmp/driver.cmake' > '$tmp/out' 2>&1"
check "  with the reason" output_has "cannot fetch file://${tmp}/src/missing.bin"
check "  and leaves nothing in the cache" bash -c "[[ -z \"\$(ls -A '$tmp/cache' 2>/dev/null)\" ]]"

echo "not a manifest line" > "$tmp/bad"
check "an unreadable manifest line fails" bash -c "! cmake -DMANIFEST='$tmp/bad' -P '$tmp/driver.cmake' > '$tmp/out' 2>&1"
check "  quoting the line" output_has "cannot read 'not a manifest line'"

echo "fetch_verified selftest: ${passed} passed, ${failed} failed"
((failed == 0))
