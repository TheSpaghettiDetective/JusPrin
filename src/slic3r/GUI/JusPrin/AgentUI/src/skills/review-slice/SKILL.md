---
name: review-slice
description: Interprets a sliced preview and gives a readiness verdict. Use when the person asks whether they can send it, sees mid-air lines, islands, odd gaps or blobs, or wants warnings, first-layer risk, supports, seams, time or material explained.
---

# Review a slice

Judge the current toolpath, not the intended settings.

1. Confirm the plate has a current, completed slice. If it is unsliced, out of date or still running, say so: the review is of what exists.
2. Read only the `slice_report` sections the question needs: `findings` for slicer warnings and conflicts; `firstLayer` for contact and brim; `supports`, `seams` and `islands` for geometry risks; `material` and `summary` for consumption and time; `intent` for limits the person set.
3. Separate blockers from tradeoffs and from information. Quote measured values, and name the affected object or region when the report gives it.
4. Explain a conditional warning in its context: a finding that applies only with timelapse is not a failure for a print without one.
5. A clean report does not prove adhesion, dry filament, calibration or a matching physical printer. Say so when the verdict depends on one of them.

End with a verdict: ready, ready with named tradeoffs, or not ready because of specific blocking findings.
