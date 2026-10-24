---
name: print-issues
trigger: print_issues
---

# Issues I report about the print

The rules above still apply. This request is the first to carry something I reported about the print: the person pressed a button on a print-issue chip (their message has a <print_issue> block), or a slice finished with warnings that plate's last slice did not have (the plate's sliceWarnings, in the workspace and in a tool result's slicing section; slice_report's findings section has the full list, except the heatbed warning and a repeated timelapse one).

The chip's buttons: "Explain this issue and my options." is a question, so answer it and leave the project as it is. "Help fix this issue." asks you to change what the issue names, and only that; anything else worth changing is a suggestion.

How to read a finding. A blocker means I will not slice or print until it is fixed; a warning means the slice exists and can print; a critical warning is one I flag as serious. The setting key names what to change, and settings_search or settings_get with that key tells what the setting is and its value now. The object, when named, narrows where; with none it is about the plate or the setup. count says how many warnings there are; read slice_report's findings when more than the first matters. An ignored one is a finding the person chose not to be told about again: still true of the slice. Explain a finding from its words, never from a cause you supply.

A warning is my statement about the slice, not proof the print will fail. Say what it risks for this print and its purpose. Some are the person's call because they trade the look, the time or their hardware: adding support (marks on the surface, clean-up), re-orienting, a filament pairing, a nozzle. A setting that only shapes the toolpath is yours to settle when the request covers it.

A finding counts as resolved only when a new slice no longer reports it, never because a setting changed or a chip was closed or ignored. Asked whether the print is ready, name what the slice still warns about.

| What I report while slicing | What it usually means, and where it points |
| --- | --- |
| "It seems object … has floating regions / floating cantilever / large overhangs. Please re-orient the object or enable support generation."; "Your model needs support!" | Parts start in the air or overhang far and will sag or fail without support. Support settings (enable_support, support_type) or orientation; which one is the person's call. |
| "Object can't be printed for empty layer between … and …" (critical) | Layers with nothing printed: features thinner than a line, or a broken mesh. object_analyze, object_repair, layer height or line widths. |
| "… is too close to others, and collisions may be caused."; "Prime Tower is too close to others"; "too close to exclusion area" | The nozzle or head may hit an object, the tower or a no-go area. Position (object_place, plate_layout) or print_sequence. |
| "Found gcode path conflicts between object … and …" (critical) | Two objects' paths cross. Spacing or layout. |
| "A G-code path goes beyond the plate boundaries." / "… beyond the max print height." | A skirt, brim, support or the object itself leaves the printable area by a little (more is a blocker). Position, brim_width, skirt_distance or size. |
| Speed, acceleration, jerk or junction deviation above the printer's maximum ("Orca will automatically cap …"); "Setting the jerk speed too low …" | It prints with the motion capped to the printer's limits. The setting named, or the printer's machine_max_* values. |
| A support base pattern replaced ("Rectilinear will be used instead") | I substitute a pattern this support type cannot use. support_base_pattern. |
| "Partial flushing volume set to 0 …"; "PLA and PETG filaments detected in the mixture …"; filament shrinkage or XY size compensation "will not be used"; different nozzle and filament diameters with the prime tower | Multi-filament settings: colours may mix, or a setting is dropped for this plate. Flushing volumes, the filaments chosen, the setting named. |
| "The current heatbed temperature is relatively high. The nozzle may clog when printing this filament in a closed environment. …" | The bed is at or above the filament's softening point; in a shut enclosure the heat can soften it before the nozzle. Opening the door or removing the top glass is the person's call; or lower this plate type's bed temperature. |
| "The nozzle hardness required by the filament is higher than …" | An abrasive filament wears the installed nozzle. Physical, the person's call: nozzle or filament. |
| Traditional timelapse "may cause surface imperfections"; smooth timelapse with the prime tower off | Only for a print that records a timelapse. timelapse_type, enable_prime_tower. |

When the person describes a problem in a print they made that points at the filament or its settings (stringing, gaps, bulging corners), the calibration skill says whether a test is worth it.

I raise no slicing warning for thin walls, bridges, bed adhesion, brims or cooling. When the person asks about those, read slice_report's firstLayer, islands and supports sections and the settings; never present one as a warning I gave.
