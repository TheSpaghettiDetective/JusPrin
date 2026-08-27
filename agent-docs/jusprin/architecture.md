# JusPrin production architecture

**Status:** Selected architecture and production implementation boundary for `jusprin-newui`.

JusPrin replaces OrcaSlicer's primary presentation and workflow, while retaining OrcaSlicer's mature manufacturing behavior. Product work must simplify how users express intent and verify a print; it must not fork slicing, geometry, rendering, picking, serialization, or history behavior.

## Product composition

The ordinary project window has four stable regions:

1. A compact machine and project status row.
2. A collapsible, thin plates and objects pane.
3. OrcaSlicer's existing Prepare or G-code Preview canvas.
4. A fixed Agent conversation and print-plan pane.

Prepare is the persistent workspace. **Check print** temporarily replaces its center canvas with Orca's real sliced Preview. **Print** opens Send preflight and never sends immediately. Project details are a drawer, and Monitor is a separate surface that can remain visible during other work.

## Runtime architecture

```text
OrcaSlicer process (C++17 / wxWidgets / OpenGL)
├── JusPrin application shell
│   ├── compact top navigation and machine status
│   ├── native plates/objects pane
│   ├── center workspace host
│   │   ├── existing GLCanvas3D Prepare canvas
│   │   └── existing G-code Preview canvas
│   └── wxWebView
│       └── local React/TypeScript Agent interface
├── typed workspace/controller boundary
├── existing Orca model, selection, plates, gizmos, and undo stack
└── existing slicing, project I/O, device, and print subsystems
```

The boundary is deliberately conservative:

- The viewport and overlays over the viewport remain native.
- The top row and object/plate pane start as native wxWidgets.
- C++ owns application and project state.
- The WebView receives versioned events and sends versioned commands.
- A WebView object pane may be evaluated later only as a contained presentation choice. It does not justify Electron or a WebGL viewport.

## Authoritative ownership

| Concern | Authoritative owner | JusPrin responsibility |
|---|---|---|
| Project model and object identity | Orca `Model` and `ObjectID` | Project a typed, session-scoped view |
| Plates and active plate | Orca `PartPlateList` | Present and request changes through commands |
| Selection and picking | Orca `Selection` and `GLCanvas3D` | Synchronize UI without feedback loops |
| Geometry transforms | Orca gizmos and `GizmoObjectManipulation` | Launch existing behavior and present exact values |
| Undo and redo | Orca undo stack | Expose availability and outcomes without a shadow stack |
| Slicing and Preview | Orca slicing actions and Preview canvas | Present Slice and Check print states |
| Project serialization | Orca 3MF and model serializers | Preserve compatibility; add migration only for new data |
| Agent conversation | React/TypeScript WebView | Render messages and submit typed commands |
| Agent print plan | C++/agent application boundary | Explain intent, decisions, uncertainty, and consequences |

## Production integration boundaries

### Shell host

Use one fork-owned shell installed through one small `MainFrame` attachment. Startup policy belongs to a fork-owned bootstrap or shell factory, not to a growing collection of environment checks inside Orca constructors.

The first production version may keep the legacy top bar, tab panel, Sidebar, and `GUI_ObjectList` constructed when existing Orca behavior depends on them. They should be excluded from the visible layout, not covered by a separate top-level window. Removing their construction is a separate lifecycle project.

### Build registration

Keep the explicit JusPrin source list in `src/slic3r/GUI/JusPrin/sources.cmake`. Orca's central `src/slic3r/CMakeLists.txt` should contain one stable include rather than a growing list of fork-owned files.

### Workspace boundary

The production workspace API should retain the useful POC concepts: strong plate and object IDs, read-only snapshots, explicit command errors, typed change reasons, monotonic revisions, and move-only RAII subscriptions. Before production use it must also:

- define GUI-thread requirements;
- define exactly when a revision advances;
- invalidate session IDs explicitly when a project is replaced;
- observe changes made outside the adapter;
- report history independently of widget visibility;
- report whether undo and redo actually changed authoritative state;
- avoid depending on `GUI_ObjectList` for model commands;
- provide plate selection, object import/addition, and other product-required commands;
- remain free of wxWidgets and Orca types in its public contract.

