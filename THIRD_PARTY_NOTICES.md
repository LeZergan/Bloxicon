# Third-party notices

Bloxicon rendering code is GPL-3.0-only (see Rendering/LICENSE). The rest of
the Bloxicon-authored Vita client and modifications are under the Bloxicon
Source-Available License (see LICENSE). Existing third-party notices remain in
force; the Bloxicon licenses do not replace them.

| Component | Location or dependency | Terms |
| --- | --- | --- |
| Luau Ast/Common parser and printer | `PSVita/compat/luau` | MIT; see `LICENSE.txt` and pinned revision in `UPSTREAM.txt` |
| Zstandard decoder | `PSVita/compat/zstd` | BSD; see `LICENSE` and source notices |
| SoLoud Vita output backend | `PSVita/compat/SoLoudVitaOutput.cpp` | zlib license; see the notice at the top of the file |
| minimp3 | `PSVita/compat/thirdparty/minimp3` | CC0/public-domain dedication in the source headers |
| vitaGL | External VitaSDK dependency | Upstream GPL-3.0 and LGPL-3.0 notices; see upstream `COPYING`, `COPYING.LESSER` and individual source headers |

[vitaGL upstream](https://github.com/Rinnegatamante/vitaGL) supplies its own
license files. The Bloxicon license applies to Bloxicon code only and does not
limit your LGPL rights to modify vitaGL and relink it.

The separately obtained engine baseline and its dependencies retain their own
notices and terms. Obtain an authorized baseline and retain its notices when
applying the client changes. Original places, avatar assets, music, interface
sounds, shader compiler modules and runtime packs are separate inputs.

Preserve these notices and the corresponding source and build information when
redistributing a covered build. This source overlay is not a complete standalone
engine distribution or a binary release.
