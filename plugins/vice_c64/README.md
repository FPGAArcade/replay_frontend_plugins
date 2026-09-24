# VICE (C64)

The emulator is the libretro port of VICE, carried in-tree under `upstream/`.

## The ROMs

The plugin ships Commodore's C64 and drive ROMs, as VICE's own distribution does. They are
Commodore's copyrighted code, not GPL (VICE's `README` says the same), and they are not kept in
this repository: if the rights holder objects, removing them is a build change.

The build fetches every `.bin` under `vice/data/C64/` and `vice/data/DRIVES/` from vice-libretro at
the revision pinned in `CMakeLists.txt`, checks each against `roms.sha256`, and caches them in
`build/downloads/`. They land in `data/vice/C64/` and `data/vice/DRIVES/` beside `vice_c64.so`,
which is where the plugin points VICE's system directory.

To drop them: delete `roms.sha256` and the fetch block in `CMakeLists.txt`, and restore
`requires_bios` in `get_info` and the `bios` block in `VICE_C64.json5`.

## Re-syncing `upstream/`

Upstream carries the ROMs twice, and both copies must go again on every sync:

- delete `upstream/vice/data/**/*.bin`;
- delete the ROM headers in `upstream/include/embedded/` (all but the `*_vpl.h` palettes), the
  `retrodep/embedded/*embedded.c` files other than `c64embedded.c`, and the ROM entries in
  `retrodep/embedded.c` and `retrodep/embedded/c64embedded.c`. VICE reads an embedded ROM before
  any file, so a left-over one silently replaces the fetched set.

`retrodep/ui.c` also loses its `vicerc-dump-*` write, which would land in the plugin's `data/`.
