# Typed Agent WebView and native bridge: implementation handoff

**Status:** Approved production-shell and Agent implementation plan for
`jusprin-newui`.

This task first builds the missing production shell and then builds JusPrin's
Agent conversation pane as a local React/TypeScript application inside
`wxWebView`, connected to OrcaSlicer through a versioned native bridge. The
first implementation phases use a deterministic mock Agent. Phase 5.1 adds one
live Agent through the same boundary while retaining the mock for reproducible
tests, offline development, and complete failure-state coverage. A future MCP
server must be able to use the same tool execution path without redesigning
project ownership.

This document is written for an implementation agent that has no access to the
product-planning conversation. The product decisions below are settled unless
the current code proves that a decision is technically impossible.

## Read before implementation

Read these documents in this order:

1. [Product definition](product-definition.md) for the product workflow and
   human/Agent boundary.
2. [Production architecture](architecture.md) for ownership and component
   boundaries.
3. [Engineering and verification method](engineering-method.md) for evidence
   and release requirements.
4. [OrcaSlicer integration guide](orca-integration-guide.md) for lifecycle,
   history, and event traps.
5. [Fork stewardship](fork-stewardship.md) before editing any file outside
   `src/slic3r/GUI/JusPrin/`.
6. [Design system](design-system.md) and
   `resources/jusprin/ui/design-tokens.json` for visual implementation.
7. [POC reference](poc-reference.md) for historical WebView evidence. The POC
   React package is not in the final POC tree, so implement the production
   package fresh rather than attempting to copy it.

The workspace contract and adapter and the canvas-presentation seam already
exist on `jusprin-newui`. The production shell does not exist and is Phase 1 of
this handoff.

The compact plate/object pane is explicitly outside this task and will be added
later. Do not build a placeholder pane, copy `GUI_ObjectList`, or make the shell
or Agent depend on a future pane. Users interact with objects through the native
canvas and Agent in these phases.

If this task needs a workspace capability that is missing, follow the extension
rules in this document rather than bypassing those layers.

## Required architecture

The ownership path is:

```text
React Agent WebView
  <-> versioned JSON bridge
  <-> native AgentHost / ToolExecutionCoordinator
  <-> IWorkspace
  <-> OrcaWorkspaceAdapter
  <-> authoritative OrcaSlicer model, commands, history, and events
```

The Agent implementation is a replaceable input to the native coordinator:

```text
DeterministicMockAgent (Phases 2-6 test path)
LiveAgentAdapter (Phase 5.1)
Future MCP server adapter
                |
                v
       ToolExecutionCoordinator
                |
                v
             IWorkspace
```

The WebView renders native state and submits typed requests. It must not own an
editable copy of the Orca project, implement Orca operations in TypeScript, or
create a separate JavaScript undo stack.

### Bridge envelope

Define the schema in one shared, versioned source and generate or validate both
sides where practical. Every command and event must carry enough information to
reject duplicates and stale work. At minimum, the envelope needs:

- protocol version;
- message or event ID;
- correlation ID for request, approval, progress, and result;
- workspace session ID;
- workspace revision where applicable;
- discriminated message type;
- typed payload;
- structured success or error result.

Handshake before sending workspace data. Negotiate protocol and capabilities,
and fail visibly when the two sides cannot communicate safely. Reload must
reconstruct the page from native state and last acknowledged IDs rather than
trusting retained JavaScript state.

### Tool lifecycle and future MCP compatibility

The deterministic Agent must produce the same semantic tool activity expected
from a future MCP-backed Agent. A tool activity record needs:

- tool and server identity;
- typed arguments;
- stable action and correlation IDs;
- whether approval is required;
- workspace session and expected revision;
- lifecycle state: `pending`, `approved`, `running`, `succeeded`, `failed`,
  `cancelled`;
- progress and structured result where available.

Do not implement a live provider, authentication, an MCP client, or an MCP
server before Phase 5.1. Do not special-case deterministic mock or live-Agent
behavior inside the WebView. The mock, live Agent, and a future MCP adapter must
feed the same coordinator.

### Extending workspace commands or events

For each tool:

1. Inspect the existing `IWorkspace` contract, `OrcaWorkspaceAdapter`, and
   Orca command/event owners.
