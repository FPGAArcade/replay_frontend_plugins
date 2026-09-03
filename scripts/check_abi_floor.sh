#!/usr/bin/env bash
# Audits a built plugin against the compatibility floor a published binary has to meet.
#
#   check_abi_floor.sh <plugin.so>...
#
# The floor is glibc 2.28, which is Rocky 8's and the oldest thing a published plugin must
# run on; the device is Debian 12 at 2.36 and satisfies it with room to spare. Building in
# the toolchain image makes that true by construction, and this says so out loud, because
# the way it stops being true is silent: someone builds on their laptop, the result works
# everywhere they can test, and it faults on a host with an older libc.
#
# Three things are checked, on the ELF rather than on the build that produced it, so the
# answer holds for the artifact that actually ships:
#
#   1. Versioned symbol needs. Every one must be GLIBC_<= 2.28. A GLIBCXX_ or CXXABI_ need
#      means libstdc++ was linked dynamically after all.
#   2. DT_NEEDED. Only the C runtime and the loader; libstdc++.so.6 and libgcc_s.so.1 in
#      particular must be absent, because the device's libstdc++ is GCC 12's and cannot
#      satisfy a reference emitted by GCC 13.
#   3. Dynamic exports. The plugin entry point, and at most the optional ABI-version symbol
#      beside it. libstdc++'s headers put namespace std at default visibility, so a template
#      member instantiated in a plugin escapes -fvisibility=hidden and is exported unless the
#      version script localises it; two plugins exporting the same std:: symbol get bound to
#      whichever the loader saw first.
#
# readelf reads any architecture, so this audits the cross-built aarch64 artifact from the
# x86_64 machine that produced it. That is the whole reason it is readelf and not nm/ldd.
#
# Exit status is 0 when every named plugin passes, 1 otherwise, with each offence listed.
set -uo pipefail

if (($# == 0)); then
    echo "usage: check_abi_floor.sh <plugin.so>..." >&2
    exit 2
fi

readonly GLIBC_MAX_MAJOR=2
readonly GLIBC_MAX_MINOR=28

# The loader itself is per-architecture, hence the pattern rather than a name.
needed_allowed() {
    case "$1" in
        libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux-*.so.*) return 0 ;;
        *) return 1 ;;
    esac
}

status=0

for plugin in "$@"; do
    if [[ ! -f "$plugin" ]]; then
        echo "check_abi_floor: no such plugin: $plugin" >&2
        status=1
        continue
    fi

    name="$(basename "$plugin")"
    failed=0

    # 1. Versioned needs. Only the version-needs entries, which are the lines carrying both a
    # Name: and a Flags:; the version-symbol table readelf also prints names the same symbols.
    while read -r version; do
        [[ -z "$version" ]] && continue
        if [[ "$version" =~ ^GLIBC_([0-9]+)\.([0-9]+)(\.[0-9]+)?$ ]]; then
            major="${BASH_REMATCH[1]}"
            minor="${BASH_REMATCH[2]}"
            if ((major > GLIBC_MAX_MAJOR || (major == GLIBC_MAX_MAJOR && minor > GLIBC_MAX_MINOR))); then
                echo "check_abi_floor: FAIL - ${name} needs ${version}, above the glibc ${GLIBC_MAX_MAJOR}.${GLIBC_MAX_MINOR} floor" >&2
                failed=1
            fi
        else
            echo "check_abi_floor: FAIL - ${name} needs non-glibc version ${version}" >&2
            failed=1
        fi
    done < <(readelf -VW "$plugin" | sed -n 's/.*Name: \([A-Za-z_0-9.]*\)  *Flags:.*/\1/p' | sort -u)

    # 2. DT_NEEDED.
    while read -r dep; do
        [[ -z "$dep" ]] && continue
        if ! needed_allowed "$dep"; then
            echo "check_abi_floor: FAIL - ${name} links ${dep}, which is not part of the floor" >&2
            failed=1
        fi
    done < <(readelf -dW "$plugin" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')

    # 3. Dynamic exports. Column 7 is Ndx and column 8 the name; a defined symbol is one whose
    # Ndx is not UND, and only GLOBAL and WEAK ones are visible to another object.
    mapfile -t exports < <(readelf --dyn-syms -W "$plugin" |
        awk '$1 ~ /:$/ && $7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") { sub(/@.*/, "", $8); print $8 }' |
        sort -u)
    entry_points=0
    for symbol in ${exports[@]+"${exports[@]}"}; do
        case "$symbol" in
            rp_emu_plugin_get|rp_ui_plugin_get) ((++entry_points)) ;;
            rp_emu_plugin_abi_version|rp_ui_plugin_abi_version) ;;
            *)
                echo "check_abi_floor: FAIL - ${name} exports ${symbol}, which is not part of the plugin ABI" >&2
                failed=1
                ;;
        esac
    done
    if ((entry_points != 1)); then
        echo "check_abi_floor: FAIL - ${name} exports ${entry_points} entry points; it must export exactly one" >&2
        failed=1
    fi

    if ((failed)); then
        status=1
    else
        echo "check_abi_floor: OK - ${name}: glibc <= ${GLIBC_MAX_MAJOR}.${GLIBC_MAX_MINOR}, no C++ runtime linked, exports ${exports[*]}"
    fi
done

exit $status
