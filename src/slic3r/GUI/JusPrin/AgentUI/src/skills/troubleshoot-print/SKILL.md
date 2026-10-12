---
name: troubleshoot-print
description: "Investigate an observed failed or defective print: warping, lifting, stringing, weak parts, shifts, or spaghetti. Use evidence to distinguish causes and choose a useful next check or retry; a general material question is not a failed-print diagnosis."
---

# Troubleshoot a print

Start with the observed problem rather than a familiar explanation. Diagnosis can end with advice or a question; changing the project belongs to an authorized fix or retry.

## Establish the evidence

Use the description, photos, reported printer state, and relevant project reads to understand where and when the defect appeared. The open project may help, but its current settings are not automatically those used for the failed print. Clarify that relationship when it affects a conclusion; don't refuse useful reads merely because the user did not explicitly identify the project.

Ask a focused question, or a small group of related questions, when the answers separate plausible causes. Avoid an exhaustive questionnaire. Keep observations distinct from hypotheses and name the next check that would change the diagnosis.

## Follow the symptom

- **Lifting or warping:** distinguish loss of first-layer contact from deformation developing later. Consider the affected area, bed contact and brim, material/plate match, and physical preparation or environment. A slicer report cannot establish bed cleanliness or actual adhesion.
- **Stringing or inconsistent extrusion:** compare where the defect occurs with travel, temperature, flow, and retraction settings as relevant. Moisture, obstructions, and extrusion behavior are possible causes, not facts inferred from a selected preset. Avoid immediately increasing retraction to compensate for every symptom.
- **Shifts or spaghetti:** establish whether the part detached, a support failed, or layers shifted while the base stayed attached. Timing and photos can discriminate more usefully than an arbitrary settings patch. A previously successful file does not rule out a physical cause.
- **Breakage or dimensional errors:** distinguish layer separation, a local weak section, and fit or dimensional deviation. Ask about load direction or measured dimensions when needed. Separate model geometry, slicing strategy, and printer/material calibration.

## Choose a useful experiment

Give a short set of plausible causes when uncertainty remains, explaining the evidence and the next discriminating check. Prefer a focused causal change when preparing a retry so the result teaches something; related changes can be made together when their reason is clear. Specify what the person should observe or measure on the next attempt.

If an authorized retry requires choosing a new printing strategy, consult tune-print when useful. Inspect and verify that retry through the normal preparation tools. Do not claim a physical defect is fixed because a setting changed or slicing succeeded; that conclusion requires evidence from the next print.
