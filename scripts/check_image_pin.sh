#!/usr/bin/env bash
# Checks that docker/IMAGE names the toolchain image by digest and nothing else, and prints
# the reference it validated.
#
#   check_image_pin.sh [<docker/IMAGE>]
#
# The validated reference goes to stdout and the human-readable verdict to stderr, so this is
# both the CI gate and the one place that reads the pin:
#
#   image="$(scripts/check_image_pin.sh)"
#
# The whole point of the image is that everyone builds with the same compiler, and a tag
# quietly breaks that: ghcr.io/.../toolchain-linux:latest is a different set of bits on
# different days, so CI and a contributor could disagree while both looked correct. Only a
# digest is immutable, so only a digest is allowed, and this runs in CI to say so out loud
# rather than letting a tag slip in unnoticed.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_file="${1:-${repo_dir}/docker/IMAGE}"

if [[ ! -f "$pin_file" ]]; then
    echo "check_image_pin: no pin file at $pin_file" >&2
    exit 2
fi

# Everything that is not a comment or blank. Exactly one reference may survive.
mapfile -t refs < <(sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$pin_file" | grep -v '^$')

if ((${#refs[@]} == 0)); then
    echo "check_image_pin: FAIL - $pin_file names no image" >&2
    exit 1
fi
if ((${#refs[@]} > 1)); then
    echo "check_image_pin: FAIL - $pin_file names ${#refs[@]} images; there must be exactly one:" >&2
    printf '    %s\n' "${refs[@]}" >&2
    exit 1
fi

ref="${refs[0]}"

if [[ "$ref" != *"@sha256:"* ]]; then
    echo "check_image_pin: FAIL - not pinned by digest: $ref" >&2
    echo "    The reference must carry '@sha256:<64 hex digits>'; a tag is not reproducible." >&2
    exit 1
fi

digest="${ref##*@sha256:}"
if [[ ! "$digest" =~ ^[0-9a-f]{64}$ ]]; then
    echo "check_image_pin: FAIL - malformed digest in: $ref" >&2
    exit 1
fi
if [[ "$digest" =~ ^0+$ ]]; then
    echo "check_image_pin: FAIL - placeholder digest; the toolchain image has not been published yet." >&2
    echo "    Publish docker/Dockerfile.linux and record the digest it prints in $pin_file." >&2
    exit 1
fi

echo "check_image_pin: OK - pinned by digest: $ref" >&2
echo "$ref"