2. Reuse an authoritative command or event when one exists.
3. If no safe operation exists, add the smallest behavior-oriented,
   product-neutral native seam following the existing workspace design.
4. Add or update fake contract tests and real-adapter tests.
5. Record any upstream-owned edit and its rebase evidence as required by
   [fork stewardship](fork-stewardship.md).

Never add a WebView-specific Orca command, simulate input against a hidden
control, or copy an Orca operation into JusPrin.

## Settled product behavior

### Approval policy

The first production release asks for approval before every durable project
mutation. Group related changes into one understandable approval when they form
one logical action.

The following may run without approval because they do not change durable
project or external state:

- inspect, analyze, calculate, slice, check, or explain;
- highlight or temporarily visualize native state;
- read project context;
- prepare a proposed plan or change set.

The following require approval before execution:

- geometry, transform, plate, object, modifier, annotation, setting, or project
  changes;
- imports that alter the project;
- any other durable project mutation.

The following always require action-time approval and must not use remembered
approval:

- revert, delete, overwrite, discard, or other destructive action;
- send, start, resume, or cancel a physical print;
- upload, share, publish, or export to an external destination.

Do not offer an approval path for bypassing safety controls or gaining authority
outside the open project.

### Composer and input

- Enter sends.
- Shift+Enter inserts a newline.
- Enter during IME composition accepts the composition and does not send.
- Normal text selection, copy, paste, keyboard navigation, and focus behavior
  must work inside the WebView.
- Global Orca shortcuts must not fire while the user is editing text, but they
  should resume when focus leaves an editable WebView control.

### Agent and bridge availability

The Agent service and the native bridge are different failures.

- When the Agent is unconfigured or unavailable, show a clean setup/test empty
  state in the conversation area. Preserve saved conversation and project
  history.
- The native bridge is the internal JavaScript-to-C++ connection. It is not a
  user-configurable service. On bridge failure, show an internal connection
  error with Retry and diagnostics.
- In either case, the native canvas, project, objects, slicing, printer setup,
  and other Orca controls remain usable.

### Attachments

Supported sources are:

- file picker;
- drag and drop;
- clipboard;
- references to items already in the open project.

Agent-readable context includes UTF-8 text, code, configuration, logs, G-code,
PDF, SVG, PNG, JPEG, GIF, and WebP. Orca-supported model and project files must
go through Orca's native importer. Present models to the Agent through native
summaries or renders rather than sending large opaque model binaries as if the
Agent had read them.

Reject unsupported or unreadable binaries visibly. Never imply that the Agent
understood content that was not decoded.

### Project-owned conversation persistence

Saved conversation and timeline state belongs to the Project 3MF. Store
JusPrin-managed content under:

```text
Auxiliaries/
  JusPrin/
    state.json
    revisions/
      <revision-id>.snapshot
    attachments/
      <attachment-id>/
        <original-file>
```

`state.json` is the portable, versioned semantic state for conversations,
timeline entries, revisions, build metadata, copy metadata, and print records.
Use stable opaque IDs, UTC timestamps, discriminated entry types, and preserve
unknown optional fields where feasible.

The Project 3MF is the last explicit saved snapshot. A local recovery store may
hold newer working state:

- current draft and staged attachments;
- outbox messages and actions;
- partial Agent response;
- bridge request and acknowledgement IDs;
- throttled live progress.

Persist an outgoing message before sending it. Stable message and action IDs
must prevent reconnect or reload from duplicating a message or repeating a
native mutation.

Project lifecycle rules:

- New Project creates a new project identity, conversation, and initial
  revision.
- Open Project loads saved JusPrin state or initializes history around an Orca
  project that has none.
- Save includes JusPrin state and blobs in the same Project 3MF.
- Save As continues the project identity and lineage at the new path.
- Import adds content to the current project and does not import the foreign
  file's JusPrin identity or conversation.
- Full project reset creates a new project boundary.
- A recognized sliced `.gcode.3mf` opens as a print artifact rather than an
  editable project.
- Clean-sharing export removes conversation, revision history, and private
  attachments.

A normal Project 3MF includes saved Agent conversation and sent attachments,
so the sharing UI must make that privacy consequence clear. Never store tokens,
credentials, account identifiers, telemetry secrets, or live printer
credentials in the Project 3MF.

