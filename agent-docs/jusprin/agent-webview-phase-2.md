# Agent WebView Phase 2: implementation record

**Status:** Phase 2 of the Agent WebView plan (read-only deterministic
Agent) — implementation and evidence record.

## User-visible behavior delivered

- The shell's fixed right pane now hosts the production Agent conversation: a
  local React/TypeScript page running in `wxWebView` (WKWebView on macOS),
  packaged as one self-contained `resources/jusprin/agent/index.html`.
- The page and the native host speak the versioned `jusprin-agent-bridge`
  JSON protocol: hello handshake with version and capability negotiation
  before any workspace data, unique envelope IDs, correlation IDs, and the
  workspace session/revision stamped on host envelopes.
- A deterministic mock Agent answers questions with a streamed description of
  the authoritative workspace: project name, dirty state, printer and
  filament presets, plates, per-plate objects, slice validity, selection, and
  undo/redo availability. Scripted scenarios: `/fail` (always fails,
  retryable), `/flaky` (fails once, succeeds on retry), `/slow` (long
  stream). Streaming supports Stop; failed replies offer Retry.
- Native project, plate, selection, printer, and slicing context reaches the
  page at handshake and again on every committed workspace change (the
  existing project-state seam feeds the workspace adapter; no new native
  event sources).
- Reloading the WebView reconstructs the page entirely from native state: the
  host owns the conversation, answers every hello with the full state
  (including a mid-stream reply's partial text), deduplicates resent user
  messages by client ID, and resumes a paused stream after reconnection.
- Honest failure states, kept distinct: *Agent unavailable* (service
  unconfigured; clean setup notice rendered by the page, conversation
  preserved, composer disabled — driven by app config `jusprin_agent=0`) vs.
  *internal bridge errors* (native panel with Retry and diagnostics for page
  load/creation failures and handshake timeouts; page-side panel with Retry
  and diagnostics for missing transport, handshake timeout, and version
  rejection). The canvas and all other Orca controls remain usable in both.
- Composer behavior: Enter sends, Shift+Enter inserts a
  newline, Enter during IME composition does not send; text selection,
  clipboard, and focus behave natively. Message list keeps stable scroll
  anchoring (follows the stream only while the reader is at the bottom).
- Light/dark appearance follows the host (semantic tokens from
  `resources/jusprin/ui/design-tokens.json` applied as CSS custom
  properties; the host pushes appearance changes over the bridge).

## Architecture

```text
React page (AgentUI, single-file bundle)
  <-> window.wx.postMessage / RunScript(deliver)   [wx script-message channel]
  <-> AgentWebView (wx transport, timers, native error surface)
  <-> AgentHost (GUI-free coordinator: conversation, handshake, streaming)
  <-> DeterministicMockAgent (GUI-free, replaceable input)
  <-> IWorkspace (extended with WorkspaceSetup + per-plate sliced flag)
  <-> OrcaWorkspaceAdapter -> authoritative Orca state and events
```

- `AgentHost` and `DeterministicMockAgent` are GUI-free and compiled into a
  Catch2 target with `FakeWorkspace`, so the bridge contract is tested
  without wx. Streaming advances only through `pump_stream()`; the wx layer
  paces it with a timer, tests call it directly.
- The mock produces the same semantic activity a future Agent/MCP adapter
  must produce; nothing in the page or transport special-cases mock behavior.
- The shared protocol source is `resources/jusprin/agent/protocol.json`. The
  page derives its constants from it at build time; both the C++ and the
  TypeScript test suites assert their constants against it.
- The WebView uses Orca's existing `WebView::CreateWebView` factory (script
  message handler, user agent, per-platform backends) rather than a parallel
  creation path.

## Files changed

Fork-owned (new):

- `src/slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp` — protocol constants and
  conversation types (GUI-free).
- `src/slic3r/GUI/JusPrin/Agent/AgentHost.{hpp,cpp}` — native bridge
  coordinator and authoritative conversation state.
- `src/slic3r/GUI/JusPrin/Agent/DeterministicMockAgent.{hpp,cpp}` —
  deterministic scenarios and context-aware replies.
