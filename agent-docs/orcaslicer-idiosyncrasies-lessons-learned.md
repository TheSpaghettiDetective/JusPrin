# OrcaSlicer idiosyncrasies and integration lessons learned

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

Related documents:

- [Workspace adapter spike](orca-workspace-adapter-spike.md)
- [Workspace adapter spike results](orca-workspace-adapter-spike-results.md)
- [Invisible legacy UI prerequisite spike](orca-invisible-legacy-ui-spike.md)
- [Invisible legacy UI spike results](orca-invisible-legacy-ui-spike-results.md)
- [Full-window UI coupling spike](orca-full-window-ui-coupling-spike.md)
- [Full-window UI coupling spike results](orca-full-window-ui-coupling-spike-results.md)

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

### Undo and redo restore state without explaining the affected area

OrcaSlicer's undo stack correctly restores the project, but it does not tell the
adapter whether an operation changed object contents, selection, transforms, or
plates.

The workspace adapter solves this by taking read-only projections before and
after OrcaSlicer performs undo or redo. The adapter reports `History` together
with the areas whose authoritative state changed.

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

### OrcaSlicer object IDs are useful only within an application session

The tested project used OrcaSlicer's in-process `ObjectID` as the basis for a
strongly typed JusPrin `ObjectId`. Undoing and redoing a duplicated object
restored the same ID during the running session.

This evidence supports stable IDs within one application session. It does not
support persistence across application restarts, project reloads, imports, or
file serialization. Future APIs must not imply stronger persistence guarantees.

### Current object commands still require the legacy object list to exist

The current rename, duplicate, and delete paths update `GUI_ObjectList` after or
during the model operation. The workspace-adapter spike ran with OrcaSlicer's
normal `MainFrame`, `Sidebar`, and `GUI_ObjectList` constructed and visible.

The result proves that a JusPrin companion or overlay UI can use the adapter
while the legacy UI remains operational. It does not prove that the legacy UI
can be omitted from construction.

The follow-up invisible-UI spike demonstrated a narrower migration strategy:
keep the legacy UI constructed and event-capable, but completely cover it with
an opaque empty JusPrin window. Every tested probe command continued to work,
including undo and redo with stable session-scoped object identity. This proves
concealment for the tested command surface; it still does not prove that the
legacy UI can be omitted from construction.

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

`Plater::can_undo()` is `IsShown() && p->is_view3D_shown() &&
p->undo_redo_stack().has_undo_snapshot()`, and `can_redo()` matches it. Two
thirds of that predicate describe the GUI, not the undo stack. An application
started without the plater panel as the shown tab reports `can_undo == false`
with work sitting on the stack, and the value can flip between one event-loop
turn and the next as startup settles.

Any consumer projecting these into a contract is publishing a GUI state as if it
were a workspace fact. Programmatic drivers must bring the 3D editor tab up
first, and must issue the command in the same event-loop turn that observes
availability; a turn's delay is enough for the answer to change before the
command re-checks it.

### Undo and redo can silently do nothing

`Plater::undo()` and `Plater::redo()` return `void`. `Plater::priv::undo()`
walks back for a snapshot that modifies the project and returns without acting
if it reaches `snapshots.begin()`, which a session holding a single action after
a bare model import can do. `can_undo()` uses a different and weaker test than
that walk, so the two can disagree: the predicate says an undo is available and
the call then does nothing.

Callers cannot learn from the return value whether anything happened. They must
compare authoritative before and after projections and report the command result
from that comparison.

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

**Resolution:** A reusable `Plater::rename_object` entry point now owns those
steps, and the existing object list calls the same entry point.

**Lesson for future agents:** Extract one shared command at the authoritative
owner. Presentation code and adapter code should call the same behavior rather
than maintaining separate copies.

The current rename entry point still refreshes the constructed legacy object
list. That remaining runtime prerequisite is intentional under the amended spike
scope and must not be confused with complete UI independence.

### Delayed notifications created an observer-lifetime risk

**Observed risk:** The adapter delays notifications until the next GUI event-loop
turn so it can merge related events. The adapter or consumer may be destroyed
before that delayed callback executes.

**Resolution:** Consumer subscriptions use RAII, which means destruction
automatically unsubscribes them. The adapter unbinds native OrcaSlicer events and
checks its lifetime before delayed work accesses adapter state.

**Lesson for future agents:** Every delayed GUI callback needs an explicit
lifetime strategy. Closing a window does not make queued lambdas safe.

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

## 5. Rebase-risk lessons from the full-window UI spike