### Revisions and Revert here

All conversations in a project share one linear manufacturing-revision
history. Creating a conversation does not create a branch.

Create a revision after each completed manufacturing change. Manufacturing
state includes geometry, transforms, plates, arrangement, modifiers, painted
surfaces, printer/process/material settings, custom G-code, and other inputs to
the manufactured result. Messages, camera movement, selection, drafts, and
project renaming do not create manufacturing revisions.

The first implementation may use compressed full native checkpoints, excluding
`Auxiliaries/JusPrin/` to prevent recursive snapshots. This is an implementation
proposal rather than a final storage requirement: benchmark save time, restore
time, and project-size growth before retaining it.

“Revert here” is atomic and destructive:

1. Restore the selected native revision.
2. Make it the current revision.
3. Remove all later editable entries across every conversation.
4. Remove later revision checkpoints.
5. Remove later builds, exported-copy records, drafts, and staged attachments
   unless retained only as immutable evidence for a physical print.
6. Preserve no redo branch or hidden later timeline.

Externally exported files cannot be recalled. Revert may remove their project
records, but the files remain at their external destinations.

### Builds, exports, and physical prints

A build is an immutable slice of one plate at one project revision. Re-slicing
creates a new build. Store its manufacturing-input hash, output hash, slicer and
configuration provenance, statistics, warnings, and source identifiers. Keep
generated G-code in the build cache or `.gcode`/`.gcode.3mf` artifact rather
than embedding it in the editable Project 3MF by default.

Derive build staleness by comparing its manufacturing-input hash with the
current input hash for the same plate. Do not persist a `stale` flag as the
source of truth.

Exported file and printer-storage copies belong to a build and retain the
expected output hash. External modification is determined by later checksum
verification, not by an editable status flag.

Physical print records form a separate, non-revertible factual ledger. A print
record must retain enough data to be useful without its original editable
timeline:

- printer, plate, and material;
- start/end timestamps and outcome;
- failure information where applicable;
- build and G-code hashes;
- slice statistics;
- original project and revision IDs.

When Revert here removes the source revision of a physical print, retain the
print record and show the exact user-facing status:

> Project timeline removed

Do not retain a recoverable later project branch merely to support that record.

## Implementation phases

Each phase is a shippable vertical slice. Finish its automated checks and
manual acceptance script before starting the next phase. Use repository-owned,
deterministic fixtures and stable mock scenarios so another person can reproduce
every state-machine and native-workspace result. Credentialed live-service
checks in Phase 5.1 supplement these deterministic gates; they do not replace
them or become required for ordinary offline test runs.

### Phase 1 — Usable production shell

Deliver:

- a fork-owned shell attached to `MainFrame` through one small integration
  point;
- the real Orca Prepare and G-code Preview canvases;
- compact project, printer, and material status;
- working Slice and Check print flow;
- an honest Agent-unavailable state in the future Agent region;
- restoration of standard Orca presentation if the shell cannot load.

Do not add a compact plate/object pane in this phase.

Manual acceptance:

1. Open a project and inspect it on the real canvas.
2. Orbit, pan, zoom, and select an object.
3. Slice the project, open Check print, and return to Prepare.
4. Resize, maximize, restore, switch light/dark appearance, and open a different
   project.
5. Exercise the shell-failure path and verify that standard Orca remains
   usable.

Automated gate:

- full native-application shell startup and fallback tests;
- deterministic Prepare, Slice, Check print, and return-to-Prepare test;
- canvas input, resize, project replacement, and original-Orca regression
  tests.

### Phase 2 — Read-only deterministic Agent

Deliver:

- local React/TypeScript Agent UI embedded in the production shell;
- versioned handshake, capability negotiation, and packaged local resources;
- deterministic messages, incremental streaming, stop, error, and retry;
- native project, plate, selection, printer, and slicing context;
- reconstruction from authoritative native state after reload;
- loading, Agent-unavailable, and internal bridge-error states;
- composer, IME, clipboard, focus, keyboard, accessibility, and stable-scroll
  behavior defined above.

Manual acceptance:

