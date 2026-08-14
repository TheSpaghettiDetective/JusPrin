# Orca workspace adapter practicality spike results

**Status:** Complete. The implementation, all three hard events, and the full
vertical workflow were exercised against the real Orca workspace.

**Acceptance condition:** The normal Orca `MainFrame`, `Sidebar`, and
`GUI_ObjectList` are allowed to remain constructed and operational as runtime
prerequisites for the JusPrin UI. In this run they were visible. An invisible or
fully occluded legacy window was not tested here.

**Exit verdict:** **Pass under that acceptance condition.** The adapter supports
a JusPrin companion or overlay UI on top of the existing Orca application.
This result does not demonstrate that the legacy UI can be omitted from
construction.

## 1. Commit and build configuration tested

| Item | Tested value |
|---|---|
| Feature branch | `codex/orca-workspace-adapter-spike` |
| Implementation commit | `4e278e3276a91a7142615492beb58bdf71116a78` (`Add Orca workspace adapter spike`) |
| Base revision | `254c67ffdd5d10947eaadf780642bd45982709f7` (`v2.4.2-12-g254c67ffdd`) |
| Host | macOS 26.2 (`25C56`), Apple silicon |
| Generator | Ninja Multi-Config |
| Configuration | `RelWithDebInfo` |
| Architecture | `arm64` |
| Deployment target | macOS 15.0 |
| Tests configured | `BUILD_TESTING=ON`, `BUILD_TESTS=ON` |
| Dependency prefixes | Current checkout's pinned Eigen 5.0.1 install plus the existing OrcaSlicer arm64 dependency bundle |

The full `OrcaSlicer` application target and the focused
`workspace_contract_tests` target compile and link. The native runtime used the
real `Plater`, `GLCanvas3D`, `Model`, part-plate list, selection, and undo stack.

## 2. Verdict

**Pass for a companion/overlay architecture in which the legacy Orca UI remains
constructed and operational.** All required hard events and the deterministic
native workflow passed against real Orca state. No observed behavior requires a
shadow model, copied undo logic, or Orca types in the public contract.

This is not evidence that `GUI_ObjectList` can be removed. Rename, duplicate,
and delete still update it through existing Orca paths. The completed run used
the normal visible Orca UI, so invisibility is a separate question addressed by
[orca-invisible-legacy-ui-spike.md](orca-invisible-legacy-ui-spike.md).

## 3. Final contract summary

The fork-owned contract is in
`src/slic3r/GUI/JusPrin/Workspace/Workspace.hpp` and contains only C++17
standard-library types:

- Strong, non-interchangeable `PlateId` and `ObjectId` session identifiers.
- Plate, object, instance-transform, active-plate, object-selection, history
  availability, and monotonic revision projections.
- Explicit `CommandResult` values with invalid-ID, missing-object, stale-ID,
  unsupported-selection, unavailable-operation, and invalid-argument errors.
- Commands for object selection, rename, duplicate, removal, undo, and redo.
- `WorkspaceChanged { revision, reasons }` notifications and a move-only RAII
  subscription.
- A GUI-independent change hub that merges reasons and increments the revision
  once per logical flush.

`WorkspaceSnapshot` is built on demand. The adapter retains only the set of IDs
previously observed, so it can distinguish missing from stale session IDs; it
does not retain editable project state or history.

## 4. Existing behavior paths

| Capability | Existing Orca owner/API | Snapshot owner/timing | Event source | Unproven delta |
|---|---|---|---|---|
| Select object | `Selection::add_object`, followed by Orca's existing `EVT_GLCANVAS_OBJECT_SELECT` synchronization path in `Plater::priv::on_object_select` | Selection does not add a project snapshot; Orca owns selection/history side effects | `GLCanvas3D` `EVT_GLCANVAS_OBJECT_SELECT` | None for the tested object-level selection path |
| Rename object | New narrow `Plater::rename_object` seam, also used by `GUI_ObjectList::update_name_in_model`; after model mutation it directly refreshes `sidebar->obj_list()` | Existing `Plater::TakeSnapshot("Rename Object")` timing, then model/volume name and mesh-name update | Adapter schedules `Contents` after the seam returns | Requires a constructed legacy object list; invisible operation was not tested |
| Duplicate object | `Selection::clone(1)` through new `Plater::duplicate_object` seam; `paste_objects_from_clipboard` directly calls `wxGetApp().obj_list()->paste_objects_into_list` | Existing `"Selection-clone"` snapshot inside `Selection::clone` | Existing selection event plus adapter's post-command `Contents`, coalesced | Requires a constructed legacy object list; invisible operation was not tested |
| Remove object | Existing `Plater::priv::delete_object_from_model` through new `Plater::delete_object` seam, followed by a direct `sidebar->obj_list()->delete_object_from_list` call | Existing `TakeSnapshot("Delete Object: <name>")` before worker cancellation and model deletion | Existing selection event plus adapter's post-command `Contents`, coalesced | Requires a constructed legacy object list; cut-object confirmation and invisible operation were not tested |
| Transform committed | Native `GLCanvas3D::do_move` / `do_rotate`; the tested pointer drag used the native move gizmo | Existing `"Gizmo-Move"` snapshot at `GLGizmoMove3D::on_stop_dragging` | `EVT_GLCANVAS_INSTANCE_MOVED` / `EVT_GLCANVAS_INSTANCE_ROTATED` | Rotation was not separately exercised; one move or rotate is required |
| Undo/redo | `Plater::undo`, `Plater::redo`, `UndoRedo::Stack`, and `update_after_undo_redo` | Entirely existing Orca stack and restoration timing | Adapter compares authoritative before/after projections and adds `History` plus the changed domains | Direct legacy toolbar/menu undo observation is out of scope; adapter commands are runtime-proven |

