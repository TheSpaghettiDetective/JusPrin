# Native UI rewrite plan

**Status:** Architecture selected; Spike 1 evidence complete; production
implementation may proceed

**Platform decision:** Keep OrcaSlicer's existing C++17, wxWidgets, ImGui,
OpenGL, and CMake platform. Electron and a WebGL viewport are out of scope.

**Current execution:** Do not run another native-shell feasibility spike. Build
the production JusPrin shell using the established native/WebView boundary.
Finish the remaining Spike 1 platform checks as non-blocking QA. Run the
Move/Rotate presentation work only to discover a narrow reusable controller
seam before converting the other gizmos.

**Results document:** `agent-docs/native-ui-spike-results.md`

**Risk and verification method:** `agent-docs/native-ui-risk-and-verification.md`

---

## 1. Target product

The new UI is intentionally much smaller than stock OrcaSlicer. The Prepare
screen has four stable regions:

1. A compact machine/status row at the top
2. A collapsible plates/objects pane on the left
3. The existing 3D workspace in the center
4. A fixed Agent conversation pane on the right

The primary action, Slice, remains visible at the bottom of the workspace. The
Preview screen continues to use OrcaSlicer's existing G-code preview. Advanced
operations appear only when the user or agent invokes them.

The UI rewrite must not become a rewrite of slicing, project state, mesh
operations, viewport rendering, picking, undo/redo, or G-code preview.

## 2. Architecture baseline

```text
OrcaSlicer process (C++ / wxWidgets)
├── New compact top navigation
├── New thin plates/objects pane
├── Existing GLCanvas3D
│   ├── existing rendering and camera
│   ├── existing picking and selection
│   ├── existing geometry operations
│   └── restyled/reorganized ImGui or GL overlays
├── Existing G-code Preview canvas
├── Existing slicing actions and state
└── wxWebView
    └── Local React/TypeScript Agent interface
```

The initial boundary is deliberately conservative:

- The viewport and everything drawn over it stay native.
- The Agent interface uses React/TypeScript inside `wxWebView`.
- The top bar and left pane start as native wxWidgets.
- C++ remains the authoritative owner of application and project state.

If the thin native object pane proves disproportionately difficult to style or
maintain, a WebView implementation may be compared later. That is a boundary
optimization within the native application, not a reason to reconsider
Electron.

## 3. Evidence baseline and assessment rule

Before assigning risk to a requirement, classify its underlying technical
nature and search for a mature implementation of that same nature. Search the
current codebase and history first, followed by upstream/fork code and mature
open-source implementations using the same or a comparable stack. The exact
wireframed arrangement does not need to exist for its underlying capability to
be proven.

Assess only the unproven delta from that precedent. Keep technical feasibility,
Orca integration, product behavior, delivery effort, regression exposure, and
platform QA separate. Use a spike only for consequential uncertainty that code
inspection, mature precedent, or a focused test cannot resolve. The complete
method and current evidence ledger are in
[`native-ui-risk-and-verification.md`](native-ui-risk-and-verification.md).

These are established capabilities, not hypotheses the spikes need to retest:

- `Plater` already places the sidebar and central viewport under a
  `wxAuiManager`: [`Plater.cpp`](../src/slic3r/GUI/Plater.cpp).
- The sidebar already supports collapse and restore.
- `View3D` already creates `GLCanvas3D` with picking, moving, selection, labels,
  toolbars, and gizmos enabled: [`GUI_Preview.cpp`](../src/slic3r/GUI/GUI_Preview.cpp).
- Prepare and Preview already use separate native canvas views.
- OrcaSlicer already creates WebView2 on Windows, WKWebView on macOS, and
  WebKitGTK on Linux: [`WebView.cpp`](../src/slic3r/GUI/Widgets/WebView.cpp).
- Existing UI exchanges structured messages between JavaScript and C++:
  [`WipeTowerDialog.cpp`](../src/slic3r/GUI/WipeTowerDialog.cpp).
- Existing painter gizmos already provide mesh raycasting, brush selection,
  clipping behavior, and facet visualization:
  [`GLGizmoPainterBase.cpp`](../src/slic3r/GUI/Gizmos/GLGizmoPainterBase.cpp).
- `GLCanvas3D` already projects object bounds into screen coordinates and
  positions ImGui windows next to objects.
- `GizmoObjectManipulation` already owns exact numerical transforms and their
  existing undo snapshots.
- `FacetsAnnotation` and the existing painter gizmos already provide facet
  states, copying, undo integration, and 3MF persistence.

These facts, mature uses elsewhere in OrcaSlicer, and the completed macOS and
Linux/X11 Spike 1 demonstrations establish that the proposed architecture is
technically feasible. Remaining platform configurations and visual treatment
still require targeted integration and QA evidence; they are not, by
themselves, architecture risks.

## 4. Current execution order

The architecture question for the shell is closed. Work now proceeds in this
order:

1. Implement the production JusPrin shell: compact top navigation, thin native
   object/plate pane, existing viewport and Preview, existing Slice action, and
   the fixed Agent WebView with C++ authoritative state.
