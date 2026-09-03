#!/usr/bin/env bash
# The smoke host's own test: the stub passes its smoke, and every way a plugin or its configuration
# can fail one is shown to fail, for the stated reason. Run it after changing anything under smoke/.
#
#   smoke/selftest.sh [debug|release|asan]
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"

config="${1:-debug}"
if ! ./build.sh stub "$config" > /dev/null; then
    echo "smoke selftest: the build failed; run ./build.sh stub $config to see why" >&2
    exit 1
fi

host="build/${config}/smoke/replay_smoke"
stub="build/${config}/plugins/stub/stub.so"
fakes="build/${config}/smoke"

passed=0
failed=0

# check <what it proves> <pass|fail> <substring the output must contain, or -> <command...>
check() {
    local label="$1" expected="$2" needle="$3"
    shift 3
    local output status=0
    output="$("$@" 2>&1)" || status=$?

    local wrong_status=0
    if [[ $expected == pass && $status -ne 0 ]] || [[ $expected == fail && $status -eq 0 ]]; then
        wrong_status=1
    fi
    # Asserting the reason as well as the status: a fake that failed for an unrelated reason would
    # otherwise look like proof of the check it was built for.
    local wrong_reason=0
    if [[ $needle != "-" && $output != *"$needle"* ]]; then
        wrong_reason=1
    fi

    if ((wrong_status == 0 && wrong_reason == 0)); then
        echo "ok   - ${label}"
        passed=$((passed + 1))
    else
        if ((wrong_status)); then
            echo "FAIL - ${label} (exit ${status}, expected to ${expected})"
        else
            echo "FAIL - ${label} (exit ${status} as expected, but the output never said '${needle}')"
        fi
        sed 's/^/       /' <<< "$output"
        failed=$((failed + 1))
    fi
}

# What a published plugin must do, and the two failures the policy names.
check "the stub passes its smoke"            pass "PASS"          "$host" "$stub" plugins/stub/smoke.toml
check "a plugin that hangs times out"        fail "timeout"       "$host" "$fakes/fake_hang.so" smoke/fakes/fake.toml
check "a blank framebuffer fails"            fail "is blank"      "$host" "$fakes/fake_blank.so" smoke/fakes/fake.toml

# The rest of the load-time and run-time contract.
check "a working fake passes"                pass "PASS"          "$host" "$fakes/fake_silent.so" smoke/fakes/fake.toml
check "a mismatched ABI version fails"       fail "ABI version"   "$host" "$fakes/fake_abi.so" smoke/fakes/fake.toml
check "an empty required slot fails"         fail "required slot" "$host" "$fakes/fake_noslot.so" smoke/fakes/fake.toml
check "asserted audio that never comes fails" fail "no audio"     "$host" "$fakes/fake_silent.so" smoke/fakes/audio.toml

# Booting with nothing mounted, for a machine whose own ROM comes up on an empty drive. The fake
# accepts only a null path, so these two also pin which one of the two the host passes.
check "no fixture boots with none"           pass "PASS"          "$host" "$fakes/fake_nomedia.so" smoke/fakes/no_fixture.toml
check "a fixture is passed as a path"        fail "refused"       "$host" "$fakes/fake_nomedia.so" smoke/fakes/fake.toml

# The host's own string functions, which it defines for the plugins it loads rather than links.
check "the host's strings round-trip"        pass "PASS"          "$host" "$fakes/fake_strings.so" smoke/fakes/fake.toml

# Fixture provenance, and the configuration itself.
check "a fixture without provenance fails"   fail "provenance"    "$host" "$stub" smoke/fakes/unprovenanced.toml
check "a smoke.toml that is not there fails" fail "cannot read"   "$host" "$stub" smoke/fakes/no-such.toml
check "an unknown key fails"                 fail "unknown key"   "$host" "$stub" smoke/fakes/bad_unknown_key.toml
check "a key set twice fails"                fail "set twice"     "$host" "$stub" smoke/fakes/bad_duplicate.toml
check "a non-numeric frame count fails"      fail "unsigned"      "$host" "$stub" smoke/fakes/bad_frames.toml
check "a zero frame count fails"             fail "between 1"     "$host" "$stub" smoke/fakes/bad_zero_frames.toml

# ./build.sh --smoke: it runs, and a red smoke leaves the build's own status alone. The second half
# needs a smoke that fails, so the stub's fixture loses its provenance record for the length of one
# run and the trap puts it back however this script exits.
provenance="plugins/stub/smoke/fixture.stub.provenance.toml"
withheld="${provenance}.withheld"
# Returns 0 even when there is nothing to put back: this runs from the EXIT trap, whose last
# command decides the exit status the script reports.
restore_provenance() { [[ -f $withheld ]] && mv "$withheld" "$provenance"; return 0; }
trap restore_provenance EXIT

check "build.sh --smoke runs the smoke"      pass "Smoke passed"  ./build.sh stub "$config" --smoke
mv "$provenance" "$withheld"
check "a red smoke does not fail build.sh"   pass "Smoke FAILED"  ./build.sh stub "$config" --smoke
restore_provenance

echo "smoke selftest: ${passed} passed, ${failed} failed"
((failed == 0))
