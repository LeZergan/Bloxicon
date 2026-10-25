# Development notes

This initial repository contains project documentation, an issue form and a
self-contained HTML page preview. Engine source, private deployment tooling,
Git history, credentials, runtime packs, original places and local build
evidence are excluded.

Build commands and dependency setup will be added when the source distribution
is ready. The current scaffold does not claim a self-contained engine build.
Existing dependency authorship and required notices must remain intact when
code is included.

## Checking repository content

The content gate is read-only and reports categories and file locations without
printing matched values:

```powershell
python tools/check-publication.py --identity PRIVATE_IDENTIFIER --forbidden-term EXCLUDED_NAME
```

Repeat either option for additional private identifiers or excluded names. Git
metadata is excluded from this working-file check; review staged files and
commit history separately before pushing future additions.

## Page preview

Open `docs/index.html` locally. It uses embedded styles and local links, with no
external assets, analytics or deployment workflow. GitHub Pages remains off.
