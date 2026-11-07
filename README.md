> **THIS PROJECT IS FULLY VIBECODED.**

# Bloxicon

An experimental native PlayStation Vita client for original places through 2016. It uses native controller input and implements compatibility in the shared loader, renderer, engine, APIs and core scripts.

[Status](docs/status.md) | [Controls](docs/controls.md) | [Setup](docs/setup.md) | [Source layout](docs/development.md) | [Page preview](docs/index.html)

## Hardware status

The observations below were recorded before the current source refresh. This
source publication does not certify a new hardware-tested build.

| Area | Evidence |
| --- | --- |
| Original place loading | Crossroads v2 loaded twice and returned to the loader both times. The original SHA-256 matched before and after. |
| Partial gameplay | Analog movement and the native Start and Leave menu worked in Crossroads v2. |
| Loader | Live Friends portraits and place preview retention were observed on Vita. |
| Complete compatibility | Acceptance across original places through 2016 remains in progress. |

## Source draft

This repository contains the Vita loader, renderer, native controls, and shared engine changes needed by the Vita client. See [development notes](docs/development.md) for the baseline and build inputs.

The latest source refresh includes shared persistence, avatar-pack installation,
loader diagnostics, badge synchronization, network transport and rendering work.
Operational endpoints are placeholders; configure your own service locally.
The engine patch is checked against its documented baseline without including
removed baseline credentials.

Only client source and reviewed title branding are included. Places, runtime
packs, account data, host services, deployment tools and build artifacts are
separate inputs. The repository is a source overlay, so building also requires
an authorized engine baseline and the listed dependencies.

## License

Bloxicon-authored code and modifications are [GNU GPL version 3 only](LICENSE)
(`GPL-3.0-only`). Dependencies retain their original licenses and notices; see
[third-party notices](THIRD_PARTY_NOTICES.md).

Open a [bug report](../../issues/new?template=bug-report.yml), select `v1`, and describe the issue in your own words.