## 5. Hard-event results

### Event A — viewport selection changed

Passed in both directions in the real application:

- Selecting object `102` through `IWorkspace::select_object` updated the real
  viewport and produced one coalesced event at revision 3; the callback
  snapshot selected object `102`.
- Clicking `Slider` directly in the native viewport changed the active plate to
  `87` and produced selection `91` in the callback snapshot at revision 5.
- Selecting `Grundkörper` through the adapter again produced one event at
  revision 6 with selection `102`.
- No repeated selection loop occurred in either direction.

### Event B — workspace contents and history changed

Passed in the real application for the tested full objects:

- Rename changed `Grundkörper` to `Grundkörper renamed`; undo restored the
  original name and redo restored the new name. Events were `Contents`, then
  `Contents|History` for both history directions.
- Duplicate returned new stable `ObjectId 234`; the callback snapshot contained
  both `102` and `234`. Undo removed `234`; redo restored the same ID and
  selection according to Orca's stack.
- Remove deleted the duplicate `234`; undo restored it with the same ID and
  name while leaving the original `102` present.
- Every callback read the event revision and stable post-command state. Reasons
  from command and selection paths were coalesced.

The runtime values of `can_undo` and `can_redo` were projected directly from
`Plater::can_undo/can_redo`; the adapter did not infer or maintain them.

### Event C — object transform committed and restored

Passed through the literal native pointer path. With Orca's Move tool active, a
pointer drag of the visible gizmo moved object `102` from X `140.800` to
`195.165`. Exactly one `Transform` event was emitted when the drag committed at
revision 9; intermediate repaint state did not reach the consumer. Undo emitted
`History|Transform` and restored X `140.800`; redo emitted
`History|Transform` and restored X `195.165`. Orca's own gizmo snapshot and
history paths remained authoritative.

## 6. Captured vertical-workflow log

The following is the complete concise log for content/history operations. The
project had two plates and three named objects (`Grundkörper`, `Deckel`, and
`Slider`). The initial empty projection occurred because the development probe
was constructed before command-line project loading; revision 1 is the first
stable loaded-project projection.

```text
SNAPSHOT source=initial revision=0 plates=1 active=11 selection=none can_undo=0 can_redo=0 objects=[]
EVENT revision=1 reasons=Selection
SNAPSHOT source=callback revision=1 plates=2 active=83 selection=none can_undo=1 can_redo=1 objects=[102:Grundkörper; 113:Deckel; 91:Slider]
COMMAND select success
EVENT revision=2 reasons=Selection
SNAPSHOT source=callback revision=2 plates=2 active=83 selection=102 can_undo=1 can_redo=0 objects=[102:Grundkörper; 113:Deckel; 91:Slider]
COMMAND rename success
EVENT revision=3 reasons=Contents
SNAPSHOT source=callback revision=3 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed; 113:Deckel; 91:Slider]
COMMAND undo success
EVENT revision=4 reasons=Contents|History
SNAPSHOT source=callback revision=4 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper; 113:Deckel; 91:Slider]
COMMAND redo success
EVENT revision=5 reasons=Contents|History
SNAPSHOT source=callback revision=5 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed; 113:Deckel; 91:Slider]
COMMAND duplicate success object_id=234
SNAPSHOT source=command revision=5 plates=2 active=83 selection=234 can_undo=1 can_redo=0 objects=[102:Grundkörper renamed; 113:Deckel; 234:Grundkörper renamed; 91:Slider]
EVENT revision=6 reasons=Selection|Contents
SNAPSHOT source=callback revision=6 plates=2 active=83 selection=234 can_undo=1 can_redo=0 objects=[102:Grundkörper renamed; 113:Deckel; 234:Grundkörper renamed; 91:Slider]
COMMAND undo success
EVENT revision=7 reasons=Selection|Contents|History
SNAPSHOT source=callback revision=7 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed; 113:Deckel; 91:Slider]
COMMAND redo success
EVENT revision=8 reasons=Selection|Contents|History
SNAPSHOT source=callback revision=8 plates=2 active=83 selection=234 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed; 113:Deckel; 234:Grundkörper renamed; 91:Slider]
COMMAND remove success
EVENT revision=9 reasons=Selection|Contents
SNAPSHOT source=callback revision=9 plates=2 active=83 selection=none can_undo=1 can_redo=1 objects=[102:Grundkörper renamed; 113:Deckel; 91:Slider]
COMMAND undo success
EVENT revision=10 reasons=Selection|Contents|History
SNAPSHOT source=callback revision=10 plates=2 active=83 selection=234 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed; 113:Deckel; 234:Grundkörper renamed; 91:Slider]
```

