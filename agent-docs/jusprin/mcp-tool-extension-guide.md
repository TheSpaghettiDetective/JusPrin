# Guide for adding JusPrin tools

**Status:** Extension guide for the implemented shared registry, embedded MCP server, and stdio bridge. The six-tool MCP catalog includes workspace inspection, slice-review reporting, and the verified process-settings workflow. A [candidate catalog](#candidate-catalog) derived from the product surfaces follows the implemented catalog record; nothing in it exists unless the catalog record says so.

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

The candidate catalog later in this guide is a map, not a licence. A row there does not exempt a tool from this section; it records which product surface and eval family the tool would serve so the eval is easier to write, and it fixes the noun, class, and owner so two people do not build the same capability twice under different names.

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

Use a preview/apply pair only where Orca rewrites, substitutes, or refuses what the caller asked for, so the caller must see the real consequence before proposing it: settings (the normalizer), preset selection (compatibility substitution and lost dirty edits), topology changes (resulting pieces), and preflight (mapping and readiness). Everywhere else the coordinator computes the after-state at proposal time and shows it on the approval card. A preview that would only echo its input is not a tool.

Prefer typed reads with fixed schemas over one polymorphic record search. A model guesses less against `objects_list` and `project_details_get` than against a generic `records_search` whose result shape depends on a `kind` argument.

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

## JusPrin-owned project state

Some product state has no Orca owner: the print intent, the pinned agent plan, semantic region annotations, physical facts the user confirmed about a printer, and the monitoring policy that authorizes automatic pausing. For these the "current Orca owner" step of the authority path is a JusPrin store; every other step stays. The rules:

- **Storage.** Project-scoped state (intent, plan, regions) lives in the 3mf auxiliary directory under `jusprin/`, using the same persistence, healing, and revert copy-forward the Agent bridge already uses for conversation state. Printer-scoped state (confirmed facts, monitoring policy) lives in app data keyed by printer identity; confirmed facts carry an expiry because the physical world changes without telling the app.
- **Revision.** Every write publishes a workspace change reason (`Intent`, `Plan`, `Regions`, `PrinterFacts`, `MonitoringPolicy`) so readers refresh and pending proposals are invalidated like any other edit.
- **Undo.** None of this state is in Orca's undo stack. Results say `projectUndo: false`. Region annotations generate Orca artifacts (modifier volumes, enforcers, blockers, paint) that *are* in the undo stack, so project Undo can strand an annotation without its artifacts or an artifact without its annotation. `regions_list` reports both conditions and `regions_set` regenerates; a transform, cut, split, scale, or repair result lists the regions whose binding it broke.
- **Provenance.** Any field that records a fact about the world or the user's wishes carries where it came from: `file` (read from the project or profile), `observed` (a sensor or printer report, with a timestamp), `agent_inferred`, or `user_confirmed`. A tool never promotes an inferred value to confirmed; only an approved write that shows the value on the card does. Printer reads distinguish `configured` from `observed` for the same fact and compute `mismatches` between them rather than leaving that to the model.

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
- `duplicate_object`, `import_model`, and `inspect_selection` are in-app fixtures, not released contracts. The candidate catalog retires them in favour of `objects_edit`, `object_import`, and the selection ids in `workspace_inspect`. Until its replacement lands, each fixture keeps its current camelCase inputs (`sessionId` plus `objectId` or `attachmentId`; no `expectedSessionId`, `expectedRevision`, or `count`), and the replacement's PR removes the fixture in the same change. The coordinator captures session/revision at proposal time, invalidates pending proposals on relevant workspace events, and rechecks before executing. Selection-only changes do not redirect or invalidate a pinned object target.
- For a new read-modify-write operation that must detect changes since the caller's earlier read, design and test explicit expected-session/revision inputs in its own schema and decoder. Proposal-time checks alone do not prove the caller's earlier read is current; do not retrofit required arguments onto existing tools silently.
- Batch changes that must be atomic.
- Prefer explicit selectors over magic defaults, except when the name clearly promises the current selection or active plate.
- Reject unknown fields and conflicting selectors.
- A filesystem path may cross the boundary only as an input to a tool whose approval card shows that exact path and whose executor touches the file only after approval. Approval is per call: it is not a standing grant, and it does not authorize any other tool to read or write that path. The candidate catalog has four such tools: `object_import_file`, `project_open`, `project_save`, and `export_file`. No read-only tool accepts a path, and internal history-record fields such as an export destination are not permission for an AI adapter to read or write that path. In-app clients keep the attachment route, which needs no path.
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

### Image results

Three candidate reads return a picture: `project_attachment_read` (a reference image), `view_render` (the prepared or sliced scene), and `printer_camera_snapshot`. The registry result carries the encoded image plus a structured block describing it (dimensions, camera or layer range, capture time), and each adapter projects that one result:

- MCP: an `image` content block beside the text and structured result; on `2025-03-26` the same block, since image content predates structured content.
- OpenAI: the text and structured block as the function result, and the image appended as an image input in the continuation, since function results are text-only.

This is an adapter projection, not forked behavior. Both projections must be tested before the first image-returning tool ships. Cap every image (1280 pixels on the long edge, 2 MB) so one frame never dominates the context window, and never return an image the user has not allowed: camera frames require the privacy grant in Printer Configuration and otherwise fail with `camera_disabled`.

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
- **Destructive:** the current policy includes revert, delete, overwrite, discard, print and export, and restoring a history snapshot through Undo or Redo, which discards the current state. Do not downgrade an existing classification based on whether you believe its effects are reversible. Always requires explicit current approval.

Every non-read-only tool requires approval except under the computation-only policy below. There is no remembered-approval mechanism; `remembered_approval_allowed` only records a policy distinction for possible future work. The internal manufacturing-history entries retain their existing classifications and are not exposed to either AI adapter.

A tool that "previews" changes is read-only only if fake and real tests prove it does not dirty presets, advance the workspace revision, create history, cancel slicing, or write files.

### Computation-only mutations

A mutation may run without an approval card when all of the following hold: it changes no project geometry, preset, file, printer, or durable product state; its only effects are computation, replacing a previously computed result, or recording the agent's own statement; and the person can see and reverse the effect in the UI. Three candidate tools qualify: `slice_start`, `activity_cancel`, and `plan_set`. Without this policy every "Check print" would cost an approval card, and the card stops meaning anything.

The exemption is declared in the registry beside the action class, covered by the coordinator tests, and never applied to a tool whose effect touches disk or a printer. `slice_start` must refuse to pre-empt a slice the GUI started unless the caller passes `preempt: true`, and in that case the card appears.

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
- attachment-based import (`import_model` today, `object_import` in the candidate catalog) is in-app-only, and path-based `object_import_file` is MCP-only, because a file reaches the app through the conversation in one case and through an approved path in the other; that is two honest definitions, not one tool with forked behavior; and
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
| `settings_apply_patch` | Apply the approved batch without overwriting a newer edit | mutation | both | preview session/revision and confirmed before/after values | `IWorkspace::apply_settings` through `Tab::load_config` | bounded actual changes/normalization, revision, dirty flag and `projectUndo: false` |
| `settings_get` | Read current values and preset origin | read-only | both | 1–32 process keys | `IWorkspace::read_settings` using the edited process preset | at most 32 values and unknown-key issues; canonical values are preserved |
| `settings_preview_patch` | Check a batch before requesting approval | read-only | both | active FFF process preset; seven writable keys | `IWorkspace::preview_settings` using a clone and Orca validation/normalization | at most 32 input keys; bounded changes, dependencies, issues and warnings |
| `settings_search` | Find a process setting without loading its full catalog | read-only | both | active FFF process preset | `IWorkspace::search_settings` using Orca definitions | 1–25 matches; deterministic cursor paging and bounded metadata |
| `workspace_inspect` | Client needs current project context | read-only | both | current document | `IWorkspace::snapshot` | at most 16 plates, 64 objects across returned plates and 64 selected IDs; labels at most 256 UTF-8 bytes; totals and truncation flags |

Only `workspace_inspect`, `settings_search`, `settings_get`, `settings_preview_patch`, and `settings_apply_patch` are MCP-visible. `duplicate_object` and `inspect_selection` remain in-app fixtures pending an in-app eval; they are not callable over MCP. There is no MCP attachment-import contract.

Settings search/read cover the active FFF process preset. The write allowlist is `layer_height`, `wall_loops`, `sparse_infill_density`, `sparse_infill_pattern`, `top_shell_layers`, `bottom_shell_layers`, and `brim_width`. Apply takes `changes`, `expectedSessionId`, and `expectedRevision` from a fresh preview. Native approval captures the exact before/after values, including normalization dependencies, then revalidates before applying. It publishes one `Settings` revision, updates native fields and dirty state, and invalidates slicing. Use Orca preset revert or a previewed inverse patch to restore values; ordinary project Undo does not reverse preset edits.

The OpenAI adapter preserves the registry schemas and uses non-strict function calling for optional arguments or dynamic patch maps, which OpenAI strict mode cannot express. Native registry validation remains authoritative. Stateless Responses continuations retain user context and all prior tool results; the live multi-tool regression covers this path.

Update the actual table when implementation changes names, limits, or ownership. The source registry remains authoritative; this table explains why the surface exists.

## Candidate catalog

This is the end-state catalog derived surface by surface from [Designing an AI-Piloted OrcaSlicer](orca-feature-discovery.md). Each row names the surface it serves, so an eval can be written against it; none of it is implemented unless the catalog record above says so, and every row still owes the eval failure, owner trace, and test matrix this guide requires. Tiers are delivery order by the eval that unblocks them, not importance: Tier 1 closes the Prepare, Check, Print loop; Tier 2 adds meaning, geometry, and history; Tier 3 adds the machine, calibration, and expert diagnostics. Tier counts are 27, 14, and 11. If the Tier 1 eval passes without `presets_preview_select` or `printer_facts_confirm`, they slide to Tier 2; nothing depends on them.

Conventions every row inherits: every read returns `sessionId` and `revision`; every write takes `expectedRevision`; handles for detected faces and holes carry the revision they were computed at and fail with `feature_expired` after it moves; facts carry provenance as defined under [JusPrin-owned project state](#jusprin-owned-project-state); long-running rows follow [Asynchronous operations](#asynchronous-operations) and return an action handle read through `activity_get`; every read states a hard cap and a truncation flag. Classes: **R** read-only, **M** mutation with approval, **M\*** mutation under the computation-only policy, **D** destructive.

### Workspace

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `workspace_inspect` | 1 | R | Anchor read. Project identity and saved state; printer pointer (printer, nozzle, plate, filament per extruder); intent summary line; selected object ids; plate and object counts; slice state and current action handle per plate; pinned plan headline; top unresolved warning. `level` concise or detail. | workspace snapshot | 16 plates, 64 objects, 64 selected ids, labels 256 bytes |

### Project knowledge and intent (surfaces 2.1, 2.3, Home)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `project_details_get` | 1 | R | Description, designer, license, safety and usage notes, assembly instructions, bill of materials and accessories, profile notes, attachment list. Each field carries provenance. `sections` selector. | `ModelInfo`, `ModelDesignInfo`, `ModelProfileInfo`; 3mf auxiliary files | 4 KB per field, 64 attachments |
| `project_attachment_read` | 2 | R | One attachment by id; image or text result per [Image results](#image-results). | 3mf auxiliary directory | 2 MB image, 32 KB text with byte range |
| `project_details_update` | 2 | M | Patch of the same fields, recording what the user said. The card shows every field before and after; never promotes an inferred value. | same | |
| `intent_get` | 1 | R | Intended use, appearance versus function, load direction, accuracy class, critical dimensions, environment, surface quality, time budget, material and color, support contact rules, prototype or final. Provenance per field and an `unanswered` list. | JusPrin project state | 16 critical dimensions |
| `intent_update` | 1 | M | Patch; the card shows the interpreted answer so the user confirms what the agent understood. | same | |
| `project_versions_get` | 3 | R | Autosave and backup versions with timestamps, and recovery state for unsaved work. | Orca backup directory | 32 versions |
| `project_open` | 3 | D | Exactly one of `path`, `new: true`, or `versionId`. Replaces the document; the card names the path or version and any unsaved work. | `Plater` load_project and load_files; backup restore | 32 import warnings |
| `project_save` | 3 | D | Save to the current path or an explicit new one shown on the card. | `Plater` export_3mf | |

### Printers and presets (surfaces 2.2, 2.9)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `printer_list` | 1 | R | Physical printers: id, model, connection kind and state, busy or idle, nozzle and materials when reported, `observedAt`. `query`, `cursor`. | `DeviceManager`; print host list | 25 per page |
| `printer_state_get` | 1 | R | One printer: state, job progress with layer and remaining time, temperatures, fans and light, slot inventory with material, color, remaining and humidity, detected plate and nozzle, health alerts. Every observed value has provenance and a timestamp. With a project open: `mismatches` between configured and observed nozzle, plate, and filaments, and current `confirmedFacts`. | `MachineObject`; `PresetBundle` for the configured side | 16 slots, alerts, mismatches, facts |
| `printer_facts_confirm` | 1 | M | Record a physical fact the user stated that the app cannot observe (plate installed, spool dry, glue applied, bed clear) with an expiry. The card is the confirmation; preflight shows the facts instead of asking again. | JusPrin per-printer state | 16 facts |
| `presets_list` | 1 | R | Presets of one `type` with vendor, system or user, compatibility with the current printer and nozzle, selected flag. `query`, `compatibleOnly`, `cursor`. | `PresetBundle`, `PresetCollection` | 25 per page, descriptions 256 bytes |
| `presets_preview_select` | 1 | R | Dry run of a selection patch: the substitutions Orca would make for compatibility, dirty edits that would be lost, resulting selection. Must be proven not to dirty presets or advance the revision. | `PresetBundle` compatibility evaluation without committing | |
| `presets_select` | 1 | M | Patch of printer preset and nozzle, plate type, filament per extruder, process preset, applied in Orca's order so dependent updates run once. Result lists `substituted`. | `Tab` select_preset; bed type seam; filament combos via a product-neutral seam | |
| `presets_save` | 2 | D | Persist the edited preset of one type as a named user profile, overwriting on name match. The card shows name and changed keys. | `Tab` save_preset | |

### Settings (surface 2.7 method, expert layer)

The four implemented tools, extended compatibly. `scope` (process, filament, printer) and `target` (object or modifier id) arrive as optional fields, each advertised only once its adapter is tested per standing decision 2 below. `settings_search` gains `writable` and `changedOnly` filters; the second answers "what deviates from the profile" for the expanded plan view without a separate tool. `settings_get` reports origin as system, user preset, project edit, or object override.

### Objects, plates, and geometry (surfaces 2.4, 2.5)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `objects_list` | 1 | R | Job manifest: plates, objects, parts (model, negative, modifier, enforcer, blocker), instances, names, enabled, extruder, quantity, bounding box and position, override count, region count, print order. `plateId` or `objectIds` filter, `cursor`. | `Model`, `ModelObject`, `ModelVolume`, `PartPlateList` | 64 objects per page, 32 parts per object |
| `object_analyze` | 1 | R | One object: dimensions and volume; mesh health; planar face groups and cylindrical holes with revision-scoped handles, area, normal, diameter, axis; overhang and support area for the current or a proposed orientation; overlaps; plate fit; likely duplicates elsewhere in the project; `measurements` between two handles. `include` list. | `TriangleMesh` statistics; `Measure` feature detection; `OrientJob` overhang evaluation; `PartPlate` collision and fit checks | 32 faces and holes by area; 16 overlaps, duplicates, measurements |
| `object_transform` | 1 | M | Ordered operations on one object or instance: move, rotate, scale uniform or to a dimension, mirror, place on a face handle, auto-orient, drop to bed. One undo snapshot. Mirror and scale are named on the card. Auto-orient runs as an Orca job and returns an action handle. Result lists regions whose binding broke. | Selection and `GizmoObjectManipulation`; `OrientJob` | |
| `objects_arrange` | 1 | M | Arrange one plate, all plates, or selected objects with spacing and rotation allowance. Runs as an Orca job; the terminal result lists objects that did not fit. | `ArrangeJob` | |
| `objects_edit` | 1 | M | Batch: enable or disable, rename, set extruder, set quantity, move to plate, duplicate. One snapshot. Retires `duplicate_object`. | `Plater` and `ObjectList` operations via the workspace command | |
| `object_reshape_preview` | 2 | R | Dry run of a topology change: resulting pieces with volumes and bounding boxes, support-area change, regions that would lose binding, an image. Must be proven not to dirty the model. | Cut utilities and split on a clone; `EmbossJob` glyph rendering | 1 image |
| `object_reshape` | 2 | M | One operation: cut by plane keeping upper, lower, or both; split to objects or parts; merge listed objects; repair mesh; add text as a part. The card describes the result in words. | `Cut`, `ModelObject` split and merge, `TriangleMesh` repair, `EmbossJob` | |
| `object_import` | 2 | M | In-app only. Add a model from a chat attachment id to a target plate. Retires `import_model`. | `Plater` load_files | 32 import warnings |
| `object_import_file` | 2 | M | MCP only. Add a model from a path; the file is read only after the card showing that path is approved. | same | same |
| `plates_edit` | 2 | M | Add, rename, set bed type, set print sequence or order, lock. | `PartPlateList` | |
| `project_delete_items` | 2 | D | Delete objects, parts, plates, or region annotations by id. Result says whether project Undo covers each. | `Plater` remove; `PartPlateList` delete | |

### Regions (surface 2.6)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `regions_list` | 2 | R | Annotations on one object or all: id, kind (smooth face, no support, support allowed, precision hole, reinforce, flexible, visible, hidden, seam preferred, seam forbidden, material or color), bound geometry, derived artifacts, and `bindingLost` or `artifactsMissing` flags. | JusPrin annotation store; `ModelVolume` and `FacetsAnnotation` for artifacts | 32 per object |
| `regions_set` | 2 | M | Create, update, remove. Geometry is a face or hole handle, a primitive in object coordinates, or a direction with tolerance. Generates modifier volumes with settings, enforcers and blockers, seam and support paint, per-object overrides, and regenerates stale ones. The card shows the label and what will be generated. | `ModelObject` add_volume, `FacetsAnnotation`, `ModelConfig` | |

### Plan (surface 2.7)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `plan_get` | 1 | R | The pinned plan plus computed context: profile deviations from `settings_search changedOnly`, estimates when a valid slice exists, bounded change history. | JusPrin plan store; dirty options; slice statistics | 2 KB per field, 16 decisions, 32 history entries |
| `plan_set` | 1 | M\* | Orientation rationale, strategy per concern, unverified assumptions, compromises and risks, confidence per decision, alternatives considered. Computation-only: it is the agent's own statement and changes no Orca state. | JusPrin plan store | |

### Slicing and Check print (surface 2.8)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `slice_start` | 1 | M\* | Slice one plate or all; returns an action handle. Refuses to pre-empt a GUI-started slice unless `preempt: true`, which brings the card. | `Plater` reslice; `BackgroundSlicingProcess` | |
| `activity_get` | 1 | R | State of one action handle from any long-running row: pending, approval, running with bounded progress, terminal result or error, invalidated by a later edit. Plain read; no waiting. | `ToolActivity` | |
| `activity_cancel` | 3 | M\* | Cancel through Orca's real path; reports whether cancellation won the race. | `BackgroundSlicingProcess` stop; job worker cancel | |
| `slice_report` | 1 | R | Check print report for a sliced plate, by `sections`: summary (time, filament per extruder as length, weight, cost); Orca warnings; supports (volume, contact area, contact with annotated regions or detected holes, removal-difficulty heuristics); seams against visible or forbidden faces; overhangs and bridges past thresholds; first-layer contact; islands; collisions and toolpath outside the bed; material changes and purge; prime tower; intent checks such as time over budget. | `Print` statistics; `GCodeProcessor` result with conflicts and toolpath-outside; `PrintObject` support layers and seams; JusPrin annotations | 32 findings per list by severity |
| `slice_layers` | 3 | R | Per-layer height, time, roles, speed range, fan, temperature, flow. Range and cursor. | `GCodeProcessor` result | 100 per page |
| `slice_gcode_read` | 3 | R | Raw G-code by layer or line range. | plate G-code file | 64 KB per call |
| `view_render` | 2 | R | Render the scene: mode prepare or preview, camera preset or angles, layer range and visibility toggles in preview mode. Prepare mode uses the thumbnail renderer; preview mode is a later capability on the same tool. | `GLCanvas3D` thumbnail rendering | 1 image |

### Preflight, send, export (surfaces 2.9, 2.12)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `preflight_check` | 1 | R | For a plate and destination: job name, estimates, proposed filament-to-slot mapping with problems, plate and nozzle match, options with defaults and printer support (bed leveling, flow calibration, timelapse, first-layer inspection, plate detection, tangle detection, monitoring level), blocking problems, disabled-check warnings, applicable safety notes, confirmed facts. Returns a `preflightId` bound to project revision, slice identity, printer `observedAt`, and options, with an expiry. | mapping and readiness logic lifted from `SelectMachineDialog` behind a product-neutral seam; `MachineObject` | 16 mapping rows, problems, warnings |
| `print_send` | 1 | D | Send with a `preflightId`. Re-runs the preflight at execution; fails without sending if anything bound to the digest moved or blocking problems remain. The card is the explicit Send. | `PrintJob`, `SendJob` | |
| `export_file` | 1 | D | G-code, sliced 3mf, project 3mf, STL, or preset bundle to a path. Proposal-time check surfaces license restrictions from project details on the card and in the result. | `Plater` export functions | |

### Live printing (surface 2.10)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `printer_camera_snapshot` | 3 | R | One frame; requires the privacy grant, else `camera_disabled`. | `MachineObject` live view; Moonraker webcam snapshot | 1 image |
| `printer_job_control` | 3 | D | Pause, resume, or stop the current job; the card states the agent's reason. | `MachineObject` task commands | |
| `monitoring_policy_get` | 3 | R | The user-authorized conditions under which automatic pausing is allowed. Writing the policy is a Printer Configuration action, not a tool, until an eval shows otherwise. | JusPrin per-printer state | |

### Calibration (surface 2.11)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `calibration_list` | 3 | R | Tests for the current printer and filament (flow dynamics, flow rate, temperature, pressure advance variants, retraction, max volumetric speed, VFA, junction deviation, input shaping) with parameters, safe ranges, automatic-measurement support, and machine-stored results where firmware reports them. | `CalibUtils`; `MachineObject` calibration queries | 16 tests, 32 stored results |
| `calibration_generate` | 3 | M | Generate one test's geometry and configuration onto a new plate. Analysis is reasoning over a photo or camera frame; applying a result uses the settings tools on the filament scope and `presets_save`. | `CalibUtils` generators | |

### History (surfaces 2.5, 2.7)

| Tool | Tier | Class | Surface and contract | Owner | Bound |
|---|---|---|---|---|---|
| `history_get` | 2 | R | Undo and redo stacks as named snapshots with timestamps and the tool or GUI action that produced each; current position. | `UndoRedo` stack | 32 each side |
| `history_restore` | 2 | D | Undo or redo to a snapshot id. Destructive because it discards current state. Reports preset and JusPrin-owned edits it does not reverse. | `Plater` undo_to, redo_to | |

### Deliberately not tools

One tool per setting or per "make it stronger" bundle; printing advice; preferences, cloud, firmware, plugins, sync, and update installation (user-owned per surface 2.12); manual axis movement, homing, fan and light detail, printer file storage (expert layer of Monitor); painting by stroke and the measure gizmo as UI (reached through regions, reshape, and analysis handles); asking the user a question (the conversation, or `intent_get`'s `unanswered` list for external clients); a second job engine, remembered approvals, or the MCP Tasks extension.

### Prerequisites the catalog imposes on the machinery

- `validate_output` needs `enum` (provenance, action state, region kind) and `maxLength` (capped text fields) before the first row using them merges; extend it with tests. Input unions such as the operation list in `object_transform` are the decoder's job, not the schema validator's.
- Three owners are trapped in presentation code and need a product-neutral seam first: the mapping and readiness logic in `SelectMachineDialog` for `preflight_check`, plane and circle detection in the Measure gizmo for `object_analyze`, and offscreen preview rendering for `view_render` preview mode.
- The JusPrin stores for intent, plan, regions, confirmed facts, and monitoring policy do not exist yet; the rules for them are under [JusPrin-owned project state](#jusprin-owned-project-state).
- The image projections under [Image results](#image-results) must exist in both adapters before `project_attachment_read`, `view_render`, or `printer_camera_snapshot` ships.
- The Tier 1 security review under [Proportional security growth](#proportional-security-growth).

### Evals that gate each tier

1. Open a one-object STL, state "decorative, front face visible, under five hours, PLA", and reach a sent job with no manual setting edits. Tier 1.
2. A model with a precision hole and a support-heavy overhang: find the hole, annotate it as protected, show that supports no longer enter it after re-slicing, and place the seam on a hidden face. Tier 2.
3. Stringing reported on a Bambu printer: list tests, generate a temperature tower, read the user's photo, propose a filament temperature, preview and apply it, save the profile with approval. Tier 3.
4. Concurrent edit: the user changes a setting in the GUI while an external agent has a pending apply. The apply fails as stale and the agent recovers with one preview and one apply. Every tier.

## Process-settings tools: contract and hazards

The settings tools are the reference implementation of this guide. What
follows is the part of their design that stays true after the work is done;
the implementation handoff that produced it is retired. Re-read this section
before expanding the write allowlist or adding a tool that changes a preset.

### Standing decisions

1. Settings are data: four generic tools over searchable records, never one
   tool per setting.
2. Only the active FFF process preset's edited configuration is in scope. No
   `scope` field, and no printer, filament, plate, object, or modifier layer
   until each is a separately tested capability.
3. Metadata, parsing, and serialization come from Orca's own
   `print_config_def` and config option machinery. There is no parallel table
   of types, enum values, ranges, units, or aliases.
4. Search and read cover every process-setting definition; mutation is a
   reviewed allowlist. Every search and read record says whether the key is
   writable; other keys return `unsupported_setting_mutation`. Expand the list
   only after the real-adapter tests cover the option type, its dependency
   behavior, and the visible UI update for each new key.
5. A batch applies whole or not at all, through `Tab::load_config`, the path
   Orca itself uses to load a config into a preset. Never mutate the config
   behind the visible preset UI and imitate the notifications.
6. Process preset edits are not in project Undo. Results say so and return
   the previous values so a caller can propose the inverse.
7. A preview that finds invalid settings is a successful call with
   `valid: false`. Malformed input is a tool error.
8. All values cross JSON as strings in their canonical Orca serialization;
   callers may send numbers or booleans and the decoder converts them. All
   Orca ids cross JSON as strings.

### Error codes

| Condition | Code |
|---|---|
| key not in the definition table | `unknown_setting` with suggestions |
| key not a process option | `unsupported_scope` |
| readable key outside the allowlist | `unsupported_setting_mutation` |
| parse failure, bound violation, or layer height outside the printer's range | `invalid_setting_value` with `allowed` or bounds |
| spiral-mode conflict or a validator message on a touched key | `incompatible_settings` with the conflicting keys |
| session or revision mismatch, or a before value moved since preview | `stale_workspace` with expected and current |
| user rejected in JusPrin | `approval_rejected` |
| client cancelled or the app closed | `cancelled` |
| no FFF project or no process preset | `workspace_unavailable` |
| the batch call failed after validation | `execution_failed` |
| malformed arguments | `invalid_arguments` |

Preview never fails for content reasons. Apply fails with the first blocking
code and mutates nothing. Never convert an invariant failure into success.

### Orca entry points the tools use

- **Atomic batch:** `Tab::load_config(const DynamicPrintConfig&)` in
  `src/slic3r/GUI/Tab.cpp` diffs against the edited preset, sets every changed
  key, then runs `update_dirty()`, `reload_config()`, and `update()` once.
  `TabPrint::update()` runs the FFF normalizer and reaches
  `Plater::on_config_change`, which invalidates slicing as a sidebar edit
  would. Apply is therefore: build a config holding only the changed keys and
  call `wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff)`.
- **Parsing:** clone the edited config and `set_deserialize(key, text)` per
  key; Orca throws for unknown keys and unparseable values. Canonical value is
  `option->serialize()` after parsing.
- **Bounds and enums:** `ConfigOptionDef` in `src/libslic3r/Config.hpp`
  (`type`, `label`, `category`, `tooltip`, `sidetext`, `min`, `max`, `mode`,
  `readonly`, `enum_values`, `enum_labels`). Treat `min` and `max` as absent
  when they are the float limits.
- **Validation:** `Slic3r::validate(const FullPrintConfig&)` in
  `src/libslic3r/PrintConfig.cpp`, fed from `preset_bundle->full_config()`
  with the patched keys applied. Messages for touched keys are blocking;
  messages for untouched keys are warnings, because the preset was already
  invalid. `Print::validate` is a slicing-time check and is not part of
  preview, as in Orca's own UI.
- **Dirty state:** `PresetCollection::current_dirty_options()` and
  `current_different_from_parent_options()`; the preset name is
  `prints.get_edited_preset().name`. The Tab's per-option revert buttons read
  the same data, so they keep working after a tool apply.

### The normalizer can open dialogs and rewrite values

`update_print_fff_config` and `toggle_print_fff_options` in
`src/slic3r/GUI/ConfigManipulation.cpp` run inside `TabPrint::update()` after
every batch and evaluate every rule against the whole post-batch config, not
only the changed keys. Some rules open a modal dialog and then rewrite a
value; others rewrite silently. A modal opened from inside a tool execution
blocks the GUI thread while the request waits, and a silent rewrite makes the
result lie unless it is reported.

Two rules follow. Preview evaluates the same rule set against a clone, using
the printer preset and filament count the adapter can read, so a rule that
fires because of a pre-existing value is reported too. Dialog rules are
blocking issues; silent rules are warnings that name the dependent key and
its predicted value (`normalized_dependency`), and apply lists the actual
rewrite under `normalized`. Preview reuses Orca's `ConfigManipulation` on the
clone with no presentation callbacks rather than copying its option tables.

Rules reachable through the current allowlist, as audited when the tools
were built. Line numbers drift; search for the condition text.

| Rule in Orca | Reached by | Effect | Preview must |
|---|---|---|---|
| `layer_height` at or below epsilon | `layer_height` | modal, reset to 0.2 | refuse with `invalid_setting_value` and the minimum |
| `layer_height` above the printer's `max_layer_height` when that exceeds 0.2 | `layer_height` | modal, clamp | refuse with `invalid_setting_value` and the printer's maximum |
| `seam_slope_type` not `None` and absolute `seam_slope_start_height` at or above `layer_height` | lowering `layer_height` | modal, start height reset to 0 | refuse with `incompatible_settings`; evaluate percent values through `get_abs_value`, 100% or above also trips it |
| `spiral_mode` on and not all of `wall_loops` 1, `top_shell_layers` 0, `sparse_infill_density` 0 | those three keys | yes/no dialog, several keys rewritten either way | refuse with `incompatible_settings` naming `spiral_mode` |
| `sparse_infill_pattern` without multiline support while `fill_multiline` is above 1 | `sparse_infill_pattern` | silent: `fill_multiline` reset to 1 | predict and report as `normalized_dependency` |
| support-gap rounding block | none; it is inside `#if 0` | inactive | do not predict a change that does not happen |

Every active dialog predicate must be checked, including pre-existing
invalid ironing spacing, first-layer height, XY and elephant-foot
compensation, alternate-extra-wall, infill-lock depth, and fuzzy-skin
settings; spiral mode also checks support, enforced support layers, thin
walls, overhang reversal, timelapse, and wrapping detection. The real-adapter
test asserts that no top-level dialog appears during apply for any
allowlisted key under every row above, and that every predicted silent
rewrite appears in the result with its actual value. A rule found later that
a tool can trigger is a defect in the preview, not accepted behavior. Adding
a key to the allowlist means re-reading both functions for every place that
key is read and extending this table.

### Approval binds the values the user saw

Apply is previewed three times. The client previews and sees before and
after values. At proposal time the coordinator previews again on the GUI
thread, so an invalid patch fails at once without an approval card, and
stores that result as the confirmed set the user approves; the approval
title names the keys from it. At execution the adapter previews a third time
against the then-current config and compares every before value against the
confirmed set: a moved value is `stale_workspace`, a blocking issue is
`invalid_settings`, and neither mutates. Client-supplied values are never
trusted as before values. Where Orca normalized a value differently from the
preview, the actual value is returned and the key is listed under
`normalized`; the mutation happened, so that is a success with an honest
report.

### Registry facts worth knowing

- `ToolExecutionCoordinator::execute` dispatches on `ToolHandler` with
  explicit blocks; add a block per tool, not a generic dispatch table.
- `kInvalidatingReasons` is one global mask and includes `Settings`, so any
  pending proposal fails with `stale_revision` when the user edits a setting
  in the GUI. A per-tool mask is a later refinement if evals show friction.
- `ToolRegistry::validate_output` accepts a closed schema vocabulary: `type`,
  `properties`, `required`, `additionalProperties`, `items`, `minimum`,
  `maxItems`. Any other keyword throws; extend the validator with a test
  before using a new keyword.
- Codex CLI needs `tool_timeout_sec` raised above its 60-second default for
  approval-gated calls.

### Upstream seams the tool system owns

Two added lines in `src/CMakeLists.txt`, eight in
`src/dev-utils/platform/unix/build_linux_image.sh.in`, and one additive
`notify_project_state_changed(ProjectStateChangeReason::Settings)` call at
the end of `Plater::on_config_change`. `Plater.cpp` is the fork's busiest
file; when that function changes upstream, the natural resolution is "keep
both". No line in `Tab.cpp`, `ConfigManipulation.cpp`, or `PrintConfig.cpp`
changes; the normalizer hazards are handled by refusing the inputs in
fork-owned code.

## Proportional security growth

Keep the local baseline: automatic startup with the JusPrin Agent panel, numeric loopback binding, Origin validation, request limits, and existing mutation approval. Bearer authentication is absent: local processes with socket access can inspect exposed data and propose actions. Origin validation protects a browser boundary, not local-process identity. Do not extend this unauthenticated design to remote access.

Revisit the security design when one of these becomes true:

- the server binds beyond loopback;
- a browser or remote service must connect;
- tokens or grants must survive an application restart;
- multiple users or clients require different permissions;
- tools read or write files at caller-supplied paths, or read sensitive account/device data such as camera frames;
- tools upload to a printer or begin a physical print;
- unattended or remembered approvals expand, including the computation-only exemption and any monitoring policy that authorizes automatic pausing; or
- real usage makes forensic activity history a product requirement.

At that point evaluate authenticated pairing, durable credential storage, per-client grants, tool scopes, TLS/remote policy, revocation, rate controls, privacy review, and a dedicated audit log. Add them in response to the concrete exposure; do not prebuild an enterprise control plane for local v1.

Tier 1 of the candidate catalog trips three of these triggers on its own: `print_send` begins a physical print, `export_file` writes to a caller-supplied path, and `slice_start` and `plan_set` use the computation-only exemption. That review is therefore a gate on Tier 1, not a later concern. Its minimum outcome for a loopback-only v1 is a written answer to each tripped trigger and the per-call approval evidence for every path and printer tool.

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
