# Licences the built ScummVM plugin is distributed under

The plugin statically links its whole stack, so every licence below applies to the shipped shared
object, not only to the source in this repository.

| Component | Licence | Notice lives in |
|---|---|---|
| ScummVM | GPL-3.0-or-later | `GPL-3.0.txt`, from the upstream `COPYING` |
| ScummVM's bundled components | various | `../upstream/LICENSES/`, carried intact |
| libFLAC | BSD-3-Clause | `../external/libflac/` |
| libco | Public domain | `../libco.c` |

ScummVM ships its own `LICENSES/` directory covering the components it bundles, and that directory
is carried in `upstream/` unmodified rather than being summarised here -- it is the upstream's own
record, and reproducing it by hand would only let the two drift apart.

No game data is included, and none may be: ScummVM runs games the user already owns.
