#!/usr/bin/env bash
# Build plugins from this repository against the Replay SDK.
#
#   ./build.sh stub                  debug build of plugins/stub
#   ./build.sh stub release          release build
#   ./build.sh stub --deploy         build, then drop it where the frontend will find it
#   ./build.sh stub --smoke          build, then run the plugin's smoke (informational)
#   ./build.sh stub --sdk-dir ~/replay_frontend/build/x64-debug/emu-sdk
#   ./build.sh stub --docker         build through the pinned toolchain image, as CI does
#   ./build.sh stub --docker --target aarch64
#   ./build.sh --list                what is in plugins/
#
# Only the named plugins are configured, so a checkout carrying every emulator upstream still
# costs nothing to build one small plugin.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_dir"

sideload_default="${HOME}/.replay2/system/emulators"

plugins=()
config="debug"
sdk_dir=""
deploy=0
smoke=0
deploy_dir="${REPLAY_SIDELOAD_DIR:-$sideload_default}"
clean=0
docker=0
host_arch="$(uname -m)"
target="$host_arch"

usage() {
    # The header comment above, up to the first line that is not a comment.
    awk 'NR > 1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
    echo
    echo "Configurations: debug (default), release, asan"
    echo "Options:"
    echo "  --sdk-dir DIR     build against a local SDK tree instead of the submodule in sdk/"
    echo "  --deploy          copy the built plugin and its config template to the sideload directory"
    echo "  --deploy-dir DIR  where --deploy copies to (default \$REPLAY_SIDELOAD_DIR, else $sideload_default)"
    echo "  --smoke           run the plugin's smoke.toml through the smoke host; informational only"
    echo "  --clean           remove the build directory first"
    echo "  --docker          build inside the pinned toolchain image (docker/IMAGE)"
    echo "  --target ARCH     the architecture to build for; a foreign one needs --docker"
    echo "  --list            list the plugins in this repository"
}