The following is the complete selection, pointer-transform, and lifetime log
from the final manual run. Object `91` is `Slider`, the object selected directly
in the viewport:

```text
SNAPSHOT source=initial revision=0 plates=1 active=11 selection=none can_undo=0 can_redo=0 objects=[]
EVENT revision=1 reasons=Selection
SNAPSHOT source=callback revision=1 plates=2 active=83 selection=none objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
EVENT revision=2 reasons=Selection
SNAPSHOT source=callback revision=2 plates=2 active=83 selection=none objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
COMMAND select success
EVENT revision=3 reasons=Selection
SNAPSHOT source=callback revision=3 plates=2 active=83 selection=102 objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
EVENT revision=4 reasons=Selection
SNAPSHOT source=callback revision=4 plates=2 active=87 selection=none objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
EVENT revision=5 reasons=Selection
SNAPSHOT source=callback revision=5 plates=2 active=87 selection=91 objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
COMMAND select success
EVENT revision=6 reasons=Selection
SNAPSHOT source=callback revision=6 plates=2 active=87 selection=102 objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
EVENT revision=7 reasons=Selection
SNAPSHOT source=callback revision=7 plates=2 active=87 selection=none objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
EVENT revision=8 reasons=Selection
SNAPSHOT source=callback revision=8 plates=2 active=87 selection=102 objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
EVENT revision=9 reasons=Transform
SNAPSHOT source=callback revision=9 plates=2 active=87 selection=102 objects=[102:Grundkörper pos=(195.165,128.001,23.250); 113:Deckel; 91:Slider]
COMMAND undo success
EVENT revision=10 reasons=History|Transform
SNAPSHOT source=callback revision=10 plates=2 active=87 selection=102 objects=[102:Grundkörper pos=(140.800,128.001,23.250); 113:Deckel; 91:Slider]
COMMAND redo success
EVENT revision=11 reasons=History|Transform
SNAPSHOT source=callback revision=11 plates=2 active=87 selection=102 objects=[102:Grundkörper pos=(195.165,128.001,23.250); 113:Deckel; 91:Slider]
PROBE consumer destroyed
```

After `PROBE consumer destroyed`, `Slider` was selected again directly in the
native viewport. The application remained alive and the log remained unchanged,
demonstrating that no callback targeted the destroyed subscriber.

## 7. Automated and manual evidence

| Check | Result | Evidence classification |
|---|---|---|
| Contract/fake focused tests | Passed: 41 assertions in 7 Catch2 cases, randomized order | Automated runtime |
| Contract/fake standalone C++17 syntax | Passed; no Orca GUI link dependency | Compiled evidence |
| Full `OrcaSlicer` app | Built and linked successfully | Compiled evidence |
| Affected adapter/probe/Plater/object-list translation units | Compiled in the full target | Compiled evidence |
| Real two-plate workspace projection | Passed with stable IDs `83`, `102`, `113`, `91` | Native runtime |
| Adapter selection reflected in viewport | Passed visually and in event log | Native runtime/manual visual inspection through screenshot |
| Rename/duplicate/remove and history | Passed | Native runtime |
| Direct viewport selection | Passed: `Slider` projected as selected object `91` | Manual native runtime |
| Native committed move and transform history | Passed through a pointer drag of Orca's native move gizmo | Manual native runtime |
| Subscriber destruction | Passed after a post-close viewport selection; no later callback and process stayed alive | Manual native runtime |
| Legacy UI constructed and visible | Passed; this was the runtime prerequisite used for every native check | Native runtime |
| Legacy UI completely covered by an empty JusPrin window | Not tested in this spike; assigned to [orca-invisible-legacy-ui-spike.md](orca-invisible-legacy-ui-spike.md) | Missing evidence for the follow-up question |
| `libslic3r` existing suite | 115/116 passed; one unchanged placeholder-parser case deterministically segfaulted | Automated runtime; unrelated failure, not claimed green |

