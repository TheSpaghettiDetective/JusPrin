# Guide for adding JusPrin tools

**Status:** Extension guide for the implemented shared registry, embedded MCP server, and stdio bridge. The six-tool MCP catalog includes workspace inspection, slice-review reporting, and the verified process-settings workflow.

JusPrin has one tool system with multiple adapters. New capabilities are added to the shared registry, executed by `ToolExecutionCoordinator`, and implemented through the typed live-workspace boundary; the OpenAI and MCP adapters only translate that contract to their wire formats. This guide keeps the catalog small, honest, safe to evolve, and driven by real printing tasks rather than an abstract feature inventory.

Use this guide as the base, then revise it when implementation evidence, real MCP clients, or evals show that a rule, contract, limit, or sequence should change. Do not preserve a planning assumption merely because it is written here. Keep revisions evidence-based, record substantive changes in the PR, and preserve the product's authoritative ownership and verification requirements.

Start with [the production documentation](README.md), [production architecture](architecture.md), and [OrcaSlicer integration guide](orca-integration-guide.md) before touching a live Orca owner. Current contracts are defined in [`ToolRegistry.cpp`](../../src/slic3r/GUI/JusPrin/Agent/ToolRegistry.cpp), execution in [`ToolExecutionCoordinator.cpp`](../../src/slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.cpp), and MCP projection in [`McpProtocol.cpp`](../../src/slic3r/GUI/JusPrin/Mcp/McpProtocol.cpp).

For local external clients, use the bundled `jusprin-mcp` stdio helper through **Connect AI tools...**. The helper and native server compile the same registry. Every new exposed tool must therefore have consistent live and offline catalog/schema behavior for the same build and negotiated protocol version; no new adapter-specific catalog is allowed. Direct HTTP is a developer route, not the default setup instruction.

## Current connection and execution boundary

- The server starts automatically with the JusPrin Agent panel. There is no `JUSPRIN_MCP` enable switch or bearer token. It binds numeric loopback, prefers port 47301, and falls back to an ephemeral port only when that port is occupied.
- The shell supplies `<data_dir>/jusprin/mcp.json`. The runtime atomically publishes the actual address, PID, unique runtime `instanceId`, build version and protocol versions. Removal checks the runtime identity under the discovery lock, so an older runtime cannot remove a newer runtime's record, even in the same process. POSIX files use mode `0600`.
- The helper validates discovery and probes the endpoint. It follows later app starts/restarts; it never launches JusPrin. A last-writer-wins record supports one selected instance, not an instance chooser. `--discovery`, `JUSPRIN_MCP_DISCOVERY`, and `JUSPRIN_MCP_URL` are explicit path/address overrides, not enable switches; connection entries use `--discovery` for the actual app data directory.
- Offline discovery and tool lists come from the compiled registry; offline tool calls return `workspace_unavailable`. With a reachable app, use its live catalog/results, including across version skew; do not substitute the helper's compiled tool list for a different live build. Version skew is logged.
- The HTTP adapter accepts protocol `2026-07-28`. The helper additionally negotiates legacy `2025-03-26`, `2025-06-18`, and `2025-11-25`. It handles version projection; tool executors must not implement protocol-specific behavior.
- **Connect** confirms the command or file edit before invoking Claude Code/Codex's CLI or merging the JusPrin JSON entry for Claude Desktop/local Cowork, Cursor or VS Code. JSON edits preserve unrelated settings, make a backup, and reject stale previews or symlink destinations. This is not a cross-process transaction against another editor writing at the exact replacement instant.
- macOS bundles the helper beside the app executable; Windows has a matching install rule. Linux AppImage entries use the installed image path plus `--mcp-bridge`, dispatched before GUI startup. Do not use a temporary AppImage mount path as the saved command.

Native workspace commands run on the GUI thread through the shared coordinator. A shell-owned timer continues execution independently of the WebView handshake. No external client gets direct access to Orca objects.

An external mutation has no in-app chat message. Preserve the trusted adapter-assigned `ToolSource::Mcp` and verify that its approval card is visible independently of Agent setup and chat navigation, including after page reload. Tests must exercise the rendered Approve/Reject controls and check native workspace results. Calling a hidden decision hook proves coordinator behavior but does not prove that a user can approve or reject the request; real-client testing exposed precisely that gap in the original MCP tests.

## Start with an eval failure

Do not add a tool because Orca has a menu item or config key. Start with a transcript or automated eval where the Agent cannot complete a real user task, completes it unreliably, or consumes unreasonable steps or tokens.

