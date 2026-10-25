# Development status

Bloxicon is a private development project. This repository currently contains
documentation and a page scaffold; it does not contain a buildable engine or
downloadable runtime.

The latest local engine hardening checks completed on 24 October 2026:

| Check | Result and scope |
| :-- | :-- |
| PC engine and Windows client | Build and link passed |
| Vita cross-build | Passed; package audit found no required unresolved symbols |
| Native PC checks | Nine passed, including task forwarding, vehicle input/physics and runtime APIs |
| Focused suites | Sixteen passed, including production texture parsers, request admission and model-error handling |
| Host suite | Passed; original-place checks ran separately |
| Original-place PC screen | 18 files, 12 distinct payloads, 36 headless runs with no process failures; partial or unaudited loading results |
| Windows dependency probe | Header tracking and a Cyrillic/CJK path with spaces verified |
| Protected local files | 1,038 originals, archives, packs and earlier candidate files retained their hashes |
| Native Vita loading | Untested for the current candidate |
| Partial Vita gameplay | Untested for the current candidate |
| Fully tested Vita gameplay | Untested for the current candidate |

These checks apply to the separately maintained local engine candidate. They
are not CI results from this documentation repository and do not establish
complete place compatibility or release readiness.

Known class/property gaps remain. Fresh dependency bootstrap and visual preview
validation are also unfinished. Hardware acceptance must cover original menus,
native controls, scripts/assets, gameplay, ordinary loader responsiveness,
performance/memory and repeat loading without crashes.

The compatibility target remains all original places through 2016. A small
cohort or a crash-free boot is not the entire acceptance target.
