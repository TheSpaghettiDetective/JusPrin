# Guide for adding JusPrin tools

**Status:** Extension guide for the implemented shared registry, embedded MCP server, and stdio bridge. The MCP catalog includes workspace inspection, slice-review reporting, and the settings workflow over the process, one object, and filament and printer presets. A [candidate catalog](#candidate-catalog) derived from the product surfaces follows the implemented catalog record; nothing in it exists unless the catalog record says so.

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

External calls preserve the trusted adapter-assigned `ToolSource::Mcp` for activity history and diagnostics. They execute through the same coordinator and workspace commands as in-app calls.
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
- general printing advice the model can reason about from a skill;
- a fixed bundle such as "make it stronger" whose right answer depends on the part and user priorities;
- UI automation against a button, widget, or hidden panel;
- data already present in a bounded existing result;
- speculative future capability with no eval or user task; or
- an operation the real adapter cannot perform honestly through an authoritative Orca owner.

Deterministic workflow machinery can be a tool. Examples include generating Orca's calibration geometry or starting Orca's slice action. Advice should remain a skill until there is a separately specified deterministic operation behind it.

## Skills versus tools

A skill is text the model reads. A tool is something the app executes. A candidate is one or the other by one question: does it have to execute (read state the model does not have, compute what only Orca can, change the project), or does it only have to be read (judgement, sequencing, domain facts, how to read a tool's result, which question to ask first)? The section above gives the tool side. This section gives the skill side and rules on the cases that fall between.

A skill holds what the model could do with the right knowledge and nothing that executes: judgement over evidence (telling an adhesion failure from wet filament in a photo), the order of a sequence of tool calls and the reasons for it ("make it stronger": read the settings, move walls and infill, slice, read the report), domain facts (what PETG tolerates), and which question to ask when the answer would change the plan. The file-load instructions (`AgentUI/src/fileReportInstructions.ts`) are a skill the app injects for one turn: the first message after a load is judgement over the report, not an operation, so it is text. That is the only kind of skill built so far. A skill the model opens by name, from an index of one line each, has no loader yet; until it does, a skill is text the app injects for the turn whose situation the app knows.

The cases between:

- **A procedure over existing tools** is a skill while the sequence takes judgement. It becomes a tool only when the sequence is fixed and the person wants it as one undoable action; many such sequences go in one workflow registry (see [When resources or skills are appropriate](#when-resources-or-skills-are-appropriate)).
- **An analysis** is a tool when Orca computes it deterministically (overhang angles from geometry) and a skill when it is judgement over evidence, paired with the tool that supplies the evidence (`view_render`, `slice_report`).
- **Large reference data** is a tool that returns the part an argument selects (`settings_search`) when the model needs one part of it, and text when the model needs all of it to use any of it. The printer list under [Input design](#input-design) is the measured case where the search tool lost to the whole list in the instructions.
- **A tool description** says when to call that one tool, in the words a person would use. **A skill** says how several tools fit together and what to conclude from their results. A description that has grown into a procedure is a skill that has not been split out.

When unsure, start as a skill. A skill is a text file the page ships; a tool is the implementation sequence below, with fakes, tests and rebase evidence. Promote a skill to a tool on the evidence every tool starts from (see [Start with an eval failure](#start-with-an-eval-failure)): transcripts show the model getting the sequence wrong in ways text does not fix, or the action needs atomicity, validation or undo only the app can give. The signal from the other side: a skill that has become exact values and ordered calls with no judgement between them is a tool that has not been written yet.

Loading does not decide the question. Today a skill costs its line in the instructions and a tool costs its definition on the first request of a conversation (see [Tool budget and tool shape](#tool-budget-and-tool-shape)). Under platform deferral both are an index line and a load on demand, and a skill may then be a deferred read-only tool whose description is its index line and whose result is its text, so tools and skills share one discovery mechanism and one eval.

## Prefer extending nouns over multiplying verbs

The settings workflow uses four stable operations over searchable data, regardless of which preset layer owns the values:

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

A new verb is justified when it has meaningfully different authorization, atomicity, lifecycle, or result semantics. `slice_start` is not just another settings mutation: it starts asynchronous Orca work and has a different completion contract. That distinction deserves a tool.

Use a preview/apply pair only where Orca rewrites, substitutes, or refuses what the caller asked for, so the caller must see the real consequence before proposing it: settings (the normalizer), preset selection (compatibility substitution and lost dirty edits), topology changes (resulting pieces), and preflight (mapping and readiness). Everywhere else the coordinator computes and validates the after-state as part of the call. A preview that would only echo its input is not a tool.

Prefer typed reads with fixed schemas over one polymorphic record search. Sections of `workspace_inspect` each keep their own fixed shape; a generic `records_search` whose result shape depends on a `kind` argument makes the model guess.

## Tool budget and tool shape

Tool count is bounded by what a turn loads, not by what the app can do. The evidence behind these rules: OpenAI's function-calling guidance keeps fewer than 20 functions available at the start of a turn and defers the rest; Anthropic's [tool-writing guidance](https://www.anthropic.com/engineering/writing-tools-for-agents) asks for a few tools targeting high-impact workflows and names "tools that merely wrap existing software functionality or API endpoints" as the common error; a [domain-oriented MCP study](https://arxiv.org/html/2608.22063v1) measured a generic thin-tool interface below raw SQL and domain-shaped tools far above both; and the "god tool" that dispatches many unrelated actions through one `action` parameter is a documented anti-pattern. The current five-tool MCP catalog measures about 1.9 KB per definition, two thirds of it output schema.

- **Loading, for now.** Every registered tool loads on every turn in both adapters: the in-app adapter sends the full function list with each request, and MCP `tools/list` returns the whole catalog. This is the simplest correct behaviour and it keeps the tools array byte-identical across a chat, which is what OpenAI's prompt cache keys on. The budget of 20 definitions per turn is a measured trigger, not a gate: the live regression records definition bytes, input tokens, and cached tokens per request, and the journey catalog below will exceed 20 definitions from its first milestone. That is accepted until those measurements show selection errors or context pressure.
- **The baseline, measured on Windows against the live app.** After M5: in-app 25 definitions and 29,148 bytes, MCP 26 and 33,013 over two `tools/list` pages. After M4: in-app 20 definitions and 21,594 bytes, MCP 21 and 24,923 (24,901 through the offline helper recipe). After M3: in-app 20 definitions and 19,331 bytes, MCP 21 and 22,661 (22,639 through the offline helper recipe). After M2: in-app 18 definitions and 12,721 bytes, MCP 16 and 13,911 (13,894 through the offline helper recipe). After M1: in-app 11 definitions and 6,482 bytes, MCP 9 and 6,915. At M0 it was in-app 7 and 3,145 bytes, MCP 5 and 3,147.

  The `--live-agent` regression at both points says what that costs. The first request of a conversation caches nothing and pays for the whole catalog: 803 tokens at M0, 1,733 at M1, so four more definitions cost about 930 tokens, once. Every later request in the same conversation came back 77 to 92 per cent cached (5,864 in, 5,376 cached at M1), because the tools array is byte-identical across a chat and that is what the cache keys on. At M2 the same regression's eleven requests took 2,875 to 6,763 input tokens with 42 to 91 per cent cached; none started cold, because an earlier run of the same day had warmed the cache for the shared prefix, so the M2 cold cost is not measured by that run.

  So the cost of a definition is what it adds to the *first* request of each conversation, not to every turn, and the budget stays a trigger rather than a gate. What would change that reading: a catalog large enough to make the first request of every conversation expensive on its own, or transcripts showing the model choosing the wrong tool. Watch both; neither has appeared yet.
- **The printer panel follows the same rule with its own set.** Home's printer panel runs its own conversation (`PrinterConversation`), offered the five printer tools and the four settings tools, the same list in every mode, so the list stays byte-identical across a conversation and across conversations of the same mode. What a call may do depends on the conversation, and `PrinterConversation::preflight_tool` refuses the rest before any card, with an error that says where to go instead: finding and adding a printer (`printer_identify`, `printer_add`) only in an Add conversation, since a Change or Connect conversation is about a printer the person already has and an add there would save another printer and move the conversation onto it; `printer_change` only on the printer the conversation is about, once there is one; the connection tools only once there is a saved printer; the settings tools only with `scope: "printer"` on the printer the conversation is about. A Change conversation may be about the stock profile the project has selected (the header's Printer settings… on a project that uses one); its nozzle and connection are refused with `stock_profile`, and its settings are saved as a copy that the conversation then follows (`PrinterConversation::tool_settled`). A Change conversation is never turned into an Add one: its opener checks that the printer is there and shows Orca's error dialog when it is not. The header's filament chat (the same panel, `PrinterPanel::open_filament`) is offered the four settings tools and nothing else, with no project state, and its own instructions, which the page writes (`AgentUI/src/filamentInstructions.ts`, sent with `filament_instructions`) from the facts the app sends as the state's session (`PrinterPanel::filament_session_json`: the preset by name, its slot and material, and for a stock filament the copy a change is saved as). Its preflight holds the settings tools to `scope: "filament"` on that preset and refuses an apply without `persistAs` (`not_saved`), since the chat is gone when it closes; a copy saved in the preset's place is the preset it is about from then on, and the page writes the instructions again for it. Until 2026-09-29 it ran on the project assistant's instructions and tools, which read "make the nozzle 5 degrees hotter" as a print request, and in another run set 205 °C from 220 °C without reading it. A list that differs by mode is not used: the conversation's own refusal is needed whatever the model is shown, and a restricted-but-stable list (`tool_choice` with `allowed_tools`) exists only on some providers. What would change that: transcripts of the model reaching for a refused tool and not recovering, which `tests/printer_prompt` measures (`change-add-another`). Until 2026-09-23 the panel offered one tool per mode (7f66e75b07), to keep a lookup from clearing the pinned card of the printer a Change session was about; that card is gone (4d4f9e466b), and one tool per step left an Add conversation with no tool to connect the printer it had just added (8334b3395b). On 2026-09-28 the three tools that opened a window or closed the panel -- `printer_manual_setup`, `printer_manual_connection` and `printer_setup_finish` -- were removed (b60fc2d8e6): OrcaSlicer's full printer list and a printer's settings stay reachable from the panel's own buttons, and the person closes the panel. The app decides the tools; the words are the page's (`AgentUI/src/printerInstructions.ts` for the instructions, `AgentUI/src/printerWords.ts` for the notes the model reads after a tap), and a turn the page has not yet written them for is refused rather than sent with the project assistant's prompt. The Add instructions carry the whole printer list, about 28 KB, which caches as a fixed prefix. No printer conversation carries the project: no workspace in the turn, none in a tool result.
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
7. What happens if the user edits the GUI while the call is queued or running?

If these answers are unclear, the tool contract is not ready.

## Reuse the one authority path

Every mutation must follow this route:

```text
adapter call
→ shared registry validation
→ ToolExecutionCoordinator queue
→ immediate execution
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

- **Storage.** Project-scoped state (intent, plan, regions, conversation, and attachments) lives in the local project store under a stable project ID. The current `ProjectStateDocument` is saved independently of model checkpoints; imported legacy `Auxiliaries/JusPrin/state.json` and its recovery mirror are read for migration only. The print intent (`printIntent`, upserted by field name, each with provenance), the plan (`plan`, replaced whole) and the region annotations (`regions`, replaced whole, each record stamped with a seq when it changes) remain fields of that document. Printer-scoped state (confirmed facts, monitoring policy) stays in app data keyed by printer identity, as `PrinterFactsStore` keeps it; confirmed facts carry an expiry because the physical world changes without telling the app. See [Local autosave and version history](autosave-version-history.md).
- **Revision.** Every write announces itself so readers refresh. Which announcement, and whether it invalidates queued actions, is **open** and belongs to the milestone that lands the first writer. The obvious reading — publish a `WorkspaceChangeReasons` bit and add it to `kInvalidatingReasons` — is not implementable as stated: `settings_apply_patch` compares its caller's `expectedRevision` against the current revision for exact equality, so any revision advance between a client's preview and its apply fails as `stale_workspace`. An agent that records its plan between previewing and applying would break its own call. The edit feed (`WorkspaceEditHub`) is the precedent for a feed that reports a change without advancing the revision. Decide per kind: a region annotation generates Orca artifacts and is a real project change; an intent answer or a plan statement is not. As built, a region record's own write announces nothing; the artifacts it generates are Orca edits and advance the revision the ordinary way, so an action queued before an annotation goes stale exactly as it would after any other model edit.
- **Undo.** None of this state is in Orca's undo stack. Results say `projectUndo: false`. Region annotations generate Orca artifacts (modifier volumes, enforcers, blockers, paint) that *are* in the undo stack, so project Undo can strand an annotation without its artifacts or an artifact without its annotation. the `regions` section of `object_analyze` reports both conditions and `region_annotate` regenerates; a place, divide, or repair result lists the regions whose binding it broke.
- **Provenance.** Any field that records a fact about the world or the user's wishes carries where it came from: `file` (read from the project or profile), `observed` (a sensor or printer report, with a timestamp), `agent_inferred`, or `user_confirmed`. A tool marks a value `user_confirmed` only when the call is grounded in an explicit user statement; inferred values stay `agent_inferred`. Printer reads distinguish `configured` from `observed` for the same fact and compute `mismatches` between them rather than leaving that to the model.

## Define the contract before the implementation

Every new tool's registry entry and accompanying contract/tests must specify:

- stable lowercase underscore name;
- concise title shown in activity UI;
- compact description that says when to use the tool;
- strict input JSON Schema with `additionalProperties: false` where practical;
- output JSON Schema;
- trusted `ActionClass`;
- exposure to the in-app Agent, MCP, or both;
- availability requirements, such as an open FFF project or selected object;
- whether an action is revision-sensitive;
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
- The three in-app fixtures of the first catalog (`duplicate_object`, `import_model`, `inspect_selection`) are retired: `plate_layout` replaced the first, `object_import` the second, and the selection ids in `workspace_inspect`'s summary the third. The coordinator captures session/revision when it queues an action, invalidates queued actions on relevant workspace events, and rechecks before executing. Selection-only changes do not redirect or invalidate a pinned object target.
- For a new read-modify-write operation that must detect changes since the caller's earlier read, design and test explicit expected-session/revision inputs in its own schema and decoder. Proposal-time checks alone do not prove the caller's earlier read is current; do not retrofit required arguments onto existing tools silently.
- Batch changes that must be atomic.
- Prefer explicit selectors over magic defaults, except when the name clearly promises the current selection or active plate.
- Reject unknown fields and conflicting selectors.
- A filesystem path may cross the boundary only through a tool that validates the exact absolute path, constrains its kind and location, and documents whether it reads, creates, or overwrites. The candidate catalog has three such tools: `object_import_file`, `project_open`, and `export_file`. No read-only tool accepts a path. In-app clients keep the attachment route, which needs no path.
- Do not make the model repeat data the server can read authoritatively at execution time.
- Do not encode a large catalog as a schema enum; use bounded search and detail retrieval.
- **One exception, with its evidence: the printer list.** Identifying a printer from what someone says is the model's understanding, not a lookup, and a search tool in front of it did it badly: `printer_catalog_search` required every word of the query in a model's name and returned the first few in file order, so "voron 2.4 350mm" came back unsupported, "x1c" matched another manufacturer's printer and "prusa" showed three of thirteen. The Add session's instructions instead carry every printer the panel offers, one line each (`<catalogId> | <brand> <model> | <build volume>`, 383 lines from `PrinterCatalog::panel_printers`, sent to the page as the session's `context` and written into the instructions by `AgentUI/src/printerInstructions.ts`), and `printer_identify` checks the ids the model copies from it. Measured 2026-09-18 through the app's own request and tool results on `gpt-5.4-mini`, three runs of each case: the word test set 138/150, the photo test set 29/45, the handoff's own cases (those four queries among them) 21/21. The model tried to show more than three printers 33 times; each was refused with `too_many` and it then asked a narrowing question, so no answer ever showed three of many. The weak spots are a query that equals one model name and begins others ("bambu a1": 0/3) and photos with no readable name. The test sets and the runner are in `agent-docs/jusprin/spikes/printer-identify/` (uncommitted, beside the earlier spikes). The list is a fixed prefix of the instructions, so it caches; it is the right shape only while it stays a few hundred lines.

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

Current MCP errors include `invalid_arguments`, `cancelled`, `workspace_unavailable` and `stale_workspace`. The MCP adapter normalizes native `stale_revision`/`stale_id` to `stale_workspace` with expected/current session and revision details, and `unavailable_operation` to `workspace_unavailable`. Transport-level protocol errors are separate JSON-RPC errors. Do not require identical spelling across the native and MCP error boundaries.

The settings tools implement the following bounded correction details:

- unknown setting: canonical `unknown_setting`, original key, and a short `suggestions` list;
- invalid enum: key, diagnostic message, and valid values in `allowed`;
- out of range: key, diagnostic message, and `min`/`max` bounds; units are available from setting metadata;
- incompatible settings: conflicting keys and Orca's reason;
- stale call: expected and current session/revision, plus instruction to read again;
- unavailable operation: the requested scope has no active preset or target;

Unexpected invariant failures must remain visible to diagnostics and Sentry. Catch at an abstraction boundary only to recover, translate a known error, or add essential context.

## Read, mutation, and destructive policy

Use the registry's action class, not naming conventions:

- **Read-only:** cannot alter project, preset, disk, printer, or durable product state.
- **Mutation:** changes project, preset, or durable product state.
- **Destructive:** may delete, overwrite, discard, print, export, or restore history.

Action class remains metadata for UI wording, MCP annotations, staleness checks, and audit history. Valid tool calls start immediately after validation. A tool that previews changes is read-only only if fake and real tests prove it does not dirty presets, advance the workspace revision, create history, cancel slicing, or write files.

## Asynchronous operations

Use asynchronous behavior only when Orca's real operation is asynchronous. Do not invent a second job engine.

For slicing and later long-running actions:

1. the workspace command starts work through Orca's current owner and returns a stable action handle;
2. `ToolActivity` records running and terminal state with bounded progress;
3. Orca completion or failure advances/invalidates authoritative workspace state;
4. the consumer fetches the authoritative slice report or error after the event; and
5. cancellation calls Orca's real cancellation path and reports whether cancellation won the race.

MCP progress notifications are optional request-scoped presentation. They must be monotonic and must not be the only way to learn the terminal result. Add the MCP Tasks extension only when a real client/eval needs durable calls that outlive one request; do not add it merely because an operation takes several seconds.

The current coordinator stages execution through GUI ticks; it does not provide durable asynchronous jobs. The bridge accepts `notifications/cancelled` in both protocol eras, closes the forwarded connection, and suppresses subsequent output for that request. EOF ends the bridge session. A lost connection before the final response produces `connection_lost` with an **unknown outcome** warning, not proof the mutation was cancelled. Never automatically retry a mutation after such a loss; inspect authoritative state first.

## Tool exposure

Default to both adapters when both can satisfy the same input contract. A deliberate filter is appropriate when context is supplied differently:

- `workspace_inspect` is exposed to both adapters so either can obtain a fresh completed slice identity after slicing, independently of the initial turn context;
- attachment-based import (`object_import`) is in-app-only, and path-based `object_import_file` is MCP-only, because a file reaches the app through the conversation in one case and through an explicit path in the other; that is two honest definitions over one workspace command, not one tool with forked behavior; and
- `intent_update` and `plan_set` are in-app-only: the intent record and the pinned plan are the chat's own, and an external agent keeps what its user said, and its plan, in its own conversation. MCP still reads both through `workspace_inspect` and `slice_report`. For the same reason the in-app assistant's print guidance (`kPrintJourneyGuidance`), which names these tools, is not part of MCP's server instructions;
- a future MCP diagnostics tool may be MCP-only if it exists to establish the external connection; and
- the printer panel's five own tools carry only the `Printer` exposure: neither adapter lists them, and the panel's conversation offers all of them in every mode and refuses what does not fit it (see "Tool budget and tool shape"). and what the model reads back from them is the tool's own result, not the host's workspace envelope.

Document the reason beside the registry definition and test it. Exposure is not a place to fork behavior: if two adapters need different semantics, they need a better shared command or honestly separate definitions.

## When resources or skills are appropriate

Do not make MCP resources load-bearing until intended clients prove that model-driven resource retrieval is reliable. A bounded data-returning tool is the compatibility baseline. Resources may be added as an optional alternate projection of large, readable, stable data.

Keep printing playbooks—reducing stringing, improving strength, choosing support strategy—as skills the model can read (see [Skills versus tools](#skills-versus-tools)). Convert a workflow into an executable tool only when it maps to a named, deterministic Orca operation with defined inputs, outputs, validation, and undo/reset/cancellation behavior. If many procedural workflows eventually qualify, prefer one discoverable workflow registry over dozens of nearly identical tool definitions.

## Required implementation sequence for one new tool

1. Add or update the failing eval and expected transcript.
2. Trace the real Orca owner and record lifecycle, threading, history, and event behavior.
3. Define the typed workspace input, output, and error contract.
4. Implement the fake adapter and workspace contract tests.
5. Implement the real adapter through the smallest owner seam.
6. Prove direct GUI edits and tool edits produce equivalent observable state.
7. Add the registry definition, decoder/validator, executor association, and output schema.
8. Add coordinator tests for action class, staleness, atomicity, cancellation, and terminal activity.
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
| Coordinator | stale session/revision, cancellation race, terminal result |
| Real adapter | live Orca owner, GUI parity, truthful undo/reset/revert behavior, slicing invalidation/completion, document replacement |
| OpenAI adapter | canonical schema projection and successful continuation |
| MCP adapter | canonical schema/output projection, bounded response, protocol errors, disconnect behavior |
| Shell/eval | real project and intended client complete the user task |

If a layer does not apply, say why in the PR. "Not testable" is a visible limitation, not permission to omit evidence silently.

## Candidate catalog

This is the end-state catalog derived surface by surface from [Designing an AI-Piloted OrcaSlicer](orca-feature-discovery.md), shaped by the [tool budget and tool shape](#tool-budget-and-tool-shape) rules. Nothing in it is implemented unless the catalog record above says so, and every row still owes the eval failure, owner trace, and test matrix this guide requires. The count that matters is the number of definitions loaded per turn, not the total. The total here is 36. All of them load on every turn until the measurements under [tool budget and tool shape](#tool-budget-and-tool-shape) justify deferral; the toolsets below are the deferral units for that day and, until then, a naming and delivery grouping.

Conventions every row inherits: every read returns `sessionId` and `revision`; every revision-sensitive write takes `expectedRevision`; handles for detected faces and holes carry the revision they were computed at and fail with `feature_expired` after it moves; facts carry provenance as defined under [JusPrin-owned project state](#jusprin-owned-project-state); long-running rows follow [Asynchronous operations](#asynchronous-operations) and return an action handle whose state is read from the `slicing` section of `workspace_inspect`; every read states a hard cap and a truncation flag. Classes: **R** read-only, **M** mutation, **D** destructive.

### Toolsets and loading

| Toolset | Tools | Used for |
|---|---|---|
| `core` | 10 | Every turn: state, geometry facts, settings, intent, plan, slicing. |
| `project` | 2 | Opening, reading attachments. |
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
| `workspace_inspect` | R | The one read for current state. `sections` selects any of: `summary` (default: project identity and saved state, printer pointer, intent line, selection ids, plate and object counts, plan headline, top unresolved warning); `project` (description, designer, license, safety and usage notes, assembly, bill of materials, profile notes, attachment list, versions and recovery state, each with provenance); `intent` (the intent record with provenance and an `unanswered` list); `printer` (the selected printer: configured versus observed nozzle, plate, and filaments with timestamps, computed `mismatches`, `confirmedFacts`, job progress, temperatures, alerts, monitoring policy); `objects` (plates, objects, parts, instances, enabled, extruder, quantity, bounding box, override and region counts, print order); `plan` (the pinned plan, deviations from the profile, estimates when a slice is valid); `slicing` (state and action handle per plate, progress, invalidation); `level` concise or detail. | workspace snapshot; `MachineObject`; `PresetBundle`; JusPrin stores; `UndoRedo` | per section: 4 KB text fields, 64 attachments, 16 slots and alerts, 64 objects with cursor |
| `object_analyze` | R | Geometry facts for one object. `include` any of: `mesh` (dimensions, volume, health, units suspicion); `features` (planar face groups and cylindrical holes with revision-scoped handles); `orientations` (score each entry of `candidates`, or Orca's auto-orient candidates, for overhang area, support volume, bed contact, and which handles face down); `fit` (plate fit, overlaps, likely duplicates); `regions` (annotations and their `bindingLost` or `artifactsMissing` flags); `measure` (distances between two handles). | `TriangleMesh` statistics; `Measure` feature detection; `OrientJob` evaluation; `PartPlate` checks; JusPrin annotation store | 32 faces and holes by area; 8 candidates; 16 overlaps, duplicates, measurements; 32 regions |
| `settings_search` | R | Required `scope`; required `target` for `object`, `filament` and `printer`, forbidden for `process`; `writable` and `changedOnly` filters. `changedOnly` answers what differs from the owning preset or inherited value. | `print_config_def`; scoped edited preset or `ModelConfig`; dirty options | 25 per page |
| `settings_get` | R | Required `scope` and its exact target; origin reported as system, user preset, project edit, filament preset, printer preset, or object override. | scoped edited presets; `PresetBundle::filament_presets`; `ModelConfig` | 32 keys |
| `settings_preview_patch` | R | Required `scope` and its exact target; applies the scope-specific writable allowlist, dependencies, validation, persistence and undo semantics. | scope-specific config clone; `ConfigManipulation` where applicable; `Slic3r::validate` | 32 keys |
| `settings_apply_patch` | M | Required `scope` and the same target as preview. Optional `persistAs` is valid only for preset scopes and saves the result as a named user preset. | owning `Tab` and `PresetCollection`; `ModelConfig` for object | 32 keys |
| `intent_update` | M | Record what the user said about the print: a same-shape list of `{field, value}` over the intent record. Use `assumed` for agent inferences; only values grounded in the user's statement become `user_confirmed`. In-app only. `setupTitle` is the user's stated purpose as the chat-owned title of the setup card, with or without fields; it applies no setting. | JusPrin intent store | 32 fields, 120-character title |
| `plan_set` | M | In-app only. The agent's own statement: orientation rationale, strategy per concern, unverified assumptions, compromises and risks, confidence per decision, alternatives. | JusPrin plan store | 2 KB per field, 16 decisions |
| `slice_start` | M\* | Slice one plate or all; returns an action handle. Refuses to pre-empt a GUI-started slice unless `preempt: true`. | `Plater` reslice; `BackgroundSlicingProcess` | |
| `slice_report` | R | Check print report for a sliced plate, by `sections`: summary (time, filament per extruder as length, weight, cost); Orca warnings; supports (volume, contact area, contact with annotated regions or detected holes, removal-difficulty heuristics); seams against visible or forbidden faces; overhangs and bridges past thresholds; first-layer contact; islands; collisions and toolpath outside the bed; material changes and purge; prime tower; intent checks such as time over budget. | `Print` statistics; `GCodeProcessor` result; `PrintObject` supports and seams; JusPrin annotations | 32 findings per list by severity |

### `project`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `project_open` | D | Exactly one of `path`, `new: true`, or `versionId`. Explicit inputs for the choices Orca otherwise asks in dialogs: `loadProjectSettings` (project or keep), `unitConversion` (none, inches to millimetres); every dialog Orca raises while loading is answered with the least change and returned in the load report. Replaces the document; the activity names the path or version and any unsaved work. | `Plater` load_project and load_files; backup restore | 32 import warnings |
| `project_attachment_read` | R | One attachment by id; image or text result per [Image results](#image-results). | 3mf auxiliary directory | 2 MB image, 32 KB text |

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
| `object_place` | M | Place one object for printing. Optional facets, each independently typed, conflicting ones rejected: `faceDown` (a face handle), `autoOrientFor` (minimal support, protect a face handle, strength along an axis), `rotateDegrees`, `position`, `scale` (factor or target dimension, or `unitsFix`), `mirrorAxis`, `dropToBed`, `arrangeAfter`. One undo snapshot. Mirror and scale are named on the activity. Auto-orient runs as an Orca job and returns an action handle. Result lists regions whose binding broke. | Selection and `GizmoObjectManipulation`; `OrientJob` | |
| `plate_layout` | M | Lay out the job: `objects`, a same-shape list of `{objectId, enabled?, quantity?, plateId?, extruder?, name?}`; `plates`, a same-shape list of `{plateId?, name?, bedType?, sequence?}` where a missing id adds a plate; `arrange` (none, plate id, all, or selected) with spacing and rotation allowance. One snapshot. Arrange runs as an Orca job and returns an action handle whose terminal result lists objects that did not fit. Retires `duplicate_object`. | `Plater`, `ObjectList`, `PartPlateList`; `ArrangeJob` | 64 object rows, 16 plate rows |
| `object_import` | M | In-app only. Add a model from a chat attachment id to a target plate, with the same dialog-choice inputs as `project_open`. Retires `import_model`. | `Plater` load_files | 32 warnings |
| `object_import_file` | M | MCP only. Same contract from a path; the path is validated before the file is read. | same | same |
| `project_delete_items` | D | Delete objects, parts, plates, or region annotations by id. Result says whether project Undo covers each. | `Plater` remove; `PartPlateList` delete; annotation store | 64 ids |

### `reshape`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `object_divide_preview` | R | Dry run of `object_divide`: resulting pieces with volumes and bounding boxes, support-area change, regions that would lose binding, an image. Must be proven not to dirty the model. | cut utilities and split on a clone | 1 image |
| `object_divide` | M | Cut one object by a plane (position and normal; keep upper, lower, or both) or split it into its shells as objects or parts. The activity describes the pieces in words. | `Cut`, `ModelObject` split | |
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

Reads folded into `workspace_inspect` sections: project details, intent, printer state and mismatches, confirmed facts, monitoring policy, object manifest, plan, activity state, versions. Reads folded into `object_analyze`: region list, orientation scoring. Writes folded: `printer_facts_confirm` into `printer_setup`; `presets_save` into `settings_apply_patch` `persistAs`; transform, arrange, object edits, and plate edits into `object_place` and `plate_layout`; `slice_layers` and `slice_gcode_read` into `slice_inspect`. Not tools until an eval demands them: editing project details (a Project details drawer action), writing the monitoring policy (a Printer Configuration action), text embossing, and everything under "Deliberately not tools".

### Deliberately not tools

One tool per setting or per "make it stronger" bundle; printing advice; preferences, cloud, firmware, plugins, sync, and update installation (user-owned per surface 2.12); manual axis movement, homing, fan and light detail, printer file storage (expert layer of Monitor); painting by stroke and the measure gizmo as UI (reached through regions and analysis handles); asking the user a question (the conversation); a second job engine or the MCP Tasks extension; a single write tool that interprets a list of unrelated commands.

### Prerequisites the catalog imposes on the machinery

- `validate_output` has `enum` (provenance, action state, region kind, section names) and `maxLength`, so a row may use them. Input unions such as the facets of `object_place` are the decoder's job, not the schema validator's.
- Neither adapter needs new loading machinery. The projection tests need to record definition bytes per adapter, and the live regression needs to record input and cached tokens per request, so the deferral decision is evidence-based.
- Three owners are trapped in presentation code and need a product-neutral seam first: mapping and readiness logic in `SelectMachineDialog` for `preflight_check`; plane and circle detection in the Measure gizmo for `object_analyze`; offscreen preview rendering for `view_render` preview mode. Import and open need the dialog choices (`loadProjectSettings`, `unitConversion`, `oversized`) reachable without the dialogs, the same hazard class as the settings normalizer.
- The JusPrin stores for intent and plan exist in `ProjectStateDocument`; regions, confirmed facts, and monitoring policy do not, and land with the tools that write them. How a write announces itself is still open. Rules under [JusPrin-owned project state](#jusprin-owned-project-state).
- The image projections under [Image results](#image-results) in both adapters before `project_attachment_read`, `view_render`, or `printer_camera_snapshot` ships.
- The Tier 1 security review under [Proportional security growth](#proportional-security-growth).

### Evals that gate delivery

1. Open a one-object STL, state "decorative, front face visible, under five hours, PLA", and reach a sent job with no manual setting edits. Needs `core`, `project`, `setup`, `geometry`, `output`. Its transcript is also the first call log for consolidation: any two tools always called together are candidates to merge.
2. A model with a precision hole and a support-heavy overhang: find the hole, annotate it as protected, show that supports no longer enter it after re-slicing, and place the seam on a hidden face. Needs `regions` and `check`.
3. Stringing reported on a Bambu printer: list tests, generate a temperature tower, read the user's photo, propose a filament temperature, preview and apply it with `persistAs`. Needs `calibration` and the filament scope.
4. Concurrent edit: the user changes a setting in the GUI before an external agent's apply executes. The apply fails as stale and the agent recovers with one preview and one apply. Every toolset.
5. Load measurement: the live regression reports, per request, definition bytes, input tokens, and cached tokens for the full catalog, and the transcript is reviewed for wrong-tool selections. These numbers decide whether deferral is adopted.

## Settings tools: contract and hazards

The settings tools are the reference implementation of this guide. What
follows is the part of their design that stays true after the work is done;
the implementation handoff that produced it is retired. Re-read this section
before expanding the write allowlist or adding a tool that changes a preset.

### Standing decisions

1. Settings are data: four generic tools over searchable records, never one
   tool per setting.
2. Every settings call requires `scope`, whose enum is exactly `process`,
   `object`, `filament`, or `printer`. Missing scope is `invalid_arguments`.
   There is no default, inference from `target`, alias, or compatibility mode.
3. `target` is scope-dependent and closed:

   | `scope` | `target` | Meaning |
   |---|---|---|
   | `process` | forbidden | the active edited FFF process preset |
   | `object` | required `{objectId}` | that object's `ModelConfig` overrides |
   | `filament` | required `{preset}` | that filament preset, by name |
   | `printer` | required `{preset}` | that printer preset, by name |

   These rules apply to `settings_search`, `settings_get`,
   `settings_preview_patch`, and `settings_apply_patch`. Reject an extra,
   missing, empty, or wrong-shaped target as `invalid_arguments`.
   A preset is named the way Orca keys it: by its name, unique within its
   own collection (`Preset.hpp`: "The preset name shall be unique across a
   single PresetCollection"). Never by the label a menu shows -- the header
   shows a stock printer's model, and `Preset::label` puts `"* "` in front of
   a preset with unsaved edits -- and never by an alias. A name no visible
   preset has is `unknown_preset`, whose suggestions are the project's
   filaments or the printer in use. (The first draft of this contract named a
   filament by its 1-based project slot and edited only the selected printer;
   on 2026-09-29 Kenneth chose the preset's name for both, so a chat opened
   for one preset, and a saved printer the project does not use, can be
   changed without selecting anything first.)
4. Scope identifies the settings owner; `printer_setup` remains the only tool
   that picks the project's printer, process, or filament. A settings call
   never discards dirty edits, changes nozzle topology, or substitutes a
   compatible profile. It selects a preset only where Orca's own Save or edit
   path does: a copy saved with `persistAs` takes the original's place, and a
   filament that is not the one open in its tab is opened there the way the
   sidebar's edit button opens it (`PlaterPresetComboBox::switch_to_tab`).
5. Metadata, parsing, and serialization come from Orca's own
   `print_config_def` and config option machinery. There is no parallel table
   of types, enum values, ranges, units, or aliases.
6. Search and read cover every definition applicable to the requested scope;
   mutation has a separate reviewed allowlist per scope. Every search and read
   record says whether the key is writable; other keys return
   `unsupported_setting_mutation`. Expand an allowlist only after real-adapter
   tests cover the option type, dependency behavior, visible UI update, dirty
   state, persistence and undo/revert behavior for that scope.
7. A batch applies whole or not at all, through `Tab::load_config` or the
   scope's existing owner path. Never mutate a config behind the visible
   preset UI and imitate the notifications.
8. Process, filament, and printer preset edits are not in project Undo.
   Results say `projectUndo: false` and return previous values so a caller can
   propose an inverse. Object overrides use one Orca snapshot and return
   `projectUndo: true`.
9. Every result echoes the validated `scope`, the owning `presetName` (the
   process preset for an object), `sessionId`, and `revision`. An apply also
   returns `savedAs`, the name it saved under or empty, and `presetDirty`.
10. A preview that finds invalid settings is a successful call with
   `valid: false`. Malformed input is a tool error.
11. All values cross JSON as strings in their canonical Orca serialization;
   callers may send numbers or booleans and the decoder converts them. All
   Orca ids cross JSON as strings.

The schema change is intentionally breaking. This interface does not need a
backward-compatibility period: update the in-app adapter, MCP projection,
deterministic agent, fixtures, and eval prompts in the same change. Do not add
a protocol-version branch, feature flag, alias, fallback, or migration path
for the old missing-scope or implicit-object forms. After the change, every
caller must send the new contract.

### Scope-specific ownership

- **Process:** the active `prints.get_edited_preset()` and
  `Tab(Preset::TYPE_PRINT)`. The implemented normalizer, validation, dirty
  state, preview binding, and inverse-patch rules below continue to apply.
- **Object:** the selected `ModelObject`'s `ModelConfig`, identified only by
  `target.objectId`. The implemented project-Undo behavior below continues to
  apply.
- **Filament and printer, the preset being edited** (the one selected in its
  collection): its edited copy and its `Tab`, as for the process. Reads
  include its unsaved edits.
- **Filament, another preset:** must be in one of the project's slots
  (`not_in_project`), and changes only by being saved (`not_selected` without
  `persistAs`). Apply opens it in the filament tab as the sidebar's edit
  button does -- `Tab::select_preset`, then `set_filament_idx` for its slot --
  then loads and saves through the tab. If the filament open in the tab has
  unsaved edits, that switch would discard or prompt, so preview refuses with
  `unsaved_edits`.
- **Printer, another preset:** a saved printer the project does not use,
  saved in place only (`Printers::write_unselected_printer`, the write
  `change_named_printer_nozzle` already makes: the difference from its parent,
  queued for the cloud). A copy of it would be a second printer made behind
  the project's back, so a different `persistAs` is `not_selected`.
- The printer allowlist (`writable_printer_setting`) is custom g-code,
  printable height, layer-height limits, Z offset, retraction and machine
  limits. It excludes the keys that decide which filament and process fit, or
  how many extruders there are -- bed shape, nozzle size and type, extruder
  count, printer model -- which change with `printer_setup` or
  `printer_change`. The filament allowlist (`writable_filament_setting`) is
  temperatures, cooling, flow, pressure advance, volumetric speed, density,
  cost and custom g-code.
- A setting that holds one value per extruder or per nozzle kind keeps its
  length: a patch with a different number of values is
  `invalid_setting_value`, since a shorter list would drop the rest.

`persistAs` is invalid for `object`. It is Orca's Save with the name typed
in: the preset's own name overwrites it (`read_only_preset` for one Orca
ships, with `"<name> - Copy"`, the name SavePresetDialog offers, as the
suggestion); another name that no preset has saves a copy that takes the
original's place, in every project slot that used it for a filament
(`Sidebar::update_presets_from_to`); a name another preset has is
`name_taken`; a name SavePresetDialog would refuse is `invalid_preset_name`.
Saving the preset being edited also saves its other unsaved edits, as Save
does; preview warns with `saves_unsaved_edits` and lists them. The card's
title says where it saves ("; save it", or "; save as \"<name>\""). Without
`persistAs`, the tool changes only the edited preset and leaves it dirty.

Dialogs the filament and printer tabs would open are refused in preview:
a max volumetric speed below 0.5 (`ConfigManipulation::
check_filament_max_volumetric_speed`), a first-layer temperature further
from the other layers' than `Tab::validate_filament_temperature_pairs`
allows when saving, and firmware retraction with a wipe that retracts less
than 100% first (`TabPrinter::toggle_options`).

### Scope expansion acceptance criteria

- The registry exposes the same required `scope` enum and target shape through
  the in-app and MCP adapters; catalog tests pin both projections.
- Argument tests reject omitted or unknown scopes and every missing, extra,
  zero-based, mixed, or malformed target. No test accepts the old call shape.
- Search, get, preview, and apply each have fake- and real-adapter coverage for
  all four scopes, including a readable but unwritable key.
- Filament tests cover a preset in a slot that is not open in its tab, a copy
  taking its slot, and a dirty preset that cannot be changed without an
  implicit switch or discard.
- Printer tests prove that a normal writable value updates the native field,
  dirty marker, slice invalidation, and read-back value, while topology and
  compatibility keys remain unwritable.
- Apply tests prove atomic failure, scope-specific activity text, preset revert
  or inverse-patch behavior, and project Undo only for object overrides.
- Evals and deterministic-agent fixtures use explicit scopes before the old
  schema is removed; there is no dual-schema phase.

### Error codes

| Condition | Code |
|---|---|
| missing or unknown `scope`; forbidden, missing, or malformed `target`; `persistAs` for an object | `invalid_arguments` |
| no visible preset of that scope has the target's name | `unknown_preset` with suggestions |
| a filament or printer preset that is not being edited, without `persistAs`, or a copy of a printer not in use | `not_selected` |
| a filament that is in none of the project's slots, changed by name | `not_in_project` |
| `persistAs` overwriting a preset Orca ships | `read_only_preset`, suggesting `"<name> - Copy"` |
| `persistAs` naming another existing preset | `name_taken` |
| `persistAs` SavePresetDialog would refuse | `invalid_preset_name` |
| key not in the definition table | `unknown_setting` with suggestions |
| known key not applicable to the requested scope | `unsupported_scope` |
| readable key outside that scope's allowlist | `unsupported_setting_mutation` |
| requested object is gone, or no FFF printer or process is selected | `workspace_unavailable` |
| opening a filament in its tab would discard another dirty edited preset | `unsaved_edits` |
| parse failure, bound violation, or layer height outside the printer's range | `invalid_setting_value` with `allowed` or bounds |
| a scope-specific dependency or validator rule conflicts | `incompatible_settings` with the conflicting keys |
| session, revision, target mapping, preset, or before value changed since preview | `stale_workspace` with expected and current |
| client cancelled or the app closed | `cancelled` |
| the batch call failed after validation | `execution_failed` |

Preview never fails for content reasons. Apply fails with the first blocking
code and mutates nothing. Never convert an invariant failure into success.

### Orca entry points the tools use

- **Process atomic batch:** `Tab::load_config(const DynamicPrintConfig&)` in
  `src/slic3r/GUI/Tab.cpp` diffs against the edited preset, sets every changed
  key, then runs `update_dirty()`, `reload_config()`, and `update()` once.
  `TabPrint::update()` runs the FFF normalizer and reaches
  `Plater::on_config_change`, which invalidates slicing as a sidebar edit
  would. Apply is therefore: build a config holding only the changed keys and
  call `wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff)`.
- **Filament and printer batches:** use the same `Tab::load_config` owner on
  `Preset::TYPE_FILAMENT` or `Preset::TYPE_PRINTER`, but only after preview
  models that tab's dependencies, compatibility consequences and dialog
  predicates. A scope is not complete until a real-adapter test proves the
  native fields, dirty marker, slicing invalidation and read-back values.
- **Parsing:** clone the scoped config and `set_deserialize(key, text)` per
  key; Orca throws for unknown keys and unparseable values. Canonical value is
  `option->serialize()` after parsing.
- **Bounds and enums:** `ConfigOptionDef` in `src/libslic3r/Config.hpp`
  (`type`, `label`, `category`, `tooltip`, `sidetext`, `min`, `max`, `mode`,
  `readonly`, `enum_values`, `enum_labels`). Treat `min` and `max` as absent
  when they are the float limits.
- **Validation:** build the full effective configuration with the scoped clone
  installed, then run the validator appropriate to that owner. Process and
  object continue to use `Slic3r::validate(const FullPrintConfig&)`. Do not
  assume that process normalization covers filament or printer dependencies.
- **Dirty state:** use the owning `PresetCollection`'s dirty and
  different-from-parent APIs. The result must name the owning preset and
  whether it is dirty after the call.

### The process normalizer can open dialogs and rewrite values

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

### Apply binds the previewed values

Apply is previewed three times. The client previews and sees before and
after values. When queuing the action the coordinator previews again on the GUI
thread, so an invalid patch fails at once, and stores that result as the
previewed set. The activity title names the keys from it. At execution the adapter previews a third time
against the then-current config and compares every before value against the
confirmed set: a moved value is `stale_workspace`, a blocking issue is
`invalid_settings`, and neither mutates. Client-supplied values are never
trusted as before values. Where Orca normalized a value differently from the
preview, the actual value is returned and the key is listed under
`normalized`; the mutation happened, so that is a success with an honest
report.

### Per-object overrides

`scope: "object"` with `target: {"objectId": "..."}` reads the value the
object prints with: its own override where `ModelConfig` has one
(`overridden: true`), otherwise the process value.
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
The result says `projectUndo: true`; project Undo removes the override. The activity names the object. The coordinator looks the object up in the
current snapshot when queued and at execution and fails with `missing_object`
when it is gone; the adapter treats a missing target as a broken precondition.
No patch value removes an override; Undo, or the object settings panel,
does.

### Registry facts worth knowing

- `ToolExecutionCoordinator::execute` dispatches on `ToolHandler` with
  explicit blocks; add a block per tool, not a generic dispatch table.
- `kInvalidatingReasons` is the mask for most queued actions and includes
  `Settings`, so a queued action fails with `stale_revision` when the user
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

### Upstream seams the tool system owns

Two added lines in `src/CMakeLists.txt`, eight in
`src/dev-utils/platform/unix/build_linux_image.sh.in`, and one additive
`notify_project_state_changed(ProjectStateChangeReason::Settings)` call at
the end of `Plater::on_config_change`. `GUI_Utils.hpp` gains one declaration and two lines at the top of `DPIAware::ShowModal` (`answer_modal`, defined in fork code); if upstream changes that function, keep both. `Plater::priv::load_files` gains a first-line `FileLoadScope` and a call after `load_config_model` to mark settings actually applied. The file-open entry points gain `FileOpenScope` to include choices before loading begins. The two model metadata resets at the start of `Plater::load_project` were moved below its cancelable checks so an aborted open leaves the project intact; carry those resets to that later point on rebase. `FileLoads.hpp` is reached through the neutral `ProjectState.hpp` forwarding header. `libslic3r/Orient.hpp` and `.cpp` gain `score_orientations`, additive beside `orient()`; it reuses `AutoOrienter` and duplicates the five lines of `process()` that build Orca's candidate set. Current upstream (72774e5398) reworks `Orient.cpp` for cooling-fan orientation and removes `OrientParamsArea`, which the adapter copies from today's `OrientJob`: the text merges cleanly, but at that rebase `score_orientations` must be rechecked against the new `AutoOrienter` and the adapter must build its parameters the way the new `OrientJob` does. `Plater.cpp` is the fork's busiest
file; when that function changes upstream, the natural resolution is "keep
both". No line in `Tab.cpp`, `ConfigManipulation.cpp`, or `PrintConfig.cpp`
changes; the normalizer hazards are handled by refusing the inputs in
fork-owned code.

## Proportional security growth

The server starts automatically, binds numeric loopback, validates browser origins, and enforces request limits. It has no bearer authentication or per-call confirmation. Any local process that can reach the socket can execute exposed tools, including mutations and validated file operations. Origin validation protects the browser boundary; it does not identify local processes. Never expose this endpoint beyond loopback.

Revisit the security design before adding remote access, cross-restart grants, multiple permission levels, camera data, printer upload, or physical print start. At that point evaluate authenticated pairing, durable credential storage, per-client scopes, revocation, rate controls, privacy review, and a dedicated audit log.

Caller-supplied paths remain narrowly constrained. `project_open` and `object_import_file` accept absolute readable paths; `export_file` requires the expected extension, an existing parent directory, an explicit `overwrite` flag for replacement, and refuses OrcaSlicer's data and temporary project directories. `project_attachment_read` resolves only an attachment ID inside the project attachment directory. These constraints reduce accidental scope; they are not a substitute for authenticating local clients.
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

The workspace adapter harness runs there too, and passes (617 checks, exit 0, as of 2026-09-17). Put the app's `src\Release` on `PATH`, or the exe cannot load the `api-ms-win-crt` DLLs, and read the exit code with `$LASTEXITCODE` in PowerShell:

```powershell
$env:PATH = "$PWD\build\src\Release;$env:PATH"
build\tests\workspace\Release\workspace_adapter_integration_harness.exe
```

On 2026-09-16 it failed on Windows: `select_object(0)` returned false after `load_files`, seven checks failed, and the process died with `0xC0000005`. The test code was not stale. The cause was the machine setup. The Mesa provisioning script put `opengl32.dll` only in `mesa\`, where only the app launcher looks for it. A harness has no launcher, so this one ran on the system's GL 1.1 and its canvas never initialized. Selection is built from the canvas's volumes, so nothing could be selected, and the gizmo check then opened a paint gizmo with nothing selected. The script now also copies `opengl32.dll` beside each harness (see "Software GL on Windows machines without a GPU" in [engineering-method.md](engineering-method.md)); re-run it on existing build trees. With GL in place, two harness faults surfaced that only Windows shows:

- The move-event check asserted that the move was the last change. On Windows, queued `EVT_GLCANVAS_OBJECT_SELECT` events publish two Selection-only changes after it, so the check now skips those.
- The data-directory cleanup threw, because the open log file cannot be deleted, and the uncaught exception turned a PASS into `0xC0000409`. It now warns instead, as the shell harness already did.

Pulling the script change does not fix a build tree that was already provisioned; run the script on it again. So that a tree without GL cannot fail this way unnoticed again, the harness now checks, right after its first model load, that the canvas initialized GL. If it did not, the harness stops with `HARNESS ERROR the 3D canvas did not initialize OpenGL` and the command to fix it, exits 1, and does not crash. A clean exit on that early path also needed the harness to close the main frame, as the shell harness does. It used to call `ExitMainLoop()`, which skips `MainFrame::shutdown()`, and a pending project backup then posted to the deleted frame.

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

Executed evidence includes native reads, mutations, Undo, staleness, cancellation and shutdown checks; real Claude Code and Codex CLI reads, writes, changed-port restart and offline errors using session-only configuration; and Linux ARM64 container tests of the production helper and a compressed AppImage transport fixture. Those Linux tests are not a full GUI release image. A macOS Quit attempt during a setup fixture retained the monitor and later reaped the timed-out children; that does not prove forced OS shutdown behavior.

Still unverified: persistent one-click writes against real client installations; desktop-client read/write/restart/offline workflows (including local Cowork availability); complete Windows/Linux release packages; macOS universal signing/notarization; and remaining DPI/live-theme checks. The development macOS bundle failed strict signature verification, and the existing CI signing step is restricted to the upstream OrcaSlicer repository. Do not equate a successful local CLI test, a packaging rule, or the presence of signing identities with passing these gates.

## Definition of done

A new tool is done when:

- it fixes a named eval failure;
- its scope and authority are honest;
- the registry is the only definition source;
- the coordinator owns policy and lifecycle;
- the real operation uses Orca's current owner on the GUI thread;
- results and errors are structured and bounded;
- mutation history, staleness, events, and cancellation behave as documented;
- fake, real-adapter, adapter, protocol, and shell evidence pass as applicable;
- no unrelated Orca behavior changes when the JusPrin feature is disabled; and
- the PR records any upstream seam and its rebase evidence.

Tool count is not the success metric. A compact catalog that reliably completes real printing tasks is.
