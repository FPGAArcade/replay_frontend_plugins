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
./build.sh mesen2_nes           # a real emulator core
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
    smoke.toml          the boot smoke: fixture, frame count, whether audio is asserted
    smoke/              the fixtures smoke.toml names, each with its provenance record
    LICENSES/           every licence the built artifact is distributed under
    patches/            the patch series applied to upstream/, in order
    upstream/           submodule, pinned at a commit of the emulator's own repository
```

`plugins/stub` is the reference: the smallest thing that builds, deploys and launches. It has no
upstream, so it carries neither `upstream/` nor `patches/`.

**A port stays faithful to the source it came from.** Glue moved here from the frontend keeps
upstream's spelling -- `NULL`, `uint32_t`, `float` -- rather than being converted to this project's
`nullptr` and `u32`/`f32` aliases, and its comments are left alone. The only changes a port carries
are the ones it needs to build here: the include block the SDK's include root requires, and
whatever the plugin boundary genuinely demands. The reason is auditability -- a port that is a move
can be checked against its original with a single `diff`, and one carrying incidental cleanup
cannot. The convention applies in full to code written here, `plugins/stub` included, so the
reference plugin and a ported one do read differently on purpose.

A real emulator normally arrives as a submodule of its *original* upstream pinned at a commit, with
our changes as a patch series beside it, so provenance stays honest and this repository's history
stays small. The exception is an upstream that is both small and pristine: Mesen2 is carried
in-tree, because at 10 MB with no changes against it there is no series for a pin to reproduce and
nothing for the indirection to buy. An upstream we patch, or a large one, is always a submodule --
ScummVM and vAmiga are hundreds of megabytes, and copying those in would put the bytes in this
repository's history permanently. `./build.sh` initialises those submodules
shallowly (`--depth 1`) and only for the plugins being built, then applies that plugin's
`patches/*.patch` in filename order. Both halves are idempotent, so building again does neither
twice, and together they mean the pin plus the series reproduce the exact source that builds.

A change to an emulator's own source is added as a numbered patch (`git format-patch` output,
`0001-...`), never committed into `upstream/`. No plugin here uses this yet -- Mesen2 is carried
in-tree because it is small and we have no changes against it -- but ScummVM and vAmiga will, and
their series are what the pin has to reproduce.

## Smoke

"It loads" is not "it runs". Every plugin here carries a `smoke.toml`, and the smoke host boots the
built shared object against it: mount the fixture, run the frames, and fail unless the plugin is
still running with a framebuffer that is not blank.

```bash
./build.sh stub --smoke
```

is informational — it prints the verdict and leaves the build's own exit status alone. The gate
that stops a plugin being published lives in the release pipeline.

`smoke.toml` is the whole of it; adding a plugin's smoke is writing one file:

```toml
fixture = "smoke/fixture.stub"   # relative to this file; optional, see below
frames = 10                      # run this many before the assertions
timeout_seconds = 10             # longer than this is a hang, not a slow core
audio = true                     # also assert the plugin produced audio (optional)
```

A machine that comes up on its own ROM with an empty drive omits `fixture` entirely. The host then
mounts nothing -- the plugin is handed a null path -- and asserts against the screen the machine
reaches by itself. A console needs a cartridge to show anything, so the Mesen2 plugins all name a
fixture; a home computer that boots to its own prompt would not.

`frames` has to run past the whole boot, not just the first picture. A machine typically draws a
boot screen, blanks while it clears, and only then settles, so a count chosen just past the first
picture lands in the gap and fails.

Every fixture needs a `<fixture>.provenance.toml` beside it, and a run whose fixture has none
fails:

```toml
source = "where this file came from"
license = "what it is distributed under"
```

Prefer an open BIOS replacement over a dumped ROM — EmuTOS, AROS, the open C64 ROMs. A private CI
ROM store is the fallback for a system that has no open equivalent, and is recorded as such in the
fixture's provenance; it is never the default.

`smoke/` holds the host itself. It is SDK-only: it links nothing of the frontend, and the
`fl_*`/`arena_*` symbols a plugin binds to come from the host executable, which is the frontend's
side of the same ABI in miniature. A plugin calling a host symbol the smoke host does not implement
fails at load with that symbol named, and the fix is to implement it there.

`smoke/selftest.sh` is the host's own test. It checks that the stub passes, that a working fake
passes, and that each way a plugin or its configuration can fail does fail *and says why*: a plugin
built to hang, one with a blank framebuffer, one reporting the wrong ABI version, one leaving a
required vtable slot empty, one asserting audio it never produces, a fixture without provenance,
and a malformed `smoke.toml`. It also runs `./build.sh stub --smoke` both ways, to check that a red
smoke still leaves the build's exit status alone. Run it after changing anything under `smoke/`.

## Tests

Two suites, both self-contained and offline:

```bash
smoke/selftest.sh              # the smoke host: 18 cases
scripts/upstream_selftest.sh   # prepare_upstream: 6 cases
```

`smoke/selftest.sh` is described above. `scripts/upstream_selftest.sh` covers the half of
`./build.sh` that every emulator plugin depends on and no plugin's own build exercises: that an
uninitialised upstream is fetched, that its patch series is applied, that a second run refetches
and reapplies nothing, and that a patch which does not apply stops the build with the reason. It
builds a throwaway repository under `/tmp` shaped like this one, so it needs neither the network
nor a checked-out emulator.

## The shared `cmake/`

`cmake/ReplayPlugins.cmake` resolves the SDK, applies the repository-wide configuration, and
includes the SDK's own `ReplaySDK.cmake`. A plugin's `CMakeLists.txt` needs nothing else.

## Upstream mirrors

Every emulator here is pinned at a commit of a repository we do not control, so the pin is only as
durable as the upstream. Against that, FPGAArcade is to keep a mirror of each one. The mirrors are
insurance, never the build's source: `.gitmodules` points at the real upstream, and a mirror would
be switched to by hand if an upstream went away.

| Plugin | Upstream |
|--------|----------|
| mesen2 | https://github.com/SourMesen/Mesen2 |
| scummvm | https://github.com/scummvm/scummvm |
| vamiga | https://github.com/dirkwhoffmann/vAmiga |
| vice | https://sourceforge.net/projects/vice-emu (Subversion; git mirror at https://github.com/VICE-Team/svn-mirror) |

Creating the FPGAArcade-owned mirrors is a manual step and is not done yet.

## Licensing

The glue in this repository is MIT (see `LICENSE`). Each plugin's `LICENSES/` carries the licences
its built artifact is actually distributed under, which for a real emulator is the upstream's.
