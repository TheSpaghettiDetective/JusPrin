# Native UI risk and verification method

**Status:** Required method for planning and validating the native UI rewrite

**Recorded:** 2026-08-12

## Lesson learned

The initial assessment of the native shell overstated its architectural risk.
It treated the absence of the exact wireframed composition as evidence that the
composition might not be technically viable. That was the wrong unit of
analysis.

The shell's underlying technical nature is a resizable native desktop layout
containing an accelerated OpenGL canvas, native controls, and a continuously
updating embedded browser. OrcaSlicer already contained mature implementations
of each part and several closely analogous compositions. Spike 1 subsequently
demonstrated the exact boundary on macOS and on Ubuntu/X11 with WebKitGTK and
Mesa software rendering. The remaining work is integration and platform QA,
not an unresolved architecture decision.

This lesson applies to every requirement in the rewrite:

> A new product arrangement is not necessarily a new technical capability.
> Classify the nature of the requirement, find mature precedent for that
> nature, and assess only the unproven difference.

## Definition of risk

For this project, risk is **residual, evidence-backed uncertainty that remains
after accounting for mature implementations of the same technical nature, and
that could materially affect architecture, correctness, delivery, or
regressions**.

Keep the following terms separate:

- **Feasibility risk:** uncertainty that the proposed technical boundary can
  work at all.
- **Integration risk:** uncertainty about connecting proven components without
  duplicating behavior or creating unsafe ownership.
- **Product-behavior risk:** uncertainty about what the feature must do or how
  its lifecycle should behave.
- **Delivery effort:** known implementation work. A large amount of understood
  work is not automatically high technical risk.
- **Regression risk:** the chance and consequence of disturbing mature Orca
  behavior while changing its presentation.
- **Platform QA:** evidence still required on a particular operating system,
  browser backend, display stack, input method, or DPI. Missing platform
  coverage is not automatically architecture risk.

A bug, an untested configuration, and an inconvenient implementation detail
should not be promoted to architecture risk unless failure would force a broad,
expensive, or difficult-to-reverse design change.

## Required assessment method

Use the following process before assigning risk or authorizing a spike.

### 1. Define the product requirement

State the user-visible behavior and its boundaries without assuming an
implementation or failure cause.

### 2. Classify its technical nature

Describe the general engineering capability underneath the product wording.
For example:

- Product wording: fixed Agent pane beside the Orca workspace.
- Technical nature: native split layout containing an OpenGL surface and a
  live embedded browser.

This prevents an unfamiliar visual arrangement from being mistaken for an
unfamiliar technical problem.

### 3. Search for mature precedent

Search in this order:

1. The exact behavior in the current OrcaSlicer codebase.
2. An implementation of the same nature elsewhere in OrcaSlicer or its Git
   history.
3. OrcaSlicer upstreams, forks, or sibling slicers using the same stack.
4. Mature, maintained open-source applications using the same or a comparable
   stack.
5. Framework documentation and small examples, which are supporting evidence
   but weaker than shipped source code.

Do not stop after checking the most obvious file. Search by behavior, component,
event flow, ownership, serialization, undo, and history. Record the source and
what it proves.

### 4. Test whether the precedent is comparable

A precedent is strong evidence when it is shipped or maintained, source is
available, its lifecycle and interaction resemble the requirement, it operates
at comparable scale, and it covers the relevant platforms. State any mismatch
instead of discarding the precedent or treating it as exact proof.

### 5. Isolate the delta

List only what the precedent does not prove. The delta is the candidate risk.
Examples include exposing a private behavior through a controller, defining
arbitrary annotation names, or validating IME routing on a particular WebView
backend.

### 6. Assess the residual uncertainty

For each delta, record:

- the failure that is still plausible;
- evidence for its likelihood;
- consequence and blast radius;
- whether failure would change architecture or only require localized work;
- reversibility;
- the cheapest evidence that would resolve it.

Avoid artificial numeric scores. Use `very low`, `low`, `medium`, or `high`,
and explain the evidence behind the label.

### 7. Choose the appropriate verification

- Use **code inspection or a focused test** when the capability already exists.
- Use an **integration prototype** when the pieces are proven but their Orca
  seam needs measurement.
- Use **platform validation** when the uncertainty is backend-, display-, or
  input-specific.
- Use a **feasibility spike** only when no mature comparable implementation
  resolves a consequential architectural uncertainty.

Every verification must name the hypothesis, evidence to collect, pass/fail
condition, and decision that a failure could change. A prototype should not be
described as proving feasibility when it merely estimates coupling or exercises
QA.

## Evidence baseline and residual risk