Write down:

- the user outcome;
- the exact point where the current tools become insufficient;
- the authoritative Orca state or operation that is missing;
- the expected successful transcript;
- unsafe or ambiguous cases that must fail; and
- the evidence that will show the new contract fixed the problem.

If the failure can be fixed by a clearer description, a better result, or a more useful validation error, improve the existing definition instead of adding a tool.

## Decide whether this should be a tool

Add or extend a tool when the Agent needs fresh authoritative state, deterministic computation owned by Orca, or a side effect in the live project.

Do not add a tool for:

- one setting key; setting keys are records used by generic settings tools;
- general printing advice the model can reason about from a guide;
- a fixed bundle such as "make it stronger" whose right answer depends on the part and user priorities;
- UI automation against a button, widget, or hidden panel;
- data already present in a bounded existing result;
- speculative future capability with no eval or user task; or
- an operation the real adapter cannot perform honestly through an authoritative Orca owner.

Deterministic workflow machinery can be a tool. Examples include generating Orca's calibration geometry or starting Orca's slice action. Advice should remain retrievable guidance until there is a separately specified deterministic operation behind it.

## Prefer extending nouns over multiplying verbs

The implemented process-settings workflow uses four stable operations over searchable data:

```text
settings_search → settings_get → settings_preview_patch → settings_apply_patch
```

Do not create:

```text
set_layer_height
set_wall_loops
set_infill_density
set_support_angle
```

The same rule applies to presets, project objects, plates, warnings, and slice reports. Extend their typed record formats and bounded query tools before creating dozens of special-purpose actions.

A new verb is justified when it has meaningfully different authorization, approval, atomicity, lifecycle, or result semantics. `slice_start` is not just another settings mutation: it starts asynchronous Orca work and has a different completion contract. That distinction deserves a tool.

## Choose the narrowest honest scope

Never put an enum value such as `printer | filament | process | object` in a schema unless every advertised value works in the fake and real adapter and has the documented inheritance behavior.

For each proposed capability, answer:

1. Which Orca owner contains the authoritative state or operation?
2. Is the command valid for the whole project, one plate, one object, one instance, or one preset layer?
3. What stable ID identifies the target, and when does that ID expire?
4. What revision must still match when the command executes?
5. What existing Orca snapshot and undo/reset/revert behavior encloses it, or does ordinary project Undo not apply?
6. Which workspace change reasons announce that readers must refresh?
7. What happens if the user edits the GUI while the call is pending?

If these answers are unclear, the tool contract is not ready.

## Reuse the one authority path

Every mutation must follow this route:

```text
adapter call
→ shared registry validation
→ ToolExecutionCoordinator proposal
→ approval policy
→ IWorkspace command
→ current Orca owner
→ Orca history and state change
→ coalesced workspace event
→ fresh snapshot/result
```

Do not let an adapter call `Plater`, `Model`, `PresetBundle`, wxWidgets, or slicing objects directly. Do not copy an Orca operation into JusPrin. When a required operation is trapped in presentation code, add the smallest behavior-oriented, product-neutral seam at its current owner.

Changes in OrcaSlicer-owned files must satisfy [fork stewardship](fork-stewardship.md): keep the diff small and additive, record why the seam belongs there, identify upstream lines touched, and include rebase evidence.

## Define the contract before the implementation

Every new tool's registry entry and accompanying contract/tests must specify:

- stable lowercase underscore name;
- concise title shown in approval/activity UI;
- compact description that says when to use the tool;
- strict input JSON Schema with `additionalProperties: false` where practical;
- output JSON Schema;
- trusted `ActionClass`;
- exposure to the in-app Agent, MCP, or both;
- availability requirements, such as an open FFF project or selected object;
- whether a proposal is revision-sensitive;
- atomicity and the real undo, reset, or revert behavior (including an explicit statement when normal project Undo does not apply);
- cancellation behavior;
- bounded output and pagination limits; and
- structured domain errors.

The registry is immutable and deterministically ordered. Adapters may filter by declared static exposure, but they must not rewrite action class, validation, or behavior. Keep the MCP-visible list stable unless the server also implements and tests the current protocol's tool-list change notification contract; report unmet live preconditions from `tools/call` instead.

`ToolDefinition` currently stores name, title, description, input/output schemas, action class, exposure, availability and handler. Revision sensitivity, atomicity, cancellation and history semantics live in the coordinator/workspace implementation and tests, not in additional registry fields. `ToolAvailability` currently describes request context (`Always` or `ImportableAttachment`), not a dynamic project-state filter.

