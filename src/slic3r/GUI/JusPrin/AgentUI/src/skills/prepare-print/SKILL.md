---
name: prepare-print
description: Own a general end-to-end request such as “get this ready” or “make this printable” when no specific setup, support, slice-review, or failed-print problem dominates. End at a checked slice; export only when requested.
---

# Prepare a print

Use this when the person delegates the outcome instead of asking for one specific setting change. The core print-request policy already owns intent, planning, slice checking, and export; this skill adds how to scope an open-ended preparation request without turning it into a ritual.

1. Inspect the workspace before making claims about the model or setup. Use only the current session's IDs.
2. Find the smallest preparation path that satisfies the stated purpose. Ask only for information whose answer would materially change that path; otherwise state a conservative, reversible assumption.
3. If the original request is primarily a preset mismatch, support problem, slice review, or failed-print diagnosis, the matching specialist skill should have been used instead of this umbrella skill. Do not load another skill merely because every print has a setup and may have overhangs.
4. Inspect geometry only when orientation, scale, placement, thin features, or repair could change the plan. A simple known-good part does not need every analysis tool.
5. Check the configured printer, plate, process, and every filament slot before changing them. Preserve a valid setup when the model gives no reason to replace it.
6. After following the core print-request policy, summarize what is ready, the assumptions that remain, and any named tradeoff. “Slice this” is not permission to export.

Keep exploration proportional. A simple known-good part does not need every geometry tool or every slice-report section.