list_plugins() {
    for dir in plugins/*/; do
        [[ -f "${dir}CMakeLists.txt" ]] && basename "$dir"
    done
}

while (($#)); do
    case "$1" in
        debug|release|asan) config="$1" ;;
        --sdk-dir) sdk_dir="${2:?--sdk-dir needs a path}"; shift ;;
        --deploy) deploy=1 ;;
        --smoke) smoke=1 ;;
        --deploy-dir) deploy_dir="${2:?--deploy-dir needs a path}"; shift ;;
        --clean) clean=1 ;;
        --docker) docker=1 ;;
        --target) target="${2:?--target needs an architecture}"; shift ;;
        --list) list_plugins; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "build.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
        *) plugins+=("$1") ;;
    esac
    shift
done

if ((${#plugins[@]} == 0)); then
    echo "build.sh: name at least one plugin (--list shows them)" >&2
    exit 2
fi

for plugin in "${plugins[@]}"; do
    if [[ ! -f "plugins/${plugin}/CMakeLists.txt" ]]; then
        echo "build.sh: no such plugin: '${plugin}' (--list shows them)" >&2
        exit 2
    fi
done

# The SDK: the pinned submodule in sdk/, or whatever tree --sdk-dir names. Every line this script
# and the build print carries the [LOCAL SDK] marker while the override is in effect.
marker=""
if [[ -n "$sdk_dir" ]]; then
    sdk_dir="$(cd "$sdk_dir" 2>/dev/null && pwd)" || { echo "build.sh: no such --sdk-dir: $sdk_dir" >&2; exit 2; }
    if [[ ! -f "$sdk_dir/cmake/ReplaySDK.cmake" ]]; then
        echo "build.sh: '$sdk_dir' is not a staged SDK (no cmake/ReplaySDK.cmake)" >&2
        exit 2
    fi
    marker="[LOCAL SDK] "
else
    sdk_dir="$repo_dir/sdk"
    if [[ ! -f "$sdk_dir/cmake/ReplaySDK.cmake" ]] && ! git submodule update --init sdk; then
        echo "build.sh: could not fetch the SDK submodule into ${sdk_dir}." >&2
        echo "  Or pass --sdk-dir <a staged SDK>." >&2
        exit 1
    fi
fi

say() { echo "${marker}$*"; }

# The toolchain image, when --docker asks for it. The build re-enters this script inside the
# container with the same arguments minus --docker, so there is one build path rather than two,
# and that is what makes a local build and a CI build the same thing. The repository is always
# mounted at /src, because -ffile-prefix-map maps the source directory into the artifact: a
# build from ~/code and a build from a runner's workspace only agree byte for byte if the path
# the compiler saw was identical.
if ((docker)); then
    if ! command -v docker > /dev/null; then
        echo "build.sh: --docker needs docker, which is not on PATH" >&2
        exit 2
    fi
    if ((deploy)); then
        echo "build.sh: --deploy has nothing to deploy to inside the container; drop one of them" >&2
        exit 2
    fi

    # Both the gate that a tag never reaches a build and the one place the pin is read.
    image="$(bash "${repo_dir}/scripts/check_image_pin.sh")"

    inner=("${plugins[@]}" "$config" --target "$target")
    ((clean)) && inner+=(--clean)
    ((smoke)) && inner+=(--smoke)

    mounts=(-v "${repo_dir}:/src")
    if [[ -n "$marker" ]]; then
        mounts+=(-v "${sdk_dir}:/sdk:ro")
        inner+=(--sdk-dir /sdk)
    fi

    say "Building through ${image}"
    # A separate build directory from the bare-host one: a CMake cache records the absolute
    # paths it was generated with, and the container sees this repository at /src, so the two
    # would keep invalidating each other's cache if they shared a tree.
    exec docker run --rm \
        "${mounts[@]}" \
        -w /src \
        -u "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -e REPLAY_BUILD_TAG=docker \
        "$image" \
        ./build.sh "${inner[@]}"
fi

case "$config" in
    debug)   build_type="Debug"   ; asan="OFF" ;;
    release) build_type="Release" ; asan="OFF" ;;
    asan)    build_type="Debug"   ; asan="ON"  ;;
esac

# A target that is not this machine's is cross-compiled through a toolchain file, and those
# only work inside the image, which is where the sysroot they name lives.
toolchain=()
build_name="$config"
if [[ "$target" != "$host_arch" ]]; then
    toolchain_file="${repo_dir}/cmake/toolchain-${target}.cmake"
    if [[ ! -f "$toolchain_file" ]]; then
        echo "build.sh: no cross toolchain for '${target}' (expected ${toolchain_file})" >&2
        exit 2
    fi
    toolchain=(-DCMAKE_TOOLCHAIN_FILE="$toolchain_file")
    build_name="${target}-${config}"
    if ((smoke)); then
        echo "build.sh: --smoke runs the plugin, so it cannot run a ${target} build on ${host_arch}" >&2
        exit 2
    fi
fi

# Set by the --docker branch above, and by nothing else.
build_dir="build/${REPLAY_BUILD_TAG:+${REPLAY_BUILD_TAG}-}${build_name}"

((clean)) && rm -rf "$build_dir"

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

source "${repo_dir}/scripts/upstream.sh"
for plugin in "${plugins[@]}"; do
    prepare_upstream "$plugin" || exit 1
done

say "Configuring ${plugins[*]} (${config}, ${target}) against ${sdk_dir}"
selected="$(IFS=';'; echo "${plugins[*]}")"
cmake -S . -B "$build_dir" "${generator[@]}" "${toolchain[@]}" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DREPLAY_SDK_DIR="$sdk_dir" \
    -DREPLAY_LOCAL_SDK="$([[ -n $marker ]] && echo ON || echo OFF)" \
    -DREPLAY_PLUGIN_ASAN="$asan" \
    -DREPLAY_PLUGINS="$selected" > /dev/null

say "Building"
# Ninja prints one line per compiled file; NINJA_STATUS puts the marker on each of them.
NINJA_STATUS="${marker}[%f/%t] " cmake --build "$build_dir"

for plugin in "${plugins[@]}"; do
    say "Built ${build_dir}/plugins/${plugin}/${plugin}.so"
done

# Informational: a red smoke here is a signal to look, not a build failure. The gate that stops a
# plugin being published lives in the release pipeline, not in a local build.
if ((smoke)); then
    for plugin in "${plugins[@]}"; do
        config_file="plugins/${plugin}/smoke.toml"
        if [[ ! -f "$config_file" ]]; then
            say "No ${config_file}, so ${plugin} has no smoke to run"
            continue
        fi
        if "${build_dir}/smoke/replay_smoke" "${build_dir}/plugins/${plugin}/${plugin}.so" "$config_file"; then
            say "Smoke passed for ${plugin}"
        else
            say "Smoke FAILED for ${plugin} (informational; the build itself is fine)"
        fi
    done
fi

if ((deploy)); then
    for plugin in "${plugins[@]}"; do
        dest="${deploy_dir}/${plugin}"
        mkdir -p "$dest"
        cp "${build_dir}/plugins/${plugin}/${plugin}.so" "$dest/"
        # The template beside the shared object is what makes the directory a plugin the
        # frontend will pick up; without it the .so is just a file.
        cp plugins/"${plugin}"/*.json5 "$dest/"
        say "Deployed ${plugin} to ${dest}"
    done
fi