Input validation uses the registry's handler-specific `valid_arguments` decoder; changing only JSON Schema does not change accepted calls. MCP success output is checked by `validate_output`, whose schema vocabulary is deliberately limited. Extend that validator with tests before adding unsupported schema keywords. Do not assume it implements all of JSON Schema or that every adapter automatically validates output.

### Naming and compatibility

- Prefer names such as `slice_start` and `presets_compare`.
- Do not rename a released tool casually. Treat names and schemas as public APIs.
- Add optional fields compatibly. Do not change the meaning or type of an existing field.
- When a breaking contract is unavoidable, add a versioned replacement, stop advertising the old definition after a documented migration window, and keep a focused compatibility test while both exist.
- Keep one canonical name across adapters. Do not maintain OpenAI and MCP aliases unless a client defect is documented and tested.

## Input design

Inputs should express intent and identity, not transport or UI mechanics.

- Send Orca IDs as JSON strings to preserve native width.
- Use canonical setting keys and normalized values.
- Preserve the existing camelCase input contracts: `duplicate_object` takes only `sessionId` and `objectId`; `import_model` takes only `sessionId` and `attachmentId`. They do not accept `expectedSessionId`, `expectedRevision`, or `count`. The coordinator captures session/revision at proposal time, invalidates pending proposals on relevant workspace events, and rechecks before executing. Selection-only changes do not redirect or invalidate a pinned object target.
- For a new read-modify-write operation that must detect changes since the caller's earlier read, design and test explicit expected-session/revision inputs in its own schema and decoder. Proposal-time checks alone do not prove the caller's earlier read is current; do not retrofit required arguments onto existing tools silently.
- Batch changes that must be atomic.
- Prefer explicit selectors over magic defaults, except when the name clearly promises the current selection or active plate.
- Reject unknown fields and conflicting selectors.
- For new AI-exposed tools, do not accept arbitrary filesystem paths. Use an attachment/import capability with a deliberate transfer and permission contract. Internal history-record fields such as an export destination are not permission for an AI adapter to read or write that path.
- Do not make the model repeat data the server can read authoritatively at execution time.
- Do not encode a large catalog as a schema enum; use bounded search and detail retrieval.

The coordinator resolves the action class and handler from the name. Never accept either from call arguments.

## Output design

Design response size before adding fields. A full project tree, all setting definitions, per-layer G-code statistics, or unbounded warnings can consume more context than the entire tool catalog.

Every read tool should use one or more of:

- a concise/default/detail level;
- explicit field selection;
- pagination with deterministic order;
- a hard item limit;
- aggregate summaries followed by targeted detail calls; or
- an explicit `truncated` flag and continuation cursor.

For new live-state contracts, include `sessionId` and `revision` when needed to identify and validate later calls. Preserve existing results: `workspace_inspect` returns both; `inspect_selection` returns object **names** and revision, not IDs or a session field. Use `workspace_inspect` for target IDs. The current duplication/import results return revision plus operation-specific fields; internal history-record results have their own schemas. MCP activity results also carry action ID, current session and revision in `_meta["io.jusprin/activity"]`. If an event says state changed, fetch a fresh snapshot; never treat the event itself as the new state.

For MCP success, produce schema-valid `structuredContent` and a serialized text block describing the same result. Errors use the shared `{error: {code, message, details}}` envelope with `isError: true`, not the success output schema. The bridge removes modern-only result/cache fields for legacy clients. For `2025-03-26` it also omits `structuredContent` and catalog `outputSchema`/`title`, preserving the serialized text result; later supported revisions retain them. These are intentional compatibility projections, not conflicting tool definitions.

## Errors should teach the next valid call

Expected invalid states use structured control flow, not thrown exceptions. An error should identify the problem and provide only the correction data the Agent needs.

Current MCP errors include `invalid_arguments`, `approval_rejected`, `cancelled`, `workspace_unavailable` and `stale_workspace`. The MCP adapter normalizes native `stale_revision`/`stale_id` to `stale_workspace` with expected/current session and revision details, and `unavailable_operation` to `workspace_unavailable`. Transport-level protocol errors are separate JSON-RPC errors. Do not require identical spelling across the native and MCP error boundaries.

The settings tools implement the following bounded correction details:

