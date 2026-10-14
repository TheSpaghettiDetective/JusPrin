# JusPrin production architecture

**Status:** This is the architecture chosen for the `jusprin-newui` branch. It also says where production code may and may not reach into OrcaSlicer.

JusPrin replaces what OrcaSlicer looks like and how people move through it. It keeps everything OrcaSlicer already does well to turn a model into a print. Product work should make it easier for people to say what they want and to check a print before sending it. It must not create JusPrin's own version of slicing, geometry, rendering, picking (working out which object the pointer is on), saving and loading, or undo history.

## Product composition

The project window we are building toward has four regions that stay in place:

1. A compact row that shows the printer and the project status.
2. A thin pane that lists plates and objects and can be collapsed.
3. OrcaSlicer's existing Prepare canvas (the 3D view).
4. A fixed pane that holds the Agent conversation and the print plan.

The compact plates/objects pane is deferred to a later task. The first
production shell and the first Agent implementation must not depend on it.
Until the pane is added, people work with objects through the native canvas
and the Agent.

Prepare is the screen people stay on. **Check print** opens a modal window. That window holds a second copy of OrcaSlicer's real Preview widget and shows the current valid slice of the active plate. OrcaSlicer's original Preview stays where it was, in `Plater`'s Notebook (OrcaSlicer's tabbed container). A small factory function on `Plater`, with nothing JusPrin-specific in it, gives that window the existing model, bed, slicing process, and G-code result. Closing the window returns focus to Prepare. The window also closes by itself when the project or the slice changes. **Print** currently opens OrcaSlicer's existing send confirmation dialog. It never sends straight away. Project details open as a drawer. Monitor is a separate screen that can stay visible while the person does other work.

## Runtime architecture

```text
OrcaSlicer process (C++17 / wxWidgets / OpenGL)
├── JusPrin application shell
│   ├── compact top navigation and machine status
│   ├── native plates/objects pane
│   ├── center workspace host
│   │   └── existing GLCanvas3D Prepare canvas
│   └── wxWebView
│       └── local React/TypeScript Agent interface
├── modal Check print window with Orca G-code Preview
├── typed workspace/controller boundary
├── existing Orca model, selection, plates, gizmos, and undo stack
└── existing slicing, project I/O, device, and print subsystems
```

The split between native code and web code is deliberately cautious:

- The 3D view, and anything drawn on top of it, stays native.
- The top row and the object/plate pane start as native wxWidgets.
- C++ owns the application state and the project state.
- The WebView receives versioned events and sends versioned commands.
- We may later try building the object pane in a WebView, but only as a change to how that one pane is drawn. It would not be a reason to move to Electron or to a WebGL 3D view.

## Authoritative ownership

For each kind of state, one part of the system holds the real copy. This table says which part, and what JusPrin does with that state.

