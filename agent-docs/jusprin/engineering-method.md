# JusPrin engineering and verification method

**Status:** Required planning, implementation, and release-verification method for `jusprin-newui`.

JusPrin is changing the presentation of a mature manufacturing application. The main engineering risk is not whether common UI technology can work; it is whether new presentation code bypasses, duplicates, or destabilizes authoritative Orca behavior.

## Risk vocabulary

Risk means residual, evidence-backed uncertainty that could materially affect architecture, correctness, delivery, regressions, or a physical print after mature precedent has been considered.

Keep these categories separate:

- **Feasibility:** whether a technical boundary can work at all.
- **Integration:** whether proven components can be connected without unsafe ownership or duplicated behavior.
- **Product behavior:** uncertainty about what the feature must do.
- **Delivery effort:** understood implementation work, even when substantial.
- **Regression exposure:** risk of disturbing mature Orca behavior.
- **Platform QA:** evidence required for a particular OS, WebView backend, display stack, input method, or DPI.

A known bug, untested platform, or inconvenient implementation detail is not automatically an architecture risk.

## Required assessment process

Before adding a spike or assigning a risk label:

1. Define the user-visible requirement without assuming an implementation or cause.
2. Classify the underlying technical capability.
3. Search for mature precedent in the current Orca code, Orca history and forks, comparable slicers, mature same-stack applications, and finally framework documentation.
4. State what the precedent proves and where it differs.
5. Isolate only the unproven delta.
6. Record the plausible failure, evidence for likelihood, consequence, blast radius, reversibility, and cheapest resolving evidence.
7. Choose code inspection, a focused test, an integration prototype, platform validation, or a feasibility spike according to the uncertainty.

A new visual arrangement is not necessarily a new technical capability. Spike only consequential uncertainty that mature code and focused tests cannot answer.

## Current evidence ledger

| Requirement | Established evidence | Remaining production delta | Current classification |
|---|---|---|---|
| Native shell | Orca already composes native panels and `GLCanvas3D`; the exact native/WebView boundary ran on macOS and Linux/X11 | Production shell behavior and remaining platform/input coverage | Very low feasibility; integration and platform QA remain |
| Agent pane and bridge | Orca has local/remote WebViews, script handlers, and native/JS communication | Versioned schema, recovery, focus rules, authentication, production UI | Low architecture risk |
| Thin object/plate pane | `GUI_ObjectList`, `ObjectDataViewModel`, `Plater`, and `PartPlate` contain the behavior | A small behavior-oriented API without copied presentation logic | Low feasibility; medium integration |
| Canvas action strip | Orca projects selection bounds and activates gizmos | Product positioning rules and reusable controller | Low |
| Exact transform entry | `GizmoObjectManipulation` applies values and snapshots | Separate behavior from old ImGui presentation | Low feasibility; medium coupling |
| Semantic facet annotations | Existing painter tools prove picking, painting, visualization, copying, undo, and persistence | Arbitrary label schema and lifecycle, especially topology changes | Low feasibility; medium data-model risk |
| Gizmo rollout | Existing tools share selection, rendering, and history infrastructure | Different interaction families need distinct adapters | Medium delivery/regression; low architecture |
| Slicing, picking, history, Preview | Mature production subsystems retained unchanged | Ensure presentation never bypasses them | Low when boundary is enforced |

Update this ledger when evidence changes. Do not carry an old risk label forward without checking current code and results.

## Test strategy

### Regression baselines and failure classification

Run the closest existing test and the broadest practical baseline before changing
an upstream-owned behavior. Record pre-existing failures by exact test name and
failure mode so later runs can distinguish a regression from inherited state.

When a broad suite fails, rerun the exact case independently before assigning a
cause. Record repeatable failures as regressions until disproved; record a
one-time timeout, deadlock, or infrastructure failure separately with the
isolated rerun result. A green focused suite does not erase a new repeatable
broad-suite failure.

### Contract tests

