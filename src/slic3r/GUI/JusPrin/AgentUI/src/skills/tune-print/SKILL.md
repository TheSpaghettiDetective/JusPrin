---
name: tune-print
description: "Improve a print's strength, appearance, fit, time, material use, or support strategy. Use for tradeoffs such as “make it stronger” or “less support”; simple explanations and explicit setting or preset selections usually need only the existing tools."
---

# Tune a print

Translate the desired outcome into a focused change, then compare the result with the requirement. For a suitability or strategy question, use this reasoning to advise; apply changes when the user has asked for them.

## Establish the target

Read the relevant current settings and project evidence. Identify what should improve and what must stay: dimensions, load direction, visible faces, protected holes, material, or a time limit. Ask about missing meaning only when it changes the strategy. Preserve suitable selections and customizations; changing a material does not itself select or validate a filament or process preset.

## Choose the relevant strategy

- **Strength:** consider load direction, layer orientation, walls, and local reinforcement together. More infill alone may not address the weak region. Use object analysis and semantic regions when the requirement is local; don't infer a load direction from shape alone. Slice estimates do not measure mechanical strength.
- **Appearance:** identify the visible or mating surfaces before trading orientation, seams, layer height, and support contact. A face that looks hidden may still be a critical mating face. Use observed face handles or user annotations for local treatment.
- **Time and material:** compare a current slice baseline with the requested limit. Look at where time or material is spent before choosing changes; retain required detail and strength. Avoid repeated speculative changes when the available report cannot distinguish the alternatives.
- **Supports and orientation:** weigh overhangs, bed contact, stability, load direction, and protected surfaces together. Orca's auto-orient minimizes its own geometric cost, not every user priority. Check removal access and trapped support as well as support quantity. Use support/seam regions when a global setting cannot express the requirement.
- **Dimensional fit:** establish which dimension or clearance matters and use geometry measurements where available. Preserve intentional scale and mating relationships. Distinguish model dimensions from measured printed dimensions; a fit problem can need physical calibration evidence rather than a global scale change.

## Apply and compare

For requested changes, choose the smallest coherent set that tests the strategy. Use object scope for an object-specific requirement where supported; preview settings to see dependencies and normalization before applying. A compatible current preset is a useful starting point, not a reason to overwrite custom values.

Inspect the relevant resulting slice evidence: summary/intent for limits, supports/seams for protected regions, firstLayer/islands for contact and unsupported starts. Refresh geometry handles after edits if needed. Explain actual changes and measured differences, distinguish expected improvements from verified results, and name remaining compromises. If the target cannot be reached while preserving the user's constraints, explain that conflict instead of silently relaxing a requirement.
