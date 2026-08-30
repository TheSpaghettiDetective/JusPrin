# OrcaSlicer integration guide

**Status:** Canonical engineering guidance for the `jusprin-newui` production branch.

This document records lessons from building and testing the JusPrin project-state
adapter and full-window UI against the real OrcaSlicer application. It is written
for future agents who need to extend JusPrin without having access to the original
spike sessions.

The central architectural rule is that OrcaSlicer's C++ model, viewport, plate
list, and undo stack remain authoritative. JusPrin should expose those systems
through a narrow typed contract rather than copying project state or recreating
OrcaSlicer behavior.

## 1. Source and scope

These lessons began with two spikes on feature branch
`codex/orca-workspace-adapter-spike` and now include the full-window UI work on
branch `jusprin-v2-poc`:

- Workspace-adapter implementation commit
  `4e278e3276a91a7142615492beb58bdf71116a78`.
- Invisible legacy UI implementation commit
  `65fc3b56e12e5be66851125cbedd8974d7eeb8c6`.
- Full-window UI coupling spike commit
  `462e243b46`.
- Canvas-wrapper rebase-risk refactor commit
  `bf7a0c1e10`.

The detailed spike plans, results, evidence, and experimental code remain on `jusprin-v2-poc` at final POC commit `9bba835b92`. Use [POC reference](poc-reference.md) to locate them.

The tested adapter surface includes workspace snapshots, object selection,
rename, duplicate, remove, undo, redo, change notifications, and committed
viewport transforms. The invisible legacy UI spike repeated the complete probe
workflow while an opaque empty window covered the still-mapped OrcaSlicer
`MainFrame`.

## 2. OrcaSlicer-specific findings

### Workspace changes do not have one event source

OrcaSlicer reports selection, transforms, plate changes, content changes, and
history changes from different owners. There is no single event that means
"the workspace changed."

The adapter must combine related native events into one logical notification.
The notification should identify which areas may have changed, and the consumer
should then request a fresh workspace snapshot.

Do not place a copy of workspace state inside the notification. That creates a
second state-delivery path and increases the risk of stale data.

Coalesce events at the logical command or transaction boundary, not after an
arbitrary event-loop delay. Union every reason produced by the transaction,
advance the revision before delivery, and guarantee that a snapshot read inside
the callback has the same revision as the event. Native callbacks may run before
the command that caused them returns, so ordering must be part of the contract
rather than an assumption made by consumers.

### Undo and redo restore state without explaining the affected area

OrcaSlicer's undo stack correctly restores the project, but a limited workspace
projection cannot always show what changed. A history step may affect settings
or other authoritative project data that the current contract does not expose.

Judge history availability and command success from the authoritative history
position or an equally reliable owner-provided result. Do not infer success only
from UI button state or from differences in a partial workspace snapshot. Report
`History` together with conservative affected-area reasons so consumers refresh
from the source of truth.

Do not implement a second history stack in JusPrin. OrcaSlicer must continue to
own snapshots, restoration order, selection restoration, and object identity.

### Existing command paths are valuable and should be reused

Duplicate, remove, transform, snapshot, undo, and redo behavior already exists
inside OrcaSlicer. Reusing those paths preserves worker cancellation, plate
membership, viewport updates, selection behavior, and history timing that would
be easy to miss in a new implementation.

When a required operation is trapped inside presentation code, expose the
smallest behavior-oriented method from the current OrcaSlicer owner. Do not copy
the operation into the adapter and do not simulate clicks on hidden widgets.

### Workspace IDs need explicit project-session scope

OrcaSlicer's in-process IDs are useful raw identity, but the public contract must
not imply that they survive project replacement, reload, reset, serialization,
or application restart. Pair public IDs with an explicit project-session
identity and invalidate every prior-session ID when the authoritative project is
replaced.

Resolve IDs against current authoritative state for every command. Do not retain
model pointers or GUI indexes across mutations. Keep invalid zero IDs, unknown
current-session IDs, and stale prior-session IDs distinguishable when callers can
recover differently from each case.

### Presentation objects must not own model behavior

A pane or list may render model state and refresh after a command, but its
construction or visibility must not be required for the command to work. Put
snapshots, model mutation, selection, plate membership, background processing,
dirty state, and event publication on one behavior path owned near the
authoritative Orca subsystem.

Legacy presentation and JusPrin adapters should call that same path. Treat
presentation objects as optional observers, and avoid both copied handler logic
and simulated input against hidden controls.

