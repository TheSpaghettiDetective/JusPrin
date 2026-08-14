# Invisible legacy Orca UI prerequisite spike

**Status:** Proposed implementation handoff

**Primary question:** Can the existing Orca UI remain fully constructed and
operational, but completely hidden from the user behind an empty JusPrin shell
window, while every command available in the workspace probe continues to work?

## 1. Exact configuration being tested

This spike tests one configuration only:

1. Orca constructs its normal `MainFrame`, `Plater`, `Sidebar`,
   `GUI_ObjectList`, `GLCanvas3D`, model, plate list, and undo stack exactly as it
   does today.
2. The legacy `MainFrame` remains shown and mapped. Do not destroy it and do not
   call `Show(false)` on it.
3. A new opaque top-level wx window titled `JusPrin Empty Shell Spike` is placed
   directly over the entire legacy Orca window and kept above it in z-order.
4. The shell contains no product controls, WebView, menus, toolbar, sidebar, or
   viewport. Its client area is a single solid background.
5. The existing `JusPrin Workspace Adapter Probe` remains a separate development
   window above the empty shell so the tester can use it.
6. During the test, the user must never see or interact with the legacy Orca
   window. All test commands are initiated from the probe.

For this spike, **invisible** means fully occluded by the opaque empty shell.
It does not mean unconstructed, destroyed, minimized, transparent, or hidden
with `Show(false)`. Testing those lifecycle modes would be a different spike.

## 2. Goal

Demonstrate that a JusPrin shell can be the only application surface visible to
the user while the complete legacy Orca UI continues to satisfy internal
runtime dependencies behind it.

The dependency direction remains:

```text
visible empty JusPrin shell       development probe
             |                           |
             |                           v
             |                    IWorkspace contract
             |                           |
             v                           v
fully occluded legacy Orca MainFrame / Plater / Sidebar / GUI_ObjectList
                                      |
                                      v
                         Model / plates / GLCanvas3D / undo stack
```

This spike does not attempt to remove or decouple the legacy UI. Its purpose is
to test whether keeping that UI alive but fully occluded is operationally viable.

## 3. Implementation constraints

- Put all empty-shell spike code under `src/slic3r/GUI/JusPrin/` where practical.
- Gate the shell and probe behind a development-only environment variable such
  as `JUSPRIN_INVISIBLE_LEGACY_UI_SPIKE=1`.
- Do not change model, selection, geometry, plate, or undo algorithms.
- Do not replace, stub, or conditionally skip construction of the legacy
  `MainFrame`, `Sidebar`, `GUI_ObjectList`, or `GLCanvas3D`.
- Do not drive the covered legacy window with synthesized clicks or keystrokes.
- Do not add product styling or production shell behavior.
- Keep the shell opaque. A screenshot must demonstrate that no part of the
  legacy window is visible around or through it.
- Keep the probe above the shell without bringing the legacy window forward.
- Closing the spike shell must restore or reveal the existing legacy window so
  a failed development run is recoverable.

## 4. Empty-shell behavior

The empty shell must:

- Cover the legacy `MainFrame` client area and frame decorations on the display
  used for the test.
- Follow legacy-window movement, resizing, maximization, and restoration closely
  enough that the legacy UI never becomes exposed.
- Return to the front if an ordinary non-modal legacy refresh attempts to raise
  the old window.
- Allow the separate probe to remain above it and receive focus.
- Log shell creation, coverage bounds, legacy-window visibility state, focus or
  raise events, and destruction.

Do not silently suppress a modal dialog. If a tested command exposes a legacy
dialog above the shell, record that as a failure because the legacy UI became
user-visible.

## 5. Deterministic project

Use the same two-plate project used by the workspace-adapter spike:

`/Users/kenneth/Downloads/stls/Projekt+-+standard+-+2+plates.3mf`

Expected starting objects in the recorded run are:

- `Grundkörper`
- `Deckel`
- `Slider`

Record the actual session-scoped IDs from the initial loaded-project snapshot;
do not hard-code them into the implementation.

## 6. Required probe workflow

After project loading is complete and the empty shell fully covers the legacy
window, execute every command exposed by the existing probe:

1. **Refresh:** capture a snapshot containing two plates and all three named
   objects.
2. **Select:** choose `Grundkörper` in the probe and select it. Verify the
   callback snapshot reports that object and exactly one coalesced Selection
   notification is produced.