When an operation is trapped in presentation code, expose the smallest behavior-oriented command from its current owner. Do not copy the operation into JusPrin and do not simulate input against hidden controls.

### Canvas presentation

Retain a product-neutral presentation API on `GLCanvas3D` and a fork-owned RAII controller that applies JusPrin policy and restores previous Orca state. Visibility and input must be independently controllable so invisible chrome cannot consume pointer events while active native gizmos remain usable.

Production controls should be more granular than the POC's broad `render_overlays` switch. Independently classify the stock main toolbar, gizmo picker, active gizmo handles, plate actions, object labels, layer-editing controls, navigation aids, Preview legend, and Preview sliders. Shared controls such as Plater's collapse toolbar need one owner at the shell lifetime, not one guard per canvas.

### Plates and objects pane

The production pane is intentionally smaller than `GUI_ObjectList`. It must:

- list and switch plates;
- list objects on the active plate;
- synchronize selection in both directions;
- add/import, rename, duplicate, and remove an object;
- expose less common operations through a selected-object menu, More, or the Agent;
- expand into a full tree only for multi-object, multi-part, multi-material, modifier, or print-order complexity.

Every command must reuse Orca's snapshot, cancellation, plate-membership, selection, update, and serialization behavior.

### Agent WebView

The production Agent interface is a standalone local React/TypeScript package embedded in `wxWebView`. It requires:

- a versioned command/event schema;
- deterministic mock conversations for UI tests;
- incremental message rendering and stable scroll anchoring;
- explicit keyboard, focus, shortcut, clipboard, selection, and IME rules;
- reload, startup, bridge-error, and unavailable-agent states;
- no direct ownership of Orca project state.

Windows uses WebView2, macOS uses WKWebView, and Linux uses WebKitGTK. Local resource packaging must work on all three. The demonstrated prototype used a single-file local bundle to avoid WKWebView `file:` subresource failures; production may keep that approach or register an equivalent local resource scheme.

MCP and broad agent autonomy are later layers. They must use the same typed boundary rather than changing viewport ownership.

### Gizmo presentation

Start with Move or Rotate and create one reusable native controller seam around existing behavior:

1. Position a compact action strip beside the selected object.
2. Activate the existing native gizmo.
3. Preserve existing pointer manipulation, snapping, and selection.
4. Route exact numeric entry through `GizmoObjectManipulation`.
5. Preserve existing snapshot timing and undo/redo.
6. Dismiss without inventing a draft transaction; Undo remains the revert path unless product requirements explicitly change.

Then convert tools by interaction family: Move/Rotate/Scale; Mirror/Duplicate/Place on face; Measure; Cut/Repair; Text/Emboss; painter-based tools.

### Semantic annotations

Reuse existing painter raycasting, brush selection, clipping, facet visualization, copying, undo, and 3MF persistence. The unproven production delta is arbitrary semantic labels and their agent-facing representation. The first implementation should use two labels on one object and verify save/reload, transforms, duplication, undo/redo, and behavior under topology changes before expanding the palette.

## Non-negotiable constraints

- Do not change slicing algorithms for UI convenience.
- Do not replace `GLCanvas3D` or G-code Preview.
- Do not reimplement geometry operations in TypeScript.
- Do not place WebView content over the native GL canvas.
- Do not create a JavaScript shadow project model or undo stack.
- Preserve 3MF projects, printer profiles, shortcuts, serialization, and cross-platform behavior.
- Do not seek stock Orca feature parity when the product definition does not require it.
- Do not reopen Electron or WebGL without a new explicit product decision backed by evidence.

## Implementation order

1. Establish canonical product documents, design tokens, and curated assets.
2. Productionize the workspace contract, canonical Orca commands, observation, and real-adapter tests.
3. Productionize the canvas presentation/controller seam.
4. Build the production shell and compact object/plate pane.
5. Build the typed Agent WebView and bridge.
6. Add the Move/Rotate presentation controller and roll out gizmo families.
7. Add semantic annotations, Check print reporting, Send preflight, Monitor, calibration, and troubleshooting surfaces according to the product definition.

Historical implementations and detailed proof are intentionally not copied here. See [POC reference](poc-reference.md).