### Optimize ownership and control flow, not diff signs

A fork change is not safe merely because it adds lines instead of deleting or
replacing upstream lines. An added condition inside a changing upstream method
can still conflict textually. More dangerously, it can merge cleanly after
upstream changes the surrounding control flow and then do the wrong thing.

Evaluate rebase risk using all of these factors:

- how often upstream changes the file and the specific function;
- whether the fork changes upstream control flow or only calls a narrow seam;
- whether product policy appears in an upstream-owned type;
- whether a clean merge could hide a semantic incompatibility;
- how difficult the incompatibility would be to detect in tests.

Do not rewrite clear code into a less direct additive form merely to reduce the
number of deleted lines reported by Git. Diff statistics are evidence about the
change surface, not the architectural objective.

### State the comparison commits for every churn measurement

File-churn numbers are not reproducible unless the report names both ends of the
comparison. The audit for this branch compared pre-spike commit `254c67ffdd`
with upstream commit `142c63ab0e4a`.

That interval contained:

| Upstream-owned file | Upstream commits |
|---|---:|
| `Plater.cpp` | 93 |
| `src/slic3r/CMakeLists.txt` | 44 |
| `GLCanvas3D.cpp` | 43 |
| `MainFrame.cpp` | 25 |
| `Plater.hpp` | 17 |
| `GLCanvas3D.hpp` | 9 |
| `GUI_ObjectList.cpp` | 8 |
| `GLGizmosManager.cpp/.hpp` combined | 3 |
| `GLToolbar.cpp/.hpp` combined | 0 |

The raw line-churn totals also depend on the exact comparison commits. Record the
hashes instead of describing the interval only as "recent upstream" or "the last
two months."

### A fork-owned wrapper can reduce risk without reducing touched-file count

The first full-window implementation added the product-specific enum
`GLCanvasPresentationMode::JusPrin` to `GLCanvas3D`. It also made four
product-specific decisions inside `GLCanvas3D::on_mouse()`: three toolbar-input
conditions and a separate manual path for the active gizmo.

Commit `bf7a0c1e10` replaced that design with fork-owned
`GLCanvas3DWrapper` and product-neutral `GLCanvasPresentationOptions`. The
wrapper selects four independent capabilities: overlay rendering, overlay input,
plate-control rendering, and plate input. The normal Orca gizmo dispatch path is
used again.

The refactor produced these measurable results:

- product-specific references in the upstream-owned GL files: reduced to zero;
- product-specific decisions in `GLCanvas3D::on_mouse()`: reduced from four to
  one;
- product policy moved into a 76-line fork-owned wrapper;
- three input decisions moved from `GLCanvas3D.cpp`, changed in 43 upstream
  commits, into files changed in zero to three upstream commits;
- a three-way merge simulation automatically merged every changed canvas and
  gizmo-manager file against audited upstream.

The refactor deliberately increased the upstream-owned file count from four to
eight and grew the upstream-owned UI-spike diff from `+50/-5` to `+69/-5`.
Those raw numbers became larger because the new generic controls live in four
additional, much quieter files. The risk still fell because ordinary Orca
control flow was restored and product policy moved to a fork-owned owner.

### Shared native state needs one wrapper owner

Not every canvas-related control belongs to one `GLCanvas3D`. Orca's collapse
toolbar belongs to `Plater` and is shared by the Prepare and Preview canvases.
The full-window shell constructs two canvas wrappers, so both wrappers must not
independently save and restore that shared toolbar state.

If the remaining collapse-toolbar condition is removed from
`GLCanvas3D::on_mouse()`, use one shell-owned RAII guard for the shared toolbar.
The guard must read and save the prior input state, disable input once, and
restore the same value once. A setter without a corresponding getter is not
enough to provide exact restoration.

This is a general rule: a wrapper should own only state whose lifetime matches
the wrapper. Shared state needs a single owner at the shared lifetime boundary.

### Keep product startup policy out of high-churn constructors

The completed development spikes currently add 26 lines to `Plater.cpp` solely
for startup: four JusPrin includes, one adapter-lifetime member, and 21 lines of
environment checks and delayed callbacks. `Plater.cpp` changed in 93 upstream
commits during the audited interval.

If current-HEAD reproduction of those completed spikes remains necessary, a
fork-owned bootstrap object can reduce the upstream constructor footprint to an
include, one lifetime member, and one construction call. If historical commits
are sufficient for reproduction, retire the old runtime harnesses completely and
retain their results and reproduction commit hashes in the documents.

