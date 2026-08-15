# Orca full-window UI coupling spike

**Status:** Implementation handoff

**Timebox:** Two elapsed hours. Stop when the timebox expires and write the
result document with whatever was learned. Do not extend the spike to finish
visual polish or refactor the OrcaSlicer UI.

**Primary question:** Can the supplied JusPrin full-window design be composed
inside OrcaSlicer's existing main window around the real `GLCanvas3D`, while
the legacy Orca controls remain constructed but invisible, without creating a
large or fragile upstream rebase surface?

This spike measures coupling. A visually convincing screen is useful evidence,
but the main output is a precise record of which Orca classes and files had to
change, why they had to change, and whether those changes can be isolated.

**Related documents:**

- [`native-ui-rewrite-plan.md`](native-ui-rewrite-plan.md)
- [`orca-workspace-adapter-spike-results.md`](orca-workspace-adapter-spike-results.md)
- [`orca-invisible-legacy-ui-spike-results.md`](orca-invisible-legacy-ui-spike-results.md)
- [`orcaslicer-idiosyncrasies-lessons-learned.md`](orcaslicer-idiosyncrasies-lessons-learned.md)

---

## 1. Starting facts

The current branch already contains two relevant experiments:

1. `OrcaWorkspaceAdapter` exposes a small JusPrin-owned workspace contract over
   the real `Plater`, model, selection, plates, and undo stack.
2. The invisible-legacy experiment showed that the legacy `MainFrame`,
   `Sidebar`, and `GUI_ObjectList` can stay constructed while another opaque
   window prevents the user from seeing them.

Neither result answers the question in this spike. The production direction is
not a second top-level cover window. The new JusPrin UI should occupy the real
Orca main window and show the real `GLCanvas3D` as its center viewport.

Known ownership that matters before editing:

- `MainFrame` owns Orca's top bar, notebook, global shortcuts, and the `Plater`.
- `Plater::priv` owns the `wxAuiManager`, legacy `Sidebar`, `View3D`, Preview,
  Assemble view, model, slicing actions, and much of the event routing.
- `View3D` constructs the `wxGLCanvas` and `GLCanvas3D` and currently enables
  the stock viewport toolbars and gizmo UI.
- `GLCanvas3D::_render_overlays()` draws several stock toolbars, gizmo controls,
  labels, and other overlays in one render path.

Treat these statements as navigation clues, not as a prescribed solution.
Verify actual behavior in the current source and running application before
making conclusions.

## 2. Design slice to reproduce

Use the supplied design as the visual reference:

![JusPrin full-window design reference](</var/folders/6y/fn1z996148dc7xjy2mxx5wkc0000gn/T/codex-clipboard-77726b55-1f32-4427-a802-cdca8bcdb722.png>)

The temporary image path may not survive outside the current workstation. The
required structure is therefore described here as well:

```text
+-----------------------------------------------------------------------+
| Home  | ORCA             Slice | Check Print | Print     printer/status|
+----+-------------------------------------------+------------------------+
|    |                                           | First print          + |
| O  |                                           |------------------------|
| B  |        real Orca GLCanvas3D               | Current setup card     |
| J  |        bed, model, camera, picking        |                        |
| E  |                                           | user/agent transcript  |
| C  |                                           |                        |
| T  |                                           |                        |
| S  |                                           |------------------------|
|    |             small status toast            | chat input          ^  |
+----+-------------------------------------------+------------------------+
```

The spike must reproduce these major regions:

- A compact white top navigation row.
- A narrow, collapsed Objects rail at the left.
- The existing Orca `GLCanvas3D` filling the center.
- A fixed right pane with a print header, current-setup card, short static chat
  transcript, and input field.
- The mockup's light neutral colors, borders, spacing, and approximate
  proportions. Exact fonts, icons, shadows, and pixel matching are out of
  scope.

The new shell must be opaque. No legacy top bar, tab strip, sidebar, object
list, preset controls, or stock viewport toolbar may be visible in the resting
state. The bed, model, selection highlight, and active manipulation handles are
not legacy chrome and may remain visible.

## 3. Architectural constraints

