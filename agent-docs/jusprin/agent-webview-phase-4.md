# Agent WebView Phase 4: implementation record

**Status:** Phase 4 of the Agent WebView handoff (saved conversation and
Revert here) — implementation and evidence record.

## User-visible behavior delivered

- Conversations and the manufacturing timeline are **project-owned**: they
  live in `Auxiliaries/JusPrin/` (a versioned `state.json` plus
  `revisions/<id>.snapshot.3mf` checkpoints) inside the project's auxiliary
  directory, which Orca's own load/store paths extract from and repack into
  the Project 3MF. An explicit save therefore carries the conversation
  state as of that save; reopening the project restores it.
- **Multiple conversations** share one linear manufacturing-revision
  history: a conversation bar lists them, New creates one, and creating or
  switching never branches the project timeline. Switching is refused while
  a reply streams.
- **Revisions**: every committed manufacturing change (contents, transform,
  plates — by whatever hand, Agent-approved or native) captures a
  compressed full project checkpoint and appears as a marker in the
  conversation where it happened. Selection changes and chat messages do
  not create revisions.
- **Revert here** is atomic and destructive: an inline confirmation spells
  out the consequence; on approval the checkpoint is restored through the
  workspace (Orca's own reset and load paths), every conversation loses its
  later messages, approvals, and conversations created later, later
  checkpoints are deleted, the native undo stack is cleared (no redo
  branch), and the reverted state is what a save-and-reopen loads.
- **Local recovery**: a per-project store under the app data dir
  (`jusprin/recovery/<projectId>/`) mirrors working state newer than the
  last explicit save — messages, approvals, a partially streamed reply, and
  the composer draft. Reopening a saved project merges the newer mirror; an
  interrupted stream shows as honestly stopped, interrupted tool runs as
  cancelled, and resent messages deduplicate by their stable client IDs.
  The draft is restored into the composer and never stored in the project
  archive.
- **Lifecycle rules**: New Project and a full in-place reset start a new
  identity, conversation, and initial revision; Open adopts the project's
  own saved state (or initializes history for an Orca project that has
  none); Save As continues identity (the auxiliary dir travels with the
  archive); importing a model adds content without adopting the foreign
  file's identity (Orca only extracts auxiliaries for full project loads);
  a clean-sharing copy (`export_project_archive`) contains the project with
  no conversation, revision, or identity data.

## Architecture

- `ProjectStateDocument` (GUI-free): the semantic `state.json` document —
  schema v1, one JSON tree edited in place so unknown fields from newer
  builds survive load-edit-save; global `seq` numbers define "later than
  revision R" across all conversations; counters persist so message/action
  IDs stay unique across restarts; corrupt input falls back to a fresh
  document (the unreadable file is kept as `state.json.corrupt`), newer
  schemas are refused, older ones migrate.
- `ProjectPersistence` (GUI-free): binds the document to storage and the
  workspace. Continuous write-through (streaming deltas coalesce via a
  dirty flag flushed from the host's 33 ms pump; message boundaries flush
  immediately — an outgoing message is durable before its reply starts);
  revision capture through `IWorkspace::export_project_archive`; revert
  copies every kept checkpoint to scratch first (the old auxiliary dir does
  not survive the restore), restores, truncates only after success, and
  rolls forward the kept checkpoints so earlier reverts remain possible.
- Workspace contract additions: `auxiliary_data_dir()`,
  `export_project_archive(path)` (SkipAuxiliary — checkpoints and clean
  copies never nest), `restore_project_archive(path)` (a project
  replacement through Plater's stock reset+load paths, with the undo stack
  cleared). The real adapter refuses exports before the canvas has
  initialized GL (thumbnail rendering would call through unloaded function
  pointers — this crashed at startup until guarded), and a failed initial
  capture is retried on later events until the first manufacturing change.
- **Project-boundary adoption is evidence-based, not event-ordered.**
  Plater publishes its project-replacement event before the model adopts
  the new auxiliary directory (verified by backtrace/tracing), and
  `load_project` runs nested event loops mid-operation, so neither the
  event timing nor a timer can be trusted. Persistence instead heals
  continuously: whenever the auxiliary directory differs from the attached
  one — checked on every workspace event and every pump — it adopts what is
  actually there; a Project event with an unchanged directory parks as an
  in-place-reset candidate and resolves once the directory stays put at
  quiet time.
- Protocol version 3: page messages `create_conversation`,
  `switch_conversation`, `revert_to_revision`, `draft_update`; host message
  `revision_added`; state payload carries `conversations`,
  `activeConversationId`, `toolActivities`, `revisions`, and `draft` for
  reload reconstruction; capabilities gained `conversations` and
  `revisions`.

## Files changed

Fork-owned (new): `Agent/ProjectStateDocument.{hpp,cpp}`,
`Agent/ProjectPersistence.{hpp,cpp}`, `AgentUI/src/components/
{ConversationBar,RevisionMarker}.tsx`, `tests/agent/test_project_state.cpp`.

Fork-owned (modified): `Agent/AgentProtocol.hpp` (v3), `Agent/AgentHost.*`
(document-backed conversations, new handlers, boundary/flush pumping),
`Agent/ToolExecutionCoordinator.*` (injectable action-ID allocator,
`clear()`), `Agent/AgentWebView.*` and `Shell/{AgentPane,ShellController}.*`
(persistence wiring; recovery root under the app data dir),
`Workspace/Workspace.hpp`, `Workspace/OrcaWorkspaceAdapter.*`,
`Workspace/FakeWorkspace.hpp` (contract additions and archive test seams),
`AgentUI` page sources and the rebuilt bundle, `resources/jusprin/agent/
protocol.json`, `sources.cmake`, `tests/agent/{CMakeLists.txt,
test_agent_bridge.cpp}`, `tests/workspace/CMakeLists.txt`,
`tests/shell/shell_harness.cpp` (Phase 4 stages; 600 s deadline),
`tests/data/jusprin/OrcaSlicer.conf` (`save_project_choise` so reopen never
prompts).

Upstream-owned: **none.** The merge simulation against upstream
`56a452875e` (base `8500fcdcca`) reports the same three inherited conflicts
(`GUI_ObjectList.cpp`, `Plater.cpp`, `Plater.hpp`) as before Phase 4 —
zero new conflict sites. What the fork now owns remains product policy
only.

## Automated verification

- `agent_bridge_tests`: **46 test cases, 477 assertions, all pass**
  (`--order rand --warn NoAssertions`). New document cases: schema
  round-trip with continued counters; unknown-field preservation through
  edits; corrupt/foreign/future refusal; migration; cross-conversation
  revert truncation with kept/removed checkpoint lists; interrupted-state
  normalization. New persistence cases: adoption creating identity,
  initial revision, and both stores; manufacturing-vs-selection revision
  capture; saved-state adoption on reopen with newer-recovery merge and
  draft restore; corrupt-state preservation; in-place reset boundary
  (resolved at quiet time); the event-outruns-directory-move race; revert
  failure atomicity (missing and unreadable checkpoints, already-current);
  successful revert keeping earlier checkpoints usable and persisting; the
  clean copy carrying no JusPrin state. New bridge cases: conversation
  create/switch/refusals, draft lifecycle over the bridge, revisions and
  their events, revert-here end-to-end with truncation and no native redo.
- AgentUI vitest suite: **4 files, 35 tests, all pass** — protocol v3
  constants; reducer state including conversations/revisions/draft and
  `revision_added` current-flag movement; DOM: conversation bar switching
  and creation, revision markers with the explicit destructive
  confirmation (nothing sent on cancel), draft restore and debounced
  `draft_update`.
- `workspace_contract_tests`: **14 cases, 86 assertions, all pass.**
- `JusPrinShellHarness`: **96 checks, PASS, 0 failures** — the Phase 1–3
  flow plus: conversation created and used through the real page; a real
  project save whose archive contains `JusPrin/state.json` and checkpoint
  files; New Project starting a new identity; reopening the saved 3MF
  adopting the saved identity, conversations, messages, and revisions; a
  native change captured as a revision and reverted through the page with
  the model restored, no redo, and the timeline truncated; a model import
  keeping the project identity; a clean copy verified to be a project
  archive with no conversation state or checkpoints.
- `JusPrinShellHarness --stock`: **PASS** (original-Orca regression).
- `JusPrinWorkspaceHarness`: **PASS** (real-adapter suite with the shell
  and persistence installed).

## Checkpoint benchmarks (handoff-required)

From the shell harness run on the deterministic two-plate fixture
(macOS/arm64, RelWithDebInfo):

- checkpoint captures: 11 succeeded, 3 refused (pre-GL startup and
  mid-replacement interim directories — recorded honestly as revisions
  without checkpoints, healed by the retry where the state was still
  reachable);
- total checkpoint size: ~345 KB (~31 KB per checkpoint for the fixture);
- last capture: ~171 ms (dominated by thumbnail regeneration inside
  Orca's exporter); project restore: ~882 ms; explicit project save:
  ~179 ms, 220 KB archive including the embedded state and checkpoints.

Assessment: acceptable for the fixture, but capture cost scales with model
size and thumbnail rendering, and it runs synchronously inside the
workspace-change callback on every committed manufacturing change. Before
Phase 6, re-benchmark on a large real project; candidates if it is too
slow: skipping thumbnail regeneration for checkpoints (needs a small
upstream-negotiable strategy bit) or Orca's incremental backup machinery.

## Manual acceptance status

Steps 1–6 of the handoff's Phase 4 script are verified programmatically
against authoritative state (harness plus the GUI-free recovery tests,
which cover the restart-with-draft scenario the in-process harness
cannot). Remaining for a human on macOS, consistent with earlier phases:
a visual pass of the conversation bar and revision markers in light/dark,
and a real quit-and-relaunch draft recovery. Windows/WebView2 and
Linux/WebKitGTK remain scheduled for Phase 6.

## Known gaps (deliberate)

- Checkpoint capture performance on large projects is unproven (numbers
  above; flagged for Phase 6).
- Preset (printer/process/material) changes still do not produce workspace
  events (Phase 1 gap), so they neither refresh Agent context immediately
  nor create revisions despite being manufacturing state.
- Recovery-store directories for abandoned unsaved projects accumulate
  (small JSON files); a cleanup policy is future work.
- Builds, exported-copy records, and physical print history are Phase 6;
  `.gcode.3mf` print artifacts rely on Orca's existing exported-file mode.
- The initial revision of the startup project is un-revertible if a
  manufacturing change happens before the canvas can render (the capture
  retry closes this in every observed flow).