- unknown setting: canonical `unknown_setting`, original key, and a short `suggestions` list;
- invalid enum: key, diagnostic message, and valid values in `allowed`;
- out of range: key, diagnostic message, and `min`/`max` bounds; units are available from setting metadata;
- incompatible settings: conflicting keys and Orca's reason;
- stale call: expected and current session/revision, plus instruction to read again;
- unavailable operation: no active FFF process preset;
- rejected approval: terminal `approval_rejected`, not a generic execution failure.

Unexpected invariant failures must remain visible to diagnostics and Sentry. Catch at an abstraction boundary only to recover, translate a known error, or add essential context.

## Read, mutation, and destructive policy

Use the registry's action class, not naming conventions:

- **Read-only:** cannot alter project, preset, disk, printer, or durable product state.
- **Mutation:** a project/preset change or creation of a local artifact whose consequences are understood and are not classified as destructive. Requires the existing approval policy unless a separately reviewed policy says otherwise.
- **Destructive:** the current policy includes revert, delete, overwrite, discard, print and export. Do not downgrade an existing classification based on whether you believe its effects are reversible. Always requires explicit current approval.

Currently every non-read-only tool requires approval. There is no remembered-approval mechanism; `remembered_approval_allowed` only records a policy distinction for possible future work. The internal manufacturing-history entries retain their existing classifications and are not exposed to either AI adapter.

A tool that "previews" changes is read-only only if fake and real tests prove it does not dirty presets, advance the workspace revision, create history, cancel slicing, or write files.

## Asynchronous operations

Use asynchronous behavior only when Orca's real operation is asynchronous. Do not invent a second job engine.

For slicing and later long-running actions:

1. the workspace command starts work through Orca's current owner and returns a stable action handle;
2. `ToolActivity` records pending, approval, running, terminal state, and bounded progress;
3. Orca completion or failure advances/invalidates authoritative workspace state;
4. the consumer fetches the authoritative slice report or error after the event; and
5. cancellation calls Orca's real cancellation path and reports whether cancellation won the race.

MCP progress notifications are optional request-scoped presentation. They must be monotonic and must not be the only way to learn the terminal result. Add the MCP Tasks extension only when a real client/eval needs durable calls that outlive one request; do not add it merely because an operation takes several seconds.

The current coordinator stages execution through GUI ticks; it does not provide durable asynchronous jobs. The bridge accepts `notifications/cancelled` in both protocol eras, closes the forwarded connection, and suppresses subsequent output for that request. EOF ends the bridge session. A lost connection before the final response produces `connection_lost` with an **unknown outcome** warning, not proof the mutation was cancelled. Never automatically retry a mutation after such a loss; inspect authoritative state first.

## Tool exposure

Default to both adapters when both can satisfy the same input contract. A deliberate filter is appropriate when context is supplied differently:

- `workspace_inspect` is exposed to both adapters so either can obtain a fresh completed slice identity after slicing, independently of the initial turn context;
- attachment-based `import_model` remains in-app-only until MCP has a file-transfer contract; and
- a future MCP diagnostics tool may be MCP-only if it exists to establish the external connection.

Document the reason beside the registry definition and test it. Exposure is not a place to fork behavior: if two adapters need different semantics, they need a better shared command or honestly separate definitions.

## When resources or workflow guides are appropriate

Do not make MCP resources load-bearing until intended clients prove that model-driven resource retrieval is reliable. A bounded data-returning tool is the compatibility baseline. Resources may be added as an optional alternate projection of large, readable, stable data.

Keep printing playbooks—reducing stringing, improving strength, choosing support strategy—as guides or skills the model can read. Convert a workflow into an executable tool only when it maps to a named, deterministic Orca operation with defined inputs, outputs, validation, and undo/reset/cancellation behavior. If many procedural workflows eventually qualify, prefer one discoverable workflow registry over dozens of nearly identical tool definitions.

## Required implementation sequence for one new tool

1. Add or update the failing eval and expected transcript.
2. Trace the real Orca owner and record lifecycle, threading, history, and event behavior.
3. Define the typed workspace input, output, and error contract.
4. Implement the fake adapter and workspace contract tests.
5. Implement the real adapter through the smallest owner seam.
6. Prove direct GUI edits and tool edits produce equivalent observable state.
7. Add the registry definition, decoder/validator, executor association, and output schema.
8. Add coordinator tests for action class, approval, staleness, atomicity, cancellation, and terminal activity.
9. Add projection tests for every exposed adapter.
10. Add protocol fixtures and output-limit tests.
11. Run a real-app harness and one end-to-end external-client transcript.
12. Update the catalog below and attach verification/rebase evidence to the PR.

