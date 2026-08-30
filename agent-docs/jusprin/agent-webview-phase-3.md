# Agent WebView Phase 3: implementation record

**Status:** Phase 3 of the Agent WebView handoff (approved Agent changes) —
implementation and evidence record.

## User-visible behavior delivered

- The mock Agent can now propose one clearly visible native change:
  duplicating the selected object. Asking it anything containing "duplicate"
  streams a short explanation and attaches an **action card** to the reply.
- The card shows the concrete action ("Duplicate \"cube-a\""), the tool and
  server identity, and Approve/Reject buttons. Nothing touches the project
  until Approve; Reject leaves the project byte-for-byte unchanged.
- An approved action runs with visible progress and a Cancel button;
  execution goes through OrcaSlicer's own duplicate command, so the new
  object appears on the real canvas, participates in Orca's undo/redo as one
  history step, and pushes fresh workspace context to the page like any
  native change.
- Failure states are honest and distinct: a deterministic execution failure
  shows the workspace error; a proposal that went stale (the project changed
  before the decision) is marked failed with a dedicated explanation and can
  no longer be approved; cancellation and rejection say "nothing was
  changed".
- Deterministic scenarios for tests and manual scripts: `/toolfail`
  (execution fails), `/toolslow` (long progress run for cancellation),
  `/inspect` (a read-only action that runs without approval, per policy).
- Reloading the WebView reconstructs action cards from native state; a
  resent decision after a reload is acknowledged with the authoritative
  record and can never run an action twice.

## Architecture

```text
DeterministicMockAgent -- ToolRequest --> ToolExecutionCoordinator
Future Agent / MCP adapter (same path)         |  approval policy,
                                               |  activity records,
                                               v  deterministic pump()
                                           IWorkspace
                                               |
                                               v
                            authoritative Orca command, history, events
```

- `ToolExecutionCoordinator` (GUI-free, fork-owned) is the single execution
  path and the future MCP integration point. It owns the MCP-compatible
  activity records — tool and server identity, typed arguments, stable
  action and correlation IDs, approval requirement, workspace session and
  expected revision, lifecycle state (`pending`, `approved`, `running`,
  `succeeded`, `failed`, `cancelled`, plus `rejected`), progress, and
  structured result — and executes exclusively through the `IWorkspace`
  contract. Nothing in the page, transport, host, or coordinator
  special-cases the mock.
- The approval policy from the handoff lives in `ToolExecution.hpp`:
  read-only actions run without approval; every durable mutation requires
  action-time approval; destructive actions may never use a remembered
  approval (no remembered approvals exist in this release).
- Staleness: a workspace change whose reasons include Contents, Transform,
  Plates, History, or Project eagerly fails every still-pending proposal
  with error code `stale_revision`; selection-only changes do not, because a
  proposal pins its target by ID. A running action re-validates naturally at
  execution through the workspace command's own invalid/missing/stale
  errors.
- Idempotency: `approve`/`reject`/`cancel` change state only from the
  states they are valid in and report whether they did; the host answers a
  replayed decision with the authoritative record instead of re-executing.
  Cancellation succeeds only while nothing durable has happened (pending, or
  running before the final execution tick).
- Determinism: like Phase 2 streaming, execution advances only through
  `pump()`; the wx layer paces it from the existing 33 ms timer and pauses
  it (with streams) while the page is disconnected, resuming after the next
  handshake.
