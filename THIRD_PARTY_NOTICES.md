# Third-party notices

The Bloxicon-authored Vita client and modifications are GPL-3.0-only. Existing
third-party notices remain in force; the top-level license does not replace them.

| Component | Location or dependency | Terms |
| --- | --- | --- |
| Luau Ast/Common parser and printer | `PSVita/compat/luau` | MIT; see `LICENSE.txt` and pinned revision in `UPSTREAM.txt` |
| Zstandard decoder | `PSVita/compat/zstd` | BSD; see `LICENSE` and source notices |
| minimp3 | `PSVita/compat/thirdparty/minimp3` | CC0/public-domain dedication in the source headers |
| vitaGL | External VitaSDK dependency | Upstream GPL-3.0 and LGPL-3.0 notices; see upstream `COPYING`, `COPYING.LESSER` and individual source headers |

[vitaGL upstream](https://github.com/Rinnegatamante/vitaGL) supplies its own
license files. Bloxicon's GPL-3.0-only selection applies to Bloxicon code.

The separately obtained engine baseline and its dependencies retain their own
notices and terms. Obtain an authorized baseline and retain its notices when
applying the client changes. Original places, avatar assets, music, interface
sounds, shader compiler modules and runtime packs are separate inputs.

Preserve these notices and the corresponding source and build information when
redistributing a covered build. This source overlay is not a complete standalone
engine distribution or a binary release.