### GUI event-loop delay is not a project-ready signal

The development probe can be constructed before command-line project loading
finishes. Even two delayed GUI callbacks may run before the requested project is
available, so the first probe snapshot can be empty. The invisible legacy UI
spike also observed a separate non-modal `Loading...` top-level during startup.
Its existence was another sign that window construction and event-loop turns do
not mean that project loading has completed.

Tests must wait for a snapshot containing the expected loaded project before
starting commands. Tests that make visibility claims must also wait until
temporary startup windows have disappeared. Production code should attach to an
explicit project-ready signal rather than treating `CallAfter` as a readiness
guarantee.

### History availability depends on which tab is on screen

Some existing availability predicates combine undo-stack state with whether the
relevant panel is currently visible. That is valid for enabling a stock menu or
button, but it is not a project capability.

Keep UI affordance predicates separate from model/history capability. A typed
workspace contract must read availability from the authoritative history owner
without requiring a tab, sidebar, or canvas to be shown.

### Undo and redo can silently do nothing

Availability and execution may follow different internal paths, so an operation
can still leave the history position unchanged. Compare the authoritative
history position before and after execution, or consume an equally reliable
owner-provided result. Report unchanged execution as failure and emit no false
state-change event.

Do not use a partial workspace projection for this check. A real history move
may change authoritative project data that the projection does not yet expose.

### A loaded object is not automatically printable

Plate membership is positional: `PartPlateList` assigns instances to plates by
where their convex hull sits, and `load_files` on a bare mesh does not place
the object on any plate. An object can render, select, and transform correctly
while `PartPlate::has_printable_instances()` is false — and then `reslice()`
silently starts nothing ("background process in idle state" in the log).

`PartPlate::add_instance(obj, instance, /*move_position=*/true)` is the
existing centering path (`PartPlateList::add_to_plate` uses it). Test fixtures
must place every object explicitly; the workspace and shell harness fixtures
do. A second trap: the plate's outside-instance set recomputes asynchronously,
so `has_printable_instances()` read immediately after placement can still
report the stale value. Judge printability from the slice result, not from a
check in the same event-loop turn as the placement.

### Application teardown can re-enter destroyed owners

Quitting while model volumes are loaded can crash:
`~GLCanvas3D → reset_volumes → Selection::clear → Plater::canvas3D()` runs
after `Plater::priv` is destroyed and dereferences null. This is inherited
upstream behavior — crash reports with the identical signature predate the
JusPrin shell (2026-08-28) and the stock-mode harness reproduces it with all
fork runtime code inert. Until it is fixed in upstream teardown ordering,
automated GUI scenarios should end on an empty project (`new_project`) before
exiting; both harnesses do.

## 3. Resolved implementation problems

### Selection events arrived before the duplicate command returned

**Observed problem:** OrcaSlicer emitted selection callbacks while duplication
was still executing. The probe refreshed from those callbacks before it received
the newly created object's ID, so a later command could target the original
object.

**Resolution:** The probe treats the command's returned `ObjectId` as the
preferred result and performs a fresh authoritative snapshot after the command
returns.

**Lesson for future agents:** Do not assume that event callbacks occur after a
command returns. A synchronous native event may arrive from inside the command.
Use the command result to identify newly created resources, then confirm their
state through the authoritative snapshot.

### Adapter selection needed exactly one notification

**Observed risk:** Selecting an object through the adapter changes OrcaSlicer's
real viewport selection, which already causes OrcaSlicer to emit its normal
selection event. Publishing another adapter-owned selection event would produce
duplicates or a feedback loop.

**Resolution:** The adapter changes OrcaSlicer's authoritative selection and
allows the existing canvas selection event to drive the workspace notification.

**Lesson for future agents:** When a command already produces an authoritative
native event, observe that event. Do not create a parallel synthetic event for
the same state change.

### Rename behavior was embedded in the object-list presentation

**Observed problem:** `GUI_ObjectList` performed the rename snapshot, model-name
update, single-volume-name update, and mesh-name update itself. Copying that code
into the adapter would create two implementations with different behavior over
time.

**Resolution:** One behavior-oriented command at the authoritative owner now
owns those steps, and both presentation and adapter paths call it.

**Lesson for future agents:** Extract one shared command at the authoritative
owner. Presentation code and adapter code should call the same behavior rather
than maintaining separate copies. Any presentation refresh must be optional and
must not own the mutation.

