# Replay emulator plugins

Emulator plugins for the Replay frontend. `sdk/` is a submodule of the public
[`replay_emulator_sdk`](https://github.com/FPGAArcade/replay_emulator_sdk); `./build.sh`
fetches it.

## Build

```bash
./build.sh --list                           # plugins in this repository
./build.sh stub [release|asan]              # into build/<config>/
./build.sh stub --deploy                    # copy to ~/.replay2/system/emulators/<plugin>/
./build.sh stub --smoke                     # run smoke.toml (informational)
./build.sh stub --sdk-dir <frontend>/build/x64-debug/emu-sdk
./build.sh stub --docker [--target aarch64] # release through the pinned image, as CI does
```

Release builds use the Rocky 8 image pinned by digest in `docker/IMAGE` (glibc floor 2.28).
`scripts/check_abi_floor.sh <plugin.so>` audits an artifact.

## Layout

```
plugins/<name>/
    CMakeLists.txt      one add_replay_emu_plugin() call
    <name>_core.c(pp)   RpEmuAPI glue
    <Name>.json5        config template
    smoke.toml          boot smoke
    LICENSES/           licences the artifact ships under
    upstream/           emulator source, in-tree
```

`plugins/stub` is the reference. The `mesen2_*` plugins share `plugins/mesen2_shared`.
Ported glue keeps upstream's style; new code follows the project conventions.

## Publish gate

```bash
./scripts/publish_gate.py check --build-dir build/docker-release --out index/ stub vamiga
./scripts/publish_gate.py reproduce stub vamiga -- --docker --target aarch64
```

## Tests

```bash
smoke/selftest.sh
scripts/upstream_selftest.sh
scripts/publish_gate_selftest.sh
```

Update channels are described in [`channels/`](channels/README.md).

## Licensing

Glue is MIT (`LICENSE`). Each plugin's `LICENSES/` covers its built artifact.
