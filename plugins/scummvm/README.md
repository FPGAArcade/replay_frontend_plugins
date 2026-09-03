# ScummVM

The whole upstream is carried in-tree under `upstream/`, and `external/` holds the bundled libFLAC
decoder it links.

Unlike the emulators beside it, ScummVM is not scaffolding waiting for an FPGA core: it runs game
engines, not hardware, so no core will ever replace it.

## Engines

48 engines are enabled in `CMakeLists.txt` -- SCUMM, SCI, AGI, Sword 1 and 2, Sky, Queen, Kyra,
Mohawk, Grim and the rest. Two are commented out, `zvision` because it needs FreeType. The list is
curated rather than globbed, because globbing every engine directory picks up files upstream's own
`module.mk` excludes.

Some engines need a data file from `upstream/dists/engine-data` to start -- `drascula`, `kyra`,
`teenagent`, `hugo`, `access` and `cryo` among them -- and installing those files is not wired up
here. Engines driven purely by the game's own data, which is most of them, are unaffected.

## Local changes to `upstream/`

Because the copy is in-tree there is no patch series; the edits are simply present. Re-apply them
if you re-sync:

- `base/version.cpp`: `gScummVMBuildDate`, `gScummVMVersionDate` and `gScummVMFullVersion` all
  embedded `__DATE__` and `__TIME__`. A published plugin has to rebuild byte-identically, so the
  timestamps are gone and the toolchain's `-Werror=date-time` fails the build if they return. The
  version and revision still identify the build; only the clock reading went.

## No game data

None is included and none may be: ScummVM runs games the user already owns.