These are hard rules for the experiment:

1. Gate the spike behind `JUSPRIN_FULL_WINDOW_UI_SPIKE=1`. With the variable
   absent, OrcaSlicer must retain its current layout and behavior.
2. Put new shell and presentation code under `src/slic3r/GUI/JusPrin/` wherever
   practical.
3. Use the existing `View3D` and its real `GLCanvas3D`. Do not create a fake
   viewport, render a screenshot, or create a second model/canvas solely for the
   JusPrin shell.
4. Keep the legacy `Sidebar` and `GUI_ObjectList` constructed. They may be
   excluded from the visible layout, but do not delete them or skip their
   construction in this spike.
5. Build the new surface inside the existing `MainFrame`. Do not revive the
   separate top-level cover-window approach from the invisible-legacy spike.
6. Orca remains authoritative for model state, selection, slicing, Preview,
   transforms, and undo/redo. New controls must call existing behavior paths or
   the existing JusPrin workspace adapter.
7. Do not synthesize mouse or keyboard events against invisible legacy
   controls. Any such requirement is a coupling finding and should be recorded.
8. Do not copy selection, slicing, camera, geometry, transform, plate, or undo
   logic into the new UI.
9. Do not broadly refactor `MainFrame`, `Plater`, `Sidebar`, `GUI_ObjectList`,
   `GLCanvas3D`, or the gizmo hierarchy to make the experiment look complete.
   Stop and report the obstacle instead.
10. Changes to upstream-owned Orca files should be attachment points or narrow,
    reusable presentation seams. The default legacy mode must not pass through
    fork-owned types.

## 4. Required hard events

The resting screenshot alone is not enough. Demonstrate the following events
against a real loaded model.

### Event A — The real viewport survives the new shell

1. Start in the spike mode with a project containing at least one model.
2. Orbit, pan, and zoom in the center viewport.
3. Select the model by clicking it.
4. Resize and maximize the main window, then repeat an orbit and selection.

Required result:

- The viewport is the existing production `GLCanvas3D`.
- Camera input, picking, selection highlighting, and resizing continue to work.
- No legacy panel becomes visible, overlays the new pane, or steals focus.
- The selected object reported through `IWorkspace::snapshot()` agrees with the
  visible canvas selection.

### Event B — A new top-level Slice action preserves the new shell

1. Click the new `Slice` button in the JusPrin top row.
2. Let Orca complete its normal slicing path.
3. Show the existing G-code Preview in the center region.
4. Return to Prepare and confirm that the same project and selection remain.

Required result:

- The new button delegates to Orca's existing slice action and snapshot/state
  machinery. Do not reimplement slicing orchestration.
- The center swaps between the existing Prepare and Preview views.
- The JusPrin top row, Objects rail, and right pane remain in place during both
  views.
- Hidden legacy tabs or buttons do not need to be clicked programmatically.

If keeping the shell while switching to Preview requires changes in several
owners, record every owner and stop before generalizing the solution.

### Event C — Stock viewport chrome is hidden without disabling behavior

1. In the spike resting state, hide the stock main toolbar, plate toolbar,
   collapse control, and stock gizmo picker shown over `GLCanvas3D`.
2. Select the model and activate either Move or Rotate from a small JusPrin
   control. The control may be a temporary button in the new shell; do not spend
   the timebox building the final contextual action strip.
3. Drag the existing native gizmo handle.
4. Undo and redo the transform through Orca's existing history path.

Required result:

- Hiding stock chrome does not alter which plate or objects render, disable
  picking, disable active gizmo handles, or change transform semantics.
- Move or Rotate is activated through an explicit behavior-oriented API, not a
  click on an invisible toolbar icon.
- The existing gizmo performs the transform and creates the existing Orca undo
  snapshot.
- Undo and redo restore the transform.

This is expected to be the most informative event. Existing enable/disable
flags may combine visual state with behavior. Do not paper over that coupling.
If a narrow visibility-versus-capability split is required, implement at most
one such split and record exactly what was entangled.

## 5. Recommended implementation order

The agent may choose a different implementation if source inspection shows a
smaller seam, but remain inside the two-hour limit.