- `src/slic3r/GUI/JusPrin/Agent/AgentWebView.{hpp,cpp}` — wx transport,
  stream/handshake timers, internal-connection error surface with Retry and
  diagnostics.
- `src/slic3r/GUI/JusPrin/AgentUI/` — the React/TypeScript package (Vite,
  vitest; `npm run build` writes the committed single-file bundle).
- `resources/jusprin/agent/protocol.json` — shared versioned protocol source.
- `resources/jusprin/agent/index.html` — committed production bundle.
- `tests/agent/{CMakeLists.txt,test_agent_bridge.cpp}` — GUI-free bridge
  contract tests.

Fork-owned (modified):

- `src/slic3r/GUI/JusPrin/Workspace/Workspace.hpp` — `WorkspaceSetup`
  (project name/dirty, printer and filament presets) and per-plate `sliced`,
  read fresh from authoritative owners at snapshot time.
- `src/slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.cpp` — fills the new
  snapshot fields from `Plater`, `PresetBundle`, and `PartPlate`.
- `src/slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp` — setters for setup
  and plate slice state.
- `src/slic3r/GUI/JusPrin/Shell/AgentPane.{hpp,cpp}` — now hosts the
  AgentWebView instead of the Phase 1 static unavailable notice.
- `src/slic3r/GUI/JusPrin/Shell/ShellController.{hpp,cpp}` — owns the one
  `OrcaWorkspaceAdapter`, reads the `jusprin_agent` availability config, and
  releases the adapter after the pane on uninstall.
- `src/slic3r/GUI/JusPrin/sources.cmake` — registration.
- `tests/shell/shell_harness.cpp` — new end-to-end agent stage (below).
- `tests/workspace/test_workspace_contract.cpp` — plate initializers updated
  for the new `sliced` field.

Upstream-owned:

- `tests/CMakeLists.txt` — one `add_subdirectory(agent)` line appended to the
  same fork-added tail block that already carries `workspace` and `shell`.

No upstream line was deleted or rewritten; no other upstream-owned file was
touched. The Phase 1 gap "preset changes do not produce a project-state
event" is deliberately still open: the Agent reads printer context fresh at
every snapshot, so pushed context is stale only until the next workspace
event; closing it fully needs a preset-changed seam and was not required by
the Phase 2 acceptance criteria.

## Rebase evidence

Simulation: `git merge-tree --write-tree --merge-base=8500fcdcca
upstream/main <working tree>` with upstream/main at `56a452875e`.

- The one upstream-owned edit, `tests/CMakeLists.txt`, **auto-merges
  cleanly** against current upstream.
- The simulation reports content conflicts in `GUI_ObjectList.cpp`,
  `Plater.cpp`, and `Plater.hpp`. Running the identical simulation against
  HEAD (without any Phase 2 change) reports the **same three conflicts**:
  they are inherited from the pre-Phase-2 fork seams meeting the upstream
  advance `05b3c9053e..56a452875e`, and Phase 2 adds **zero new conflict
  sites**. Assessing those inherited conflicts is recorded as a follow-up
  task.

What the fork now owns: product policy only (everything under
`src/slic3r/GUI/JusPrin/`, `tests/{agent,shell,workspace}`, and
`resources/jusprin/`). No new neutral seams and no upstream behavior were
taken over in this phase.

## Automated verification

- `agent_bridge_tests` (new, GUI-free): **12 test cases, 149 assertions,
  all pass.** Covers: protocol.json/C++ constant agreement; handshake with
  version negotiation, capability report, and state delivery;
  incompatible-version rejection; handshake-required, wrong-version,
  malformed-JSON, and unknown-type bridge errors; deterministic streamed
  conversation with gap-free sequencing and unique envelope IDs; duplicate
  clientMessageId deduplication; stop; `/fail`—`/flaky` failure and retry;
  context push on selection change and on changes made outside the bridge;
  project replacement invalidating the session in pushed context; reload
  reconstruction including an in-flight stream pausing while disconnected
  and resuming after the new handshake; the unavailable-service state; and
  appearance propagation.
