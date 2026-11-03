# Vita source layout

| Path | Purpose |
| --- | --- |
| `PSVita/renderer`, `PSVita/compat`, `PSVita/src` | Loader, renderer, native controls, and Vita compatibility code |
| `App`, `Base`, `Network`, `Rendering` | New shared source used by the Vita client |
| `patches/engine.patch` | Changes to existing engine files at baseline commit `e0cfac59f` |

On an authorized baseline checkout, apply the engine patch with `git apply --unidiff-zero patches/engine.patch` and overlay the source directories. A Vita build requires VitaSDK and the project's third-party build dependencies. The small LiveArea and boot branding assets are included for packaging. The runtime pack, places and third-party game assets are supplied separately.

Copy `PSVita/assets/site.example.json` to `PSVita/assets/site.json` and configure your own HTTPS account service for a connected build. Without a local site file, packaging uses the example with a placeholder origin.

Run `python tools/check-publication.py` against a source export. Review commit authorship and existing history separately before a public release.