### 0–15 minutes — Baseline and map

- Record the starting commit and dirty worktree state.
- Confirm the existing application can be launched incrementally.
- Capture the legacy Prepare screen before editing.
- Trace the current owners of the MainFrame top bar, Plater AUI layout,
  Prepare/Preview switch, Slice action, and viewport overlays.

### 15–45 minutes — Fork-owned shell

- Create one fork-owned spike component under `src/slic3r/GUI/JusPrin/`.
- Construct the top row, collapsed Objects rail, center host, and right pane.
- Prefer one attachment call from Orca into the spike component over scattered
  `if (spike_mode)` conditions.
- Keep the visual controls simple and deterministic.

### 45–80 minutes — Real canvas and legacy coexistence

- Place or retain the existing `View3D` in the center of the new layout.
- Keep the legacy Sidebar/ObjectList alive but outside the visible layout.
- Remove the legacy top bar/tab/sidebar pixels from the spike presentation.
- Exercise Event A before adding more behavior.

### 80–105 minutes — Behavior seams

- Wire the new Slice action and Prepare/Preview switch for Event B.
- Attempt the smallest presentation-only separation needed for Event C.
- Prefer the existing `OrcaWorkspaceAdapter` for workspace reads and commands
  it already supports.

### 105–120 minutes — Evidence and ledger

- Build and run the affected application if the incremental build remains
  usable.
- Capture screenshots of Prepare, selected/transform state, and Preview.
- Run the three hard events and record exact failures or partial results.
- Finish the coupling and rebase ledger before doing any optional cleanup.

If a clean or unexpectedly broad rebuild consumes the timebox, stop and report
that limitation. Do not claim runtime success from compilation or inspection
alone.

## 6. Scope limits

Do not implement:

- Production-ready visual styling, responsive breakpoints, or final assets
- A production Agent WebView, backend, streaming, authentication, or settings
- A real AI conversation; static transcript content is sufficient
- A production plates/objects browser; the collapsed rail is sufficient
- Check Print or Print behavior
- Printer and material selectors
- Final contextual action-strip placement
- New selection, transform, undo, slicing, or Preview logic
- Deconstruction of the legacy Sidebar or ObjectList
- General presentation frameworks, dependency injection frameworks, event
  buses, or plugin systems
- Project-format, profile, mesh, or slicing-algorithm changes

Do not fix unrelated bugs discovered during the spike. Record them separately.

## 7. Verification and evidence

At minimum, the result must include:

- Starting commit, final worktree diff, build configuration, and launch command
- Whether the build and application launch succeeded
- A before screenshot of the legacy UI
- A Prepare screenshot of the new full-window shell with a real model
- A screenshot with a selected object or active Move/Rotate gizmo
- A Preview screenshot after the new Slice action
- An ordered log for Events A–C, including partial or failed steps
- Confirmation that launching without the environment variable retains the
  existing Orca layout
- Any legacy control, dialog, focus transfer, or visual flash that surfaced
- Any untested item and the reason it was not tested

Tests should target new fork-owned presentation decisions where practical.
Do not add large widget snapshot tests merely to increase test count. The real
application workflow is the main evidence for this spike.

## 8. Coupling and rebase ledger

The result document must contain a section with this exact title:

> ## Coupling and rebase ledger

List every changed production file, including fork-owned files and build files.
Use this table:

| File | Ownership | Lines +/− | Integration point | Why required | Coupling kind | Expected upstream churn | Containment/removal path | Risk |
|---|---|---:|---|---|---|---|---|---|

Use these ownership labels:

- **Fork-owned:** a file under the JusPrin namespace/directory
- **Build registration:** a CMake or resource list edit
- **Upstream attachment point:** a small call that attaches the fork-owned shell
- **Upstream presentation seam:** a reusable visibility/layout API
- **Upstream behavior seam:** a reusable command or state API
- **Existing behavior modification:** a change to Orca's default code path
- **Duplicated behavior:** Orca logic copied into the fork

Use one or more of these coupling kinds:

