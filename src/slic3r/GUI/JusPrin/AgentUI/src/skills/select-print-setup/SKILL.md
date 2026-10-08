---
name: select-print-setup
description: Use when someone says a new roll or spool is in a bay or slot, Orca shows Generic, or they ask which profile to use. Reconcile physical filament, printer, plate, and process setup.
---

# Select the print setup

Choose a coherent combination of printer, plate, process, and filament rather than optimizing each preset independently.

1. Read the current workspace setup and, when a physical printer matters, its observed or user-confirmed facts. Treat missing facts as unknown and call out configured-versus-observed mismatches.
2. Use `presets_list` for canonical selectable names. Search narrowly and paginate when needed; do not invent a preset from its display label.
3. Base the choice on the model's needs and the user's stated priorities. Separate compatibility requirements from quality, speed, strength, appearance, and cost tradeoffs.
4. Use `printer_setup_preview` before `printer_setup`. Explain substitutions, compatibility issues, and unsaved preset edits before applying. Never discard unsaved edits unless the user has agreed.
5. If the user asks to tune values inside a preset rather than select a preset, read the exact settings, preview a scoped patch, and preserve built-in presets by saving a copy when required.
6. After applying setup, inspect the workspace again. If a slice already existed, assume it is stale until a new slice completes.

When the user asks only what a preset means or wants general material advice, answer directly unless the live project is needed.