### Logical transactions are safer than delayed coalescing

**Observed risk:** The POC delayed notifications until the next GUI event-loop
turn to merge related events. That made revision timing ambiguous and allowed the
adapter or consumer to disappear before queued work ran.

**Resolution:** Coalesce low-level reasons inside a logical transaction and
deliver the committed revision at the transaction boundary. Use move-only RAII
subscriptions and dispatch state that tolerates unsubscribe or owner destruction
inside a callback.

**Lesson for future agents:** Prefer an explicit commit boundary over a timer or
`CallAfter` used as a batching mechanism. If delayed work is genuinely required,
give every queued callback an explicit lifetime token; closing a window does not
make captured objects safe.

### A parented top-level shell did not have an independent lifetime

**Observed problem:** The first opaque shell was a top-level `wxFrame` parented
to the legacy OrcaSlicer frame. On macOS, destroying the probe also caused the
tracked legacy frame and shell to be destroyed. A parent relationship chosen for
window stacking had unintentionally coupled the windows' lifetimes.

**Resolution:** The shell became an unparented top-level window. It explicitly
observes legacy move, size, visibility, activation, and destruction events, while
the probe remains owned by the shell. The final run destroyed the probe while
the application, legacy frame, and opaque shell remained alive.

**Lesson for future agents:** Treat wx top-level parenting as lifecycle
ownership, not as a harmless z-order hint. Windows that must survive
independently should have independent ownership and explicit lifecycle
observation.

### Native choice dismissal moved focus to the opaque shell

**Observed problem:** Dismissing the native macOS `wxChoice` popup could leave
the opaque shell as the key window. The probe still existed, but it was no longer
the usable window above the shell.

**Resolution:** The choice handler schedules probe `Raise()` and control
`SetFocus()` calls with `CallAfter`, after the native popup has completed its own
focus transition.

**Lesson for future agents:** Focus after a native popup is asynchronous. Check
the settled state on the next event-loop turn, and restore the intended window
there instead of fighting the native control inside its event callback.

### One-time window raising did not guarantee continued occlusion

**Observed problem:** Raising the shell and probe once at startup was not enough.
Normal OrcaSlicer activation during project loading brought the legacy frame
forward, and OrcaSlicer also created a separate non-modal `Loading...` top-level
that was not part of `MainFrame`.

**Resolution:** The shell observes legacy activation and geometry events, checks
coverage on a timer, and restores the order of legacy frame, shell, and probe
after ordinary non-modal activation. The spike also inventories shown
`wxTopLevelWindows`. An unexpected modal is logged as a test failure rather than
silently hidden.

**Lesson for future agents:** Occlusion is a continuously maintained invariant,
not a one-time `Raise()` call. Covering `MainFrame` is insufficient because
OrcaSlicer may create other top-level progress, error, or modal windows.

### Automatic top-level placement did not ensure visible placement

**Observed problem:** The probe could be constructed, accessible, and reported
as active without appearing visibly above the shell on the tested multi-display
macOS arrangement.

**Resolution:** The shell positions the probe explicitly inside its own screen
rectangle and repositions it when shell coverage changes.

**Lesson for future agents:** Do not rely on operating-system placement for a
top-level tool that must remain visible relative to another top-level window.
Set and verify its screen position explicitly.

### A borderless shell had no normal recovery action

**Observed problem:** The borderless, no-taskbar shell had no close button, and
the normal Command-W shortcut did not close it. A failed development run could
therefore leave the legacy frame covered without an obvious recovery path.

**Resolution:** The shell handles Escape and Command-W explicitly. Closing the
shell stops event tracking, hides and destroys only the shell, and raises the
already-existing legacy frame.

**Lesson for future agents:** Every borderless development shell needs an
explicit, tested escape path. Recovery must reveal the existing application
state without depending on hidden controls or process termination.

### Unsupported native selection could be mistaken for no selection

**Observed problem:** The JusPrin contract currently represents object-level
selection, while OrcaSlicer also supports volumes, instances, parts, and mixed
selection.

**Resolution:** The contract reports `Unsupported` separately from `None`.

**Lesson for future agents:** When a new API supports only part of a native state
model, represent an untranslatable state explicitly. Returning an empty value
would give consumers incorrect information.

### Invalid, missing, and stale IDs needed different errors

**Observed problem:** A zero ID, an ID never present in the workspace, and an ID
for an object that was removed are different command failures.

