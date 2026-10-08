---
name: choose-support-strategy
description: Decide orientation and supports when someone asks “do I need supports?”, sees a forest of trees, fears removal damage, or needs clean letters, faces, holes, bridges, or overhangs.
---

# Choose a support strategy

Treat orientation, support placement, removability, dimensional accuracy, and surface finish as one decision.

1. Inspect the object and its current placement. Use `object_analyze` when geometric evidence is needed; compare plausible orientations by bed contact, overhang burden, critical faces, and stability.
2. Identify the surfaces and features that must be protected: show faces, mating faces, holes, bridges, narrow tips, or internal cavities. Ask which face matters only when the project and user input do not make it clear.
3. Prefer geometry and orientation that reduce support without creating a worse first layer, weak layer direction, trapped support, or unacceptable finish.
4. Use regions when support behavior or seam treatment must differ in a specific geometric area. Keep region labels tied to observed geometry; report when a region loses its binding after model edits.
5. Read current settings before proposing changes. Preview the smallest process or object-scoped patch that tests the strategy, then record the decision and its tradeoffs in the plan.
6. Slice and compare the relevant `slice_report` sections, especially `supports`, `firstLayer`, `islands`, and `seams`. Reassess rather than declaring success from settings alone.

Do not promise easy removal or a clean protected surface without toolpath evidence. If two strategies remain close, present the tradeoff and recommend one based on the user's stated priority.
