> [!CAUTION]
> **DEVELOPMENT — HARDWARE VALIDATION PENDING**
>
> Local engine checks have passed on PC and the Vita cross-build succeeds.
> Native loading, partial gameplay and fully tested gameplay on Vita remain
> unverified for the current candidate.

# Bloxicon

An experimental native PlayStation Vita client for original 2016-era places,
with native controller input and shared engine compatibility work.

[Status](docs/status.md) · [Controls](docs/controls.md) · [Setup](docs/setup.md) · [Development](docs/development.md) · [Page preview](docs/index.html)

| | |
| :-- | :-- |
| Working title | **Bloxicon** |
| Platform | PlayStation Vita |
| Repository | Private project scaffold |
| Compatibility target | Original places through 2016 |
| Controls | Native gamepad input |
| Hardware acceptance | Pending |

## Current state

This repository contains the project page, documentation and issue template.
Engine source and release artifacts are maintained separately and are not
included in this initial scaffold. The working engine and existing candidates
have not been renamed or modified by this repository setup.

Local hardening work covers task scheduling, checked texture sizes, texture
request admission, model-loading errors, vehicle input, render callbacks and
sound loading. See [status and verification scope](docs/status.md).

## Compatibility

Original places must run directly from their original bytes. Compatibility work
belongs in shared engine, loader, renderer, APIs, core scripts or hosting code.
Conversion, removed scripts and per-place patches are not acceptance criteria.

A complete pass requires working original menus, native controls, core gameplay,
required scripts/assets, usable performance and repeated loading without a
crash. Booting or reaching a menu alone is a partial result.

## Controls and presentation

Native controller input remains the gameplay interface. Shared vehicle control
work covers forward/reverse, steering, cancelled input, seat transitions and
respawn. Individual places still require hardware acceptance.

The loader retains existing avatar idle animations and dependencies, current
audio preferences and existing interface feedback assets.

## Reporting an issue

Use the [bug report form](../../issues/new?template=bug-report.yml). Include the
candidate hash, original place SHA256, reproduction steps and observation scope.
Report native loading, partial gameplay and fully tested gameplay separately.

Include sanitized diagnostics only. Keep account information, credentials,
private paths, server details, original place files and runtime packs out of
reports and commits.

## Development

This scaffold has no engine build or deployment workflow. Source preparation,
dependency bootstrap and hardware validation remain separate work. See
[development notes](docs/development.md).

GitHub Pages is disabled. The [HTML preview](docs/index.html) is a local project
page artifact, not a deployed website.