**Resolution:** Separate `PlateId` and `ObjectId` types prevent accidental
interchange. Commands reject the zero value as invalid, and the adapter remembers
which object IDs have previously appeared without caching editable object state.
Commands return explicit invalid, missing, or stale errors.

**Lesson for future agents:** Define identifier lifetime and failure semantics at
the contract boundary. A generic Boolean failure hides information that future UI
and Agent consumers will need.

### Sibling widgets and static owners must plan for destruction order

**Observed problem:** The shell's status row binds handlers on the Notebook —
a sibling, not a parent — and the shell controller lives in a static slot
that outlives `MainFrame`. wx destroys sibling children in unspecified order,
so an `Unbind` in the row's destructor could run against an already-destroyed
Notebook, and the static controller's destructor could "restore" widgets that
no longer exist.

**Resolution:** The row also binds the Notebook's `wxEVT_DESTROY` and skips
`Unbind` once the Notebook is gone. The controller binds the frame's
`wxEVT_DESTROY`, marks itself uninstalled, and abandons (rather than
restores) its canvas-presentation state, so nothing touches widgets during or
after frame teardown.

**Lesson for future agents:** An event binding on any window you do not own
needs a destruction watcher before `Unbind` is safe, and an object that
outlives the widget tree must have an abandon path that forgets pointers
without touching them. Restoring state is for runtime detach; teardown only
ever abandons.

## 4. Testing and verification lessons

### Run adapter workflows against the complete native application

A fake workspace can verify contract behavior, revisions, merged reason flags,
and subscription lifetime. A fake cannot prove that OrcaSlicer's viewport,
sidebar, plates, workers, snapshots, and undo stack remain synchronized.

Future changes to adapter commands must include both focused contract tests and a
native workflow against a deterministic multi-plate project.

### Test the automation mechanism before relying on it

The local computer-control backend could activate accessible controls but could
not send coordinate clicks or drags to OrcaSlicer's wx/OpenGL viewport.

The required viewport selection and gizmo drag were completed manually while the
probe recorded the resulting events and snapshots.

Future agents should test pointer automation at the beginning of a GUI task. If
the tool cannot reach a required control, switch to a documented manual check
instead of spending the entire verification window retrying equivalent pointer
techniques.

### Accessibility state did not prove visual stacking

The computer-control backend could inspect the probe's accessibility tree and
capture the individual probe window even when a full-display screenshot showed
that the probe was not visibly above the shell. Accessibility confirms that a
window exists and can expose controls; it does not prove what pixels the user
can currently see.

Visibility and occlusion claims need both kinds of evidence: authoritative
window state and a display-level screenshot. For covering windows, also record
screen rectangles, focus changes, z-order recovery events, and unexpected
top-level windows.

### Manual instructions must name visible controls

An early instruction said "click Select" without explaining that `Select` was a
button in a separate window titled `JusPrin Workspace Adapter Probe`. Another
instruction referred to a menu path that did not exist.

Reproduction steps must name the window title, visible control label, selected
object name, action order, and expected result. Internal class names and unstated
window context are not usable instructions for a human tester.

### Isolate unrelated existing test failures

The existing `libslic3r` suite contained one deterministic placeholder-parser
crash. The failing test also crashed when run alone, and the workspace adapter
did not modify the affected subsystem.

The result was recorded as 115 of 116 tests passing. Independence from the spike
was labelled a strong inference rather than a proven before-and-after result.

Future agents should rerun unexpected failures independently, inspect whether
the changed files can affect them, and report the evidence accurately. Do not
change unrelated code only to make the final test count green.

### Verify state from callbacks, not from visual appearance alone

The probe logs the notification revision and then reads an authoritative
snapshot from inside the callback. This proves that consumers see stable
post-operation state at the advertised revision or later.

A highlighted model or changed list row is useful manual evidence, but it does
not replace checking the IDs, names, transforms, selection, and history flags in
the callback snapshot.

### An assertion that expects success can score a defect as a pass

The adapter self test asserted that `undo()` reports success. When `undo()` was
reporting success for an operation that changed nothing, that assertion passed
and recorded the defect as healthy. The bug was visible only as a contradiction
between two lines of the transcript: a command reporting success next to a state
check showing nothing had changed.

Assert the contract, not just the happy path. For every command, check that the
reported result matches reality in both directions — success implies the
workspace changed, failure implies it did not. Such a check is never vacuous and
catches a lying result that an expect-success assertion rewards.