1. Ask the Agent about the open project and selected object.
2. Change the native selection and verify that the Agent sees the new context.
3. Exercise streaming, stop, retry, upward scrolling, Enter, Shift+Enter,
   copy/paste, and an IME.
4. Reload the WebView and verify that native context is reconstructed.
5. Exercise Agent and bridge failures while continuing to use the canvas and
   other Orca controls.

Automated gate:

- bridge version, capability, ID, reconnect, and incompatible-version tests;
- deterministic conversation and browser interaction tests;
- fake-workspace contract and real-adapter tests for selection, external native
  changes, reload, and project-session invalidation.

### Phase 3 — Approved Agent changes

Deliver:

- native `ToolExecutionCoordinator` and MCP-compatible tool records;
- approval, rejection, cancellation, progress, success, stale-revision, and
  failure states;
- one clearly visible native change, such as duplicating or moving the selected
  object;
- authoritative Orca execution, revision reporting, and undo/redo behavior;
- approval policy defined in this handoff for all available actions.

Manual acceptance:

1. Ask the mock Agent to change the selected object.
2. Reject the proposal and verify that nothing changes.
3. Request it again, approve it, and verify the change on the canvas.
4. Undo and redo through Orca.
5. Exercise deterministic cancellation, failure, and stale-project scenarios.

Automated gate:

- coordinator state-machine, approval-policy, and idempotency tests;
- fake and real-adapter mutation/error tests;
- shell end-to-end approval, rejection, undo/redo, and stale-state tests.

This phase proves that a future MCP adapter can replace the mock without
changing WebView or workspace ownership.

### Phase 4 — Saved conversation and Revert here

Deliver:

- project-owned conversations and attachments metadata under
  `Auxiliaries/JusPrin/`;
- local recovery for drafts, staged attachments, outbox, partial response, and
  bridge acknowledgements;
- multiple conversations sharing one linear manufacturing-revision history;
- Save, reopen, Save As, reset, import, and clean-sharing behavior;
- atomic destructive Revert here with no redo or hidden later branch;
- checkpoint implementation plus save, restore, and project-size benchmarks.

Manual acceptance:

1. Chat with the Agent and approve several changes in two conversations.
2. Save, close, and reopen the project; verify conversation and project state.
3. Leave an unsent draft, restart, and verify recovery without duplicated
   messages or actions.
4. Revert to an earlier message and verify that native project state and all
   conversations lose their later editable history.
5. Close and reopen and verify that the reverted state persists with no redo.
6. Exercise Save As, reset, import, and clean-sharing behavior.

Automated gate:

- schema round-trip, migration, unknown-field, and corrupt-state tests;
- Project 3MF lifecycle and crash/reconnect deduplication tests;
- revision grouping, multi-conversation truncation, atomic failure/rollback,
  and real-adapter restore tests;
- recorded checkpoint storage and performance results.

### Phase 5 — Attachments

Deliver:

- file picker, drag/drop, clipboard, and project-reference sources;
- supported context decoding and previews;
- Orca-native model/project import path;
- native model summaries or renders for Agent context;
- clear unsupported or unreadable-file errors;
- Project 3MF persistence for sent attachments and clean-sharing removal.

Manual acceptance:

1. Attach text, image, PDF, SVG, and G-code files.
2. Paste and drag in an image.
3. Add a model and verify that Orca's importer is used.
4. Add an unsupported binary and verify clear rejection.
5. Save and reopen and verify sent attachments; create a clean-sharing copy and
   verify that private attachments are absent.

Automated gate:

- type detection, size/error, attachment-ID, safe-path, and decoding tests;
- native import integration tests;
- Project 3MF round-trip, revert cleanup, and clean-sharing tests.

### Phase 5.1 — Live Agent end-to-end vertical slice

Deliver:

- one configured live Agent implementation as a replaceable input to the native
  `AgentHost` and `ToolExecutionCoordinator`;
- streaming responses, cancellation, retry, timeout, authentication failure,
  rate-limit, service-unavailable, and malformed-response handling;
- native workspace context and supported attachments supplied to the Agent
  through typed, size-bounded context records;
- Agent-requested tools translated into the existing MCP-compatible tool
  records, with typed argument validation before presentation or execution;
- the existing approval policy for every proposed action, with no provider or
  Agent path capable of bypassing native approval;
