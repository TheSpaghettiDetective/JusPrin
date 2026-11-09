# Saved project stores

Each folder is named for a JusPrin release and holds one local project that
release saved: a 20 mm box with one chat turn, copied from
`<data dir>/jusprin/projects/` after the app quit. The runtime folders
(`lock`, `pending`, `materialized`, an empty `operations`) are left out, and
the path the model was opened from is replaced with a neutral one. The
2.0.0 project was saved by a build from just before that release, so its
`pins.json` is the bare list such builds wrote, not the numbered file the
release writes.

`verify_released_project_store` in `tests/workspace/real_adapter_harness.cpp`
opens every project here with the current build. When a change to the store's
files breaks one, add the step that upgrades the old format
(`src/slic3r/GUI/JusPrin/Workspace/StoredFormat.hpp`) rather than changing the
saved files. Add a folder for each release that changes the store's files.
