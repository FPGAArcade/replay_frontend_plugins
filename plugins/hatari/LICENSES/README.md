# Licences the built hatari plugin is distributed under

The plugin statically links its whole stack, so every licence below applies to the shipped
shared object, not only to this repository's source.

| File | Covers |
|------|--------|
| `GPL-2.0.txt` | hatariB, the hatari source it carries, and the EmuTOS images compiled into the artifact |
| `SDL2-zlib.txt` | SDL2, linked statically |
| `zlib.txt` | zlib, linked statically |
| `libretro-MIT.txt` | `libretro.h`, the API header hatariB's core implements |

The glue in this repository — `hatari_plugin.c` and `CMakeLists.txt` — is MIT, like the rest of
the repository. GPL-2.0 governs the artifact as a whole because hatari is linked into it.
