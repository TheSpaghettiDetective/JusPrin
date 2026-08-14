# Orca workspace adapter practicality spike

**Status:** Proposed implementation handoff

**Timebox:** Two elapsed hours. Stop at the timebox and report the coupling
discovered, including any hard events not reached. Do not extend the timebox or
turn this into a general OrcaSlicer refactor.

**Primary question:** Can JusPrin read and control the small workspace surface
needed by the new shell through a fork-owned, typed adapter while preserving
OrcaSlicer's authoritative state, events, and undo/redo behavior?

**Related documents:**

- [`native-ui-rewrite-plan.md`](native-ui-rewrite-plan.md)
- [`native-ui-risk-and-verification.md`](native-ui-risk-and-verification.md)
- [`native-ui-spike-results.md`](native-ui-spike-results.md)

---

## 1. Objective

Implement a narrow vertical slice of this dependency direction:

```text
Thin native pane / future Agent bridge
                  |
                  v
       JusPrin workspace contract
                  |
                  v
       Orca workspace adapter
                  |
                  v
 Existing Plater / GLCanvas3D / Model / undo stack
```

The spike is successful only if the JusPrin-facing contract stays independent
of Orca GUI and model types and the implementation delegates behavior to
existing Orca paths. It must not become a second project model, a generic event
bus, or a replacement for `Plater`.

This spike does not reopen the native/WebView architecture decision. It tests
whether the boundary selected for production is maintainable and practical.

## 2. Why this is the next uncertainty

The current branch has not yet modified production Orca source relative to the
`v2.4.2` baseline. This is the right time to establish the fork boundary.

The relevant upstream areas are large and highly connected:

- `Plater` owns the model, plates, Prepare/Preview composition, slicing,
  snapshots, undo/redo, and much of the event routing:
  [`Plater.cpp`](../src/slic3r/GUI/Plater.cpp).
- `GUI_ObjectList` combines presentation, selection translation, object
  commands, and snapshots:
  [`GUI_ObjectList.cpp`](../src/slic3r/GUI/GUI_ObjectList.cpp).
- `GLCanvas3D` owns the authoritative viewport selection and emits many of the
  existing interaction events:
  [`GLCanvas3D.cpp`](../src/slic3r/GUI/GLCanvas3D.cpp).
- Exact transform edits already pass through `GizmoObjectManipulation`, whose
  public `on_change()` entry point delegates to the existing transform and
  snapshot paths:
  [`GizmoObjectManipulation.cpp`](../src/slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp).

Calling one Orca method is not the risk. The risk is whether real workflows can
be represented without leaking those classes through the JusPrin boundary or
copying their behavior.

## 3. Architectural rules

These are hard constraints for the spike:

1. The contract is owned by JusPrin and uses only JusPrin value types.
2. C++ Orca state remains authoritative. The adapter may compute snapshots but
   must not cache a second editable project model.
3. No public contract type may contain `wxWindow`, `wxEvent`, `Plater`,
   `GLCanvas3D`, `Selection`, `ModelObject`, `PartPlate`, or raw pointers.
4. Commands must delegate to existing Orca behavior and existing snapshot
   timing. Do not duplicate geometry, selection, plate, or undo logic.
5. `GUI_ObjectList` is a presentation consumer, not the adapter's business-logic
   API. Do not drive hidden `wxDataView` rows to execute commands.
6. JSON, WebView lifecycle, authentication, and React state are outside the
   adapter. No production Agent pane is part of this spike.
7. New code should live under a uniquely owned directory such as
   `src/slic3r/GUI/JusPrin/Workspace/`. If a different location is chosen,
   record why.
8. Changes to upstream-owned files must be attachment points or small reusable
   capability seams. Do not broadly reorganize `Plater`, `GUI_ObjectList`,
   `GLCanvas3D`, or the gizmo hierarchy.
9. All adapter calls and observer callbacks execute on the wx GUI thread during
   this spike. Background-thread invocation is out of scope.

## 4. Minimum JusPrin-facing contract

The exact C++ spelling is an implementation decision, but the contract must
provide the following concepts.

### 4.1 Strong identifiers

- `PlateId`
- `ObjectId`

Do not expose interchangeable bare integers. Prefer Orca's stable in-process
object identity where available. If plates do not have a suitable stable
identity, use a typed session identifier and document its invalidation rules.
Do not invent persistence semantics in this spike.

### 4.2 Read-only snapshot

At minimum, `WorkspaceSnapshot` contains:

- Monotonically increasing adapter revision
- Plates with ID, name, and active state
- Objects on each plate with ID and name
- Active plate ID
- Object-level selection
- `can_undo` and `can_redo`

Object-level selection is sufficient. Volume, instance, connector, layer, and
mixed-selection representations are out of scope, but the implementation must
report when the current Orca selection cannot be represented rather than
silently returning an incorrect object selection.

### 4.3 Commands

Implement these commands through existing Orca behavior:

