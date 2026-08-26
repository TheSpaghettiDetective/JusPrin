# OrcaSlicer idiosyncrasies and integration lessons learned

This document records lessons from building and testing the JusPrin workspace
adapter against the real OrcaSlicer application. It is written for future agents
who need to extend JusPrin without having access to the original spike session.

The central architectural rule is that OrcaSlicer's C++ model, viewport, plate
list, and undo stack remain authoritative. JusPrin should expose those systems
through a narrow typed contract rather than copying project state or recreating
OrcaSlicer behavior.

## 1. Source and scope

These lessons come from two spikes on feature branch
`codex/orca-workspace-adapter-spike`:

- Workspace-adapter implementation commit
  `4e278e3276a91a7142615492beb58bdf71116a78`.
- Invisible legacy UI implementation commit
  `65fc3b56e12e5be66851125cbedd8974d7eeb8c6`.

Related documents:

- [Workspace adapter spike](orca-workspace-adapter-spike.md)
- [Workspace adapter spike results](orca-workspace-adapter-spike-results.md)
- [Invisible legacy UI prerequisite spike](orca-invisible-legacy-ui-spike.md)
- [Invisible legacy UI spike results](orca-invisible-legacy-ui-spike-results.md)

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

## 5. Checklist for future workspace changes

Before adding or changing a workspace capability:

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

## 6. Known follow-up questions

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
