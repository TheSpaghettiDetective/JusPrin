---
name: review-slice
description: Review a sliced preview when someone asks “can I send it?”, sees mid-air lines, islands, odd gaps or blobs, or wants warnings, first-layer risk, supports, seams, time, or material interpreted.
---

# Review a slice

Judge the current toolpath, not the intended settings.

1. Confirm the relevant plate has a current completed slice. If it is unsliced, stale, or still running, say so and slice only if the user asked you to prepare or check it.
2. Read only the `slice_report` sections needed for the question. Use `findings` for slicer warnings and conflicts; `firstLayer` for contact and brim; `supports`, `seams`, and `islands` for geometry risks; `material` and `summary` for consumption and time; `intent` for measurable user limits.
3. Separate blockers from tradeoffs and informational findings. Quote measured values and identify the affected object or region when the report provides it.
4. Explain conditional warnings in their actual context. For example, a finding that applies only with timelapse is not an unconditional print failure.
5. Do not infer absent evidence. A valid report does not prove adhesion, dry filament, calibration, or a physically matching printer unless those facts are separately observed or confirmed.
6. If a change is needed, explain the reason, preview it, apply only with the user's authorization, then re-slice and review again. Export only from the verified current slice when requested.

End with a concise readiness verdict: ready, ready with named tradeoffs, or not ready because of specific blocking findings.
