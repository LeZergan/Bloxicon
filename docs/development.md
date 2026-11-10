# Vita source layout

| Path | Purpose |
| --- | --- |
| `PSVita/renderer`, `PSVita/compat`, `PSVita/src` | Loader, renderer, native controls, and Vita compatibility code |
| `App`, `Base`, `Network`, `Rendering` | New shared source used by the Vita client |
| `patches/engine.patch` | Changes to existing engine files at baseline commit `e0cfac59f` |

Use a separate authorized engine checkout at baseline commit `e0cfac59f`.
Validate the engine patch before applying it:

```sh
python tools/apply-engine-patch.py --engine-dir /path/to/engine
python tools/apply-engine-patch.py --engine-dir /path/to/engine --apply
```

The helper requires unchanged baseline target files. It prepares the patch in a
temporary directory, clears two legacy telemetry declarations, validates the
complete patch, and writes engine changes only with `--apply`. Removed baseline
credential values are never stored in this repository or printed. Apply this
patch through the helper rather than invoking `git apply` directly.

The helper also expresses the baseline loopback entry with the platform's
symbolic networking constant, keeping literal addresses out of patch removals.

Overlay this repository's `App`, `Base`, `Network`, `Rendering` and `PSVita`
directories onto that separate checkout. A Vita build requires VitaSDK and the
project's third-party build dependencies. The small LiveArea and boot branding
assets are included for packaging. Runtime packs, places and third-party game
assets are supplied separately. Never replace an original place with an export.

Copy `PSVita/assets/site.example.json` to `PSVita/assets/site.json` and configure your own HTTPS account service for a connected build. Without a local site file, packaging uses the example with a placeholder origin.

The optional pinned-host DNS fallback in
`App/include/util/VitaOfficialEndpoint.h` is disabled in this source export.
Configure the trusted artwork origin in `App/include/util/BloxiconPolicy.h`
when supplying your own account service. Keep authenticated admission, TLS
verification and network policy enabled.

Run `python tools/check-publication.py --history` and Gitleaks against both
the source tree and all reachable history before pushing. The Gitleaks config
allows only two reviewed Zstandard SIMD expressions that contain no credentials.
Commit metadata uses a project identity. The checks never print matched values.

The privacy gate rejects literal IPv4 and IPv6 addresses, including loopback,
unspecified and documentation examples, in files and reachable Git history.
The double-colon grammar token in the vendored Luau lexer/printer is language
syntax and is checked separately from network addresses.

Rendering code (`Rendering/` and the rendering parts of the engine patch) is
GPL-3.0-only. The rest of the Vita client is under the Bloxicon Source-Available
License: modifications and new clients based on it need the owner's written
permission, requested per base through a permission-request issue.
Preserve all third-party notices listed in the repository root.