2. In parallel with implementation, close the remaining Windows and targeted
   input/display checks from Spike 1 as platform QA. These checks do not gate
   starting the production shell.
3. Implement one Move or Rotate presentation as a bounded controller-seam
   discovery task. Preserve the existing native manipulation, exact-value,
   snapshot, and undo/redo paths.
4. Use the established shell and controller seam for semantic annotations, the
   production Agent interface, and gizmo-family rollout.

Do not describe this sequence as two pending confidence spikes. Spike 1 is
completed evidence. The remaining shell checks are QA, and the Move/Rotate task
measures coupling rather than feasibility.

---

# Completed evidence — Native shell composition (Spike 1)

**Assessment:** The underlying capability was already supported by mature Orca
components. The spike is an integration demonstration and cross-platform QA
activity, not an architectural-feasibility gate. macOS and Ubuntu/X11 with
WebKitGTK and Mesa software rendering have demonstrated the boundary; targeted
coverage remains as recorded in the results document.

**Implementation status:** Complete in the experimental OrcaSlicer branch. Its
result informs the production JusPrin implementation; there is no remaining
Spike 1 implementation task in this plan.

Packaged or release-like checks remain a separate validation activity. macOS
and Ubuntu/X11 with software rendering demonstrated the boundary. Windows and
the explicitly listed input/display configurations remain release evidence, not
an architecture or implementation-start gate.

## 5. Composition that was demonstrated

The development-only shell used production components:

- Compact native placeholder top row
- Collapsible native left pane containing two plates and two objects
- The real `GLCanvas3D`, displaying and selecting a real model
- Fixed right `wxWebView` loading a local minimal React/TypeScript chat page
- Existing Slice action
- Existing Prepare/Preview switch

The chat prototype contained only an input, a scrollable transcript, and
simulated streaming output. It intentionally did not include an agent backend,
MCP, account UI, settings, or final visual polish.

The production implementation must retain the demonstrated ownership rule: one
C++ state owner; JavaScript sends typed commands and receives typed events; it
must not become a second source of project state.

## 6. Seams exercised

The demonstration and its follow-up validation covered or attempted:

1. Select an object in the left pane and observe the viewport selection.
2. Select an object in the viewport and observe the left-pane selection.
3. Collapse and restore the left pane repeatedly while orbiting the model.
4. Stream a long dummy response while orbiting and resizing the viewport.
5. Move focus among the canvas, chat input, and application shortcuts.
6. Switch between Prepare and Preview with the Agent pane still present.
7. Trigger Slice through the new button.
8. Resize at normal and high-DPI scaling.

## 7. Remaining cross-platform QA

`wxWebView` does not provide one identical browser engine:

| Platform | Backend used by OrcaSlicer |
|---|---|
| Windows | WebView2 / Chromium |
| macOS | WKWebView |
| Linux | WebKitGTK 4.1 |

The boundary has been demonstrated on macOS/WKWebView and Ubuntu
X11/WebKitGTK with Mesa software rendering. Run a packaged or release-like
Windows/WebView2 build and the remaining targeted configurations before the
production milestone is release-complete. Do not use those gaps to reclassify
the shell as an unresolved architecture decision.

Check and record:

- Rendering or resize glitches between WebView and GL canvas
- Focus, global shortcuts, clipboard, text selection, and IME behavior
- Chat scroll anchoring during streaming
- WebView startup and reload behavior
- CPU and memory at idle and during simulated streaming
- Light/dark appearance and 100%/200% scaling
- Any platform-specific CSS or JavaScript required

The goal is to classify observed defects accurately. Focus, CSS, packaging,
resize, theme, or input defects are localized QA findings unless reproduced
evidence shows that they require changing the widget boundary.

## 8. Spike 1 result record

The completed result is recorded in
`agent-docs/native-ui-spike-results.md`. Append future platform-QA evidence to
that record, including:

- Commit/build tested on each operating system
- Screenshots of resting, expanded, and Preview states
- Behavior observed for every item in §§6–7
- Native and WebView process memory
- Platform-specific differences
- Any recommended adjustment to the native/WebView boundary
- What could not be tested and why

Do not use this result to reopen Electron. If the layout exposes a problem,
first adjust the wx layout, WebView scope, or Agent-pane implementation.

---

# Next seam task — One complete gizmo presentation (Spike 2)

**Assessment:** Existing code already proves gizmo activation, screen-space
overlay placement, numerical transforms, snapshots, and undo/redo. This spike
measures presentation coupling and discovers the reusable native controller
seam; it does not re-prove those capabilities.

**Timebox:** Target three developer-days. Stop at the timebox and report the
coupling discovered; do not turn the spike into a general gizmo refactor.

## 9. Representative workflow

Use Move or Rotate as the representative operation. Implement the wireframed
workflow around the real selected object:

1. Show the compact screen-space action strip next to the selected object.
2. Activate the existing native gizmo from that strip.
3. Manipulate the object using the existing 3D interaction.
4. Enter one exact numerical value.
5. Verify that the value is applied and snapshotted through the existing gizmo
   path.
