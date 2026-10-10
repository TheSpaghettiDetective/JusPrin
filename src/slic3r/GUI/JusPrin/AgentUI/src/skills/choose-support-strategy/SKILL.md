---
name: choose-support-strategy
description: Weighs orientation, supports, removal and surface finish as one decision. Use when the person asks whether supports are needed, sees a forest of tree supports, fears damage removing them, or needs clean letters, faces, holes, bridges or overhangs, also when that is part of setting a part up.
---

# Choose a support strategy

Treat orientation, support placement, removability, dimensional accuracy and surface finish as one decision.

## Judging what the print needs

1. Look at the object as it stands. Use `object_analyze` when geometric evidence is needed; its orientations compare candidates by bed contact, overhang burden and the faces that would rest on the bed, without moving anything.
2. Identify what must be protected: show faces, mating faces, holes, bridges, narrow tips, internal cavities. Ask which face matters only when the project and the person's words leave it unclear.
3. Weigh the options: a strategy is better when it reduces support without creating a worse first layer, a weak layer direction, trapped support or an unacceptable finish.
4. Read how the existing slice supports the part from the `supports`, `firstLayer`, `islands` and `seams` sections of `slice_report`. When that slice is missing or out of date, give the judgement from geometry and say that it rests on geometry alone.

Easy removal and a clean protected surface are claims about toolpaths: state them as measured when a slice shows them, and as expected when only geometry does. When two strategies remain close, present the tradeoff and recommend one from the person's stated priority.

## When the person asked for the change

1. Change the smallest process or object-scoped set of settings that carries out the strategy.
2. Use regions (`region_annotate`) where support or seam treatment must differ in one area. Keep region labels tied to observed geometry, and report when a region loses its binding after the model is edited.
3. Compare the same report sections on the new slice before saying the strategy worked.