| Requirement | Mature same-nature evidence | Unproven delta | Residual assessment |
|---|---|---|---|
| Native shell | `Plater` already composes a dockable sidebar and real `GLCanvas3D`; Orca already ships native WebViews. Spike 1 demonstrated the combined boundary on macOS and Linux/X11. | Windows run; hardware-rendered Linux and Wayland; some focus, shortcut, IME, and simultaneous-input checks. | Very low feasibility risk; moderate platform QA. |
| Agent pane and bridge | `WebView.cpp`, `Project.cpp`, `WipeTowerDialog.cpp`, and `StatusPanel.cpp` provide mature local/remote WebViews, script handlers, native-to-JavaScript calls, and changing browser content. | Versioned application command schema, production reload handling, and explicit focus rules. | Low. |
| Thin object/plate pane | `GUI_ObjectList`, `ObjectDataViewModel`, `Plater`, and `PartPlate` already implement plate-aware listing, mutations, undo, and bidirectional viewport selection. | Expose a deliberately small command/query surface without copying behavior from the existing presentation class. | Low feasibility; low-to-medium integration effort. |
| Screen-space action strip | `GLCanvas3D` already projects selected-object bounds into screen coordinates and positions ImGui windows; `GLGizmosManager` already activates gizmos. | Product placement rules and a reusable presentation/controller interface. | Low. |
| Exact-value gizmo entry | `GizmoObjectManipulation` already applies numerical transforms, takes `GizmoAction` snapshots, and invokes the existing move/rotate/scale paths. | Separate those commands from the existing ImGui rendering without changing snapshot semantics. | Low feasibility; low-to-medium coupling risk. |
| Semantic facet annotations | `GLGizmoPainterBase`, `TriangleSelector`, `FacetsAnnotation`, existing painter gizmos, undo snapshots, object copying, and 3MF serialization already prove picking, painting, visualization, persistence, and lifecycle mechanics. | Arbitrary label schema, project representation, stable agent-facing data, and behavior under topology-changing operations. | Low technical feasibility; low-to-medium data-model risk. Topology-changing preservation would be a separate medium-risk requirement. |
| Gizmo rollout | Many production gizmos already share selection, overlay, picking, command, and undo infrastructure. | Interaction families are heterogeneous, so one presentation seam may not cover every tool without family-specific adapters. | Medium delivery and regression risk; low architecture risk. |
| Slicing, rendering, picking, undo, and Preview | These are mature production subsystems retained by the architecture. | Prevent presentation work from bypassing or destabilizing their existing paths. | Low if the boundary is enforced. |

## Verification plan

### A. Maintain an evidence ledger

Before implementing each product area, add or update a short entry containing:

- product requirement and technical nature;
- internal and external precedents searched;
- what each precedent proves and does not prove;
- remaining delta and risk category;
- chosen verification and exit condition.

The table above is the initial ledger. Update it when evidence changes; do not
carry an old risk label forward without rechecking its basis.

### B. Close Spike 1 as integration and platform validation

Spike 1 no longer answers whether the architecture is technically feasible.
macOS and Linux/X11 have demonstrated the native/WebView boundary. Complete the
remaining evidence as targeted QA:

1. Run the packaged shell on Windows/WebView2.
2. On Linux, test a physical GPU and Wayland if they are supported target
   configurations.
3. Complete explicit clipboard, global-shortcut, and IME checks on all three
   WebView backends.
4. Exercise viewport-to-list selection and true simultaneous orbit, streaming,
   and resize with a human or multi-input harness.
5. Record results in `docs/native-ui-spike-results.md`, distinguishing product
   defects from environment and harness limitations.

A localized focus, CSS, packaging, or resize defect does not invalidate the
boundary. Revisit the boundary only if a reproduced failure cannot be fixed
locally without broad architectural change.

### C. Run Spike 2 as seam discovery

Spike 2 must not attempt to re-prove transforms, gizmo activation, screen-space
overlays, exact-value application, snapshots, or undo. Those capabilities are
already established. It should verify only the presentation delta:

1. Identify the existing activation and numerical-edit commands before editing.
2. Add the smallest screen-space action strip for Move or Rotate.
3. Route exact-value entry through the existing `GizmoObjectManipulation`
   behavior and preserve its immediate snapshot semantics.
4. Record every production class touched and distinguish API exposure from
   behavior modification.
5. Describe how a second transform gizmo would consume the same seam without
   implementing it.
6. Validate camera movement, selection changes, resize, DPI, and undo/redo.

Pass means the new presentation can call the existing behavior through a narrow
reusable native seam without duplicating geometry, picking, transformation, or
undo logic. Failure means the observed coupling and its consequence are
documented; it does not reopen the viewport or platform decision.

### D. Verify production areas by their actual delta

- **Thin object pane:** map each required action to its existing command and
  undo path, then contract-test the extracted surface before building the final
  presentation.
- **Semantic annotations:** reuse painter mechanics first; prototype only the
  arbitrary-label schema, save/reload, duplication, undo/redo, transforms, and
  agent representation. Treat topology-changing preservation as a separate
  requirement if requested.
- **Agent pane:** test the versioned bridge independently with deterministic
  mock conversations, then run backend-specific focus/reload/IME checks.
- **Gizmo rollout:** verify one interaction family at a time and run regression
  tests against its existing selection, snapshots, shortcuts, and project
  serialization before removing the old presentation.

## Decision rule

The rewrite architecture should change only when evidence shows that a required
behavior lacks a mature comparable implementation **and** the remaining failure
would require broad, difficult-to-reverse changes. Known work, localized
defects, presentation coupling, or incomplete platform QA should be planned and
tested, but should not be mislabeled as architectural uncertainty.