| Concern | Authoritative owner | JusPrin responsibility |
|---|---|---|
| Project model and object identity | Orca `Model` and `ObjectID` | Show a typed view that is valid only for the current project session |
| Plates and active plate | Orca `PartPlateList` | Show them, and ask for changes through commands |
| Selection and picking | Orca `Selection` and `GLCanvas3D` | Keep the UI in sync without creating feedback loops |
| Geometry transforms | Orca gizmos and `GizmoObjectManipulation` | Start the existing behavior and show exact values |
| Undo and redo | Orca undo stack | Report whether undo and redo are available and what they did. Keep no second undo stack |
| Slicing and Preview | Orca slicing actions and Preview canvas | Show the states of Slice and Check print |
| Project serialization | Orca 3MF and model serializers | Keep files compatible. Add migration only for new data |
| Agent conversation | React/TypeScript WebView | Render messages, write the words people and the model read, and submit typed commands (see [Where logic goes](#where-logic-goes-the-page-first)) |
| Agent print plan | C++/agent application boundary | Explain intent, decisions, uncertainty, and consequences |

## Production integration boundaries

### Shell host

The shell is the JusPrin window frame around OrcaSlicer's content. Build one shell, in files this fork owns, and attach it to `MainFrame` in one small place. Decisions about what happens at startup go in a bootstrap or a shell factory that the fork owns. Do not spread environment checks through OrcaSlicer's constructors.

The first production version may still construct OrcaSlicer's old top bar, tab panel, Sidebar, and `GUI_ObjectList` where existing OrcaSlicer behavior depends on them. Leave them out of the visible layout. Do not hide them behind a separate top-level window. Stopping them from being constructed at all is a separate project, because it changes object lifetimes.

### Build registration

List JusPrin's source files explicitly in `src/slic3r/GUI/JusPrin/sources.cmake`. OrcaSlicer's central `src/slic3r/CMakeLists.txt` should pull that file in with one include line that never changes. The fork should not keep adding its own files to OrcaSlicer's list.

### Workspace boundary

The workspace API is the typed C++ interface JusPrin uses to read and change the project. The production version should keep the ideas from the proof of concept (POC) that worked:

- separate types for plate IDs and object IDs, so one cannot be passed where the other is expected;
- read-only snapshots of the project;
- explicit errors from commands;
- typed reasons that say what kind of change happened;
- a revision number that only goes up;
- subscriptions that end automatically when the object holding them is destroyed, and that can be moved but not copied.

Before production use, the API must also:

- say which calls must run on the GUI thread;
- say exactly when the revision number goes up;
- explicitly mark the old session's IDs invalid when a project is replaced;
- notice changes that did not go through the adapter (the JusPrin class that implements this API on top of OrcaSlicer);
- report undo and redo availability whether or not any particular widget is visible;
- report whether an undo or redo really changed OrcaSlicer's state;
- not depend on `GUI_ObjectList` for commands that change the model;
- offer plate selection, object import and addition, and the other commands the product needs;
- keep wxWidgets types and OrcaSlicer types out of its public interface.

Sometimes the operation you need exists only inside OrcaSlicer's UI code. In that case, add the smallest possible command to the OrcaSlicer class that already does the work, and name it for what it does. Do not copy the operation into JusPrin. Do not fake clicks or key presses on hidden controls.

### Canvas presentation

`GLCanvas3D` keeps a small API for controlling how its parts are presented. That API contains nothing specific to JusPrin. A controller in fork-owned code uses the API to apply JusPrin's choices, and puts OrcaSlicer's previous state back when the controller is destroyed. Whether a control is drawn and whether it receives input must be two separate switches. Otherwise a control that is hidden could still swallow pointer events. OrcaSlicer's native gizmos (the on-canvas tools such as Move and Rotate) must stay usable while one is active.

The POC had one broad switch, `render_overlays`. Production needs finer control. Treat each of these as its own item that can be shown or hidden separately: OrcaSlicer's main toolbar, the gizmo picker, the handles of the active gizmo, plate actions, object labels, layer-editing controls, navigation aids, the Preview legend, and the Preview sliders. Some controls are shared between canvases, such as `Plater`'s collapse toolbar. A shared control must be saved and restored by one object that lives as long as the shell does. Do not give each canvas its own guard for it.

### Plates and Project pane

This pane is native code owned by the fork (`Shell/LeftPane`). It sits to the left of the workspace on Prepare. Its first tab lists every plate, even when the project has only one. Its second tab shows the saved project's identity, the model's 3MF metadata, the history of physical prints, the version history, and export. The pane keeps no project state of its own and has no undo of its own. It draws what two model classes describe, and those classes do not depend on the GUI. Every click calls a workspace command.

| Piece | What it is responsible for |
|---|---|
| `Workspace/ProjectOutline.hpp`, `IWorkspace::outline()` | The data the pane reads: plates, copies of objects (each with the plate that holds it), volumes, facts about customization and the mesh, and what is selected below the object level. Which plate a copy is on comes from `PartPlateList`. The code never guesses it from where the copy sits. |
| `Shell/LeftPaneModel` | Which rows the Plates tab shows, and when. The active plate is open. Other plates are folded, each with a summary computed from its current copies. A row has child rows only when there is data for them. "Not on a plate" appears only when some copy is on no plate. This class also handles switching between the tabs and between the views inside the Project tab. |
| `Shell/ProjectPaneModel` | The contents of the Details and Print history views, as plain data. A field that nobody filled in is left out. The slicer's time estimate and the measured print duration are kept as separate values. A ledger record that has nothing to display is included in the count but gets no row. |
| `Workspace/OrcaOutline.cpp` | The commands, implemented on top of OrcaSlicer: `select_plate`, `add_plate`, `select_copy`, `select_volume`, `plate_actions`, `run_plate_action`. Each command ends by calling the same code OrcaSlicer's own UI calls (`Plater::select_plate_by_hover_id`, the toolbar's Add plate event, `Selection` plus the canvas's object-select event). The events are raised synchronously on the GUI thread, so the change has already happened when the command returns. |
| `Shell/LeftPane`, `LeftPaneProject.cpp` | Drawing and input. While drawing, the pane records where each control ended up. Pointer handling looks each click up in that record, so a click can only hit something that was actually drawn. |

The ledger is the record of physical prints. It can have any number of subscribers, through `ProjectPersistence::subscribe_ledger`. The pane and the header's overflow menu each subscribe to it separately.

### Print issues and OrcaSlicer's notifications

Inside the shell OrcaSlicer's own notifications are held but not drawn. JusPrin reads the list back and draws what it holds: print problems as a status pill in the canvas's lower right corner, the list it opens, and a bubble beside the object the selected issue is about, with **Ask AI** and **Resolve with AI**; the outcome of an action (export finished, an arrange that was refused) as a short confirmation above the pill; and work that is still running (a slice, an arrange, an export) as an entry in the chat.

**OrcaSlicer's notification list is the source of truth.** A notification carries a kind, a level and words. It does not carry the object it is about, so where OrcaSlicer lets its check be asked again, that check is asked once more for the object only.

| Piece | Owns |
|---|---|
| `PrintIssues/NoticeKinds` | Where each of OrcaSlicer's notification kinds goes: the issue list, a confirmation, running work, application state, or nowhere. Every kind is named; a kind OrcaSlicer adds stops the build in this file until someone routes it. |
| `PrintIssues/PrintIssueMonitor` | Reads the list on the notification manager's own turn, compares it with what it saw last, and rebuilds only on a change. Asks OrcaSlicer's checks for the object behind a validation, a copy over the plate's edge, an object out of a nozzle's reach and a slicing step warning. Holds no rule about what a problem is. |
| `issues_from` in `PrintIssues/PrintIssue` | The rule, as plain data with its own tests: an object lookup's entry exists only while OrcaSlicer holds a notification of its kind; a print-problem notification no lookup answers for is a plate-wide issue in OrcaSlicer's words. |
| `PrintIssues/IssueOverlayLayout`, `IssueOverlay` | Where the pieces go, how they are drawn and how they close, with ImGui inside the canvas's frame through the same overlay hook as the tool strip. The bubble never covers its object, the pill, a confirmation or the tool strip. |
| `PrintIssues/WorkFeed` | Work as it starts, moves on and ends. A slice is followed through OrcaSlicer's own events, which carry a percentage and an outcome. Other work is read from the "still running" notifications, which carry neither. |
| `AgentHost::begin_work`, `update_work`, `finish_work`; `WorkCard` in the page | The entry in the chat: saved with the project at the place in the timeline where the work started, so a later message reads below it; a progress bar and, where the work can be stopped, Cancel while it runs; completed in place. The chat panel opens when work starts. Repeats are folded by the page, never in what is saved, and an entry still "running" in a project that was closed mid-run reads as interrupted. |
| `PrintIssues/IssueContext`, `AgentHost::submit_issue_message` | The chat message a button sends, and the one way a message enters the chat from outside the page. |

Two things are read whether or not a notification exists, because OrcaSlicer's notifications do not cover them in time:

- **The slice result.** A path outside the plate, a filament its nozzle cannot reach, and a filament that cannot print on the plate surface get their notifications only when OrcaSlicer's preview loads the slice, which here means opening Check print. OrcaSlicer refuses to print from the moment the slice ends.
- **Why a slice failed.** A critical failure is shown in a dialog and leaves no notification, and no notification says which object the failure named. `EVT_PROCESS_COMPLETED` is the only place both exist.

Rules that follow:

- **No pill is the all-clear, and it is never given before a check.** The pill is drawn only while OrcaSlicer reports an issue, ignored ones included. While OrcaSlicer holds an unapplied edit the last issues are kept, the pill says it is checking and both buttons are off, so the pill does not go until OrcaSlicer has looked. An issue that was not in the list before is announced once by a ring that spreads from the pill twice; an edit that keeps an issue, and ignoring one or undoing that, announce nothing.
- **Nothing is resolved by the fork.** An issue goes when OrcaSlicer lets go of its notification. An agent saying it fixed something changes nothing.
- **"Blocks printing" is OrcaSlicer's answer.** An error-level notification blocks. For the slice result it is OrcaSlicer's own post-slice check, which allows a path 2 mm past the plate edge; the monitor does not apply a stricter test of its own.
- **The object is never read out of the words.** Two messages of one kind are told apart by comparing OrcaSlicer's words now with its words before, as OrcaSlicer does itself. Which object a message is about always comes from a check's own ids.
- **A press acts on what is true at the press**, and with no assistant set up it sends nothing and offers the chat panel's setup. **The two buttons differ only in the sentence**: Ask AI and Resolve with AI send the same context and the turn gets the same tools.
- **Operation failures are not print issues.** A failed download or a refused arrange is a confirmation, never an entry in the issue list.
- **Closing hides, it never resolves.** The pill is always there and is the way back; the list and the bubble are opened and closed, one at a time, and closing either changes nothing about the issue or the count. The list closes on a pick, a second press of the pill, Escape, or a press outside it. The bubble belongs to the selection: opening it selects its object, and it closes when that object stops being selected, which is what a click on the empty plate, Escape and a click on another object already do in OrcaSlicer. Turning the view or dragging the object leaves it open. Its close button leaves the selection alone, and a press on **Ask AI** or **Resolve with AI** that reaches the chat closes it. With one issue the pill opens the bubble directly, and with several the bubble's pager walks them all. The overlay hears Escape and presses through handlers of its own on the canvas's window, bound after OrcaSlicer's and so called first; no OrcaSlicer file is involved.
- **Ignoring a warning is the person's mark, not OrcaSlicer's.** **Ignore this warning** in the bubble leaves OrcaSlicer's notification alone: the warning stays in the list, marked, below the ones that count, and the pill stops counting it ("1 warning ignored" when nothing else is left, never "No print issues"). Only a warning can be ignored; a blocker cannot, because OrcaSlicer would still refuse to print. An ignore lasts while OrcaSlicer keeps reporting that warning on the active plate's own list, and is forgotten when OrcaSlicer stops, so a warning that comes back is shown again. It is not saved with the project. The rule is `mark_ignored` in `PrintIssues/PrintIssue`.

Known limits: application and account notifications (update available, plug-in missing, preset sync conflict) are routed to `App` and drawn nowhere yet. A confirmation shows OrcaSlicer's words and a close button; the actions OrcaSlicer attached to its own notification (open folder, eject, cancel an upload) are not offered. Only a slice has a percentage and a known ending; other work shows a bar with no amount and is recorded as done unless its Cancel was pressed. Uploads and downloads have not been run. The tips and the object size card are not shown.

### Agent WebView

The production Agent interface is a separate React/TypeScript package that runs locally inside a `wxWebView`. It needs:

- a versioned schema for commands and events;
- scripted mock conversations that play back the same way every time, for UI tests;
- messages that render as they stream in, with a scroll position that does not jump;
- written rules for keyboard, focus, shortcuts, clipboard, text selection, and IME (the input method used to type languages such as Chinese and Japanese);
- a defined state for each of these: reloading, starting up, an error in the bridge between the page and the app, and the agent being unavailable;
- no project state of its own. OrcaSlicer's project state stays in C++.

#### Where logic goes: the page first

When the same logic could live either in C++ or in a React page, put it in the page unless there is a strong reason for C++. The page is quicker to change: Vite hot-reloads it, while a C++ change means relinking a multi-gigabyte GUI library. The page's tests run in milliseconds under Vitest. The text people read is already written in the page. The text the model reads belongs there too.

Strong reasons for C++:

- **It reads or changes OrcaSlicer state:** presets, the model, plates, slicing, the device list. The page never owns project state (see the constraints below).
- **The app has to guarantee it.** This covers what a tool accepts and refuses, which tools a session may call, and what is sent to the model provider (API keys, access codes, project data). Page code can describe a limit, but only the app can enforce one.
- **It has to work without a page:** the MCP server, background work, tests that run without a browser.
- **It needs a thread the page does not have, or has to stay alive at times when the page is not.**

The printer panel shows how this works. The app sends the page only facts: sizes, spools, which printer a card shows, and what a change altered. The page turns those facts into all of the text:

- The page writes the model's instructions in `AgentUI/src/printerInstructions.ts` and sends them to the app with `printer_instructions`.
- The page writes the panel's own text in `AgentUI/src/printerWords.ts`. That is the opening line and the note that goes with each tap. The opening line is sent with `printer_opening`. The note for a tap is sent inside that tap's payload.

The app's part:

- It decides which tools the model gets: the five printer tools and the four settings tools, the same list in every mode.
- It checks every tool call.
- It refuses a call that does not fit the conversation. Examples are adding a printer outside an Add conversation, changing a printer other than the one the conversation is about, and changing a setting on anything other than that printer.
- It refuses a turn that arrives with no instructions. The app has no prompt of its own to fall back on.
- It posts the opening line once per session.
- It posts the note for a tap only when the tap goes through, and limits the note to 2 KB.
- It still writes the notes for things only the app can find out when the tap arrives: the network printer has gone, the printer's model id is not on the list, or a save or an undo failed.

The project assistant works the same way. Its whole system prompt is written in `AgentUI/src/projectInstructions.ts`, and `projectInstructions.test.ts` pins it to a fixture so that a change to it is always deliberate. The page sends it with `project_instructions` on every connection. The app keeps it in the project session's profile and sends it, unchanged, as the instructions of every project request; the provider's prompt cache depends on the text staying byte-identical. The app has no default text. A turn the app starts itself before the page has sent the prompt (the first message after a file opens, or Ask AI on a print issue, while the page is still loading) waits in the reply queue and starts when the prompt arrives. The prompt lived in C++ until 2026-10-14 so that the MCP server's instructions could share part of it; they stopped sharing it on 2026-10-10. The MCP server's own instructions (`Mcp/McpProtocol.cpp`) and the request that names a conversation stay in C++: MCP has no page, and the title request is the app's own.

Windows uses WebView2, macOS uses WKWebView, and Linux uses WebKitGTK. The way the page's files are packaged must work on all three. The prototype packed the whole page into a single local file, because WKWebView failed to load additional files referenced through `file:` URLs. Production keeps the single-file approach, and CMake now builds that file from the TypeScript/React sources.

MCP (Model Context Protocol, which lets external AI tools call the app) and wider agent autonomy are layers that come after this one. They must go through the same typed workspace interface. They must not change who owns the 3D view.

### Gizmo presentation

Start with Move or Rotate. Build one reusable native controller that wraps the existing behavior:

1. Show a compact strip of tool buttons in the upper-left corner of the Prepare canvas. The strip is a horizontal row like OrcaSlicer's own toolbar, but aligned to the left instead of centred. The strip stays put when the selection or the camera moves, because a strip attached to the object would jump every time the view orbits. `Canvas/ViewportToolStrip` draws the strip with ImGui as part of the canvas's own frame, through `GLCanvas3D::set_overlay_renderer`. On every frame it asks the gizmo manager which tool is open. The open tool's values appear below the row in `Canvas/ToolValuePanel`, lined up with the button that opened the tool.
2. Activate the existing native gizmo.
3. Keep the existing pointer dragging, snapping, and selection behavior.
4. Send exact typed-in numbers through `GizmoObjectManipulation`.
5. Keep the existing timing of undo snapshots, and the existing undo and redo.
6. Closing a tool only closes it. Do not invent a draft that can be thrown away. Undo stays the way to revert a change, unless the product requirements explicitly change that.

After that, convert the remaining tools in groups that work in a similar way: Move/Rotate/Scale; Mirror/Duplicate/Place on face; Measure; Cut/Repair; Text/Emboss; painter-based tools.

### Semantic annotations

Semantic annotations are labels a person puts on part of a model to say what that part means, such as a face that must stay smooth. Reuse what OrcaSlicer's painting tools already do: raycasting, brush selection, clipping, showing painted facets, copying, undo, and saving into the 3MF file. Two things are not yet proven for production: labels with arbitrary meanings, and how those labels are presented to the agent. The first implementation should use two labels on one object. Before adding more labels, check that those two survive save and reload, transforms, duplication, undo and redo, and changes to the mesh's topology.

## Non-negotiable constraints

- Do not change slicing algorithms to make the UI easier to build.
- Do not replace `GLCanvas3D` or the G-code Preview.
- Do not reimplement geometry operations in TypeScript.
- Do not place WebView content over the native GL canvas.
- Do not keep a second copy of the project model or of the undo stack in JavaScript.
- Preserve 3MF projects, printer profiles, shortcuts, serialization, and cross-platform behavior.
- Do not try to match every stock OrcaSlicer feature when the product definition does not require it.
- Do not reopen the Electron or WebGL question without a new, explicit product decision backed by evidence.

## Implementation order

1. Write the canonical product documents, and set up the design tokens and the curated assets.
2. Bring the workspace interface up to production quality: the interface itself, commands that go through OrcaSlicer's own code, change observation, and tests against the real adapter.
3. Bring the canvas presentation API and its controller up to production quality.
4. Build the production shell without a compact object/plate pane.
5. Build the typed Agent WebView and the bridge between the page and the app.
6. Add the presentation controller for Move/Rotate, then convert the other groups of gizmos.
7. Add semantic annotations, Check print reporting, Send preflight, Monitor, calibration, and troubleshooting screens as the product definition describes them.
8. Add the compact object/plate pane as a separately planned task when it is prioritized.

Older implementations and the detailed evidence are deliberately left out of this document. See [POC reference](poc-reference.md).