- structured tool results returned to the Agent so it can explain the
  authoritative native result and continue the conversation;
- stable message, action, and correlation IDs across provider retries, WebView
  reloads, application restarts, and stale workspace sessions;
- explicit user consent before enabling cloud communication and a clear
  disclosure of which project context and attachments will be transmitted;
- secure credential handling outside Project 3MF state, recovery state,
  conversation records, logs, diagnostics, and packaged application resources;
- the deterministic mock Agent retained for reproducible tests, offline
  development, and explicit developer-mode scenarios.

The live Agent is an input to the existing native coordinator. It must not
execute workspace operations directly, add provider-specific messages to the
WebView bridge, own editable project state, or create another tool-execution
path. A production build must not silently fall back to the mock when the live
Agent is unavailable; show the Agent-unavailable state instead.

Keep the configuration footprint narrow. Reuse `AppConfig` only for the
non-secret `jusprin_agent` section: `enabled`, `provider`, `model`, and
`cloud_consent`. Store the OpenAI key in the OS credential store under the
JusPrin Agent service. `OPENAI_API_KEY` is an explicit developer/test override,
not an application preference; `JUSPRIN_OPENAI_ENDPOINT` is an opt-in test
override. Do not add the key, authorization header, or provider response bodies
to `AppConfig`, Project 3MF data, recovery files, bridge messages, or logs.

The repository-owned shell fixture must set `provider = mock` explicitly. The
credentialed smoke mode sets `provider = openai`, consent, and the model
explicitly, then receives the key only through its process environment. Missing
consent, a missing key, or a provider failure produces Agent-unavailable or a
typed request error; none of those cases may select the mock implicitly.

Manual acceptance:

1. Configure the live Agent without placing credentials in the project.
2. Open the repository-owned multi-plate fixture and ask a question whose answer
   depends on the current native project, plate, and selection.
3. Attach a supported document or image and verify that the Agent uses its
   decoded content without claiming access to unsupported content.
4. Ask the Agent to perform the supported visible mutation.
5. Reject the proposal and verify that authoritative project state does not
   change.
6. Request it again, approve it, and verify that the native canvas, workspace
   revision, and Orca undo stack reflect exactly one change.
7. Continue the conversation and verify that the Agent receives and explains
   the structured tool result and updated native state.
8. Reload during streaming and restart during an outstanding request; verify
   recovery without duplicated messages, tool calls, or mutations.
9. Exercise cancellation, invalid credentials, service unavailability, timeout,
   rate limiting, malformed tool arguments, and stale-project responses while
   confirming that ordinary Orca functionality remains usable.
10. Save, close, and reopen the project and verify that conversation and
    attachment persistence works without storing credentials or provider
    secrets.

Automated gate:

- live-Agent adapter contract tests using a deterministic fake transport;
- streaming, cancellation, retry, timeout, rate-limit, and malformed-response
  tests;
- context serialization, attachment-boundary, tool-schema, and structured-result
  round-trip tests;
- approval, rejection, argument-validation, stale-revision, and idempotency
  tests through the existing coordinator;
- reload, restart, and provider-retry tests proving that no message or native
  mutation is duplicated;
- tests proving that credentials and authentication artifacts never enter
  Project 3MF state, recovery state, logs, diagnostics, or packaged resources;
- the complete deterministic mock regression suite remains green;
- an opt-in credentialed live-service smoke test is run and its provider, model,
  date, result, usage, and any nondeterministic behavior are recorded.

The end-to-end proof is not complete when a live model merely returns chat text.
It must cover a real user prompt, live model tool proposal, visible approval,
execution through `ToolExecutionCoordinator` and `IWorkspace`, one authoritative
Orca mutation, the structured result returned to the model, and the model's
explanation of that actual result.

Phase 5.1 verification evidence (2026-09-01):

- macOS arm64, OpenAI Responses API, `gpt-5.4-mini`: pass;
- the live model used decoded text-attachment content plus the authoritative
  two-plate workspace and selection, completed across a WebView reload, and
  disclosed the expected facts in its reply;
- the live model proposed `duplicate_object`; the first proposal was rejected
  without changing Orca state, and the second required approval before the
  existing coordinator performed exactly one Orca model mutation. Orca history
  could undo it, and the structured native result produced a completed model
  follow-up;