- Select one object
- Rename one object
- Duplicate one object
- Remove one object
- Undo
- Redo

Every command returns an explicit success or error result. Expected errors
include stale IDs, missing objects, unsupported selection, and an unavailable
operation. Do not use exceptions for expected command rejection.

Rename is deliberately included because its current behavior is coupled to
`GUI_ObjectList`. If the logic cannot be reused without driving that widget,
introduce or propose one narrow UI-independent Orca command seam. Do not copy
the rename implementation into the adapter merely to complete the demo.

### 4.4 Observation

Expose a subscription whose notification is conceptually:

```cpp
struct WorkspaceChanged {
    std::uint64_t revision;
    WorkspaceChangeReasons reasons;
};
```

The event does not carry a second copy of workspace state. A consumer responds
by reading a new snapshot. Calling `snapshot()` from the callback must return
the event revision or a later revision.

Raw Orca events may be noisy. Coalesce related raw events into one logical
notification on the next safe GUI event-loop turn. Coalescing must not delay a
notification indefinitely or create a selection feedback loop.

## 5. Required hard events

The spike must implement and demonstrate all three events below. These are
chosen because they originate from different owners and expose event-ordering
and state-synchronization problems.

### Event A — Viewport selection changed

**Trigger:** The user clicks a different object in the real `GLCanvas3D`.

**Required result:**

- The observer receives a change containing the `Selection` reason.
- A snapshot taken from the callback contains the selected `ObjectId`.
- Selecting an object through the adapter updates the real viewport and emits
  one coalesced logical selection notification.
- No list/canvas recursion, repeated notification loop, or gizmo reset beyond
  existing Orca behavior occurs.

### Event B — Workspace contents and history changed

**Trigger:** Rename, duplicate, and remove are executed through the adapter.

**Required result:**

- Each completed operation produces a `Contents` notification after the Orca
  model has reached its stable post-command state.
- The callback snapshot reflects the new name, new object, or removed object.
- Duplicate and remove preserve the existing Orca snapshot and undo behavior.
- Rename must also be undoable using the existing project history semantics.
- Undoing and redoing each operation produces a `History` reason and a callback
  snapshot representing the restored Orca state.
- `can_undo` and `can_redo` are correct after each operation.
- The adapter does not manually patch a cached object list.

If a file import or another legacy Orca UI action can be covered through the
same notification seam within the timebox, record it as additional evidence,
but it is not required.

### Event C — Object transform committed and restored

**Trigger:** Move or rotate an object using the existing native viewport gizmo.

**Required result:**

- The observer receives a `Transform` change only when Orca considers the
  operation applied or committed, not for every intermediate repaint.
- The callback snapshot or a narrowly scoped object detail query reflects the
  final transform.
- Existing gizmo snapshots and undo/redo behavior remain unchanged.
- Undoing and redoing the transform produces a `History` reason together with
  `Transform`, and the callback snapshot reflects the restored transform.
- The adapter does not maintain or replay its own history.

Do not build the new Move/Rotate presentation in this spike. This event only
tests whether the workspace boundary can observe authoritative native changes.

## 6. Required vertical workflow

Use a deterministic project with two plates and at least two named objects.
Run this sequence against the real native viewport:

1. Construct the adapter and capture the initial snapshot.
2. Select object A through the adapter and verify viewport selection.
3. Select object B in the viewport and verify Event A.
4. Rename an object, then undo and redo the rename; verify Event B.
5. Duplicate the object, verify its new identity, then undo and redo.
6. Remove the duplicate, then undo the removal.
7. Move or rotate an object with the existing gizmo and verify Event C.
8. Undo and redo the transform and verify the history result in Event C.
9. Destroy the probe/consumer, then perform another viewport selection and
    confirm that no callback targets a destroyed observer.

A development-only native probe, focused test harness, or similarly small
consumer may drive the workflow. Do not build production visual styling. The
probe must depend only on the JusPrin contract, not directly on Orca types.

## 7. Implementation sequence

### Step 1 — Record existing behavior paths

Before editing, identify the exact existing entry point and snapshot path for
each command and the existing event source for each hard event. Put this table
in the result document:

| Capability | Existing Orca owner/API | Snapshot owner/timing | Event source | Unproven delta |
|---|---|---|---|---|
| Select object | | | | |
| Rename object | | | | |
| Duplicate object | | | | |
| Remove object | | | | |
| Transform committed | | | | |
| Undo/redo | | | | |

Do not assume the entry point from method names alone. Trace the complete path
and verify it in the running application.

### Step 2 — Define contract and fake

Create the identifiers, DTOs, command results, snapshot, change reasons, and
subscription lifetime. Add a lightweight fake implementation or recording
consumer that compiles without including Orca GUI headers. This checks that the
contract is genuinely JusPrin-owned.

Do not introduce a dependency-injection framework or a general command bus.
Plain C++ interfaces, callbacks, and RAII lifetime management are sufficient.

### Step 3 — Implement read projection

