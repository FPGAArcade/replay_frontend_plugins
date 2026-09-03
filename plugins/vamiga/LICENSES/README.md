# Licences the built vAmiga plugin is distributed under

The plugin statically links its whole stack, so every licence below applies to the shipped shared
object, not only to the source in this repository.

| Component | Licence | Notice lives in |
|---|---|---|
| vAmiga (Dirk W. Hoffmann) | MPL-2.0 | `MPL-2.0.txt`, from the upstream `LICENSE` |
| DiagROM (John Hertell) | MIT | `upstream/Core/Media/RomFiles/DiagRom.h` |
| cpp-httplib (Yuji Hirose) | MIT | `upstream/Core/ThirdParty/httplib.h` |
| LZ4 (Yann Collet) | BSD-2-Clause | `upstream/Core/ThirdParty/lz4.c` |
| xdms | see its sources | `upstream/Core/ThirdParty/xdms/` |
| TLSF (Matthew Conte) | BSD-3-Clause | `../tlsf.h` |

The upstream `LICENSE` is carried here explicitly because this plugin keeps only vAmiga's `Core/`,
and that file lives at the upstream root. MPL-2.0 requires it to accompany the code, so trimming
the copy without copying it here would have dropped it.
