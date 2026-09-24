# ScummVM

The whole upstream is carried in-tree under `upstream/`, and `external/` holds the bundled libFLAC
decoder it links.

Unlike the emulators beside it, ScummVM is not scaffolding waiting for an FPGA core: it runs game
engines, not hardware, so no core will ever replace it.

## Unrostered

ScummVM builds here but is not published: it stays out of every release plugin list and index until
both criticals below are fixed, and nothing else waits on it.

- [#13](https://github.com/FPGAArcade/replay_frontend_plugins/issues/13): 47 of 48 engines are
  unreachable. `generate_engines.sh` registers only SCUMM, so the rest are linked but never detected.
  Fixing it means generating both engine tables from `ENABLED_ENGINES`.
- [#14](https://github.com/FPGAArcade/replay_frontend_plugins/issues/14): data files are never
  installed. The engine-data files some engines need to start are not shipped, and
  `addSysArchivesToSearchSet` searches nowhere. Fixing it means a `scummvm-data` companion artifact
  and a search path pointing at it.

Rostering means closing both, adding a `smoke.toml`, and adding `scummvm` to the published set.

## Engines

48 engines are enabled in `CMakeLists.txt` -- SCUMM, SCI, AGI, Sword 1 and 2, Sky, Queen, Kyra,
Mohawk, Grim and the rest. Two are commented out, `zvision` because it needs FreeType. The list is
curated rather than globbed, because globbing every engine directory picks up files upstream's own
`module.mk` excludes.

## Local changes to `upstream/`

Because the copy is in-tree there is no patch series; the edits are simply present. Re-apply them
if you re-sync:

- `base/version.cpp`: `gScummVMBuildDate`, `gScummVMVersionDate` and `gScummVMFullVersion` all
  embedded `__DATE__` and `__TIME__`. A published plugin has to rebuild byte-identically, so the
  timestamps are gone and the toolchain's `-Werror=date-time` fails the build if they return. The
  version and revision still identify the build; only the clock reading went.

## No game data

None is included and none may be: ScummVM runs games the user already owns.