The failing existing case is `Scenario: Placeholder parser coFloatsOrPercents
vector access` at `tests/libslic3r/test_placeholder_parser.cpp:244`. It failed
again when run alone. No `libslic3r` or placeholder-parser source is changed by
this spike, so independence from this spike is a strong inference, not a
before-change reproduction.

## 8. Coupling and rebase ledger

| File(s) | Classification | Why | Locality/rebase risk |
|---|---|---|---|
| `src/slic3r/GUI/JusPrin/Workspace/Workspace.hpp` | Fork-owned contract | DTOs, commands, reasons, subscription | Isolated; no upstream types |
| `FakeWorkspace.hpp`, `OrcaWorkspaceAdapter.*`, `WorkspaceProbe.*` | Fork-owned fake, adapter, probe | Implementation and development consumer | Isolated directory; adapter intentionally includes Orca headers |
| `tests/workspace/*` | Fork-owned tests | GUI-independent contract verification | Isolated |
| `src/slic3r/CMakeLists.txt`, `tests/CMakeLists.txt` | Build registration | Register sources and focused tests | Small list edits; modest list-conflict risk |
| `Plater.hpp` | Upstream behavior seam | Three behavior-oriented object commands | Four additions, one newline normalization; localized declaration block |
| `Plater.cpp` | Upstream behavior seam and probe attachment | Reusable rename/duplicate/delete seams; env-gated development adapter construction; rename/delete retain direct legacy object-list updates | 50 additions in localized areas; `Plater` is active upstream code, so moderate overlap risk |
| `GUI_ObjectList.cpp` | Existing behavior modification | Route object rename through the reusable Plater seam | 2 additions / 9 removals, one function; likely low-to-moderate overlap risk |

Upstream-owned implementation/header changes total 56 additions and 10
removals across three files, below the spike targets. No selection, geometry,
plate, or undo algorithm was copied. No JusPrin dependency was added to
`libslic3r`.

## 9. Direct Orca dependencies used by the adapter

- `Plater` and its public model, canvas, plate-list, history, and new object
  command seams.
- `Model`, `ModelObject`, `ModelInstance`, and Orca's in-process `ObjectID`.
- `PartPlate` / `PartPlateList` for stable plate IDs and membership.
- `Selection` for authoritative object-level selection classification.
- `GLCanvas3D` and existing selection, moved, rotated, and plate-selection
  events.
- wx GUI-thread `CallAfter` solely for next-turn coalescing.

None of these dependencies appears in the JusPrin-facing contract or probe.
At runtime, however, the exercised command implementations also require the
legacy `Sidebar` and `GUI_ObjectList` instances to exist. That dependency is
behind the adapter and is explicitly permitted by this result's revised
acceptance condition.

## 10. Missing events or APIs

- Orca has no single object controller covering rename, duplicate, and delete.
  Three narrow `Plater` seams were added, but their underlying paths still
  update the constructed legacy `GUI_ObjectList`.
- There is no centralized native notification for every possible legacy
  content mutation. Import observation was optional and was not generalized in
  this spike.
- Undo/redo has no domain-reason event. The adapter invokes the existing stack,
  then compares read-only authoritative projections to classify the restored
  domains.
- The local Computer Use backend could not send coordinate pointer actions to
  this wx/OpenGL window, so the viewport click, gizmo drag, and post-destruction
  selection were completed manually and captured by the probe log.

## 11. Recommended boundary under the revised prerequisite

Retain this boundary:

```text
future native pane / Agent bridge
              -> IWorkspace (JusPrin value types)
              -> OrcaWorkspaceAdapter (GUI thread)
              -> small Plater behavior seams and existing Orca events/history
              -> constructed legacy Sidebar / GUI_ObjectList where existing paths require it
```

Keep projections read-only and on demand. Keep IDs session-scoped. Add domain
commands only as behavior-oriented Orca seams; do not expose model containers
or make `GUI_ObjectList` the public command surface. The old list remains an
internal runtime prerequisite under this architecture. A production lifecycle
owner should replace the environment-gated probe construction, while the
contract and adapter can remain unchanged.

