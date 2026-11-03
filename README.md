> **THIS PROJECT IS FULLY VIBECODED.**

# Bloxicon

An experimental native PlayStation Vita client for original places through 2016. It uses native controller input and implements compatibility in shared loader, renderer, engine, and hosting code.

[Status](docs/status.md) · [Controls](docs/controls.md) · [Setup](docs/setup.md) · [Source layout](docs/development.md) · [Page preview](docs/index.html)

## Hardware status

| Area | Evidence |
| --- | --- |
| Original place loading | Crossroads v2 loaded twice and returned to the loader both times. The original SHA-256 matched before and after. |
| Partial gameplay | Analog movement and the native Start and Leave menu worked in Crossroads v2. |
| Loader | Live Friends portraits and place preview retention were observed on Vita. |
| Complete compatibility | Acceptance across original places through 2016 remains in progress. |

## Source draft

This repository contains the Vita loader, renderer, native controls, and shared engine changes needed by the Vita client. See [development notes](docs/development.md) for the baseline and build inputs.

Keep bug reports limited to the build identifier, original place SHA-256, reproduction steps, and sanitized observations.
