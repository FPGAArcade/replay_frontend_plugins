#!/usr/bin/env bash
# Stage files as the assets of the release for an existing tag, creating the release if needed.
#
#   scripts/stage_release.sh [--prerelease] <tag> <notes> <file>...
#
# Releases are immutable: a file already staged must be byte-identical to the one given, so a
# re-run of a failed workflow uploads only what is missing and never replaces anything.
set -euo pipefail

prerelease=()
if [[ ${1:-} == --prerelease ]]; then
    prerelease=(--prerelease)
    shift
fi
tag="$1" notes="$2"
shift 2

if ! gh release view "$tag" > /dev/null 2>&1; then
    gh release create "$tag" --verify-tag "${prerelease[@]}" --title "$tag" --notes "$notes"
fi

staged="$(gh release view "$tag" --json assets --jq '.assets[].name')"
existing="$(mktemp -d)"
trap 'rm -rf "$existing"' EXIT
for file in "$@"; do
    name="$(basename "$file")"
    if grep -qxF "$name" <<< "$staged"; then
        gh release download "$tag" --pattern "$name" --dir "$existing"
        if ! cmp -s "$file" "$existing/$name"; then
            echo "::error::$name is already on $tag with different bytes"
            exit 1
        fi
        echo "stage: $name already on $tag"
    else
        gh release upload "$tag" "$file"
    fi
done