- recorded usage for the final passing run: 2,278 input tokens, 139 output
  tokens, 2,417 total tokens across five Responses API turns;
- pre-fix runs exposed a nondeterministic ordering race in which the first HTTP
  request's trailing completion callback could be mistaken for the tool-result
  continuation. HTTP events are now request-generation scoped, and a regression
  test reproduces the late-callback ordering deterministically;
- the credential was supplied only as a process-local `OPENAI_API_KEY` override;
  the key and provider response bodies were not written to project state,
  recovery state, packaged resources, or logs;
- an offline application harness selected OpenAI with cloud consent withheld
  and verified the unavailable state, the absence of mock fallback, and
  continued Orca canvas usability; the unavailable panel discloses the exact
  workspace summary and user-selected attachments sent after consent;
- Windows and Linux live-service smoke runs remain to be recorded before a
  cross-platform release gate is claimed.

### Phase 6 — Build, export, and print history

Deliver:

- timeline cards for builds, exported copies, and physical print records;
- revision/plate provenance, derived build staleness, and immutable hashes;
- the separate physical-print ledger and `Project timeline removed` behavior;
- complete accessibility, high-DPI, light/dark, narrow-layout, long-content,
  localization, resource-use, and regression verification;
- packaged resource and bridge validation on Windows, macOS, and Linux.

Manual acceptance:

1. Generate deterministic build, export, and print records.
2. Change manufacturing input and verify that the old build becomes stale.
3. Revert past a completed physical print.
4. Verify that later editable history is removed while the physical print
   remains and says `Project timeline removed`.
5. Verify the surviving printer, plate, material, times, outcome, hashes, and
   slice statistics.
6. Run the complete workflow on packaged Windows/WebView2, macOS/WKWebView, and
   Linux/WebKitGTK builds, including supported scaling, themes, keyboard,
   clipboard, IME, streaming while orbiting, and maximize/restore.

Automated gate:

- manufacturing-input hash, staleness, build/copy/print serialization, and
  retention tests;
- revert tests proving that a physical print survives without a recoverable
  later project snapshot;
- bridge, workspace, coordinator, persistence, browser, native workflow, and
  original-Orca regression suites green;
- platform results, inherited failures, resource use, and remaining gaps
  recorded according to [engineering-method.md](engineering-method.md).

Real printer submission and monitoring are not part of this task. Use the
deterministic Agent and typed records for reproducible presentation and
persistence verification, and repeat the Phase 5.1 live-Agent workflow as a
credentialed smoke test.

Phase 6 verification evidence (2026-09-01):

- the deterministic Agent now proposes typed build, exported-copy, and
  physical-print records through the existing approval coordinator. Builds
  retain project/revision/plate, printer/material, slicer/configuration,
  warning, SHA-256, and slice-statistic facts; exported-copy verification and
  build staleness are derived from hashes rather than persisted flags;
- the project document is schema version 2. Revert removes later builds,
  exported copies, recovery draft text, and every unsent staged/error
  attachment. The separate physical-print ledger survives with its immutable
  facts and derives the exact `Project timeline removed` label when its source
  revision no longer exists;
- the Agent pane renders the three records as semantic timeline articles with
  status text, definition lists, full selectable hashes, narrow-width wrapping,
  reduced-motion behavior, and repository semantic light/dark tokens. Existing
  UI tests continue to cover keyboard submission, IME composition, streaming,
  attachment selection, and bridge appearance changes;
- production changes are confined to `src/slic3r/GUI/JusPrin/` and the committed
  `resources/jusprin/agent/` bundle. Supporting changes are in the repository
  Agent and native-shell tests. No OrcaSlicer-owned production file was edited,
  so there is no upstream rebase patch to carry;
- `ctest --test-dir build/arm64/tests/agent --output-on-failure -j4`: 79 of 79
  passed. This includes SHA-256 known-answer/canonical-input tests, record
  round-trip and non-persisted-derived-field tests, Revert retention tests, the
  staged-attachment/draft reproductions, and the deterministic native
  coordinator workflow;
- `ctest --test-dir build/arm64/tests/workspace --output-on-failure -j4`: 14 of
  14 passed. `npm test -- --run`: 43 of 43 passed. `npm run build`: passed and
  regenerated the single-file Agent resource;
