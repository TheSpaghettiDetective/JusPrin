# JusPrin resources

This directory contains curated, production-facing JusPrin assets. Generated decks, PDFs, render trees, archives, inspection output, and design-tool working directories remain on `jusprin-v2-poc` and are indexed in `agent-docs/jusprin/poc-reference.md`.

- `branding/` contains the soft-serve mark in the gradient, ink and white.
- `ui/` contains the authoritative semantic product UI tokens, `design-tokens.json`, which the shell, the Agent page, and `tests/brand/` all read.
- `images/` contains the application icons the bundle and installer reference directly (`JusPrin.icns`, `JusPrin.ico`).
- `overlay/images/` contains the splash, About and horizontal lockups, monochrome mark, wizard watermark, and window icon PNGs under OrcaSlicer's asset names; `Slic3r::var()` serves them in place of the stock files. They also cover `OrcaSlicer.svg` (message, release-note and update dialogs) and `OrcaSlicer-mac_256px.ico` (the Mac Dock icon while the app runs).

Every logo file (marks, lockups, overlays, app icons) is drawn by `src/slic3r/GUI/JusPrin/Brand/brand_logo.py` from one geometry: the cone, and the name in HarmonyOS Sans SC Bold cut into print layers. The output is flat paths with one linear gradient, so nanosvg renders it without masks or text elements. Change the logo there and rerun it; do not edit the generated files by hand, and do not stretch or recolor them.