## 12. Human product or architecture decisions

- Whether one object may intentionally appear on multiple plate projections,
  and how a future pane should present that without implying persistent ID
  semantics.
- Whether selection outside the object-level subset should disable object
  commands or show an explicit unsupported-selection state.
- Whether production wants one aggregate workspace refresh event or finer
  detail-query APIs once object counts make full projection measurably costly.
- Whether the three object command seams should remain on `Plater` or move into
  a dedicated Orca-side workspace controller as the surface expands.
- Whether native toolbar/menu undo and redo must also be observed globally; the
  spike proves history through the contract commands.
- Whether a completely occluded legacy `MainFrame` continues to process every
  command and event without surfacing or stealing focus; this is the subject of
  the follow-up invisible-legacy-UI spike.

## 13. Reproduction steps

### Build and launch

1. Check out the feature branch:

   ```bash
   git switch codex/orca-workspace-adapter-spike
   ```

2. Build the application and focused contract tests using the configured arm64
   build tree:

   ```bash
   cmake --build build/arm64 --config RelWithDebInfo --target OrcaSlicer workspace_contract_tests --
   ```

3. Run the focused tests:

   ```bash
   build/arm64/tests/workspace/RelWithDebInfo/workspace_contract_tests.app/Contents/MacOS/workspace_contract_tests --order rand --warn NoAssertions
   ```

4. Quit any other running OrcaSlicer process. Launch the instrumented application
   from the repository root with the probe enabled and the deterministic project:

   ```bash
   JUSPRIN_WORKSPACE_SPIKE=1 \
   JUSPRIN_WORKSPACE_SPIKE_LOG=/tmp/jusprin-workspace-spike.log \
   build/arm64/src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/OrcaSlicer \
   "/Users/kenneth/Downloads/stls/Projekt+-+standard+-+2+plates.3mf"
   ```

5. Wait until both the normal OrcaSlicer window and the separate window titled
   `JusPrin Workspace Adapter Probe` appear. Wait until the probe's log shows a
   snapshot containing two plates and `Grundkörper`, `Deckel`, and `Slider`.
   The first probe snapshot may be empty because it can precede command-line
   project loading; do not use that first snapshot as the loaded baseline.

### Native workflow

Use the visible labels below. Do not look for these controls in OrcaSlicer's
menus; Refresh, Select, Rename, Duplicate, Remove, Undo, and Redo are buttons in
the separate probe window.

1. In the probe's `Object:` dropdown, choose the entry whose name is
   `Grundkörper`, then click the probe button labelled `Select`. Confirm that
   `Grundkörper` becomes selected in the normal Orca viewport and that the log
   contains one `Selection` event whose callback snapshot selects its ID.
2. In the normal Orca viewport, click the gray object named `Slider` on the
   right-hand plate. Confirm that the next stable callback snapshot selects the
   ID associated with `Slider`.
3. In the probe, choose `Grundkörper` again and click `Select`.
4. Click the probe button labelled `Rename`. Confirm that the callback snapshot
   contains `Grundkörper renamed` and a `Contents` event.
5. Click the probe button labelled `Undo`, then `Redo`. Confirm that the name
   changes back and forward and both events contain `History|Contents`.
6. Click `Duplicate`. Record the returned ID from the `COMMAND duplicate`
   line and confirm that this ID appears as a second object in the callback
   snapshot.
7. Click `Undo`, then `Redo`. Confirm that the duplicate disappears and returns
   with the same ID.
8. Click `Remove`. Confirm that the duplicate disappears while the original
   remains. Click `Undo` and confirm that the same duplicate ID returns.
9. In the probe dropdown, choose the original renamed object rather than the
   duplicate, then click `Select`.
10. Bring the normal Orca window to the front. Press Control-M to activate its
    Move tool. Drag one of the visible colored move-gizmo arrows with the pointer
    and release it after producing a clear position change.
11. Confirm that the probe log contains exactly one committed `Transform` event
    for the drag and that its callback snapshot contains the final position.
12. In the probe, click `Undo`, then `Redo`. Confirm that the log contains
    `History|Transform` and that the two callback snapshots restore the original
    and moved positions respectively.
13. Close only the probe window. In the normal Orca viewport, click `Slider`
    again. Confirm that Orca remains alive and that no line is appended to the
    probe log after `PROBE consumer destroyed`.

Session-scoped object and plate IDs may differ between runs. Match objects by
name and use the IDs recorded by that run rather than expecting the example IDs
in Section 6.