Do not merge a registry entry backed only by the fake workspace.

## Test matrix

Every added capability needs tests at the layers it crosses:

| Layer | Evidence |
|---|---|
| Registry | unique name, deterministic ordering, valid schemas, correct exposure and action class |
| Decoder | valid inputs, unknown fields, type errors, bounds, conflicting selectors |
| Fake workspace | success, expected failures, revision, events, atomicity, native undo/reset/revert and cancel semantics |
| Coordinator | approval, reject, stale session/revision, cancellation race, terminal result |
| Real adapter | live Orca owner, GUI parity, truthful undo/reset/revert behavior, slicing invalidation/completion, document replacement |
| OpenAI adapter | canonical schema projection and successful continuation |
| MCP adapter | canonical schema/output projection, bounded response, protocol errors, disconnect behavior |
| Shell/eval | real project and intended client complete the user task |

If a layer does not apply, say why in the PR. "Not testable" is a visible limitation, not permission to omit evidence silently.

## Catalog record

Current registry, in deterministic name order. `Internal` entries are native manufacturing-history commands, not AI tools:

| Tool | Added for eval/task | Action | Exposure | Scope | Owner | Output bound |
|---|---|---|---|---|---|---|
| `duplicate_object` | Duplicate a project object | mutation | in-app | one explicit session-scoped object ID, not necessarily selected | `IWorkspace::duplicate_object` | revision and optional new object ID |
| `import_model` | Add a user attachment | mutation | in-app | current document; importable attachment required | `IWorkspace::import_model` | revision, imported flag and optional new object ID |
| `inspect_selection` | Explain selected geometry | read-only | in-app | current selection | workspace snapshot | at most 64 names, each at most 256 UTF-8 bytes; revision and conditional truncation flag |
| `record_build` | Record a sliced plate | mutation | Internal | manufacturing history | coordinator's history recorder | build ID and recorded flag |
| `record_export_copy` | Record a verified G-code export | destructive | Internal | manufacturing history | coordinator's history recorder | exported-copy ID and build ID |
| `record_physical_print` | Record a completed print fact | destructive | Internal | manufacturing history; does not start a printer | coordinator's history recorder | physical-print ID, build ID and recorded flag |
| `report_slice_review` | Agent findings must drive the header's Check print state without parsing chat text | read-only (ephemeral presentation metadata) | both | exact session, plate, and completed G-code result ID; stale results fail | shared workspace `SliceReviews` | 16 findings of at most 256 UTF-8 bytes; one reported flag |
| `settings_apply_patch` | Apply the approved batch without overwriting a newer edit | mutation | both | preview session/revision and confirmed before/after values | `IWorkspace::apply_settings` through `Tab::load_config` | bounded actual changes/normalization, revision, dirty flag and `projectUndo: false` |
| `settings_get` | Read current values and preset origin | read-only | both | 1–32 process keys | `IWorkspace::read_settings` using the edited process preset | at most 32 values and unknown-key issues; canonical values are preserved |
| `settings_preview_patch` | Check a batch before requesting approval | read-only | both | active FFF process preset; seven writable keys | `IWorkspace::preview_settings` using a clone and Orca validation/normalization | at most 32 input keys; bounded changes, dependencies, issues and warnings |
| `settings_search` | Find a process setting without loading its full catalog | read-only | both | active FFF process preset | `IWorkspace::search_settings` using Orca definitions | 1–25 matches; deterministic cursor paging and bounded metadata |
| `workspace_inspect` | Client needs current project context and exact slice identity | read-only | both | current document | `IWorkspace::snapshot` | at most 16 plates, 64 objects across returned plates and 64 selected IDs; labels at most 256 UTF-8 bytes; totals, result IDs and truncation flags |

Only `workspace_inspect`, `report_slice_review`, `settings_search`, `settings_get`, `settings_preview_patch`, and `settings_apply_patch` are MCP-visible. `duplicate_object` and `inspect_selection` remain in-app fixtures pending an in-app eval; they are not callable over MCP. There is no MCP attachment-import contract.

Slice reviews use the runtime G-code processor result ID, never a selection-driven workspace revision. Unsliced plates and plates during slicing expose an empty result ID. Reports for invalidated, replaced, or re-sliced results fail with `stale_slice`; reviewing a different plate does not acknowledge this one. Identical report retries preserve acknowledgement. This metadata is not saved, does not dirty the project, and cannot authorize printing. The reporting tool is not an automatic geometry-analysis engine: the caller must establish findings and explain them in chat.

