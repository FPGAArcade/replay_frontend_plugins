# prepare_upstream <plugin>, shared by ./build.sh and scripts/upstream_selftest.sh.
#
# A plugin whose emulator lives upstream carries it as a submodule at plugins/<name>/upstream, with
# our changes beside it as a patch series rather than committed into it. Both halves land here, and
# both are idempotent: a submodule already checked out is left alone, and so is an applied patch.
#
# The caller is cd'd to the repository root, sets `repo_dir` to it, and defines say() for progress.
# Returns non-zero with the reason on stderr when the fetch fails or a patch does not apply.

prepare_upstream() {
    local plugin="$1"
    local upstream="plugins/${plugin}/upstream"

    # A plugin like the stub has no upstream at all, and needs nothing done.
    git config -f .gitmodules --get "submodule.${upstream}.path" > /dev/null || return 0

    if [[ -z "$(ls -A "$upstream" 2>/dev/null)" ]]; then
        say "Fetching ${plugin}'s upstream..."
        if ! git submodule update --init --depth 1 "$upstream"; then
            echo "prepare_upstream: could not fetch ${upstream}" >&2
            return 1
        fi
    fi

    local patches=()
    local patch
    for patch in "plugins/${plugin}/patches"/*.patch; do
        [[ -f "$patch" ]] && patches+=("$patch")
    done

    if ((${#patches[@]} == 0)); then
        return 0
    fi

    for patch in "${patches[@]}"; do
        # A patch that reverses cleanly is one that is already in the tree.
        if git -C "$upstream" apply --reverse --check "${repo_dir}/${patch}" 2> /dev/null; then
            continue
        fi
        say "Applying $(basename "$patch") to ${plugin}'s upstream"
        if ! git -C "$upstream" apply "${repo_dir}/${patch}"; then
            echo "prepare_upstream: ${patch} does not apply to ${upstream}." >&2
            echo "  The submodule is not at the revision the series was written against." >&2
            return 1
        fi
    done
}