Test strong IDs, invalid/missing/stale identifiers, command errors, subscription lifetime, event coalescing, revision behavior, and fake-workspace history without linking the GUI. Test both successful and unsuccessful commands, and assert that reported results agree with authoritative state.

### Real-adapter application tests

A fake cannot validate `Plater`, `GLCanvas3D`, plate membership, `GUI_ObjectList` dependencies, event ordering, or Orca's undo stack. Add an application-level harness that loads deterministic project fixtures and covers:

- project-ready signaling rather than fixed event-loop delays;
- a dedicated application name and bundle identity so UI automation cannot target another Orca build;
- an isolated temporary configuration and repository-owned fixture;
- deterministic on-bed object placement, active tab, selection, and camera-visible state before reporting readiness;
- object and viewport selection in both directions;
- rename, duplicate, remove, plate switch, import, undo, and redo;
- committed native transform and exact-value transform;
- callback snapshots at the advertised revision;
- subscriber destruction;
- project replacement and session-ID invalidation;
- operations initiated outside the adapter.

The POC self-test is evidence and test-design input, not a production harness: its STL setup could not reproduce rename undo, and it intentionally terminated through `std::_Exit`.

### Shell end-to-end tests

Run the complete native application with a deterministic multi-plate project. Verify the visible real canvas, camera controls, selection, native gizmos, Slice, Check print, return to Prepare, undo/redo, object pane synchronization, Agent pane persistence, resize, maximize/restore, and the unchanged legacy layout when testing a compatibility mode.

Visual appearance alone is insufficient. Validate state from authoritative callbacks and pair visibility claims with screenshots or rendered evidence.

Use the harness to establish deterministic application state; use UI automation
for real pointer, keyboard, focus, and rendering checks rather than for fragile
startup setup. For canvas policy, inventory every overlay surface and verify
rendering and hit-testing separately. Compare a stock window with the product
policy, then exercise selection, orbit, pan, zoom, active gizmo input, slicing,
Preview, and restoration of the prior presentation state.

### Platform matrix

Before release completion, cover:

- Windows with WebView2;
- macOS with WKWebView;
- Linux/X11 and supported Wayland configurations with WebKitGTK;
- supported CPU architectures and a physical GPU where applicable;
- 100%, 150%, and 200% scaling or equivalent platform DPI settings;
- light and dark mode;
- keyboard focus, global shortcuts, clipboard, text selection, and IME composition;
- WebView startup, local resource loading, reload, and recovery;
- streaming scroll anchoring while orbiting and resizing;
- idle and streaming CPU/memory observations.

Classify localized CSS, focus, packaging, or backend problems as platform defects unless evidence shows they require a different architecture.

## Implementation evidence record

For every production area, record:

- product requirement and underlying technical nature;
- current Orca command owner, snapshot location, and event source;
- mature precedents and what each proves;
- remaining delta and risk category;
- production and upstream-owned files touched;
- verification command and exit condition;
- platforms exercised and gaps remaining;
- regression checks for the original Orca path;
- any behavior that could not be tested and why.

## Rebase evidence

The rules for changing upstream-owned files are in [fork stewardship](fork-stewardship.md). This section states only what a change must produce as evidence.

A change that touches an upstream-owned file is not finished until the handback or pull request records three things:

1. **The rebase simulation and its result**, run against current upstream with both endpoint commits named. For each conflicted file, state whether the natural resolution is correct or lossy. A lossy resolution is a defect to fix before the change lands, not a note for the person who rebases.
2. **Every upstream line deleted or rewritten**, with where the logic went and why an additive form was not possible.
3. **What the fork now owns** — product policy, a small neutral seam, or upstream behavior. The third needs an explicit justification.

An automatic merge is evidence about today's text. It never proves that a future rebase will merge, and never proves that the merged result behaves correctly. After every upstream update, run the contract tests, the full native build, the multi-plate Prepare/Check workflow, native gizmo input, and the original-Orca regression checks.

The incidents that produced these rules are in [OrcaSlicer integration guide](orca-integration-guide.md). Historical logs and spike procedures are in [POC reference](poc-reference.md).
