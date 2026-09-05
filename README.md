# Replay emulator plugins

Emulator plugins for the Replay frontend. Each is a shared object built against
the plugin SDK, plus a config template the frontend reads. `sdk/` is a verbatim
copy of [`replay_frontend_sdk`](https://github.com/FPGAArcade/replay_frontend_sdk)
at the commit named in `sdk/UPSTREAM`; it is a copy rather than a submodule only
because that repository is private and CI could not check it out.

## Build

```bash
./build.sh --list                       # plugins in this repository
./build.sh stub                         # debug, into build/debug/
./build.sh stub release                 # release
./build.sh stub asan                    # debug + AddressSanitizer
./build.sh stub --deploy                # build, then copy to the frontend's sideload dir
./build.sh stub --smoke                 # build, then run smoke.toml (informational)
./build.sh stub --sdk-dir <frontend>/build/x64-debug/sdk   # against a local SDK
./build.sh stub --docker                # x86_64 release, through the pinned toolchain image
./build.sh stub --docker --target aarch64   # cross-compiled for the device
```

Only the named plugins are configured. `--deploy` copies to
`~/.replay2/system/emulators/<plugin>/`, or `$REPLAY_SIDELOAD_DIR`, or
`--deploy-dir`. The frontend scans that directory at start and watches it, so
rebuild plus deploy is the whole loop.

`--sdk-dir` prints a `[LOCAL SDK]` marker on every line. To take a published
SDK change, replace `sdk/` wholesale with a fresh checkout, keep `sdk/UPSTREAM`,
and record the new commit in it. Nothing in `sdk/` is edited locally.

## Release builds

`--docker` re-enters `build.sh` inside the image pinned by digest in
`docker/IMAGE`. CI uses the same command. Output goes to
`build/docker-release/`, and the repository is mounted at `/src` so artifacts
are byte-identical regardless of checkout path.

| | |
| --- | --- |
| Image | Rocky 8, `gcc-toolset-13`, libstdc++ linked statically |
| glibc floor | 2.28 |
| arm64 | clang cross-compile against a Rocky 8 arm64 sysroot in the image (`cmake/toolchain-aarch64.cmake`); the frontend's Debian 12 device sysroot is not used because it would raise the floor to 2.36 |
| Pulling the image | needs `read:packages` until the GHCR package is public: `gh auth refresh -s read:packages && gh auth token \| docker login ghcr.io -u <you> --password-stdin` |

Changing the toolchain: edit `docker/Dockerfile.linux`, let the *Toolchain
image* workflow publish it, paste the printed digest into `docker/IMAGE`.
`scripts/check_image_pin.sh` fails if the pin is a tag.

Audit a built artifact:

```bash
./scripts/check_abi_floor.sh build/docker-release/plugins/stub/stub.so
```

Checks, from the ELF: every versioned symbol need is `GLIBC_<= 2.28`, `DT_NEEDED`
holds only the C runtime and loader, and the only export is the plugin entry
point. Works on the arm64 artifact from the x86_64 host. `cmake/plugin_exports.map`
is what keeps libstdc++ template instantiations from leaking as exports.

## Plugin layout

```
plugins/<name>/
    CMakeLists.txt      one add_replay_emu_plugin() call
    <name>_core.c(pp)   the RpEmuAPI glue
    <Name>.json5        config template, deployed beside the .so
    smoke.toml          boot smoke: fixture, frame count, audio assertion
    smoke/              fixtures, each with <fixture>.provenance.toml
    LICENSES/           every licence the built artifact ships under
    upstream/           the emulator's source, committed in-tree
```

`plugins/stub` is the reference and has no `upstream/`. The seven `mesen2_*`
plugins share `plugins/mesen2_shared` through `cmake/Mesen2.cmake`.

Ported glue keeps upstream's spelling (`NULL`, `uint32_t`, `float`) and comments
so a port can be diffed against its original. Only what the build here requires
changes. Code written here, `plugins/stub` included, uses the project
conventions.

`scripts/upstream.sh` still supports a submodule-plus-patch-series layout
(`upstream/` as a submodule, `patches/*.patch` applied in order). No plugin uses
it; every upstream is committed in-tree.

## Smoke

`smoke.toml` is the whole of a plugin's smoke:

```toml
fixture = "smoke/fixture.stub"   # relative to this file; omit for a machine that boots to its own ROM
frames = 10                      # must run past the whole boot, not just the first picture
timeout_seconds = 10
audio = true                     # optional: also assert audio was produced
```

Every fixture needs `<fixture>.provenance.toml` beside it, with `source` and
`license` fields. Prefer open ROM replacements (EmuTOS, AROS, open C64 ROMs); a
private CI ROM store is the recorded fallback.

The host in `smoke/` links nothing from the frontend and implements the
`fl_*`/`arena_*` symbols plugins bind to. A plugin calling a host symbol the
smoke host lacks fails at load naming it; implement it in `smoke/host_symbols.c`.

`--smoke` is informational. The publish gate lives in the release pipeline.

## Tests

```bash
smoke/selftest.sh              # smoke host: 18 cases, including each failure mode's message
scripts/upstream_selftest.sh   # prepare_upstream: 6 cases, offline, throwaway repo under /tmp
```

## cmake/

| File | Purpose |
| --- | --- |
| `ReplayPlugins.cmake` | entry point: resolves the SDK, applies repository-wide config, includes the SDK's `ReplaySDK.cmake` |
| `ReplayRelease.cmake` | release floor: hidden visibility, static libstdc++, reproducible paths |
| `plugin_exports.map` | version script limiting exports to the entry point |
| `toolchain-aarch64.cmake` | cross toolchain, usable only inside the image |
| `Mesen2.cmake` | shared Mesen2 core build |

## CI

| Workflow | Trigger | Does |
| --- | --- | --- |
| *CI* | push, PR | image-pin check, both selftests, `stub` built through the image for both targets and audited, then rebuilt from another path and compared byte for byte |
| *Toolchain image* | changes under `docker/` | publishes `docker/Dockerfile.linux` to GHCR and prints the digest to pin |

Only the stub is built in CI.

## Channels

Plugin update channels are TUF repositories hosted on this repository's
releases. Their trust anchors, signing workflows and bring-up steps are in
[`channels/`](channels/README.md).

## Upstreams

| Plugin | Upstream |
| --- | --- |
| mesen2_* | https://github.com/SourMesen/Mesen2 |
| scummvm | https://github.com/scummvm/scummvm |
| vamiga | https://github.com/dirkwhoffmann/vAmiga |
| vice_c64 | https://sourceforge.net/projects/vice-emu (git mirror: https://github.com/VICE-Team/svn-mirror) |

FPGAArcade-owned mirrors are planned and not yet created.

## Licensing

Glue here is MIT (`LICENSE`). Each plugin's `LICENSES/` carries what its built
artifact is distributed under, which for a real emulator is the upstream's.
