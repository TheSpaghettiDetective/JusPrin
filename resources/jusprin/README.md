# JusPrin resources

This directory contains curated, production-facing JusPrin assets. Generated decks, PDFs, render trees, archives, inspection output, and design-tool working directories remain on `jusprin-v2-poc` and are indexed in `agent-docs/jusprin/poc-reference.md`.

- `branding/` contains the original gradient mark and approved monochrome variants.
- `ui/` contains the authoritative semantic product UI tokens, `design-tokens.json`, which the shell, the Agent page, and `tests/brand/` all read.
- `images/` contains the application icons the bundle and installer reference directly (`JusPrin.icns`, `JusPrin.ico`).
- `overlay/images/` contains the splash, About and horizontal lockups, monochrome mark, wizard watermark, and window icon PNGs under OrcaSlicer's asset names; `Slic3r::var()` serves them in place of the stock files. Lockups are generated from `branding/jusprin-mark.svg` plus the JusPrin wordmark set in HarmonyOS Sans SC Bold, so they render through nanosvg without a text element.

The original logo was supplied by the user and restored from repository revision `6eecb78b19`. Do not redraw, separate, rotate, stretch, or arbitrarily recolor it.
