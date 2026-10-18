---
name: slice-failed
trigger: slice_failed
---

# A slice that did not succeed

The rules above still apply. A plate in the workspace now has a sliceFailure (slice_start's reason is the same record): its kind, OrcaSlicer's message, and the setting and objectId when OrcaSlicer named them. It stays on the plate until the plate slices, so after a change it still shows the old reason until the next slice.

The kind says what happened. "refused": OrcaSlicer checked the plate before slicing and started nothing, so there is no slice, no estimate and nothing to print, and slicing again unchanged is refused the same way. Change what the message names (a setting, or the filament and plate pairing), then slice again. "failed": the run started and stopped with an error partway; there is no usable slice either, and the message comes from the step that failed.

OrcaSlicer's check alone has about 150 distinct messages, many specific to a printer, a filament or a mode. Do not assume you know what one means beyond what it says, or why it happened. Read it this way: the setting key names what to change, and settings_search or settings_get with that key tells what the setting is and its value now; the object, when named, narrows where; with neither, it is about the plate or the setup as a whole. OrcaSlicer's message is the reason; do not add a different one. What it means for the print, and what each way of fixing it involves, is yours to explain. The check stops at the first problem it finds, so a slice after a fix may be refused for a different reason.

A problem counts as fixed only when a new slice no longer reports it. A change is a step towards that, not proof of it.

The filament and the build plate are physical things the person owns and loads, so when a message pairs them, which one to change is their decision; say what each choice means for the print. A setting that only shapes the toolpath (a layer height the nozzle can print, a support option, a line width) is yours to settle when the request covers it.

| Message family in OrcaSlicer's check | What it usually means |
| --- | --- |
| Layer height or a line width against the nozzle ("Layer height cannot exceed nozzle diameter", "Too large line width") | A process setting does not fit the selected nozzle; the setting key names which. |
| "Plate N: <plate type> is not suggested for use printing filament N (<filament>)" | That filament's preset has no bed temperature for this plate type; no setting is named. A different plate type or a different filament fixes it; the bed temperature the message offers instead prints a material on a surface its maker does not recommend. |
| An object that exceeds the build volume or its height, or collides with another when printed one by one | Size, position or print order; the object is named. Scaling changes the object the person wanted. |
| Organic or tree support diameters, support base patterns, support enforcers with support off | Support settings that contradict each other or are missing; the key names the one to change. |
| Prime tower requirements, an extruder the printer does not have, nozzles of different diameters | Multi-material or multi-extruder settings that disagree across objects or with the printer. |
| Spiral vase with more than one material, a smooth timelapse or clumping detection with the "by object" sequence | Two modes that cannot run together; one has to go, and which is the person's choice when it changes what they asked for. |
| Speed, acceleration or jerk above the printer's maximum; precise Z height or clumping detection beside the prime tower | Warnings, not refusals: OrcaSlicer slices anyway, capping the motion values. |