- Protocol version 2 (`resources/jusprin/agent/protocol.json` bumped with
  both sides' constants asserted against it): new page messages
  `tool_decision` and `tool_cancel`, new host message `tool_activity`, new
  capability `tools`. The state payload gained `toolActivities` for reload
  reconstruction.

## Files changed

Fork-owned (new):

- `src/slic3r/GUI/JusPrin/Agent/ToolExecution.hpp` — activity records,
  lifecycle states, approval policy (GUI-free).
- `src/slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.{hpp,cpp}` — the
  native coordinator described above.
- `src/slic3r/GUI/JusPrin/AgentUI/src/components/ToolActivityCard.tsx` —
  the action card.
- `tests/agent/test_tool_coordinator.cpp` — coordinator contract tests.

Fork-owned (modified):

- `Agent/AgentProtocol.hpp` — version 2, tool message types, `tools`
  capability.
- `Agent/AgentHost.{hpp,cpp}` — owns the coordinator; serializes activity
  events; routes `tool_decision`/`tool_cancel`; proposes the mock's tool
  request when its reply completes; `pump_tools()`; state payload carries
  activities; version-reject text derives from the constant.
- `Agent/DeterministicMockAgent.{hpp,cpp}` — replies may carry a
  `ToolRequest`; the duplicate/`/toolfail`/`/toolslow`/`/inspect` scenarios.
- `Agent/AgentWebView.cpp` — the stream timer also pumps tools.
- `AgentUI/src/bridge/protocol.ts`, `state/store.ts`, `App.tsx`,
  `components/MessageList.tsx`, `styles.css` — tool activity types, reducer
  upserts by action ID, decision/cancel senders, cards rendered under the
  proposing reply, semantic-token styling; the harness hook gained
  `decide()`/`cancelTool()`.
- `resources/jusprin/agent/protocol.json` and the rebuilt committed bundle
  `resources/jusprin/agent/index.html`.
- `src/slic3r/GUI/JusPrin/sources.cmake`, `tests/agent/CMakeLists.txt` —
  registration.
- `tests/agent/test_agent_bridge.cpp`, `tests/shell/shell_harness.cpp`,
  AgentUI test files — coverage below.

Upstream-owned: **none.** No upstream file was touched in this phase.

## Rebase evidence

Simulation: `git merge-tree --write-tree --merge-base=8500fcdcca
upstream/main <phase-3 tree>` with upstream/main at `56a452875e`.

- The Phase 3 tree reports content conflicts in `GUI_ObjectList.cpp`,
  `Plater.cpp`, and `Plater.hpp` — the **same three** the identical
  simulation reports against pre-Phase-3 HEAD. They are inherited from the
  pre-existing fork seams meeting the upstream advance `05b3c9053e..56a452875e`
  (recorded since Phase 2); Phase 3 adds **zero new conflict sites**, and no
  upstream line was deleted or rewritten.
- What the fork now owns: product policy only — everything added lives
  under `src/slic3r/GUI/JusPrin/`, `resources/jusprin/`, and
  `tests/agent`/`tests/shell`. No new neutral seams, no upstream behavior
  taken over. The coordinator reuses the Phase 1/2 workspace commands
  (`duplicate_object`, `undo`, `redo`) rather than adding any.

## Automated verification

- `agent_bridge_tests` (extended): **26 test cases, 320 assertions, all
  pass** (`--order rand --warn NoAssertions`). New coordinator cases:
  approval-policy constants; pending→approved→running→succeeded lifecycle
  with progress and result agreeing with authoritative state in both
  directions; rejection and cancellation (pending and mid-run) provably
  changing nothing; duplicate decisions unable to execute twice; eager
  stale-marking on content changes but not selection changes, including one
  approved execution staling other pending proposals; deterministic
  execution failure (`missing_object`) leaving state untouched; read-only
  auto-run; the executed change moving through the workspace's own
  undo/redo; unknown-tool failure. New bridge cases: the proposed duplicate
  correlating with its assistant reply; reject-executes-nothing;
  approve-executes-once with context push and replayed-approval
  acknowledgement; unknown action IDs as bridge errors; progress +
  page-driven cancel; `/toolfail`; stale-before-decision; reload
  reconstruction of a mid-run activity that pauses while disconnected and
  resumes after the new handshake; `/inspect` running without approval.
- AgentUI vitest suite: **4 files, 31 tests, all pass.** Protocol v2
  constants against protocol.json; reducer state replacement including
  activities and upserts by action ID; DOM tests: pending card with
  Approve/Reject, approval decision payload, progress + Cancel payload,
  succeeded card, rejection flow, the distinct stale explanation, and
  reload reconstruction of a running card.
- `workspace_contract_tests`: **14 test cases, 86 assertions, all pass**
  (unchanged contract).
- `JusPrinShellHarness` (extended): **PASS, 0 failures, exit 0** — the
  Phase 1/2 flow plus the new end-to-end stage: select an object, drive
  "please duplicate the selected object" through the real page, verify the
  pending activity, reject through the page and verify the model is
  unchanged, propose again, approve through the page, verify the duplicate
  in the authoritative model and on the canvas, then undo and redo through
  Orca's project history.
- `JusPrinShellHarness --stock`: **PASS, exit 0** (original-Orca
  regression check).
- `JusPrinWorkspaceHarness` (real-adapter suite): **PASS, exit 0.**

Run everything from the repo root:

```bash
build/arm64/tests/agent/RelWithDebInfo/agent_bridge_tests.app/Contents/MacOS/agent_bridge_tests
build/arm64/tests/workspace/RelWithDebInfo/workspace_contract_tests.app/Contents/MacOS/workspace_contract_tests
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --stock
build/arm64/tests/workspace/RelWithDebInfo/JusPrinWorkspaceHarness.app/Contents/MacOS/JusPrinWorkspaceHarness
cd src/slic3r/GUI/JusPrin/AgentUI && npm test
```

## Manual acceptance status

The handoff's five Phase 3 steps are verified programmatically against
authoritative Orca state by the shell harness (reject → nothing changes;
approve → visible change; undo/redo through Orca) and by the deterministic
cancellation/failure/stale bridge tests. Remaining for a human on macOS,
matching the Phase 1/2 display-level gaps:

1. Visual pass of the action cards (pending, progress, terminal states) in
   light and dark appearance.
2. Watching a `/toolslow` run's progress bar and cancelling it by pointer.

Windows/WebView2 and Linux/WebKitGTK remain unexercised, per the handoff's
platform gate schedule (full platform evidence is due by Phase 6).

## Known gaps (deliberate)

- The tool vocabulary is two tools (`duplicate_object`,
  `inspect_selection`). The handoff requires one visible change; more tools
  arrive with later phases through the same coordinator.
- A Running action is not eagerly invalidated by concurrent native changes;
  it re-validates at execution through the workspace command's own errors
  (documented in the coordinator).
- The assistant does not send a follow-up message after an action finishes;
  the card's result is the completion surface. Revisit when conversation
  persistence (Phase 4) defines how post-action text is recorded.
- Phase 1's preset-change context gap and the minimal bridge diagnostics
  carry over unchanged.