- the repository-wide `ctest --test-dir build/arm64 --output-on-failure -j8`
  run passed 285 of 286 tests. The sole failure was the already-documented
  `Scenario: Placeholder parser coFloatsOrPercents vector access` SIGSEGV in
  `test_placeholder_parser.cpp`; it ran first, before the Phase 6 tests, and no
  Phase 6 file touches that subsystem;
- the macOS arm64 `JusPrinShellHarness` app passed its complete default workflow
  with zero failures: an authoritative `PartPlate` became slice-valid, the page
  approved build/export/print proposals through WKWebView, a later Orca model
  duplication made the build stale, and Revert restored the earlier native
  object count with no redo while removing the build/copy and retaining the
  print's exact setup, times, outcome, hashes, and statistics. The clean-share,
  resize, project-replacement, detach, and restored-stock-canvas checks also
  passed;
- the same app passed `--stock` with zero failures and `--slice-all-cold` with
  zero failures. The known placeholder-parser SIGSEGV and the Metal zero-texture
  diagnostic are recorded as inherited failures/output, not Phase 6
  regressions;
- Windows and Linux validation were intentionally skipped at the user's
  direction, so no cross-platform release claim is made. A fresh Phase 5.1
  live-service smoke could not run because this process had no
  `OPENAI_API_KEY`; the credentialed passing run recorded immediately above
  remains the live-Agent evidence for this branch.

## Design requirements

Use the [JusPrin v2 Figma design](https://www.figma.com/design/jo9J1sK9ZZ0vxncWnSp0vH/JusPrin-v2?node-id=0-1)
as the visual direction and the repository design tokens as the implementation
source of truth. The design did not specify every interaction or failure state;
the states in this handoff fill those gaps.

The production UI must:

- use semantic light/dark tokens rather than hard-coded colors;
- preserve clear focus indication and accessible contrast;
- behave correctly in device-independent units and at high DPI;
- keep the native canvas visible and interactive rather than covering it with
  WebView content;
- derive card appearance from semantic records rather than persisting visual
  choices in `state.json`.

## Required evidence at every phase

Do not mark a phase complete based only on screenshots or WebView component
tests. Its handback must include:

- the user-visible behavior delivered;
- production files and any upstream-owned files changed;
- exact automated tests and results;
- the manual acceptance steps run and their results;
- supported platforms exercised and gaps remaining;
- authoritative Orca state used to verify the result;
- inherited failures separated from regressions;
- rebase evidence for every upstream-owned edit.

Use a repository-owned deterministic multi-plate fixture. Deterministic gates
must not depend on a developer's personal files, a live network service, fixed
event-loop delays, or POC environment-gated startup paths. The opt-in Phase 5.1
credentialed smoke test is the sole live-service exception and must record the
service and model used.

## Production-shell and Agent completion criteria

This task is complete only when all phases above pass and:

- the packaged Agent pane uses the typed bridge exclusively;
- deterministic mock scenarios cover the complete state matrix;
- one recorded live-Agent run completes the prompt, approval, native tool,
  structured-result, and follow-up loop without a parallel execution path;
- project mutations require approval and execute through authoritative native
  commands;
- reload and crash recovery cannot duplicate actions;
- saved project conversations, revisions, attachments, build records, and
  physical print facts obey the lifecycle and revert rules above;
- a removed print source is shown as `Project timeline removed`;
- the integration points for the live Agent and a future MCP adapter are the
  native coordinator, not a new WebView protocol;
- Windows, macOS, and Linux release evidence is recorded;
- disabling or losing the Agent does not impair ordinary Orca behavior.

## Explicit non-goals

Do not expand this task to include:

- the compact plate/object pane, which is deferred to a later task;
- multiple live-Agent providers, a general provider marketplace, or a complete
  account-management system beyond the one Phase 5.1 integration;
- provider-specific WebView messages or a second tool-execution path;
- an MCP client/server implementation;
- broad autonomous tool execution;
- physical printer submission or monitoring;
- general project branching, redo after Revert here, or collaboration;
- a cloud-only project database;
- reimplementation of Orca geometry, slicing, Preview, history, or project
  serialization in TypeScript.
