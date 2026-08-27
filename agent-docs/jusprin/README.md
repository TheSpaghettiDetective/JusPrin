# JusPrin production documentation

This directory is the canonical starting point for production work on `jusprin-newui`. It preserves the durable product and engineering decisions from `jusprin-v2-poc` without importing the POC's temporary windows, probes, static mocks, generated evidence, or environment-gated startup paths.

Read these documents in order:

1. [Product definition](product-definition.md) — the human/agent boundary, required product surfaces, workflow, and visibility rules.
2. [Production architecture](architecture.md) — the selected native/WebView architecture, ownership rules, component boundaries, and implementation sequence.
3. [Engineering and verification method](engineering-method.md) — how to assess risk and what evidence is required before calling a feature complete.
4. [OrcaSlicer integration guide](orca-integration-guide.md) — known Orca behaviors, lifecycle traps, event and history constraints, and testing lessons.
5. [Fork stewardship](fork-stewardship.md) — required before changing any OrcaSlicer-owned file: where a seam belongs, why additive beats rewritten, how to leave a conflict that resolves correctly, and what evidence a change must record.
6. [Design system](design-system.md) — brand behavior, semantic UI tokens, typography, spacing, accessibility, and asset rules.
7. [POC reference](poc-reference.md) — exact pointers to the spike-only documents, code, commits, logs, screenshots, and generated brand artifacts left on `jusprin-v2-poc`.

## Governing decisions

- JusPrin is an AI-piloted 3D-printing product built on OrcaSlicer, not a replacement slicing engine.
- OrcaSlicer's C++ model, slicing pipeline, viewport, geometry operations, project serialization, and undo stack remain authoritative.
- The production application stays native C++/wxWidgets/OpenGL. The Agent conversation uses a local React/TypeScript application inside `wxWebView`.
- JavaScript renders state and submits typed commands; it never owns editable Orca project state.
- Product policy lives in fork-owned files. OrcaSlicer-owned files receive only small, product-neutral, additive seams — every line changed there is a line this fork re-resolves at every rebase.
- The user owns physical facts, object meaning, desired outcomes, privacy and legal decisions, and authorization of consequential physical actions.
- The production branch must not import experimental POC behavior merely because it demonstrated feasibility. Use the POC as evidence and reimplement through stable production boundaries.

## Source lineage

`jusprin-newui` and `jusprin-v2-poc` share base commit `8500fcdcca` (`v2.4.2`). The POC reference is pinned to final POC commit `9bba835b92`, so future changes to branch names do not make the historical evidence ambiguous.
