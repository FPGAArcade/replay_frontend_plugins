#!/usr/bin/env bash
# prepare_upstream's own test: a plugin's upstream is fetched once, its patch series is applied
# once, and a patch that does not apply stops the build with the reason. Every emulator plugin in
# this repository is built through it, so run this after changing scripts/upstream.sh.
#
#   scripts/upstream_selftest.sh
#
# The fixture is a throwaway git repository under /tmp shaped like this one: a plugin with an
# upstream submodule and a patch series, and one without either. Nothing here touches the real
# repository or the network.
set -uo pipefail

# Resolved before the fixture is entered, because the rest of this script runs from /tmp.
source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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

# Submodules over a local path are refused by default. This opts in for every git run this script
# makes, through the environment rather than by writing to anyone's config.
export GIT_CONFIG_COUNT=1
export GIT_CONFIG_KEY_0=protocol.file.allow
export GIT_CONFIG_VALUE_0=always

# The upstream a plugin pins, standing in for an emulator's own repository.
origin="$tmp/origin"
git init -q "$origin"
printf 'first\nsecond\nthird\n' > "$origin/FILE"
git -C "$origin" add FILE
git -C "$origin" -c user.email=t@example.com -c user.name=t commit -qm "initial"

# The repository under test, shaped like this one.
repo_dir="$tmp/repo"
git init -q "$repo_dir"
cd "$repo_dir"
mkdir -p plugins/withupstream/patches plugins/noupstream
git submodule add "$origin" plugins/withupstream/upstream > /dev/null 2>&1

say() { echo "$*"; }
# shellcheck source=/dev/null
source "${source_dir}/upstream.sh"

# The patch series the fixture applies, generated from a real edit so its context is real.
sed -i.bak 's/^second$/second, patched/' plugins/withupstream/upstream/FILE
rm -f plugins/withupstream/upstream/FILE.bak
git -C plugins/withupstream/upstream diff > plugins/withupstream/patches/0001-edit.patch
git -C plugins/withupstream/upstream checkout -q FILE

patched() { grep -q 'second, patched' plugins/withupstream/upstream/FILE; }
patched_once() { [[ "$(grep -c 'second, patched' plugins/withupstream/upstream/FILE)" == 1 ]]; }

fetches_upstream() {
    local out
    out="$(prepare_upstream withupstream)" || return 1
    [[ $out == *Fetching* && -f plugins/withupstream/upstream/FILE ]]
}

reruns_quietly() {
    local out
    out="$(prepare_upstream withupstream)" || return 1
    [[ $out != *Fetching* && $out != *Applying* ]]
}

bad_patch_fails() {
    local err status
    err="$(prepare_upstream withupstream 2>&1 > /dev/null)"
    status=$?
    ((status != 0)) && [[ $err == *"does not apply"* ]]
}

# A plugin with no upstream and no patches is left alone rather than failing.
check "a plugin with no upstream is a no-op" prepare_upstream noupstream

# The fetch half. The submodule is emptied first, which is the state a fresh clone is in.
git submodule deinit --force plugins/withupstream/upstream > /dev/null
check "an uninitialised upstream is fetched"                fetches_upstream
check "the patch series is applied"                         patched

# Both halves again, on the tree the first run left behind.
check "a second run refetches and reapplies nothing"        reruns_quietly
check "the patch is still applied exactly once"             patched_once

# A series that does not fit the pinned revision stops the build and says why.
printf -- '--- a/FILE\n+++ b/FILE\n@@ -1,3 +1,4 @@\n+nope\n absent\n lines\n entirely\n' \
    > plugins/withupstream/patches/0002-bad.patch
check "a patch that does not apply fails, with the reason"  bad_patch_fails

echo "upstream selftest: ${passed} passed, ${failed} failed"
((failed == 0))