3. **Rename:** rename `Grundkörper`. Verify the callback snapshot contains the
   new name.
4. **Undo:** undo the rename. Verify the original name is restored with a
   `History|Contents` notification.
5. **Redo:** redo the rename. Verify the new name returns.
6. **Duplicate:** duplicate the renamed object. Verify a new stable `ObjectId`
   is returned and appears in the callback snapshot.
7. **Undo:** undo duplication and verify the new object disappears.
8. **Redo:** redo duplication and verify the same object identity returns.
9. **Remove:** remove the duplicate and verify it disappears while the original
   remains.
10. **Undo:** undo removal and verify the duplicate returns with the same ID.
11. **Select another object:** choose `Slider` in the probe and select it. Verify
    the final callback snapshot reports `Slider`.
12. Close the probe. Verify its destruction is logged and that the application
    and empty shell remain alive without exposing the legacy window.

Throughout the sequence, verify all of the following:

- The empty shell remains the only main application window visible to the user.
- The legacy window never rises above, appears around, or becomes visible
  through the shell.
- No command requires clicking, focusing, or reading the legacy UI.
- No legacy confirmation, error, or progress dialog becomes visible.
- The application does not hang while the covered legacy window processes its
  normal update and event paths.
- `can_undo`, `can_redo`, selection, names, object presence, and returned IDs
  match authoritative Orca state after every command.

## 7. Scope of this workflow

The existing probe has buttons for Refresh, Select, Rename, Duplicate, Remove,
Undo, and Redo. Those buttons define “all tests using the probe” for this spike.

Direct clicking in the covered legacy viewport and dragging its gizmo are not
part of this spike because the test explicitly forbids interaction with the
covered window. Their native event paths were exercised in the preceding
workspace-adapter spike. This spike tests whether the probe command surface
continues working while the legacy UI is occluded.

## 8. Required evidence

Capture:

- The source revision and complete build configuration.
- A screenshot showing the opaque empty shell covering the legacy Orca window,
  with the separate probe visible above it.
- Shell and probe logs with timestamps or monotonically ordered sequence
  numbers.
- A concise snapshot after every probe command.
- Notification revision and reasons after every state-changing command.
- The duplicate object's returned ID across duplicate, undo, redo, remove, and
  undo.
- Evidence that no legacy window or modal dialog surfaced during the workflow.
- Evidence that closing the probe destroys its subscription without crashing or
  exposing the covered legacy window.
- Focused contract tests and a successful build of the affected application
  target.

Manual visual evidence must be labelled manual. Logs and screenshots must not
be described as proof that the legacy UI was unconstructed; it remains fully
constructed by definition.

## 9. Exit criteria

### Pass

Pass only when:

- The empty JusPrin shell fully occludes the legacy Orca window for the entire
  workflow.
- Every probe command in Section 6 succeeds against the real Orca model.
- Every callback snapshot contains stable post-command state.
- Undo and redo preserve Orca's existing name, object identity, presence,
  selection, and availability semantics.
- No command exposes or requires interaction with the legacy UI.
- No unexpected modal dialog, focus steal, hang, or crash occurs.
- The probe can be destroyed while the covered application and empty shell
  remain alive.

### Adjust

Adjust when the model and history operations remain correct but the covering
window needs a small, identifiable lifecycle or z-order integration change.
Record the exact window event or command that exposed the legacy UI.

### Reject

Reject this concealment approach when any required probe command:

- Requires the user to interact with the legacy UI.
- Consistently raises or exposes the legacy window or one of its dialogs.
- Stops processing because the covered window cannot receive the required
  lifecycle, paint, focus, or event-loop behavior.
- Produces state or history different from the visible-legacy-UI baseline.

## 10. Required result document

Create `agent-docs/orca-invisible-legacy-ui-spike-results.md` containing:

1. Commit and build configuration tested
2. Exact legacy-window and empty-shell lifecycle configuration
3. Pass, adjust, or reject result
4. Complete ordered probe workflow log
5. Before/after snapshots for every command
6. Screenshot and focus/z-order evidence
7. Any surfaced legacy windows or dialogs
8. Automated and manual test evidence
9. Production files changed and rebase risk
10. Limitations of keeping the legacy UI constructed but occluded

Do not claim success based only on the shell appearing above Orca. Every probe
command must complete while the shell remains the only user-visible main window.