6. Undo and redo the applied value.
7. Dismiss the gizmo, then deselect the object and return to the resting state.

"Apply" in this spike means preserving OrcaSlicer's current exact-value
behavior: the input updates the transform and creates an undo snapshot through
`GizmoObjectManipulation`. Dismissing the gizmo does not roll the transform
back; Undo is the existing way to revert it. A draft transaction with explicit
Apply and Cancel actions would be new product behavior and is out of scope for
this presentation spike.

The prototype may initially retain the existing 3D handles. The question is
whether presentation can be reorganized without duplicating transformation,
picking, snapping, or undo behavior.

## 10. Instrument the coupling

Record every production class changed and classify the change:

- New presentation/controller code
- A small API exposed from existing behavior
- Existing gizmo behavior modified
- Geometry or picking logic duplicated

Also record whether a second gizmo could use the same presentation API without
copying the first gizmo's glue code. Do not implement the second gizmo merely to
make the prototype look complete.

Before making changes, record the mature implementation that proves each
required behavior and identify the exact API or ownership delta the spike is
testing. Do not count already-proven behavior as residual risk.

## 11. Spike 2 result

The result should answer:

- Can the new visual hierarchy launch and control existing gizmos?
- Which parts of the old ImGui input window are behavior rather than
  presentation?
- Can numerical editing, existing snapshot timing, and undo/redo remain
  authoritative in C++?
- Does the screen-space action strip remain correctly positioned during camera,
  selection, DPI, and window-size changes?
- What reusable command/controller seam is needed before converting other
  gizmos?

If the presentation is tightly coupled, adjust the new UI design or introduce a
narrow native controller layer. This is an implementation-planning result, not
a trigger to replace the viewport.

---

# Production implementation

## 12. Thin plates/objects pane

Spike 1 has determined the widget boundary. Build the production panel now as
part of the production shell. It must cover the smaller product workflow rather
than reproduce all of `GUI_ObjectList`:

- List and switch plates
- List objects on the active plate
- Synchronize selection with the viewport
- Add, rename, duplicate, and remove an object
- Expose less common actions through a compact overflow menu or the Agent

Before implementing each behavior, identify its existing model command and
undo/redo path. Do not copy business logic out of `GUI_ObjectList` into a second
UI implementation.

## 13. Semantic annotation

Treat annotation as a product/data-model investigation on the chosen native
platform. Existing painter gizmos, `TriangleSelector`, `FacetsAnnotation`, undo,
copying, and 3MF serialization already prove the technical nature of facet
painting and persistence. The investigation is limited to the delta for
arbitrary semantic labels and an agent-facing representation. It does not gate
the platform or the shell.

The first annotation prototype should answer:

1. Can existing painter picking and facet visualization be reused for arbitrary
   named meanings?
2. Should semantic labels use existing facet-state serialization or a separate
   project-level annotation model?
3. Can annotations survive save/reload, mesh transforms, object duplication,
   and undo/redo?
4. Can the agent receive a stable structured representation of the annotated
   regions?

Start with two labels and one object. Do not build the complete palette until
the storage and lifecycle behavior are demonstrated.

## 14. Agent interface

Spike 1 validated the embedded UI boundary. Build the production Agent pane as
a standalone local React/TypeScript package with:

- A versioned typed command/event schema
- Deterministic mock conversations for UI tests
- Incremental message rendering
- Explicit focus and shortcut rules
- No direct ownership of Orca project state

MCP and broad agent autonomy are outside this plan. They can be layered on the
same command boundary later without changing the viewport architecture.

## 15. Gizmo rollout

Use the controller seam learned in Spike 2. Convert tools by interaction family,
not by visual order:

1. Move, rotate, and scale
2. Mirror, duplicate, and place-on-face
3. Measure
4. Cut and repair
5. Text and emboss
6. Painter-based tools

For each family, preserve existing selection, undo/redo, shortcuts, and project
serialization before removing its old presentation.

## 16. Constraints

- Do not modify slicing algorithms for UI convenience.
- Do not replace `GLCanvas3D` or G-code Preview.
- Do not reimplement geometry operations in TypeScript.
- Do not place WebView content over the native GL canvas.
- Do not make JavaScript a second authoritative project model.
- Do not attempt stock OrcaSlicer feature parity where the new product does not
  require it.
- Do not reopen Electron/WebGL work without a new explicit product decision.

## 17. Planning status and remaining gates

The platform and shell architecture planning is complete. Production shell
implementation does not wait for another Spike 1 run. The remaining gates are:

1. Before the cross-platform production milestone is called release-complete,
   record the remaining Windows and targeted Linux/input evidence in the Spike
   1 results.
2. Before converting gizmo families, document the reusable presentation seam
   discovered by the Move or Rotate task.
3. Estimate remaining product work from demonstrated seams rather than assumed
   risk levels.
4. Give each production area an evidence-ledger entry that states its technical
   nature, mature precedents, unproven delta, residual risk category, and
   verification exit condition.