- Widget ownership/lifetime
- Layout/parenting
- Visibility/presentation
- Input/focus
- State/selection
- Command/behavior
- Event ordering
- Rendering/OpenGL
- Undo/history
- Build/resources

For each upstream-owned change, answer all of the following in the row or a
note immediately below it:

1. Was the change necessary for the design, or only convenient for the spike?
2. Is it localized to one function or declaration block?
3. Does the default Orca UI execute the changed branch?
4. Can future JusPrin screens reuse the seam?
5. Is the file an active upstream hotspot likely to conflict during rebases?
6. Could the change move into a fork-owned file with a smaller attachment API?

Finish the ledger with these totals:

- Number of fork-owned production files changed
- Number of upstream-owned production files changed, excluding build lists
- Added and removed lines in upstream-owned files
- Number of separate upstream functions modified
- Number of direct Orca classes referenced by fork-owned code
- Number of copied Orca behavior blocks or algorithms
- Number of legacy widgets that must remain alive for Events A–C

Also include a short **coupling discovered while trying** table. This table
records failed or abandoned approaches, because a clean final diff can hide the
most important learning:

| Attempt | Expected seam | Actual dependency found | Workaround used | Architectural implication |
|---|---|---|---|---|

Do not optimize the patch merely to meet a numerical target. The following are
warning thresholds that trigger an **Adjust** or **Reject** discussion:

- More than four upstream-owned implementation/header files changed beyond
  build registration
- More than roughly 150 changed lines in upstream-owned files
- Changes scattered across more than six upstream functions
- Any copied selection, slicing, transform, camera, or undo implementation
- Any dependency on synthetic events sent to invisible controls
- Any fork-owned UI class that directly reaches through several `Plater::priv`
  internals

## 9. Exit decision

### Pass

Recommend proceeding with the shell boundary when:

- Events A–C pass against the real canvas and project state.
- The new UI lives primarily in fork-owned files.
- Upstream edits are a few clear attachment or reusable presentation seams.
- Default Orca behavior remains unchanged when the spike flag is absent.
- The shell does not copy or shadow Orca state and behavior.
- Keeping legacy widgets alive is an explicit, contained dependency.

### Adjust

Recommend changing the boundary when the design works but one concern needs a
dedicated seam, such as:

- `MainFrame` needs a shell-host interface rather than direct layout changes.
- `GLCanvas3D` needs separate chrome visibility and gizmo capability state.
- Prepare/Preview switching needs one stable view-host controller.
- The existing workspace notifications are insufficient for the new surface.

State the smallest adjusted boundary and which hard event would verify it.

### Reject

Reject this form of in-place shell when any of these remain true at the
timebox:

- Correct behavior requires scattered conditional changes through MainFrame,
  Plater, Sidebar, ObjectList, GLCanvas, and individual gizmos.
- Hiding stock viewport chrome necessarily disables rendering, picking, or
  manipulation behavior and cannot be split with one narrow seam.
- The new UI must drive invisible controls or mirror mutable project state.
- Preview or slicing can only work by exposing the legacy layout to the user.
- The fork-owned shell requires direct access to unstable `Plater::priv`
  implementation details in several places.

Rejection means redesigning the attachment boundary. It does not imply
replacing `GLCanvas3D` or rewriting Orca's slicer engine.

## 10. Required result document

Create `agent-docs/orca-full-window-ui-coupling-spike-results.md` containing:

1. Starting commit, worktree state, and build configuration
2. Pass, Adjust, or Reject verdict
3. Screenshot comparison and visible differences from the supplied design
4. Final runtime widget ownership and layout description
5. Ordered results for Events A–C
6. Legacy widgets kept alive and the observed reasons they remain necessary
7. Upstream APIs reused and any new seams introduced
8. Test, build, and manual runtime evidence
9. **Coupling and rebase ledger** in the exact format from Section 8
10. Coupling discovered in failed or abandoned implementation attempts
11. Highest-risk rebase hotspots
12. Recommended production boundary
13. Decisions that require human product or architecture judgment

Do not call the spike successful because the screen resembles the design. The
verdict must be based on the real hard events and the measured coupling/rebase
surface.