Build snapshots on demand from authoritative Orca state. Do not retain editable
copies of plates, objects, selection, or transforms. Keep ID translation in the
adapter implementation.

### Step 4 — Implement commands

Delegate to existing public APIs first. Where no suitable API exists, expose
the smallest behavior-oriented seam from the current Orca owner. Do not expose
internal containers merely because they are convenient.

### Step 5 — Bind and coalesce events

Subscribe to existing `Plater`, canvas, plate, and history events from one
adapter-owned location where possible. Add a centralized upstream notification
only when inspection and runtime evidence show that the existing events cannot
cover a required hard event.

### Step 6 — Run the vertical workflow

Capture logs containing revision, change reasons, and a concise snapshot
summary for every step. Validate visual selection in the real viewport and
record undo/redo outcomes.

## 8. Scope limits

Do not implement:

- The production thin plates/objects pane
- The production Agent WebView or JSON protocol
- Slicing, export, printer, preset, or account commands
- Plate mutation or active-plate commands
- Volume-, instance-, layer-, or facet-level selection
- Semantic annotations or 3MF changes
- A general rewrite of `GUI_ObjectList`
- A general `Plater` decomposition
- New Move/Rotate presentation
- Background-thread adapter calls
- Cross-platform visual polish

Do not modify slicing algorithms, mesh operations, project serialization,
`GLCanvas3D` rendering, picking, or existing snapshot semantics.

## 9. Verification

### Automated checks

Add focused tests for code that can run without constructing the complete GUI:

- Strong ID behavior
- Command result/error behavior
- Observer unsubscription and destruction safety
- Change-reason merging/coalescing
- Revision monotonicity
- A consumer compiling against the contract and fake without Orca GUI headers

If a hard event cannot be automated in the current test environment, document
why and provide repeatable local steps and captured evidence. Do not claim it
was tested automatically.

### Native end-to-end checks

Run the complete workflow in Section 6. At minimum verify:

- The real model and viewport are used.
- Adapter and viewport selection agree after every selection step.
- Every callback snapshot contains stable post-operation state.
- Undo and redo restore names, object presence, transforms, and selection as
  Orca currently defines them.
- Repeated selection changes do not produce an event loop.
- Destroying the observer does not leave a dangling callback.

Build the affected application target and run the relevant existing tests. If
the local development application cannot be run, stop and report the specific
blocker rather than substituting a fake-only result.

## 10. Coupling and rebase ledger

The result must list every production file changed and classify it as:

- Fork-owned contract, adapter, test, or probe
- Build-file registration
- Upstream attachment point
- Upstream behavior seam
- Existing behavior modification
- Duplicated behavior

For every upstream-owned change, record:

- Why it was required
- Lines added and removed
- Whether the change is localized to one function
- Whether future JusPrin capabilities can reuse it
- Whether it is likely to overlap active upstream development

Targets, not incentives to compress unreadable code:

- No more than three upstream-owned implementation/header files changed
  beyond build registration
- No more than roughly 100 changed lines in upstream-owned files
- Zero copied selection, geometry, plate, or undo algorithms
- Zero JusPrin contract dependencies added to `libslic3r`

Crossing a target does not automatically fail the experiment. Stop expanding
the implementation and explain why the boundary requires more surface.

## 11. Exit decision

### Pass

Recommend the adapter architecture when:

- All hard events and the vertical workflow work against real Orca state.
- The contract contains no Orca GUI or model types.
- The adapter does not own mutable project state or history.
- Commands reuse existing behavior and snapshots.
- Upstream changes are a few understandable attachment points or reusable
  behavior seams.
- A second consumer could use the contract without new Orca dependencies.

### Adjust

Recommend a narrower or differently divided boundary when the concept works
but one capability, such as rename or history observation, needs a dedicated
Orca-side controller seam. Describe the smallest revised boundary and rerun
only the affected workflow.

### Reject

Reject this form of adapter when any of these remain true at the timebox:

- Correctness requires driving hidden `GUI_ObjectList` presentation state.
- The adapter must shadow and reconcile an editable project model.
- Required notifications need scattered mutations across many upstream files.
- Consumers still require Orca types or direct `wxGetApp()` access.
- Existing undo/redo semantics cannot be preserved without copied logic.

Rejection means revising the boundary, not reopening Electron or replacing the
native viewport.

## 12. Required result document

Create `agent-docs/orca-workspace-adapter-spike-results.md` containing:

1. Commit and build configuration tested
2. Pass, adjust, or reject verdict
3. Final contract summary
4. Existing behavior-path table from Step 1
5. Results for Events A-C
6. Complete vertical-workflow log
7. Automated and manual test evidence
8. Coupling and rebase ledger
9. Direct Orca dependencies used by the adapter
10. Missing events or APIs and why they are missing
11. Recommended production boundary
12. Decisions that still require human product or architecture judgment

Do not describe a partial fake-only implementation as proof that the adapter
works with Orca. The result must distinguish compiled evidence, runtime
evidence, and inference.
