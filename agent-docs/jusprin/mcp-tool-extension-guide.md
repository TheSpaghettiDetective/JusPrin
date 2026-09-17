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

Prefer typed reads with fixed schemas over one polymorphic record search. Sections of `workspace_inspect` each keep their own fixed shape; a generic `records_search` whose result shape depends on a `kind` argument makes the model guess.

## Tool budget and tool shape

Tool count is bounded by what a turn loads, not by what the app can do. The evidence behind these rules: OpenAI's function-calling guidance keeps fewer than 20 functions available at the start of a turn and defers the rest; Anthropic's [tool-writing guidance](https://www.anthropic.com/engineering/writing-tools-for-agents) asks for a few tools targeting high-impact workflows and names "tools that merely wrap existing software functionality or API endpoints" as the common error; a [domain-oriented MCP study](https://arxiv.org/html/2608.22063v1) measured a generic thin-tool interface below raw SQL and domain-shaped tools far above both; and the "god tool" that dispatches many unrelated actions through one `action` parameter is a documented anti-pattern. The current five-tool MCP catalog measures about 1.9 KB per definition, two thirds of it output schema.

- **Loading, for now.** Every registered tool loads on every turn in both adapters: the in-app adapter sends the full function list with each request, and MCP `tools/list` returns the whole catalog. This is the simplest correct behaviour and it keeps the tools array byte-identical across a chat, which is what OpenAI's prompt cache keys on. The budget of 20 definitions per turn is a measured trigger, not a gate: the live regression records definition bytes, input tokens, and cached tokens per request, and the journey catalog below will exceed 20 definitions from its first milestone. That is accepted until those measurements show selection errors or context pressure.
- **The baseline, measured on Windows against the live app.** After M5: in-app 25 definitions and 29,148 bytes, MCP 26 and 33,013 over two `tools/list` pages. After M4: in-app 20 definitions and 21,594 bytes, MCP 21 and 24,923 (24,901 through the offline helper recipe). After M3: in-app 20 definitions and 19,331 bytes, MCP 21 and 22,661 (22,639 through the offline helper recipe). After M2: in-app 18 definitions and 12,721 bytes, MCP 16 and 13,911 (13,894 through the offline helper recipe). After M1: in-app 11 definitions and 6,482 bytes, MCP 9 and 6,915. At M0 it was in-app 7 and 3,145 bytes, MCP 5 and 3,147.

  The `--live-agent` regression at both points says what that costs. The first request of a conversation caches nothing and pays for the whole catalog: 803 tokens at M0, 1,733 at M1, so four more definitions cost about 930 tokens, once. Every later request in the same conversation came back 77 to 92 per cent cached (5,864 in, 5,376 cached at M1), because the tools array is byte-identical across a chat and that is what the cache keys on. At M2 the same regression's eleven requests took 2,875 to 6,763 input tokens with 42 to 91 per cent cached; none started cold, because an earlier run of the same day had warmed the cache for the shared prefix, so the M2 cold cost is not measured by that run.

  So the cost of a definition is what it adds to the *first* request of each conversation, not to every turn, and the budget stays a trigger rather than a gate. What would change that reading: a catalog large enough to make the first request of every conversation expensive on its own, or transcripts showing the model choosing the wrong tool. Watch both; neither has appeared yet.
- **Deferral, when the measurements say so.** Use the platform's own deferral, never a hand-rolled switcher. On the in-app side that is OpenAI's `tool_search` with `defer_loading: true` on the Responses API, documented for `gpt-5.4` and later; support on the pinned `gpt-5.4-mini` is unverified. On the MCP side clients defer on their own, as Claude Code does. Toolsets are the deferral unit, and OpenAI recommends fewer than ten functions per namespace. An enable-toolset tool that rewrites the tools array breaks the prompt cache on every switch and is not to be built.
- **Shape each tool as a domain operation.** A tool does one thing a slicer user would name: place an object, lay out the plates, mark a region, set up the printer. Not a wrapper per Orca function, and not an interpreter for a list of unrelated commands. Optional facets of one operation are fine when each is independently typed and conflicting facets are rejected.
- **Batch only where items share a shape.** A list of setting changes, a list of intent fields, a list of annotations, a list of per-object layout rows. A list that mixes transforms, imports, and deletes is the god tool and is refused.
- **Fold reads into sections and detail levels** before adding a read tool. A new read tool is justified only by a different input contract (a search over a large catalog, a per-object analysis, a sliced-plate report) or an image result.
- **Let call logs decide consolidation.** Two tools that transcripts show always called together are candidates to merge; two facets never used together are candidates to split. Consolidation follows evidence from evals, not the whiteboard.
- **Output schemas are not advertised over MCP.** No client was shown to use them, and they were two thirds of the catalog. `tools/list` stopped carrying `outputSchema`; `validate_output` still checks every MCP success, so a result that violates its contract still fails loudly. Measured on the same build with the recipe below: five tools, 9,723 bytes before, 3,141 after, about 2,430 tokens down to 785. The saving is MCP-side only: the in-app adapter has always sent `input_schema` as `parameters` and nothing else, so an in-app turn was never carrying them. Restore the field only for a client that is shown to read it.

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

- **Storage.** Project-scoped state (intent, plan, regions) lives in the 3mf auxiliary directory under `jusprin/`, using the same persistence, healing, and revert copy-forward the Agent bridge already uses for conversation state. In practice that means inside `state.json`, not beside it: `ProjectStateDocument` provisions a new top-level key on load without a schema bump, and the persistence tests assert that the JusPrin data directory holds exactly one file. The print intent (`printIntent`, upserted by field name, each with provenance), the plan (`plan`, replaced whole) and the region annotations (`regions`, replaced whole, each record stamped with a seq when it changes) are implemented there. Printer-scoped state (confirmed facts, monitoring policy) lives in app data keyed by printer identity, with `SpoolStore` as the precedent; confirmed facts carry an expiry because the physical world changes without telling the app. Regions and confirmed facts land with the tools that write them, since their record shapes are the geometry handles and printer identity those milestones define.
- **Revision.** Every write announces itself so readers refresh. Which announcement, and whether it invalidates pending proposals, is **open** and belongs to the milestone that lands the first writer. The obvious reading — publish a `WorkspaceChangeReasons` bit and add it to `kInvalidatingReasons` — is not implementable as stated: `settings_apply_patch` compares its caller's `expectedRevision` against the current revision for exact equality, so any revision advance between a client's preview and its apply fails as `stale_workspace`. An agent that records its plan between previewing and applying would break its own call. The edit feed (`WorkspaceEditHub`) is the precedent for a feed that reports a change without advancing the revision. Decide per kind: a region annotation generates Orca artifacts and is a real project change; an intent answer or a plan statement is not. As built, a region record's own write announces nothing; the artifacts it generates are Orca edits and advance the revision the ordinary way, so a pending proposal made before an annotation goes stale exactly as it would after any other model edit.
- **Undo.** None of this state is in Orca's undo stack. Results say `projectUndo: false`. Region annotations generate Orca artifacts (modifier volumes, enforcers, blockers, paint) that *are* in the undo stack, so project Undo can strand an annotation without its artifacts or an artifact without its annotation. the `regions` section of `object_analyze` reports both conditions and `region_annotate` regenerates; a place, divide, or repair result lists the regions whose binding it broke.
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
- The three in-app fixtures of the first catalog (`duplicate_object`, `import_model`, `inspect_selection`) are retired: `plate_layout` replaced the first, `object_import` the second, and the selection ids in `workspace_inspect`'s summary the third. The coordinator captures session/revision at proposal time, invalidates pending proposals on relevant workspace events, and rechecks before executing. Selection-only changes do not redirect or invalidate a pinned object target.
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

For new live-state contracts, include `sessionId` and `revision` when needed to identify and validate later calls. `workspace_inspect` returns both, with the selection as ids. Internal history-record results have their own schemas. MCP activity results also carry action ID, current session and revision in `_meta["io.jusprin/activity"]`. If an event says state changed, fetch a fresh snapshot; never treat the event itself as the new state.

For MCP success, produce schema-valid `structuredContent` and a serialized text block describing the same result. Errors use the shared `{error: {code, message, details}}` envelope with `isError: true`, not the success output schema. The bridge removes modern-only result/cache fields for legacy clients. For `2025-03-26` it also omits `structuredContent` and catalog `outputSchema`/`title`, preserving the serialized text result; later supported revisions retain `structuredContent` and `title`. No negotiated version advertises `outputSchema` any more (see [tool budget and tool shape](#tool-budget-and-tool-shape)); the bridge's own stripping stays because it also projects catalogs forwarded from a live app of another version. These are intentional compatibility projections, not conflicting tool definitions.

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

As implemented: `ToolDefinition::computation_only` feeds `approval_required(action_class, computation_only)`, and a destructive action never qualifies whatever it declares. Three places had treated "requires approval" as a synonym for "is a mutation" and now ask the action class directly — the coordinator's execution-time staleness recheck (an exempt mutation skips the card, not staleness, and is never `Pending` long enough for the eager invalidation to catch it), the MCP server's streaming decision, and the runtime's progress notifications, which say "Starting in JusPrin" where an approval-gated call says "Awaiting approval in JusPrin". No shipped tool declares the exemption yet and a test pins that; the coordinator's no-card tests arrive with `slice_start`, `plan_set`, and `activity_cancel`, because a test cannot supply its own catalog — the registry's constructor is private and its definitions are `const`.

### Grouped approvals

A plan is several operations, and the user should review it once. Every mutation accepts an optional `planId`. Proposals sharing a `planId` render as one approval card that lists each operation, its class, and the plan headline from `plan_set`; the user approves or rejects the group. The group's class is the highest class of any member, and the card names the member that made it destructive. Each member is still validated, staleness-checked, and executed as its own proposal in order, so MCP tool annotations stay truthful per tool and a member that fails stale stops the rest. Grouping is a coordinator feature; it is never a reason to build one tool that takes a list of unrelated commands.

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
- attachment-based import (`object_import`) is in-app-only, and path-based `object_import_file` is MCP-only, because a file reaches the app through the conversation in one case and through an approved path in the other; that is two honest definitions over one workspace command, not one tool with forked behavior; and
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
| `history_restore` | Take the project back, or forward again, to a named step | destructive | both | the open project's own undo history | `IWorkspace::restore_history` through `Plater::restore_project_history` | the history section and what it did not reverse |
| `record_build` | Record a sliced plate | mutation | Internal | manufacturing history | coordinator's history recorder | build ID and recorded flag |
| `record_export_copy` | Record a verified G-code export | destructive | Internal | manufacturing history | coordinator's history recorder | exported-copy ID and build ID |
| `record_physical_print` | Record a completed print fact | destructive | Internal | manufacturing history; does not start a printer | coordinator's history recorder | physical-print ID, build ID and recorded flag |
| `settings_apply_patch` | Apply the approved batch without overwriting a newer edit | mutation | both | preview session/revision and confirmed before/after values | `IWorkspace::apply_settings` through `Tab::load_config` | bounded actual changes/normalization, revision, dirty flag and `projectUndo: false` |
| `settings_get` | Read current values and preset origin | read-only | both | 1–32 process keys | `IWorkspace::read_settings` using the edited process preset | at most 32 values and unknown-key issues; canonical values are preserved |
| `settings_preview_patch` | Check a batch before requesting approval | read-only | both | active FFF process preset; the reviewed writable keys | `IWorkspace::preview_settings` using a clone and Orca validation/normalization | at most 32 input keys; bounded changes, dependencies, issues and warnings |
| `settings_search` | Find a process setting without loading its full catalog | read-only | both | active FFF process preset | `IWorkspace::search_settings` using Orca definitions | 1–25 matches; deterministic cursor paging and bounded metadata |
| `intent_update` | Record what the user wants from this print | mutation | both | project-scoped product state | `IProductState` over `ProjectStateDocument` | 32 fields per call, 2 KB per answer, 32 open questions |
| `object_analyze` | Learn what a model is before deciding how to print it | read-only | both | one object; mesh, features, fit, orientations, measure | `TriangleMeshStats`; `Measure::Measuring` per part; `PartPlateList` fit checks; `orientation::score_orientations` | 32 faces and 32 holes by size, 16 instances/overlaps/duplicates, 8 orientations |
| `object_import` | Add the model the user attached | mutation | in-app | current document; importable attachment required | `IWorkspace::import_objects` over `Plater::load_files` with `ScopedModalAnswers` | new object ids and the questions answered |
| `object_import_file` | Add a model file an external client names | mutation | MCP | an approved absolute path | same | same |
| `object_place` | Stand, size and place an object for printing | mutation | both | one instance; one undo step | `Selection` transforms and the canvas `do_*` commits, `Plater::convert_unit`, `OrientJob` under a reporting wrapper | transform, size, optional job handle |
| `plate_layout` | Set copies, what prints, which plate, names and filaments, add and name plates, and arrange | mutation | both | objects and plates in one undo step; arrange as a job | `Plater::increase_instances`/`decrease_instances`, the object list's printable toggle, `Selection` moves, `PartPlateList::create_plate`, `PartPlate::set_bed_type`, `ArrangeJob` | the objects section, added plate ids, optional job handle |
| `plan_set` | Pin the agent's plan and its reasoning | mutation (computation-only) | both | project-scoped product state | same | one headline, 16 decisions, 16 assumptions, 16 risks, 2 KB each |
| `presets_list` | Choose a printer, filament, or process from what is installed | read-only | both | one preset kind | `PresetBundle` collections, read const | 25 per page with a cursor and the total |
| `printer_setup` | Put the right printer, plate, process and filament in place, and record what the person says about the machine | mutation | both | the selected presets and the plate; facts per printer | `SetupCommands` selection paths, `Tab::select_preset` for the process, `PrinterFactsStore` | the printer section, the process, substitutions and discarded edits |
| `printer_setup_preview` | Check a setup before asking for it | read-only | both | same inputs, nothing applied | const `PresetCollection` lookups and the free `is_compatible_with_printer` | 32 issues and substitutions, 16 filaments |
| `printer_list` | Know which physical printers exist and what they are doing | read-only | both | every machine the app knows | `discover_printers` over `DeviceManager` | 25 printers, 16 materials each |
| `project_delete_items` | Remove objects, parts, copies or plates | destructive | both | up to 32 items, one undo step | the fork's `delete_object`, `ObjectList::delete_from_model_and_list`, `Plater::delete_plate` | the objects section and plate count |
| `project_open` | Get a model or project in, or start over | destructive | both | replaces the open project; a path only as an approved input | `Plater::load_project`, `new_project` + `load_files`, with `ScopedModalAnswers` | project identity and counts, and every question Orca asked with the answer given |
| `project_save` | Keep the work, under its own name or a new one | destructive | both | the open project; a path only as an approved input | `Plater::save_project` after `set_project_filename`, JusPrin state flushed first | the saved path, the dirty flag and the revision |
| `slice_report` | Check a sliced plate before committing to it | read-only | both | one plate's current slice | `Print` statistics and the plate's `GCodeProcessorResult`; `BuildVolume` for the bed check | 16 filaments, 32 findings, critical first |
| `slice_start` | Slice without leaving the conversation | mutation (computation-only; `preempt` restores the card) | both | one plate, or every plate | `Plater`'s own toolbar slice events | handle plus the slicing section |
| `workspace_inspect` | Client needs current project context | read-only | both | current document | `IWorkspace::snapshot`, plus `IProductState` for intent and plan | at most 16 plates, 64 objects across returned plates and 64 selected IDs; labels at most 256 UTF-8 bytes; totals and truncation flags |

`workspace_inspect` takes an optional `sections` list — `summary` (the default, and exactly what it returned before sections existed), `project`, `objects`, `intent`, `plan`, `slicing`, `history`, `printer`. The `objects` section lists every object (64, then `truncated`) with its plates, instance/part/modifier counts, printable flag, extruder, size and override count, from a separate adapter call so the snapshot every selection change builds stays cheap; it has no cursor yet. The `slicing` section also carries `jobs`: each Orca UI job a tool started (orient, arrange), with its handle, state, and for arrange the objects no plate holds afterwards. The `project` section reports the file's path, saved and preset-edit state, the file's own title, designer, description, license, copyright and origin (each `{value, provenance: "project_file"}`, present only when the file states it), the packed attachments by id (a read-only walk of the auxiliary folder without `JusPrin/` and `.thumbnails`; `ProjectPanel::Reload` would create folders), and whether an automatic backup covers the latest edits. The `printer` section sets the configured side (preset, model id, nozzles, plate, filament materials) beside the selected machine, with the facts the person confirmed under `factKey` (`device:<id>` when a machine is selected, otherwise `preset:<name>`) and computed `mismatches` for model, nozzle, filament material, and plate. No printer Orca talks to reports its plate, so `plateObservable` is false and a plate mismatch can only come from a confirmed fact. The `history` section extends the summary's `history` object with `restorable` and the steps, newest 32. Its output schema requires only the identity fields, so a caller that asks for one section is not handed the rest. It is no longer a strict OpenAI function: strict mode cannot express an optional argument.

Intent and plan writes do not advance the workspace revision, and that is deliberate — see [JusPrin-owned project state](#jusprin-owned-project-state). The intent vocabulary is the agent's own; an entry carrying a question and no value is what it asked and does not know, which is where `openQuestions` comes from.

`project_save` resolves its target when it is proposed — the given path, or the project's own file — and stores it in the call as `resolvedPath`, so the card names exactly the file that will be written and says when it replaces one. Nothing touches the file before approval; `tests/agent/test_tool_coordinator.cpp` checks that a rejected save leaves no file. The adapter refuses a relative path, a name not ending in `.3mf`, a missing folder, and an unwritable one before it hands Orca the name, because Orca's own failure path is a modal. A project that has never been saved has no default and fails with `unavailable_operation`.

`history_restore` addresses a step by Orca's snapshot timestamp, which is stable across undo and redo and restarts with the project, so the call also names the session. A snapshot is taken before its edit: "before" loads that snapshot, and "after" loads the next project-changing snapshot or the present, which is where Orca's own redo lands. Orca's public `undo_to`/`redo_to` count positions in whichever stack is active, including a gizmo's, and index without a bounds check, so the fork adds `Plater::restore_project_history(timestamp)` and `can_restore_project_history()` in its grouped block, a thin wrapper over the private `priv::undo_redo_to`. It refuses while a gizmo owns the active history. Setting edits, preset choices, intent and plan are outside that history; the result names the ones that exist.

`printer_setup_preview` never selects anything. It evaluates a candidate against the free `is_compatible_with_printer`, with the same `printer_preset`/`num_extruders` extra config `update_compatible_internal` builds, and reads the plates a printer offers from its vendor model's `not_support_bed_types`, as the sidebar does. Which preset Orca substitutes for one that stops fitting is its own scoring, so the preview names what will be replaced, not what replaces it; `printer_setup` reads the outcome back and reports it. Orca's `Tab::select_preset` asks about unsaved edits in a modal, so the setup counts them first (every edited preset when the printer changes, otherwise the one being switched), refuses with `unsaved_edits`, and only with `unsavedEdits: "discard"` drops them through `discard_current_changes` before selecting. There is no "keep" yet: carrying edits across a switch is Orca's transfer path, not implemented here. The proposal stores the previewed outcome as `confirmedSetup`, and execution refuses with `stale_workspace` if a fresh preview differs. `confirmFacts` go to the app-data facts store under the printer section's `factKey`, each with its own lifetime.

Verified against the in-process fake Bambu printer (`src/slic3r/GUI/JusPrin/Testing/README.md`): printer_list and the printer section show FAKE001 idle, then printing at 40% with temperatures and job from the control file; a preview of an uninstalled 0.2 nozzle preset and of an unoffered plate is refused without a revision change; an approved setup of plate, process and filament read back the new selection and, with a confirmed "Cool Plate (SuperTack)", a `user_confirmed` plate mismatch; a process switch with one unsaved edit was refused, then applied with `discard`, and no dialog window was open afterwards.

`project_open` takes exactly one of `path` and `new: true`. Orca asks its load-path questions in modals scattered through `Plater::priv::load_files`, `load_model_objects` and `close_with_confirm`; every one of them opens through `DPIAware::ShowModal`, so the fork adds a two-line hook there that asks the innermost `ScopedModalAnswers` (`JusPrin/Workspace/ModalAnswers.cpp`) for the answer. The adapter answers "Object too small" from `unitConversion`, "Object too large" from `oversized`, and Orca's save prompt with No only when the caller passed `unsavedWork: "discard"`; any other message dialog gets No and any other dialog Cancel, the choices that change least, and every question and answer is returned in `decisions`. Edited presets are discarded before the load, so `UnsavedChangesDialog`, whose answer is read from the dialog afterwards, never asks. A `.3mf` opens with its settings through Orca's own `"<silence>"` origin; `loadProjectSettings: "keep"` sets the geometry-only load behaviour for that one load, which is the answer the person would give in `ProjectDropDialog`. A model file becomes a new project named after it, with `ImperialUnits` for `unitConversion: "inches"`. The adapter does not rely on the load's own return for projects; it compares the project file afterwards.

Replacing the project tears down the old project's tool state (`AgentHost::on_document_replaced`), which ran while `project_open` itself was still executing. The coordinator now keeps the executing activity through `clear()`, the `project_open` branch finds its record again by id after the load, and `McpRuntime::detach_calls` leaves that one call attached, so an MCP client gets its result. In-app, opening a project ends the chat it was asked from, because that conversation belongs to the project being closed.

`object_analyze` handles are `f<revision>-<object>-<part>-<plane>-<feature>`: the Measure module numbers planes deterministically from the lowest facet, so the same revision finds the same feature again, and any other revision fails with `feature_expired`. Features come from one `Measuring` per model part on its untransformed mesh, moved to the world with the first instance's matrix, as the measure gizmo builds them; face area is summed from the plane's triangles because the module never fills it, and a plane whose border cannot be walked is dropped. A hole is a circular border that its own face does not cover (a boss's rim is covered); the module has no cylinder detection, so a hole is reported by its mouths. Unit suspicion asks Orca's own load-time tests of a model holding only a copy of the object. `orientations` asks the new `orientation::score_orientations` seam for the costs `orient()` ranks by (`unprintability`, weighted overhang, bed contact), for given up directions or face handles, or for Orca's own candidates, on the instance's rotated parts with `OrientJob`'s parameters; the catalog's "support volume" is not something Orca computes, so it is not reported.

`object_place` applies units, scale, mirror, then one of face-down, rotation or auto-orient, then position and drop, inside one `Plater::TakeSnapshot`, through the same `Selection` calls and `GLCanvas3D::do_*` commits the gizmos use (face-down passes the flatten gizmo's snapshot name so a sinking object stays on the bed). `unitsFix` goes through `Plater::convert_unit`, which replaces the object, so the result names the new id. Two catalog facets changed against the real owner: `autoOrientFor` is `autoOrient`, because Orca's auto-orient optimises only for minimal support — the other goals are served by scoring candidates with `object_analyze` and placing the chosen face down — and `arrangeAfter` is left to `plate_layout`. Auto-orient runs `OrientJob` under a `ReportingJob` wrapper that reports finished, cancelled (including dropped from the queue) or failed into the slicing section's jobs.

Verified against the app with Orca's bundled Voron cube: 3,414 facets, round holes of 13–16 mm with their axes, a side face put down (the old handle then refused as expired), rotate/scale/mirror/drop and scale-to-size, auto-orient finishing as a job, one "Place object" undo step per call, and a 20 mm cube written in metres flagged as `meters` and converted to 20 mm under a new object id.

`plate_layout` checks every row before anything moves, then applies plates (a row without an id adds one through the add-plate path, less its own snapshot), then objects: printable through the object list's own toggle steps, quantity through Orca's `increase_instances`/`decrease_instances` on the selected object (which refuse a disabled or cut object, and never with a count that would remove the object), a plate move as a relative move by the difference of plate origins, the name through the fork's `rename_object`, and the extruder as an object override. All of that is one "Lay out plates" undo step. Arrange sets the canvas spacing and rotation for the run and restores them when the job ends; with a plate id it arranges that plate (Orca's menu state), otherwise every unlocked plate. Orca does not report what did not fit (its `m_unarranged` is never filled), so the job's `notPlaced` is every printable instance no plate holds entirely afterwards. Two catalog details changed against the owner: `sequence` is not offered (print order is a per-plate setting and belongs with the settings tools), and arrange has no "selected" scope (`ArrangeJob` never calls its `prepare_selected`).

**M3 eval, 2026-09-16, Windows, fake Bambu A1 mini.** Over MCP (the scripted client in place of Claude Code, approvals through the page's decision hook): a 1 × 0.5 × 0.25 block written in inches was imported (Orca asked "Object too small", answered no), `object_analyze` reported `unitsSuspicion: inches`, `unitsFix` made it 25.4 × 12.7 × 6.35 mm under a new id, the 25.4 × 6.35 face was put down by handle, quantity 2 and arrange all finished with nothing unplaced, and `fit` put both copies inside the plate with the 161.29 mm² face on the bed — pass. In-app with OpenAI and the file attached to the chat, five runs, reviewed per the handoff:

- Run 1 exposed a page bug, not a model one: the test hook's `send` kept the first render's staged-attachment list and never sent the file, so the model had no import tool, invented a path for `project_open`, and was refused. Fixed in `App.tsx` (the send reads the current state).
- Run 2 imported with `convertIfTiny` (Orca converted it), then "fixed" the units again by scaling by 1/25.4, tried `instance: 1` to make a copy, recorded an intent nobody gave, and never arranged. The descriptions now say `convertIfTiny` has already converted and the import result carries each new object's size; `unitsFix` multiplies and scaling by hand is wrong; `instance` names an existing copy and copies come from `plate_layout`; `intent_update` records only what the user said.
- Runs 3 to 5 used the right tools in the right order (import, analyze, place face down, layout with quantity and arrange) and the arrange job left nothing unplaced; both copies were inside the plate each time. The face was wrong in two of the three: once the model named the right handle and a later check still found the end face down (not reproduced by any scripted sequence, arrange with or without rotation included), and once it chose the face already on the bed while saying otherwise. Faces now report `sizeMm` (their two sides), which is how a person names them. This remains an eval failure against the pinned model and is recorded as such.
- Run 3 also exposed a coordinator bug: the `project_open` record kept alive through the project replacement kept its action id, and the new project's ids restart, so a later card with the same id was shadowed. The record is now dropped once its subscribers have the result, the host ignores activities from a closed session, and the adapter clears its job list on replacement.

The live regression (`--live-agent`) at this point: 13 to 15 requests per run, 3,912 to 8,181 input tokens, 72 to 94 per cent cached after the first request of each conversation; one run of three failed because the model declined to call `settings_apply_patch`, reading "waits for approval" as a precondition, and every mutating description now says that calling the tool is what shows the card.

**M4 eval, 2026-09-16, Windows, fake Bambu A1 mini, in-app with OpenAI (`gpt-5.4-mini`).** Prompt, on `overhang.obj` opened as a new project: "Turn on tree supports, only where they touch the build plate, and add a brim around the model." A window watcher sampled every visible top-level window of the app throughout (257 samples): only the main window and Orca's own "Loading..." progress appeared, no dialog. The first attempts exposed three defects, all fixed:

- A call refused at proposal (an invalid patch, a stale revision) hung the turn: the coordinator announced the terminal state before `AgentHost` registered the continuation, so the model never got its result. The host now resumes at once when the proposed activity is already terminal.
- An unknown tool name or arguments that miss the contract ended the turn with `malformed_tool_call`. The in-app adapter now returns the refusal as that call's output, up to twice per turn, and the model corrects itself (it did, from `tree(auto)` as a style to `tree_hybrid`).
- The model searched with several keys in one query and found nothing, and guessed names (`support_build_plate_only`, `enable_brim`) got no useful suggestion. Search now takes several terms and ranks settings matching more of them first; suggestions rank by shared words, then writable, then spelling.

Four runs after the fixes. Runs 1 and 3 changed the process preset; run 3 skipped the brim, said so, and added it when asked (`brim_type` auto_brim → outer_only). Runs 2 and 4 put the same changes on the one object as overrides (`target`), which is a wrong-scope selection and an eval failure against the pinned model. That happened although the descriptions and the `target` property already said that a selected object or a single-object project is not a request for an override; wording alone has not fixed it. The project's setup card, which reads Orca's own preset deltas, showed exactly Brim type auto_brim → outer_only, Enable support 0 → 1, On build plate only 0 → 1 (the preset already had `tree(auto)`), and the harness check `verify_support_settings_patch` reads the native Support-page fields back after the same patch with a dialog counter installed. The live regression (`--live-agent`) at this point: 21 requests, 4,275 to 8,683 input tokens, 58 to 92 per cent cached.

**Regions (M5).** `region_annotate` resolves each row through `IWorkspace::plan_regions` at proposal (the card lists each label and what it will generate) and again at execution, then `apply_regions` removes the artifacts of replaced records and generates the new ones in one "Annotate regions" undo step; the coordinator writes the records to the product store. Geometry is stored in the mesh frame of one model part, so moving, turning or scaling the instance never unbinds a region; the object is found again by its id in the writing session and otherwise by name plus the facet count of each model part, so a record written before a save can find its object after a reopen (not yet exercised by a test). A face is found again as the Measure plane with the same normal, the same plane and the same area; a hole as a circle feature with the same centre and diameter. Holes are Measure's circular face borders, so a hole's depth is the distance to the matching mouth on the far side, or the part's extent along the axis for a blind hole; the blocker is a cylinder from the mouth inward, a millimetre larger all round and past each mouth because near the crown of a round hole Orca detects the overhang wider than the hole's own slice (a blocker the size of the hole left every support in place in the harness), and a reinforcing modifier is the hole plus 2 mm of wall. Kinds generate: support paint (`supported_facets`) and seam paint (`seam_facets`) through a headless `TriangleSelector`, colour paint for a material face, `SUPPORT_BLOCKER`/`SUPPORT_ENFORCER`/`PARAMETER_MODIFIER` volumes named `JusPrin rN ...` (how they are found again), and object overrides for reinforce or flexible on the whole object. Paint state is read back through a fork-owned `TriangleSelector` subclass, since the facet states are protected. Open paint gizmos are closed first, as the object list's "remove paint" does, because a gizmo writes its own copy of the paint back. `object_analyze` `regions` reports `bindingLost` and `artifactsMissing`; the shell harness proves Undo strands a record (artifacts missing, still bound), Redo restores it, a turn keeps it bound, and removal takes the artifacts away. `object_place`, `object_divide`, `object_merge` and `object_repair` list `regionsUnbound`: the regions bound before the call and not after.

**Reshaping (M5).** `object_divide_preview` runs Orca's `Cut` (which works on its own copy of the object) or `ModelObject::split` / `ModelVolume::split` on an object in a scratch `Model`; the harness proves the revision, the undo history and the dirty flag do not move. The preview reports pieces as they would lie and an overhang area (downward faces steeper than the process's support threshold, excluding the bed face): an estimate, not a slice. It does not return an image yet; that lands with the image projection in M6. `object_divide` follows `GLGizmoCut3D::perform_cut` without connectors (snapshot "Cut by Plane", `InvalidateCutInfo`, `Plater::apply_cut_object_to_model`), refuses an object already cut with connectors, and splits to objects through `Plater::split_object(false)` on the selected object (the index overload is declared upstream and never defined; auto-drop off avoids its only question), or to parts through `ModelVolume::split` as `ObjectList::split` does. `object_merge` selects exactly the named objects in the object list and runs its assemble, which takes its own snapshot. `object_repair` runs the repair command's per-part CGAL step without its progress dialog, clears the object's paint as Orca does when it does not remap it, and changes nothing when no part has open edges. `enum_bitmask`'s `|=` does not compile for `ModelObjectCutAttributes` (it assigns a bitmask to an int); combine with `|`.

**Slice checks (M5).** `slice_report` gains four sections computed from the plate's finished print on the GUI thread, gated on the object's `posSupportMaterial` / `posSlice` step as the preview gates its own drawing. supports: each support layer's footprint (`support_islands`, or `lslices` for BBS tree support, which does not fill `support_islands`; the toolpaths as a last resort) is intersected with the slices of every target at that layer's middle, taken with `slice_mesh_ex` under `PrintObject::trafo_centered()`: the cylinder or box of each bound `no_support` or `precision_hole` region, and a cylinder for every hole Measure finds in the mesh that no region names (each through-hole once). The target is shrunk by 0.2 mm first so support that only meets the rim is not "inside", and contacts under 0.5 mm² are dropped. seams: the result's `EMoveType::Seam` vertices (world coordinates, plate origin included), taken into each seam region's part mesh frame per instance and counted when within 0.6 mm of the region's facets. firstLayer: the first object layer's `lslices` area times the instance count, and `PrintObject::has_brim()`. islands: object slices that overlap nothing in the layer below (grown by 0.05 mm), with whether a support layer at most two layers under it reaches it. Face regions are not yet checked for support contact; only volumes are. The shell harness slices the T bar with normal supports, finds support inside the unannotated hole, marks it a precision hole and the back face hidden, re-slices, and proves the contact is gone and seams land on the back face.

**M5 eval, 2026-09-17, Windows, fake Bambu A1 mini.** Over MCP (the scripted client, approvals through the page's decision hook), on `tests/data/jusprin/tee_with_hole.stl` opened as a project: normal supports on everywhere; the first slice's report found support inside the unannotated hole (3,945.6 mm² over 37 layers); `object_analyze` features gave the 10 mm hole and the back face; `region_annotate` made the hole a precision hole and the back face hidden, both reported bound with their artifacts; the second slice's report had no support contact and put all 227 seams on the hidden face — pass. No dialog appeared in either eval (a watcher sampled every visible window).

In-app with OpenAI, the same request in words ("supports everywhere, keep support out of the 10 mm hole, seam on the back face, then prove it with the slice report"), five runs:

- Run 1 found a coordinator defect: the agent previewed its settings patch, annotated the hole, and every apply failed as stale because the region's model edit (and the slicing events after it) advanced the revision. Settings patches now go stale only on a settings or project change (see "Registry facts"). The same run showed that refused arguments said nothing about what was wrong; they now name unexpected and missing parameters, and the app logs every refused call.
- Run 2 marked the back face `seam_forbidden` (the opposite of the request) and ended its turn while the slice was still running. The kind descriptions now say which kinds put the seam on a face and which keep it off, and `slice_start` takes `wait`.
- Run 3 annotated both regions correctly and waited for the slice, but never turned supports on; it said so. Eval failure: a requested step skipped.
- Run 4 left `enable_support` out of its patch and then claimed supports were on, contradicting the report it had just read. Eval failure: a false claim. It had asked for `support_enable` and searched "support" with a limit of 10; unknown keys now suggest `enable_support` for that guess, and a key containing the search term ranks with the keys that start with it.
- Run 5 turned supports on correctly, then failed to annotate twice (first with invented handles, then with the front face's hole handle given as a face), sliced anyway, and reported honestly that support entered the hole. Eval failure: the protection was not applied. A handle of the wrong kind now says what kind it is and where the right ones are listed.

No in-app run met the whole request; each tool the agent used worked as its result said. The `--live-agent` regression at this point: 20 requests, 5,694 to 8,782 input tokens, 72 to 93 per cent cached.

`tools/list` pages hold 25 tools, and the catalog passed 25 at M5, so MCP clients now follow `nextCursor`; the budget test walks every page.

`object_import` and `object_import_file` share `IWorkspace::import_objects`: `Plater::load_files` with `LoadModel` (plus `ImperialUnits` for inches) after selecting the target plate, under the same `ScopedModalAnswers` rule as `project_open`, returning the ids of every object the file added. The file tool looks only at the path before approval and shows it on the card. `project_delete_items` checks everything first, because some of Orca's refusals on these paths (the last solid part, the last copy) arrive as an error dialog after the call returns, which no scope can answer; it refuses deleting the solid parts of a cut object, answers the cut-object warning for a whole object with Delete (the card already named the object), drops parts and copies of objects deleted whole, deletes parts and copies through the object list in its expected order, then objects, then plates, whose objects Orca moves to another plate. Part ids come from the `partList` object_analyze now adds to `mesh`.

`slice_start` posts Orca's own `EVT_GLTOOLBAR_SLICE_PLATE` / `EVT_GLTOOLBAR_SLICE_ALL` after selecting the plate, the way the fork's header button does, so the slice-all bookkeeping and the auto-preview rule stay with their owner and no upstream line changes. It returns when the run starts, and the result is read from the `slicing` section. Nothing in Orca records who started a run, so a second start is refused unless `preempt` is passed, and `preempt` is the catalog's one argument-sensitive approval decision.

Every catalog tool is visible over MCP except `object_import`, which takes a chat attachment; `object_import_file` is its MCP-only counterpart. The internal `record_*` tools are never exposed.

The three fixtures are retired, each in the change that landed its replacement: `duplicate_object` with `plate_layout` (the tests that used it as their canonical mutation now propose one more copy through `plate_layout` and count instances, and so do the deterministic mock agent and the live regression), `import_model` with `object_import` (the import tests, the mock agent's attachment reply and the bridge test now use it), and `inspect_selection` with the summary's selection ids (the canonical read-only action in the tests is now `workspace_inspect`). `tests/agent/test_tool_registry.cpp` pins the exact exposed-name lists, so every retirement is visible in that test's diff.

Settings search/read cover the active FFF process preset. The write allowlist is `writable_setting` in `SettingsSupport.hpp`: layers, walls, shells and infill (`layer_height`, `wall_loops`, `wall_generator`, `detect_thin_wall`, `only_one_wall_top`, the shell layer counts and thicknesses, infill density, direction and the three surface or infill patterns); the support family (`enable_support`, `support_type`, `support_style`, `support_threshold_angle`, `support_on_build_plate_only`, the interface layer counts and pattern, and the top and bottom contact distances); adhesion and seam (`brim_type`, `brim_width`, `skirt_loops`, `skirt_distance`, `seam_position`); and ten speed keys. `settings_search` takes `writable` to list only these and `changedOnly` to list only keys that differ from the saved preset; with an empty query, `changedOnly` lists every unsaved change. Apply takes `changes`, `expectedSessionId`, and `expectedRevision` from a fresh preview. Native approval captures the exact before/after values, including normalization dependencies, then revalidates before applying. It publishes one `Settings` revision, updates native fields and dirty state, and invalidates slicing. Use Orca preset revert or a previewed inverse patch to restore values; ordinary project Undo does not reverse preset edits.

The OpenAI adapter preserves the registry schemas and uses non-strict function calling for optional arguments or dynamic patch maps, which OpenAI strict mode cannot express. Native registry validation remains authoritative. Stateless Responses continuations retain user context and all prior tool results; the live multi-tool regression covers this path.

Update the actual table when implementation changes names, limits, or ownership. The source registry remains authoritative; this table explains why the surface exists.

## Candidate catalog

This is the end-state catalog derived surface by surface from [Designing an AI-Piloted OrcaSlicer](orca-feature-discovery.md), shaped by the [tool budget and tool shape](#tool-budget-and-tool-shape) rules. Nothing in it is implemented unless the catalog record above says so, and every row still owes the eval failure, owner trace, and test matrix this guide requires. The count that matters is the number of definitions loaded per turn, not the total. The total here is 38. All of them load on every turn until the measurements under [tool budget and tool shape](#tool-budget-and-tool-shape) justify deferral; the toolsets below are the deferral units for that day and, until then, a naming and delivery grouping.

Conventions every row inherits: every read returns `sessionId` and `revision`; every write takes `expectedRevision` and an optional `planId` for [grouped approval](#grouped-approvals); handles for detected faces and holes carry the revision they were computed at and fail with `feature_expired` after it moves; facts carry provenance as defined under [JusPrin-owned project state](#jusprin-owned-project-state); long-running rows follow [Asynchronous operations](#asynchronous-operations) and return an action handle whose state is read from the `slicing` section of `workspace_inspect`; every read states a hard cap and a truncation flag. Classes: **R** read-only, **M** mutation with approval, **M\*** mutation under the computation-only policy, **D** destructive.

### Toolsets and loading

| Toolset | Tools | Used for |
|---|---|---|
| `core` | 10 | Every turn: state, geometry facts, settings, intent, plan, slicing. |
| `project` | 4 | Opening, saving, restoring, reading attachments. |
| `setup` | 4 | Choosing or confirming printer, plate, filament, process preset. |
| `geometry` | 5 | Placing, laying out, importing, deleting. |
| `reshape` | 4 | Cutting, splitting, merging, repairing. |
| `regions` | 1 | Marking local meaning. |
| `check` | 3 | Inspecting a sliced result beyond the report. |
| `output` | 3 | Preflight, send, export. |
| `machine` | 3 | Other printers, camera, pause and stop. |
| `calibration` | 2 | Calibration tests. |

Both adapters load every registered tool on every turn. MCP `tools/list` returns the whole catalog and stays stable per build. The in-app adapter sends the whole function list with each request and keeps it byte-identical across a chat. The projection tests record the loaded bytes per adapter and the live regression records input and cached tokens per request; those numbers, not the toolset table, decide when deferral is adopted.

### `core`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `workspace_inspect` | R | The one read for current state. `sections` selects any of: `summary` (default: project identity and saved state, printer pointer, intent line, selection ids, plate and object counts, plan headline, top unresolved warning); `project` (description, designer, license, safety and usage notes, assembly, bill of materials, profile notes, attachment list, versions and recovery state, each with provenance); `intent` (the intent record with provenance and an `unanswered` list); `printer` (the selected printer: configured versus observed nozzle, plate, and filaments with timestamps, computed `mismatches`, `confirmedFacts`, job progress, temperatures, alerts, monitoring policy); `objects` (plates, objects, parts, instances, enabled, extruder, quantity, bounding box, override and region counts, print order); `plan` (the pinned plan, deviations from the profile, estimates when a slice is valid); `slicing` (state and action handle per plate, progress, invalidation); `history` (undo and redo snapshots). `level` concise or detail. | workspace snapshot; `MachineObject`; `PresetBundle`; JusPrin stores; `UndoRedo` | per section: 4 KB text fields, 64 attachments, 16 slots and alerts, 64 objects with cursor, 32 history entries |
| `object_analyze` | R | Geometry facts for one object. `include` any of: `mesh` (dimensions, volume, health, units suspicion); `features` (planar face groups and cylindrical holes with revision-scoped handles); `orientations` (score each entry of `candidates`, or Orca's auto-orient candidates, for overhang area, support volume, bed contact, and which handles face down); `fit` (plate fit, overlaps, likely duplicates); `regions` (annotations and their `bindingLost` or `artifactsMissing` flags); `measure` (distances between two handles). | `TriangleMesh` statistics; `Measure` feature detection; `OrientJob` evaluation; `PartPlate` checks; JusPrin annotation store | 32 faces and holes by area; 8 candidates; 16 overlaps, duplicates, measurements; 32 regions |
| `settings_search` | R | As implemented, plus `scope` and `target` as optional fields advertised per standing decision 2, and `writable` and `changedOnly` filters. `changedOnly` answers "what deviates from the profile". | `print_config_def`; dirty options | 25 per page |
| `settings_get` | R | As implemented, plus optional `scope` and `target`; origin reported as system, user preset, project edit, or object override. | edited presets; `ModelConfig` | 32 keys |
| `settings_preview_patch` | R | As implemented, extended to the same scopes and targets. | `ConfigManipulation` on a clone; `Slic3r::validate` | 32 keys |
| `settings_apply_patch` | M | As implemented, extended to the same scopes and targets. Optional `persistAs` saves the resulting preset as a named user profile in the same approval, which makes the call destructive and the card says so. | `Tab` load_config and save_preset; `ModelConfig` | 32 keys |
| `intent_update` | M | Record what the user said about the print: a same-shape list of `{field, value}` over the intent record. The card shows the interpreted answer so the user confirms the agent's understanding; provenance becomes `user_confirmed` only through this card. | JusPrin intent store | 32 fields |
| `plan_set` | M\* | The agent's own statement: orientation rationale, strategy per concern, unverified assumptions, compromises and risks, confidence per decision, alternatives. Computation-only. | JusPrin plan store | 2 KB per field, 16 decisions |
| `slice_start` | M\* | Slice one plate or all; returns an action handle. Refuses to pre-empt a GUI-started slice unless `preempt: true`, which brings the card. | `Plater` reslice; `BackgroundSlicingProcess` | |
| `slice_report` | R | Check print report for a sliced plate, by `sections`: summary (time, filament per extruder as length, weight, cost); Orca warnings; supports (volume, contact area, contact with annotated regions or detected holes, removal-difficulty heuristics); seams against visible or forbidden faces; overhangs and bridges past thresholds; first-layer contact; islands; collisions and toolpath outside the bed; material changes and purge; prime tower; intent checks such as time over budget. | `Print` statistics; `GCodeProcessor` result; `PrintObject` supports and seams; JusPrin annotations | 32 findings per list by severity |

### `project`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `project_open` | D | Exactly one of `path`, `new: true`, or `versionId`. Explicit inputs for the choices Orca otherwise asks in dialogs: `loadProjectSettings` (project or keep), `unitConversion` (none, inches to millimetres), `oversized` (keep or scale to fit). Replaces the document; the card names the path or version and any unsaved work. | `Plater` load_project and load_files; backup restore | 32 import warnings |
| `project_save` | D | Save to the current path or an explicit new one shown on the card. | `Plater` export_3mf | |
| `project_attachment_read` | R | One attachment by id; image or text result per [Image results](#image-results). | 3mf auxiliary directory | 2 MB image, 32 KB text |
| `history_restore` | D | Undo or redo to a snapshot id from the `history` section. Destructive because it discards current state. Reports preset and JusPrin-owned edits it does not reverse. | `Plater` undo_to, redo_to | |

### `setup`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `presets_list` | R | Presets of one `type` with vendor, system or user, compatibility with the current printer and nozzle, selected flag. `query`, `compatibleOnly`, `cursor`. | `PresetBundle` | 25 per page |
| `printer_setup_preview` | R | Dry run of `printer_setup`: the substitutions Orca would make for compatibility, dirty edits that would be lost, resulting selection, and remaining mismatches against the observed printer. Must be proven not to dirty presets or advance the revision. **Route, found while building `presets_list`:** `PresetCollection::update_compatible` cannot be used — it rewrites `is_compatible` on every preset, flips default-preset visibility, and selects a different preset when the current one stops being compatible. The free `is_compatible_with_printer(preset, candidate_printer, extra)` takes the printer to evaluate against as an argument, so a candidate can be scored without committing. Its one side effect is that reading a preset's `compatible_printers_condition()` inserts that key into the preset's config with an empty value; `ConfigBase::diff` ignores keys absent from one side, so it does not dirty the preset, and a test must pin that. | `PresetBundle` compatibility evaluation without committing | |
| `printer_setup` | M | Establish the hardware for this job: printer preset and nozzle, plate type, filament per extruder, process preset, applied in Orca's order so dependent updates run once; plus `confirmFacts`, a same-shape list of physical facts the user stated that no sensor can see (plate installed, spool dry, glue applied, bed clear), each with an expiry. Result lists `substituted`. | `Tab` select_preset; bed type seam; filament combos via a product-neutral seam; JusPrin per-printer state | 16 facts |
| `printer_list` | R | Physical printers known to the app: id, model, connection kind and state, busy or idle, nozzle and materials when reported, `observedAt`. Shared with `machine`. | `DeviceManager`; print host list | 25 per page |

### `geometry`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `object_place` | M | Place one object for printing. Optional facets, each independently typed, conflicting ones rejected: `faceDown` (a face handle), `autoOrientFor` (minimal support, protect a face handle, strength along an axis), `rotateDegrees`, `position`, `scale` (factor or target dimension, or `unitsFix`), `mirrorAxis`, `dropToBed`, `arrangeAfter`. One undo snapshot. Mirror and scale are named on the card. Auto-orient runs as an Orca job and returns an action handle. Result lists regions whose binding broke. | Selection and `GizmoObjectManipulation`; `OrientJob` | |
| `plate_layout` | M | Lay out the job: `objects`, a same-shape list of `{objectId, enabled?, quantity?, plateId?, extruder?, name?}`; `plates`, a same-shape list of `{plateId?, name?, bedType?, sequence?}` where a missing id adds a plate; `arrange` (none, plate id, all, or selected) with spacing and rotation allowance. One snapshot. Arrange runs as an Orca job and returns an action handle whose terminal result lists objects that did not fit. Retires `duplicate_object`. | `Plater`, `ObjectList`, `PartPlateList`; `ArrangeJob` | 64 object rows, 16 plate rows |
| `object_import` | M | In-app only. Add a model from a chat attachment id to a target plate, with the same dialog-choice inputs as `project_open`. Retires `import_model`. | `Plater` load_files | 32 warnings |
| `object_import_file` | M | MCP only. Same contract from a path; the file is read only after the card showing that path is approved. | same | same |
| `project_delete_items` | D | Delete objects, parts, plates, or region annotations by id. Result says whether project Undo covers each. | `Plater` remove; `PartPlateList` delete; annotation store | 64 ids |

### `reshape`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `object_divide_preview` | R | Dry run of `object_divide`: resulting pieces with volumes and bounding boxes, support-area change, regions that would lose binding, an image. Must be proven not to dirty the model. | cut utilities and split on a clone | 1 image |
| `object_divide` | M | Cut one object by a plane (position and normal; keep upper, lower, or both) or split it into its shells as objects or parts. The card describes the pieces in words. | `Cut`, `ModelObject` split | |
| `object_merge` | M | Merge listed objects into one. | `ModelObject` merge | 16 ids |
| `object_repair` | M | Repair a mesh; result carries the repair statistics. | `TriangleMesh` repair | |

Text embossing stays out until an eval needs it; when it does, it is its own tool on `EmbossJob`, not a facet of `object_divide`.

### `regions`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `region_annotate` | M | A same-shape list of annotations `{objectId, kind, geometry, regionId?}` where kind is one of smooth face, no support, support allowed, precision hole, reinforce, flexible, visible, hidden, seam preferred, seam forbidden, material or color, and geometry is a face or hole handle, a primitive in object coordinates, or a direction with tolerance. Generates modifier volumes with settings, enforcers and blockers, seam and support paint, and per-object overrides, and regenerates stale ones. The card shows each label and what will be generated. Read back through `object_analyze` `regions`; remove through `project_delete_items`. | `ModelObject` add_volume; `FacetsAnnotation`; `ModelConfig`; JusPrin annotation store | 32 per call |

### `check`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `view_render` | R | Render the scene: mode prepare or preview, camera preset or angles, layer range and visibility toggles in preview mode. Prepare mode uses the thumbnail renderer; preview mode is a later capability on the same tool. | `GLCanvas3D` thumbnail rendering | 1 image |
| `slice_inspect` | R | Expert detail for a sliced plate with identical inputs (`plateId`, layer or line range, `cursor`) and `view` of `layers` (height, time, roles, speed range, fan, temperature, flow per layer) or `gcode` (raw text). | `GCodeProcessor` result; plate G-code file | 100 layers or 64 KB per call |
| `activity_cancel` | M\* | Cancel an action handle through Orca's real path; reports whether cancellation won the race. | `BackgroundSlicingProcess` stop; job worker cancel | |

### `output`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `preflight_check` | R | For a plate and destination: job name, estimates, proposed filament-to-slot mapping with problems, plate and nozzle match, options with defaults and printer support (bed leveling, flow calibration, timelapse, first-layer inspection, plate detection, tangle detection, monitoring level), blocking problems, disabled-check warnings, applicable safety notes, confirmed facts. Returns a `preflightId` bound to project revision, slice identity, printer `observedAt`, and options, with an expiry. | mapping and readiness logic lifted from `SelectMachineDialog` behind a product-neutral seam; `MachineObject` | 16 mapping rows, problems, warnings |
| `print_send` | D | Send with a `preflightId`. Re-runs the preflight at execution; fails without sending if anything bound to the digest moved or blocking problems remain. The card is the explicit Send. | `PrintJob`, `SendJob` | |
| `export_file` | D | G-code, sliced 3mf, project 3mf, STL, or preset bundle to a path. Proposal-time check surfaces license restrictions from project details on the card and in the result. | `Plater` export functions | |

### `machine`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `printer_list` | R | As in `setup`. | | |
| `printer_camera_snapshot` | R | One frame; requires the privacy grant, else `camera_disabled`. | `MachineObject` live view; Moonraker webcam snapshot | 1 image |
| `printer_job_control` | D | Pause, resume, or stop the current job; the card states the agent's reason. Automatic pause under the monitoring policy is a later coordinator feature on this tool, not a new tool. | `MachineObject` task commands | |

### `calibration`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `calibration_list` | R | Tests for the current printer and filament with parameters, safe ranges, automatic-measurement support, and machine-stored results where firmware reports them. | `CalibUtils`; `MachineObject` calibration queries | 16 tests, 32 results |
| `calibration_generate` | M | Generate one test's geometry and configuration onto a new plate. Analysis is reasoning over a photo or camera frame; applying a result uses `settings_apply_patch` on the filament scope with `persistAs`. | `CalibUtils` generators | |

### Retired and folded

Reads folded into `workspace_inspect` sections: project details, intent, printer state and mismatches, confirmed facts, monitoring policy, object manifest, plan, activity state, history, versions. Reads folded into `object_analyze`: region list, orientation scoring. Writes folded: `printer_facts_confirm` into `printer_setup`; `presets_save` into `settings_apply_patch` `persistAs`; transform, arrange, object edits, and plate edits into `object_place` and `plate_layout`; `slice_layers` and `slice_gcode_read` into `slice_inspect`. Not tools until an eval demands them: editing project details (a Project details drawer action), writing the monitoring policy (a Printer Configuration action), text embossing, and everything under "Deliberately not tools".

### Deliberately not tools

One tool per setting or per "make it stronger" bundle; printing advice; preferences, cloud, firmware, plugins, sync, and update installation (user-owned per surface 2.12); manual axis movement, homing, fan and light detail, printer file storage (expert layer of Monitor); painting by stroke and the measure gizmo as UI (reached through regions and analysis handles); asking the user a question (the conversation, or the `unanswered` list for external clients); a second job engine, remembered approvals, or the MCP Tasks extension; a single write tool that interprets a list of unrelated commands.

### Prerequisites the catalog imposes on the machinery

- `validate_output` has `enum` (provenance, action state, region kind, section names) and `maxLength`, so a row may use them. Input unions such as the facets of `object_place` are the decoder's job, not the schema validator's.
- Neither adapter needs new loading machinery. The projection tests need to record definition bytes per adapter, and the live regression needs to record input and cached tokens per request, so the deferral decision is evidence-based.
- The coordinator needs [grouped approvals](#grouped-approvals) and the `planId` field on every mutation.
- Three owners are trapped in presentation code and need a product-neutral seam first: mapping and readiness logic in `SelectMachineDialog` for `preflight_check`; plane and circle detection in the Measure gizmo for `object_analyze`; offscreen preview rendering for `view_render` preview mode. Import and open need the dialog choices (`loadProjectSettings`, `unitConversion`, `oversized`) reachable without the dialogs, the same hazard class as the settings normalizer.
- The JusPrin stores for intent and plan exist in `ProjectStateDocument`; regions, confirmed facts, and monitoring policy do not, and land with the tools that write them. How a write announces itself is still open. Rules under [JusPrin-owned project state](#jusprin-owned-project-state).
- The image projections under [Image results](#image-results) in both adapters before `project_attachment_read`, `view_render`, or `printer_camera_snapshot` ships.
- The Tier 1 security review under [Proportional security growth](#proportional-security-growth).

### Evals that gate delivery

1. Open a one-object STL, state "decorative, front face visible, under five hours, PLA", and reach a sent job with no manual setting edits. Needs `core`, `project`, `setup`, `geometry`, `output`. Its transcript is also the first call log for consolidation: any two tools always called together are candidates to merge.
2. A model with a precision hole and a support-heavy overhang: find the hole, annotate it as protected, show that supports no longer enter it after re-slicing, and place the seam on a hidden face. Needs `regions` and `check`.
3. Stringing reported on a Bambu printer: list tests, generate a temperature tower, read the user's photo, propose a filament temperature, preview and apply it with `persistAs`. Needs `calibration` and the filament scope.
4. Concurrent edit: the user changes a setting in the GUI while an external agent has a pending apply. The apply fails as stale and the agent recovers with one preview and one apply. Every toolset.
5. Load measurement: the live regression reports, per request, definition bytes, input tokens, and cached tokens for the full catalog, and the transcript is reviewed for wrong-tool selections. These numbers decide whether deferral is adopted.

## Process-settings tools: contract and hazards

The settings tools are the reference implementation of this guide. What
follows is the part of their design that stays true after the work is done;
the implementation handoff that produced it is retired. Re-read this section
before expanding the write allowlist or adding a tool that changes a preset.

### Standing decisions

1. Settings are data: four generic tools over searchable records, never one
   tool per setting.
2. The active FFF process preset's edited configuration is the default
   scope. `target: {objectId}` on `settings_get`, `settings_preview_patch`,
   and `settings_apply_patch` reads and writes one object's overrides in its
   `ModelConfig` instead (see "Per-object overrides" below). No printer,
   filament, plate, part, or modifier layer until each is a separately tested
   capability.
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
| `support_style` not in the set for the support type (normal: default, grid, snug; tree: default, tree_slim, tree_strong, tree_hybrid, organic) | `enable_support`, `support_type`, `support_style` | silent: style reset to default | refuse with `invalid_setting_value` and the fitting styles in `allowed`; the patch is fixed by the caller, not rewritten |
| global `enable_support` while `detect_overhang_wall` is off | `enable_support` | silent: `detect_overhang_wall` turned on, once per query state | predict and report as `normalized_dependency` |
| `spiral_mode` on with support or thin walls | `enable_support`, `detect_thin_wall` | yes/no dialog | refuse with `incompatible_settings` (the spiral row above) |
| fuzzy skin in an Extrusion or Combined mode without Arachne | `wall_generator` set to classic | yes/no dialog | refuse with `incompatible_settings` naming `fuzzy_skin_mode` |
| `Tab::on_value_change` for `support_type` in simple mode (style reset) and for the support filaments (dialogs) | none: `Tab::load_config` never calls `on_value_change`, and the filament keys are not writable | inactive for tools | nothing |
| `toggle_print_fff_options` writes (`enable_arc_fitting`, the slope segment length, `fill_multiline`, `overhang_reverse_threshold`) | none of the new keys directly; the prediction runs the real pass on the clone | silent | report whatever the clone shows |

The brim, skirt, seam, speed, surface-pattern, interface, contact-distance, threshold and shell-thickness keys are read by no rule in either function beyond enabling or disabling their own fields; the audit covered both functions and `Tab.cpp`'s `on_value_change` on 2026-09-16. Open-enum keys (the interface layer counts) carry no `allowed` list: their enum values are shortcuts, and any number in bounds is accepted. A boolean has no numeric bounds.

Every active dialog predicate must be checked, including pre-existing
invalid ironing spacing, first-layer height, XY and elephant-foot
compensation, alternate-extra-wall, infill-lock depth, and fuzzy-skin
settings; spiral mode also checks support, enforced support layers, thin
walls, overhang reversal, timelapse, and wrapping detection. The real-adapter
test asserts that no top-level dialog appears during apply for any
allowlisted key under every row above (the support, brim and style checks apply a tree-support patch with the support page shown and read the native fields back), and that every predicted silent
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

### Per-object overrides

An object target reads the value the object prints with: its own override
where `ModelConfig` has one (`overridden: true`), otherwise the process value.
A patch is checked against the process config with the object's overrides and
the patch applied, with the same parsing, bounds, dialog predicates, support
style rule, and `Slic3r::validate` as a process patch. Keys that are neither
`PrintRegionConfig` nor `PrintObjectConfig` options (ObjectList's own list
for a whole object) are refused with `unsupported_scope`. No dependencies are
predicted: Orca runs the normalizer only on the process preset and on edits
made in the object settings panel, so an override is stored as written.

Apply takes one `Plater::TakeSnapshot` ("Change object settings"), writes each
key with `ModelConfig::set_key_value`, calls
`ObjectList::object_config_options_changed` so the list shows the settings
item, and `Plater::changed_object` so that object's slicing is invalidated.
The result says `projectUndo: true`; project Undo removes the override. The
approval card names the object. The coordinator looks the object up in the
current snapshot at proposal and at execution and fails with `missing_object`
when it is gone; the adapter treats a missing target as a broken precondition.
No patch value removes an override; Undo, or the object settings panel,
does.

### Registry facts worth knowing

- `ToolExecutionCoordinator::execute` dispatches on `ToolHandler` with
  explicit blocks; add a block per tool, not a generic dispatch table.
- `kInvalidatingReasons` is the mask for most proposals and includes
  `Settings`, so a pending proposal fails with `stale_revision` when the user
  edits a setting in the GUI. `settings_apply_patch` has its own mask,
  `Settings | Project`: the M5 live eval showed an agent annotating a region
  between its settings preview and its apply, and the model edit (and the
  slicing events after it) made every apply stale. A settings patch is bound
  to the before values it previewed, which the apply checks again, so its
  `expectedRevision` only has to be no older than the last settings or project
  change; model edits do not matter.
- `ToolRegistry::validate_output` accepts a closed schema vocabulary: `type`,
  `properties`, `required`, `additionalProperties`, `items`, `minimum`,
  `maxItems`, `enum`, `maxLength`. Any other keyword throws; extend the
  validator with a test before using a new keyword. `enum` is checked whatever
  the value's type; `maxLength` counts UTF-8 bytes, matching the byte bounds
  this registry states everywhere else.
- Codex CLI needs `tool_timeout_sec` raised above its 60-second default for
  approval-gated calls.

### Upstream seams the tool system owns

Two added lines in `src/CMakeLists.txt`, eight in
`src/dev-utils/platform/unix/build_linux_image.sh.in`, and one additive
`notify_project_state_changed(ProjectStateChangeReason::Settings)` call at
the end of `Plater::on_config_change`. `GUI_Utils.hpp` gains one declaration and two lines at the top of `DPIAware::ShowModal` (`answer_modal`, defined in fork code); if upstream changes that function, keep both. The grouped Plater block gains `restore_project_history` and `can_restore_project_history`. `libslic3r/Orient.hpp` and `.cpp` gain `score_orientations`, additive beside `orient()`; it reuses `AutoOrienter` and duplicates the five lines of `process()` that build Orca's candidate set. Current upstream (72774e5398) reworks `Orient.cpp` for cooling-fan orientation and removes `OrientParamsArea`, which the adapter copies from today's `OrientJob`: the text merges cleanly, but at that rebase `score_orientations` must be rechecked against the new `AutoOrienter` and the adapter must build its parameters the way the new `OrientJob` does. `Plater.cpp` is the fork's busiest
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

The same run on the Windows verification machine, where the generator is Visual Studio and there is no `python3`:

```sh
cmake --build build --config Release --target OrcaSlicer agent_bridge_tests shell_integration_harness -- -m
build/tests/agent/Release/agent_bridge_tests.exe --order rand --warn NoAssertions
npm --prefix src/slic3r/GUI/JusPrin/AgentUI test
build/tests/shell/Release/shell_integration_harness.exe --mcp-bridge
```

Node is required: the Agent and Home pages are built from TypeScript and their `index.html` is generated, not committed, so a tree without npm fails at configure time and an app built without them shows no Agent panel at all.

The Python bridge tests run there too, with the helper named explicitly, since the default path in `tests/mcp/test_bridge_process.py` is the macOS bundle:

```sh
JUSPRIN_TEST_BRIDGE=build/src/Release/jusprin-mcp.exe python -m unittest discover -s tests/mcp
```

That run is green as of 2026-09-16. It was not before. Eight live-peer tests failed on Windows — SSE progress, client cancellation, EOF with a pending call, stdout backpressure, the live catalog's header mirroring, and a malformed peer result — and the same eight failed from commits well before the M0 work, which is why they were first recorded here as an unexamined platform gap. Examining them found a single cause, and it was the helper's, not the tests': Windows hands `main()` its arguments and `getenv()` its values in the active code page, so `fs::u8path(argv[i])` could not spell the fixture's `discovery 打印.json`, `read_discovery` never found the file, every call fell back to the offline catalog, and nothing that needs a live peer could pass. The helper now takes anything that becomes a path from `CommandLineToArgvW` and `GetEnvironmentVariableW`; POSIX argv already carries the native bytes and is unchanged. The standing rule: in helper code a path may never arrive through a narrow Windows API, and the fixture's non-ASCII name earns its keep by catching exactly this.

Nothing else in the stdio contract differs on Windows — non-blocking stdout, SSE framing, cancellation and teardown all behave as the tests assume, including twenty consecutive runs of the blocked-stdout and backpressure cases. The same run also caught `tools/list` still asserting the pre-M0 catalog size, which the milestone updated in the C++ tests and not here. Run this suite on Windows for every milestone; a macOS pass is not evidence for it.

Catalog bytes per tool, the measurement every milestone PR reports, without `python3`. Run it with `pwsh`, not `powershell`: Windows PowerShell 5.1 cannot parse a catalog whose schemas nest as deep as this one's and reports **no tools at all** rather than an error, which looks exactly like a catalog that shrank to nothing. Keep `-Depth` on both conversions.

```powershell
$H = "build\src\Release\jusprin-mcp.exe"; $D = New-Item -ItemType Directory -Path (Join-Path $env:TEMP ([guid]::NewGuid()))
@('{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"measure","version":"0"}}}',
  '{"jsonrpc":"2.0","method":"notifications/initialized"}',
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}') |
  & $H --discovery (Join-Path $D "none.json") 2>$null |
  ForEach-Object { try { $m = $_ | ConvertFrom-Json -Depth 100 } catch { return }
                   if ($m.id -eq 2) { $m.result.tools | ForEach-Object { "{0,6}  {1}" -f ($_ | ConvertTo-Json -Depth 40 -Compress).Length, $_.name } } }
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