Settings search/read cover the active FFF process preset. The write allowlist is `layer_height`, `wall_loops`, `sparse_infill_density`, `sparse_infill_pattern`, `top_shell_layers`, `bottom_shell_layers`, and `brim_width`. Apply takes `changes`, `expectedSessionId`, and `expectedRevision` from a fresh preview. Native approval captures the exact before/after values, including normalization dependencies, then revalidates before applying. It publishes one `Settings` revision, updates native fields and dirty state, and invalidates slicing. Use Orca preset revert or a previewed inverse patch to restore values; ordinary project Undo does not reverse preset edits.

The OpenAI adapter preserves the registry schemas and uses non-strict function calling for optional arguments or dynamic patch maps, which OpenAI strict mode cannot express. Native registry validation remains authoritative. Stateless Responses continuations retain user context and all prior tool results; the live multi-tool regression covers this path.

Update the actual table when implementation changes names, limits, or ownership. The source registry remains authoritative; this table explains why the surface exists.

## Proportional security growth

Keep the local baseline: automatic startup with the JusPrin Agent panel, numeric loopback binding, Origin validation, request limits, and existing mutation approval. Bearer authentication is absent: local processes with socket access can inspect exposed data and propose actions. Origin validation protects a browser boundary, not local-process identity. Do not extend this unauthenticated design to remote access.

Revisit the security design when one of these becomes true:

- the server binds beyond loopback;
- a browser or remote service must connect;
- tokens or grants must survive an application restart;
- multiple users or clients require different permissions;
- tools can read arbitrary files or sensitive account/device data;
- tools upload to a printer or begin a physical print;
- unattended or remembered approvals expand; or
- real usage makes forensic activity history a product requirement.

At that point evaluate authenticated pairing, durable credential storage, per-client grants, tool scopes, TLS/remote policy, revocation, rate controls, privacy review, and a dedicated audit log. Add them in response to the concrete exposure; do not prebuild an enterprise control plane for local v1.

## Verification commands and limits of current evidence

From the repository root on the existing macOS development build:

```sh
cmake --build build/arm64 --config RelWithDebInfo --target OrcaSlicer agent_bridge_tests shell_integration_harness -- -j6
build/arm64/tests/agent/RelWithDebInfo/agent_bridge_tests.app/Contents/MacOS/agent_bridge_tests --order rand --warn NoAssertions
python3 -m unittest discover -s tests/mcp -v
npm --prefix src/slic3r/GUI/JusPrin/AgentUI test
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --mcp-bridge
```

Also run the shell harness with `--mcp` for direct HTTP, `--mcp-setup` for the isolated setup-command fixtures, no argument for normal shell regression, and `--stock` for stock behavior. `--manual-mcp <dedicated-temporary-directory>` provides a disposable two-plate fixture for real clients; do not use the user's normal data directory. Linux helper/launcher checks are documented in [the test README](../../tests/mcp/linux/README.md).

Executed evidence includes native read/visible approval/rejection/Undo/staleness/cancellation/shutdown checks; real Claude Code and Codex CLI reads, rejection, approval, changed-port restart and offline errors using session-only configuration; and Linux ARM64 container tests of the production helper and a compressed AppImage transport fixture. Those Linux tests are not a full GUI release image. A macOS Quit attempt during a setup fixture retained the monitor and later reaped the timed-out children; that does not prove forced OS shutdown behavior.

Still unverified: persistent one-click writes against real client installations; desktop-client read/approve/reject/restart/offline workflows (including local Cowork availability); complete Windows/Linux release packages; macOS universal signing/notarization; and remaining DPI/live-theme checks. The development macOS bundle failed strict signature verification, and the existing CI signing step is restricted to the upstream OrcaSlicer repository. Do not equate a successful local CLI test, a packaging rule, or the presence of signing identities with passing these gates.

## Definition of done

A new tool is done when:

- it fixes a named eval failure;
- its scope and authority are honest;
- the registry is the only definition source;
- the coordinator owns policy and lifecycle;
- the real operation uses Orca's current owner on the GUI thread;
- results and errors are structured and bounded;
- mutation history, approval, staleness, events, and cancellation behave as documented;
- fake, real-adapter, adapter, protocol, and shell evidence pass as applicable;
- no unrelated Orca behavior changes when the JusPrin feature is disabled; and
- the PR records any upstream seam and its rebase evidence.

Tool count is not the success metric. A compact catalog that reliably completes real printing tasks is.
