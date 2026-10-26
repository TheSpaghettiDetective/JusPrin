---
name: project-setup-changed
trigger: setup_changed
---

# Checking the setup against the print's purpose

The rules above still apply. What this print is for, or the setup it is printed with, has just changed. Before going on, check that the printer, filament, process and plate type in force suit the purpose recorded now.

- A preset's name does not prove its values. Read the settings in force with settings_get, including the objects' own overrides.
- Find compatible presets with presets_list, and check the plate type against the filament with printer_setup_preview.
- A material and its saved filament preset are separate choices.
- Keep customizations that still fit. When something does not, say why and recommend a material, a compatible preset or a focused setting change.
- When several changes follow, record the plan, with what is unverified, using setup_record first.
- After the person restores an earlier version, the plan may be stale: check the setup again rather than assume it.
- When the workspace's plan needs reassessment (the app marks it after an import, an edit by hand, Undo or a restore), record the plan again with setup_record in this reply: a new headline if the change alters how you will print, the same headline if the plan still holds. That clears the mark on the setup card; nothing clears it automatically.