- AgentUI vitest suite (new): **4 files, 26 tests, all pass.** Protocol
  constants against protocol.json; bridge client handshake, hello_reject,
  no-transport, timeout + retry, unique page envelope IDs, foreign-message
  filtering; reducer state replacement, ordered/duplicate/gapped deltas,
  failure + retry, stop, context/appearance/status events; DOM interaction
  tests with a scripted mock host: connect and reconstruct, Enter sends,
  Shift+Enter and IME Enter do not send, streaming with Stop, Retry on
  failure, clean unavailable state with preserved history and disabled
  composer, version-reject and no-transport error panes, and context header
  updates on selection change. The suite also covers a transport that
  appears only after page startup (the late `window.wx` injection).
- `workspace_contract_tests`: **14/14 pass** (extended snapshot fields).
- `JusPrinShellHarness` (extended with the agent stage): **54/54 checks
  pass, exit 0** — the Phase 1 shell/canvas/slice flow plus the 16 agent
  checks described below.
- `JusPrinShellHarness --stock`: **pass, exit 0** (no shell, stock
  presentation intact — the original-Orca regression check).
- `JusPrinWorkspaceHarness` (real-adapter suite): **pass, exit 0** with the
  extended snapshot fields and the shell + Agent installed by default.

Run the harnesses from the repo root:

```bash
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --stock
build/arm64/tests/workspace/RelWithDebInfo/JusPrinWorkspaceHarness.app/Contents/MacOS/JusPrinWorkspaceHarness
ctest --test-dir build/arm64/tests/agent -C RelWithDebInfo
cd src/slic3r/GUI/JusPrin/AgentUI && npm test
```

## Shell harness agent stage

Two harness defects surfaced and were fixed while adding this stage, both
worth keeping in mind for future GUI tests:

- The harness's original wait mechanism was a self-reposting `CallAfter`
  spin. wx's WKWebView `AddScriptMessageHandler` internally runs script
  through a nested `YieldFor`, and a pending-event spin keeps the pending
  queue non-empty, so that yield could never drain: the WebView setup
  deadlocked until the harness deadline. Waits are now polled through a
  one-shot `wxTimer`.
- The page originally read `window.wx` once at startup, but wx injects the
  message-handler wrapper into an already-loading page slightly later. The
  bridge client now re-polls for the transport (250 ms, up to 10 s) before
  declaring the no-transport error.

The end-to-end harness now, after the Phase 1 Prepare/Slice/Check flow:

1. verifies the pane created a real WebView and the packaged bundle
   completed the versioned handshake (`agent_bridge_handshake`);
2. drives a user message through the page itself (`window.__jusprinTest`
   hook via `RunScript`) so the real page → script-message → host path is
   exercised, and waits for the reply to stream to completion;
3. asserts the reply describes the authoritative two-plate fixture;
4. changes the native selection and waits for the host to push context over
   the live bridge;
5. reloads the WebView and verifies a fresh handshake reconstructs the page
   while the native conversation is preserved and no bridge error shows.

## Manual acceptance status

Verified programmatically through the harness against authoritative state.
Remaining for a human on macOS (same display-level gaps as Phase 1: screen
capture and GL pointer automation are unavailable to this environment):

1. Visual pass of the conversation UI in light and dark appearance.
2. A real IME composition (the harness cannot synthesize platform IME
   events; the page-level rule is covered by the vitest suite).
3. Upward scrolling feel during a long streamed reply.
4. Clipboard and focus behavior between the WebView and the canvas,
   including that global shortcuts stay quiet while typing in the composer.

Windows/WebView2 and Linux/WebKitGTK remain unexercised; complete platform
evidence is scheduled for the final phase, per
[engineering-method.md](engineering-method.md).

## Known gaps (deliberate)

- The mock Agent is the only conversation provider; there is no live
  provider, authentication, or MCP client/server (explicit non-goals).
- Printer-preset changes refresh the Agent context only at the next
  workspace event (see the Phase 1 gap note above).
- Bridge diagnostics are minimal (URL, last load error, traffic counters);
  they can grow with Phase 3's tool records.
