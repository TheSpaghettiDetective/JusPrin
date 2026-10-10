---
name: select-print-setup
description: Reconciles the physical filament, printer and plate with the project's printer, plate, process and filament presets. Use when a new roll or spool is in a bay or slot, the app shows Generic, or the person asks which profile to use, also when that is part of getting a part ready.
---

# Select the print setup

Choose a coherent combination of printer, plate, process and filament rather than tuning each preset on its own.

1. Read the current setup and, when a physical printer matters, what it reports and what the person has confirmed. Treat a missing fact as unknown, and point out where configured and observed disagree.
2. Take selectable names from `presets_list`, searching narrowly and following its pages. A display label is not a preset name.
3. Base the choice on what the model needs and the priorities the person stated. Keep compatibility requirements apart from quality, speed, strength, appearance and cost tradeoffs.
4. `printer_setup_preview` shows the substitutions, compatibility issues and unsaved preset edits a switch would cause. Explain the ones that matter before a switch the person asked for.
5. Tuning values inside a preset is a settings change, not a selection: patch the exact settings, and keep a built-in preset intact by saving a copy.
6. After a setup change, read the workspace again, and treat an earlier slice as out of date until a new one completes.

A question about what a preset means, or general material advice, needs the project only when the answer depends on it.