Do not leave one environment-variable branch in an upstream constructor for
every experimental screen. One fork-owned bootstrap boundary should own all
development-only startup policy.

### Put fork source registration in a fork-owned manifest

The central `src/slic3r/CMakeLists.txt` currently contains 14 JusPrin source
entries inside Orca's main source list. That file changed in 44 upstream commits.
A fork-owned `GUI/JusPrin/sources.cmake` can keep the explicit source list while
reducing the central Orca file to one `include()` line.

Prefer an explicit fork-owned manifest over recursive source globbing. The goal
is to move ownership, not to make build inputs implicit.

### A one-line hook may be better than eliminating a shared command

`GUI_ObjectList.cpp` now calls `Plater::rename_object()` so the legacy object
list and `OrcaWorkspaceAdapter` use the same snapshot, model update,
single-volume update, mesh persistence, and list-refresh behavior. That call
overlaps an upstream localization change and therefore conflicts in the current
merge simulation.

Reverting the call would remove the conflict but recreate two rename
implementations. That exchanges a small, visible rebase conflict for long-term
semantic divergence. Keep the one-line shared-command hook unless the shared
command itself can move to a more authoritative owner without duplicating
behavior.

When resolving the current conflict, carry upstream's localized `_u8L()` snapshot
label into the shared rename command. Do not describe a conflict that Git stops
for human resolution as a silent regression.

### Some remaining seams are candidates, not proven improvements

The following refactorings may reduce future rebase exposure, but they have not
been implemented or behaviorally verified:

- Move duplicate and delete orchestration from the new `Plater` methods into
  `OrcaWorkspaceAdapter` by using existing selection, deletion, snapshot, and
  object-list APIs. This would remove 19 lines from `Plater.cpp` and two
  declarations from `Plater.hpp`, but it must preserve cut-object warnings,
  worker cancellation, undo, selection, plate membership, and list updates.
- Reduce the 14-line `MainFrame.cpp` shell attachment to one fork-owned install
  call. The function must still respect platform-specific top-bar behavior,
  widget ownership, sizer ownership, and restoration.
- Replace the `JUSPRIN_FULL_WINDOW_UI_SPIKE` check inside
  `Plater::priv::enable_sidebar()` with a generic persistent sidebar-presentation
  policy. The shell should apply and restore that policy; `Plater` should not
  query a JusPrin environment variable whenever Orca asks to show its sidebar.
- Remove the last collapse-toolbar input condition from
  `GLCanvas3D::on_mouse()` only after the shared-toolbar ownership problem is
  solved as described above.

These are hypotheses until the pre-change behavior is reproduced and the same
native workflows pass afterward. A smaller diff is not proof that the refactor
preserved Orca behavior.

### Use a three-way merge simulation as evidence, not a guarantee

Against upstream `142c63ab0e4a`, the current JusPrin changes automatically
merged in `GLCanvas3D`, `GLToolbar`, `GLGizmosManager`, `MainFrame`, `Plater`,
and the production CMake file. The two relevant content conflicts were in
`GUI_ObjectList.cpp` and `tests/CMakeLists.txt`.

This simulation identifies today's textual conflicts. It does not prove that a
future rebase will merge, and an automatic merge does not prove that the result
is behaviorally correct. After every upstream rebase, run focused contract tests,
the full native build, the real multi-plate Prepare/Preview workflow, native
Move/Rotate input, and the flag-absent Orca UI regression check.

### Preserve upstream formatting when it already converged

Before reverting a whitespace-only fork difference, compare it with current
upstream. For example, the pre-spike `Plater.hpp` lacked a final newline, while
both current upstream and the branch now include one. Reverting that newline
would recreate a difference rather than reduce one.

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
- Name the exact fork base and upstream commit used for churn and merge
  measurements.
- Measure function ownership and semantic coupling; do not rank risk from
  addition/deletion counts alone.
- Prefer fork-owned RAII wrappers that restore exact prior native state.
- Give shared native state one wrapper owner at the shared lifetime boundary.
- Keep product names and environment-variable policy out of upstream-owned
  presentation types and high-churn constructors.
- Use an explicit fork-owned CMake source manifest instead of interleaving every
  fork source into Orca's central list.
- Preserve one shared command path when the alternative is duplicated model,
  history, or presentation behavior.
- Run a three-way merge simulation, then test the merged behavior; neither step
  substitutes for the other.
- Compare formatting-only changes with current upstream before deciding whether
  to keep or revert them.

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
