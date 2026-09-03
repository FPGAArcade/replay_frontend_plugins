# vAmiga (Amiga)

The emulator is vAmiga, carried in-tree under `upstream/`.

## Only `Core/` is here

vAmiga's repository is about 115 MB, and the build uses one directory of it. What is deliberately
absent is the macOS GUI application, the Xcode project, a 42 MB manual and 42 MB of documentation
-- none of which a headless emulator plugin compiles, links or reads. Keeping only `Core/` brings
the copy to under 8 MB, and since an in-tree copy lives in this repository's history permanently,
that difference is worth the one-line note.

The upstream `LICENSE` sits at the repository root, so it is *not* inside `Core/`. It is copied to
`LICENSES/MPL-2.0.txt` instead. **If you re-sync `upstream/`, copy it again** -- MPL-2.0 requires
the licence to travel with the code, and trimming the tree is exactly how it would get lost.

## The Kickstart shim

`hle_rom.c` implements enough of AmigaDOS -- `AllocMem`, `FreeMem` and friends -- for the machine
to reach a usable state with no Kickstart ROM present. That is what lets this plugin smoke without
shipping or requiring Commodore's copyrighted ROM.

It runs a heap inside the emulated Amiga's own RAM, and uses TLSF for it. `tlsf.c` and `tlsf.h` are
vendored beside the plugin rather than reached for inside flowi: they live in flowi's private
sources, and a plugin has no access to the host's internals by design.

## Local changes to `upstream/`

Because the copy is in-tree there is no patch series; the edits are simply present. Re-apply them
if you re-sync:

- `Core/Components/Amiga.cpp`: `Amiga::build()` returned a string containing `__DATE__` and
  `__TIME__`. A published plugin has to rebuild byte-identically, so the timestamp is removed and
  the toolchain's `-Werror=date-time` fails the build if it comes back.