### A check that cannot reach the code path is worse than no check

The first version of that contract check passed while the defect was present,
because `can_undo()` was false, `undo()` refused early, and the buggy line was
never executed. It looked like coverage and was not.

Before trusting a new check, confirm it fails when the defect is present. If it
passes both with and without the bug, it is measuring nothing.

## 5. Rebase risk

The rules for changing upstream-owned files — where to put a seam, when an
additive form is required, how to leave a resolvable conflict, and what evidence
a change must record — are in [fork stewardship](fork-stewardship.md). Apply that
document to every line this fork changes in an OrcaSlicer-owned file.

Two OrcaSlicer-specific facts belong here rather than there.

### Shared native state needs one owner

Not every canvas-related control belongs to one `GLCanvas3D`. Orca's collapse
toolbar belongs to `Plater` and is shared by the Prepare and Preview canvases,
so two per-canvas guards cannot save and restore it correctly. Shared state needs
a single owner at the shared lifetime boundary: read and save the prior state,
change it once, restore the same value once. A setter without a corresponding
getter cannot provide exact restoration.

This generalizes: a wrapper should own only state whose lifetime matches the
wrapper.

### Churn is uneven inside a file as well as between files

`Plater.cpp` is by far the busiest file this fork touches, but several of the
functions the fork hooks have not been changed upstream in the entire life of the
fork. Judge a seam by the history of the function it sits in, not only by the
file's overall rate. Name both endpoint commits whenever that history is quoted;
the numbers are not reproducible otherwise.

The spike-era measurements, the wrapper refactor that produced these rules, and
the candidate refactorings that were never verified are preserved in the history
of this file and in [POC reference](poc-reference.md).

## 6. Checklist for future project-state changes

Before adding or changing a project-state adapter capability:

- Identify the existing OrcaSlicer command owner, undo snapshot location, and
  native event source.
- Confirm whether the command calls `GUI_ObjectList`, `Sidebar`, `wxGetApp()`, or
  another presentation object anywhere in its complete call path.
- Reuse OrcaSlicer's model and history behavior instead of copying it.
- Keep OrcaSlicer and wx types out of the JusPrin-facing contract.
- Return explicit command errors rather than exceptions or generic false values.
- Treat IDs as session-scoped unless persistence is separately proven.
- Expect native callbacks to occur during a command, before the command returns.
- Use one authoritative event source for each change to avoid feedback loops.
- Merge noisy events, but protect every delayed callback against destruction.
- Give top-level windows with independent lifetimes independent ownership;
  observe their lifecycle explicitly instead of parenting them for z-order.
- Treat focus after native popups as asynchronous and verify the settled window
  on the next event-loop turn.
- Maintain covering-window geometry and z-order continuously, and inventory all
  shown top-level windows rather than assuming `MainFrame` is the only one.
- Position required auxiliary top-level windows explicitly and verify their
  placement on the tested display arrangement.
- Give every borderless development shell an explicit, tested recovery action.
- Report unsupported native state instead of silently translating it incorrectly.
- Wait for an explicit loaded-project condition before running a workflow.
- Run focused contract tests and the real native workflow.
- Use display-level screenshots for visibility claims; an accessibility tree or
  individual-window capture does not prove visual stacking.
- Use exact visible language in every manual test instruction.
- Separate verified behavior, inference, tooling limitations, and missing
  evidence in the final report.
- Prefer fork-owned RAII wrappers that restore exact prior native state.
- Give shared native state one wrapper owner at the shared lifetime boundary.
- Apply [fork stewardship](fork-stewardship.md) to every line changed in an
  OrcaSlicer-owned file, and record the rebase evidence it requires before
  calling the change finished.

## 7. Known follow-up questions

- Does the covering-window lifecycle behave consistently on Windows and Linux?
- Do movement, resizing, maximization, restoration, display changes, and multiple
  monitor arrangements preserve complete occlusion?
- How should a production shell present required legacy modal, error, and
  progress windows without exposing the rest of the legacy UI?
- What explicit OrcaSlicer event should replace event-loop delays as the
  production project-ready signal?
- Which additional workspace mutations need observation beyond the current
  select, rename, duplicate, remove, transform, undo, and redo surface?
- If the project later requires the legacy sidebar not to be constructed, which
  remaining `GUI_ObjectList` updates must be separated from model operations?

Do not answer these questions from source inspection alone. Each question needs
a focused implementation or native runtime workflow with captured evidence.
