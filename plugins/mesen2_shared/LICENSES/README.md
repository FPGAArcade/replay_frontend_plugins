# Licences the built Mesen2 plugins are distributed under

All seven Mesen2 plugins link the same upstream, so this set covers every one of them. The plugin
statically links its whole stack, which means each licence below applies to the shipped shared
object, not only to the source in this repository.

Mesen2 itself is GPL-3.0, and that is the licence the built artifact carries. Every bundled
component is either permissive or a "GNU ... version N or, at your option, any later version"
grant, so all of them fold into GPL-3.0 without conflict.

| Component | Licence | Notice lives in |
|---|---|---|
| Mesen2 (Sour) | GPL-3.0 | `GPL-3.0.txt`, from the upstream `LICENSE` |
| LZMA SDK (Igor Pavlov) | Public domain | `Mesen2/SevenZip/*.c` headers |
| miniz | Public domain (Unlicense) | `Mesen2/Utilities/miniz.h` |
| kissfft (Mark Borgerding) | BSD-3-Clause | `Mesen2/Utilities/kissfft.h` |
| magic_enum (Daniil Goncharov) | MIT | `Mesen2/Utilities/magic_enum.hpp` |
| md5 (Alexander Peslyak) | Public domain | `Mesen2/Utilities/md5.cpp` |
| sha1 | Public domain | `Mesen2/Utilities/sha1.cpp` |
| hqx (Maxim Stepin, Cameron Zemek) | LGPL-2.1-or-later | `Mesen2/Utilities/HQX/*.cpp` |
| nes_ntsc (Shay Green) | LGPL-2.1-or-later | `Mesen2/Utilities/NTSC/nes_ntsc.cpp` |
| blip_buf (Shay Green) | LGPL-2.1-or-later | `Mesen2/Utilities/Audio/blip_buf.cpp` |
| Scale2x (Andrea Mazzoleni) | GPL-2.0-or-later | `Mesen2/Utilities/Scale2x/scale2x.cpp` |
| 2xSaI / SuperEagle (Arntzen, De Matteis) | GPL-3.0-or-later | `Mesen2/Utilities/KreedSaiEagle/*.cpp` |

Only the GPL-3.0 text is carried here, because it is the only full licence text the upstream ships.
The LGPL-2.1, GPL-2.0, MIT and BSD-3-Clause texts are not in the tree, and a GPL distribution is
expected to carry the full text of the licences it conveys under. Adding them is a release
requirement rather than a build one, so it belongs to the publish gate that assembles the
distributable, not to this directory alone.
