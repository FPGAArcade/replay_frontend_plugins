# Replay emulator plugins

Every emulator plugin the Replay frontend can run. Each one is a shared object built against the
[Replay plugin SDK](https://github.com/FPGAArcade/replay_frontend_sdk), pinned here at a commit,
plus the config template that tells the frontend what it is.

The frontend does not know about this repository, and nothing here is pinned by it: the SDK is the
only thing crossing between them, and the pin lives on this side.

## Building one

```bash
./build.sh stub                 # debug, into build/debug/
./build.sh stub release         # release
./build.sh stub asan            # debug + AddressSanitizer
./build.sh stub --deploy        # build, then install it where the frontend looks
./build.sh --list               # what is here
```

Only the plugins you name are configured, and only their submodules are fetched. Adding ScummVM to
this repository costs nothing when you are building a small core.

The first build fetches the pinned SDK into `sdk/` (a shallow submodule clone).

## Trying it

`--deploy` copies the built shared object and its config template into the frontend's sideload
directory, which is `~/.replay2/system/emulators/<plugin>/` unless `$REPLAY_SIDELOAD_DIR` or
`--deploy-dir` says otherwise. Start the frontend with a matching `--data-dir` if you deployed
somewhere else.

```bash
./build.sh stub --deploy
~/code/replay_frontend/build/x64-debug/replay_frontend
```

The frontend scans that directory at start and watches it afterwards, so a rebuild plus a deploy is
the whole loop.

## Working on the SDK at the same time

```bash
./build.sh stub --sdk-dir ~/code/replay_frontend/build/x64-debug/sdk
```

builds against a staged SDK from a frontend build tree instead of the pin. Every line the build
prints carries a `[LOCAL SDK]` marker while that is in effect, because a plugin that only builds
against an unpublished SDK is not one anyone else can build.

Once the SDK change is published, bump the pin and the marker goes away:

```bash
git -C sdk fetch && git -C sdk checkout <sha>
git add sdk && git commit -m "Bump the SDK pin"
```

## Anatomy of a plugin

```
plugins/<name>/
    CMakeLists.txt      one add_replay_emu_plugin() call
    <name>_plugin.c     the glue: the RpEmuAPI the frontend drives
    <Name>.json5        the config template deployed beside the shared object
    LICENSES/           every licence the built artifact is distributed under
    patches/            the patch series applied to upstream/, in order
    upstream/           submodule, pinned at a commit of the emulator's own repository
```

`plugins/stub` is the reference: the smallest thing that builds, deploys and launches. It has no
upstream, so it carries neither `upstream/` nor `patches/`.

A real emulator is never copied into this repository. It arrives as a submodule of its *original*
upstream pinned at a commit, with our changes as a patch series beside it, so provenance stays
honest and this repository's history stays small. `./build.sh` initialises those submodules
shallowly (`--depth 1`) and only for the plugins being built.

## The shared `cmake/`

`cmake/ReplayPlugins.cmake` resolves the SDK, applies the repository-wide configuration, and
includes the SDK's own `ReplaySDK.cmake`. A plugin's `CMakeLists.txt` needs nothing else.

## Licensing

The glue in this repository is MIT (see `LICENSE`). Each plugin's `LICENSES/` carries the licences
its built artifact is actually distributed under, which for a real emulator is the upstream's.
