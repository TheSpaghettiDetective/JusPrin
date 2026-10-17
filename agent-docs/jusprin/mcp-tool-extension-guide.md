# Guide for adding JusPrin tools

**Status:** This guide explains how to add a tool to JusPrin. The three things it builds on are already implemented: the shared tool registry, the MCP server embedded in the app, and the stdio bridge (a small helper program that connects external AI clients to that server). Over MCP, the tools currently cover inspecting the workspace, reporting on a slice for review, and the settings workflow. The settings workflow works on the process preset, on one object, and on filament and printer presets. Later in this guide there is a [candidate catalog](#candidate-catalog): a list of tools we might build, worked out from the product's screens. It follows the implemented catalog record. Nothing in the candidate catalog exists unless the catalog record says it does.

JusPrin has one tool system. A new capability is defined once, in the shared registry. `ToolExecutionCoordinator` runs it. Its implementation reads and changes the open project through the typed workspace interface. Two adapters, one for OpenAI and one for MCP, only translate that single definition into the format each side expects. The aim of this guide is a tool catalog that stays small, does only what it claims to do, can change safely over time, and is shaped by real printing tasks. It should not be shaped by a list of every feature OrcaSlicer has.

Treat this guide as the starting point. Change it when what you learn from implementing, from real MCP clients, or from evals shows that a rule, a contract, a limit, or a sequence here should change. Do not keep a planning assumption just because it is written here. Base every revision on evidence, and describe significant changes in the PR. Two things do not change: OrcaSlicer stays the owner of the state it owns, and the verification requirements stay.

Before you touch any OrcaSlicer class that holds live state, read [the production documentation](README.md), [production architecture](architecture.md), and [OrcaSlicer integration guide](orca-integration-guide.md). The current tool definitions are in [`ToolRegistry.cpp`](../../src/slic3r/GUI/JusPrin/Agent/ToolRegistry.cpp). The code that runs them is in [`ToolExecutionCoordinator.cpp`](../../src/slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.cpp). The code that presents them over MCP is in [`McpProtocol.cpp`](../../src/slic3r/GUI/JusPrin/Mcp/McpProtocol.cpp).

External clients on the same machine connect through the bundled `jusprin-mcp` stdio helper, which is set up from **Connect AI tools...**. The helper and the server inside the app are compiled from the same registry. So every new tool you offer must have the same catalog entry and the same schema whether the app is running (live) or not (offline), for the same build and the same negotiated protocol version. Do not add a catalog that exists in only one adapter. Connecting to the server directly over HTTP is a route for developers. It is not what the default setup instructions tell people to do.

## Words this guide uses

- **Registry.** The one list of tool definitions, compiled into the app (`ToolRegistry.cpp`).
- **Catalog.** The list of tools as a caller sees it.
- **Coordinator.** `ToolExecutionCoordinator`, the class that queues tool calls and runs them on the GUI thread.
- **Workspace.** The typed C++ interface (`IWorkspace`) through which JusPrin reads and changes the open project. It has two implementations, called workspace adapters. The real one works on live OrcaSlicer. The fake one is for tests.
- **Tool adapter.** Code that translates registry definitions and results for one kind of caller. There are two: the in-app adapter, which uses OpenAI's format and serves the Agent panel, and the MCP adapter, which serves external clients. "Both adapters" in this guide means these two.
- **Projection.** The form in which one tool adapter presents a definition or a result.
- **Exposure.** The registry setting that says which callers are offered a tool.
- **Owner.** The OrcaSlicer class that holds the real copy of some state, or that already implements an operation.
- **Seam.** A small addition to a file OrcaSlicer owns, which lets fork code call or observe OrcaSlicer's behavior.
- **Session and revision.** `sessionId` identifies the project that is currently open. `revision` is a counter the workspace raises when its state changes. A call that was prepared against an older session or revision is called stale.
- **Action class.** Whether a tool is read-only, a mutation (it changes something), or destructive.
- **Dirty.** A preset is dirty when it has unsaved edits.
- **Eval.** An automated test that gives a model a task and checks the transcript that results.
- **M0 to M5.** The milestones in which the tool catalog was delivered.

## Current connection and execution boundary

- The server starts automatically with the JusPrin Agent panel. There is no `JUSPRIN_MCP` switch to enable it and no bearer token. The server listens only on the numeric loopback address. It prefers port 47301, and falls back to a port chosen by the operating system only when 47301 is taken.
- The shell tells the server where its discovery file is: `<data_dir>/jusprin/mcp.json`. The running server writes its details there in one atomic step: the actual address, the PID, a unique `instanceId` for this run, the build version, and the protocol versions. Before a server removes the record, it checks, while holding the discovery lock, that the record is its own. An older server therefore cannot remove a newer server's record, even inside the same process. On POSIX systems the file has mode `0600`.
- The helper checks that the discovery file is valid, and then tests that the address in it answers. When the app starts or restarts later, the helper follows the new record. The helper never launches JusPrin. The discovery file holds one record, and the last server to write it wins. So the helper works with one selected instance; there is no way to choose among several. `--discovery`, `JUSPRIN_MCP_DISCOVERY`, and `JUSPRIN_MCP_URL` override the file path or the address. They do not switch anything on. The connection entries that Connect writes use `--discovery` with the app's actual data directory.
- When the app is not running, the helper answers discovery and tool-list requests from the registry compiled into the helper. Tool calls made while the app is not running return `workspace_unavailable`. When the app can be reached, the helper uses the app's live catalog and results, even when the app and the helper are different versions. Do not replace a live app's tool list with the helper's compiled one. A version difference between the two is logged.
- The HTTP adapter accepts MCP protocol version `2026-07-28`. The helper can also negotiate the older versions `2025-03-26`, `2025-06-18`, and `2025-11-25`. The helper does all conversion between versions. Code that executes a tool must not behave differently for different protocol versions.
- **Connect** shows the person the command or the file edit and asks for confirmation first. Then it either runs the Claude Code or Codex CLI, or merges the JusPrin entry into the JSON configuration of Claude Desktop/local Cowork, Cursor or VS Code. A JSON edit keeps unrelated settings and makes a backup. It is refused when the preview is out of date or when the destination is a symlink. It does not protect against another editor writing the same file at the exact moment of replacement.
- On macOS the helper is bundled next to the app executable. Windows has a matching install rule. On Linux, the entry for an AppImage uses the installed image's path plus `--mcp-bridge`, and the app handles that flag before the GUI starts. Do not save a temporary AppImage mount path as the command.

Workspace commands run on the GUI thread, through the shared coordinator. A timer owned by the shell keeps execution moving, so execution does not depend on the WebView's handshake. No external client ever gets direct access to OrcaSlicer objects.

A call from an external client is tagged `ToolSource::Mcp`. The MCP adapter sets that tag, so it can be trusted, and it is kept for the activity history and for diagnostics. External calls run through the same coordinator and the same workspace commands as calls from the in-app Agent. The change log records the edits they make, as it records every edit, and marks them `external`. In the conversation thread those edits appear as rows that say "External tool", where a change made in the app says "Agent".

## Start with an eval failure

The fact that OrcaSlicer has a menu item or a config key is not a reason to add a tool. Start from a transcript or an automated eval in which the Agent cannot finish a real user task, finishes it only some of the time, or needs an unreasonable number of steps or tokens.

Write down:

- what the user wanted to achieve;
- the exact point where the current tools stop being enough;
- which OrcaSlicer state or operation is missing;
- what a successful transcript should look like;
- the unsafe or ambiguous cases that must fail; and
- the evidence that will show the new tool fixed the problem.

If the failure can be fixed by a clearer description, a better result, or a more useful validation error, improve the existing definition instead of adding a tool.

The candidate catalog later in this guide lists tools we might build. Being listed there does not accept a tool, and it does not excuse a tool from this section. A row in that catalog does two jobs. It records which product screen and which group of evals the tool would serve, which makes the eval easier to write. It also settles what the tool acts on, its action class, and its OrcaSlicer owner, so that two people do not build the same capability twice under different names.

## Decide whether this should be a tool

Add or extend a tool when the Agent needs one of three things: up-to-date state read straight from OrcaSlicer, a calculation that OrcaSlicer does and that always gives the same answer for the same input, or a change to the live project.

Do not add a tool for:

- a single setting key. Setting keys are records that the generic settings tools search and edit;
- general printing advice that the model can work out from a skill;
- a fixed bundle of changes such as "make it stronger", where the right answer depends on the part and on what the user cares about;
- driving the UI, such as pressing a button or operating a widget or a hidden panel;
- data that an existing tool already returns in a size-limited result;
- a capability someone might want in the future, with no eval or user task behind it; or
- an operation that the real workspace adapter cannot truly perform through the OrcaSlicer class that owns it.

A fixed, mechanical step of a workflow can be a tool. Examples are generating OrcaSlicer's calibration geometry and starting OrcaSlicer's slice action. Advice should stay a skill until there is a separately specified operation behind it that always behaves the same way.

## Skills versus tools

A skill is text the model reads. A tool is something the app executes. To decide which one a candidate is, ask one question: does it have to run, or does it only have to be read?

- It has to run if it reads state the model does not have, computes something only OrcaSlicer can compute, or changes the project. That makes it a tool.
- It only has to be read if it is judgement, the order to do things in, facts about printing, how to interpret a tool's result, or which question to ask first. That makes it a skill.

The section above covers tools. This section covers skills, and decides the cases that fall in between.

A skill contains things the model could do by itself if it had the right knowledge. It contains nothing that executes. Examples:

- judging evidence, such as telling an adhesion failure from wet filament in a photo;
- the order of a series of tool calls, and the reasons for that order. For "make it stronger": read the settings, change walls and infill, slice, read the report;
- facts about printing, such as what PETG tolerates;
- which question to ask when the answer would change the plan.

A skill is a file on the Agent page, `AgentUI/src/skills/<name>/SKILL.md`: frontmatter with its `name` and a `trigger`, a `description` or both, then the text the model gets. The page sends every skill once per connection with the `skills` bridge message. A skill with a trigger is the app's: when the event it names happens, the host adds it to the one request where the model first reads about the event, as the request's last item, and to no later request. A skill with a description is the model's: `skill_read` lists the described skills in its own description and returns one by name, and is offered only while at least one exists (none does yet). The first skill is `model-opened-or-imported` (trigger `model_arrived`): the first message after a model arrives is a judgement about the load report. It fires when the person opens or drops a file, after an in-app `object_import` (and a failed `project_open`) in the same turn, and in the opened project's chat after an in-app `project_open`, which replaces the conversation and so ends the turn that asked. An MCP client's loads fire nothing. The host sends a picture of the active plate with it and, when no purpose is recorded, one sentence of its own built from that state.

The cases in between:

- **A procedure made of existing tool calls.** It is a skill for as long as carrying it out takes judgement. It becomes a tool only when the steps are fixed and the person wants the whole thing as one action that can be undone. If there are many such fixed sequences, they go in one workflow registry (see [When resources or skills are appropriate](#when-resources-or-skills-are-appropriate)).
- **An analysis.** It is a tool when OrcaSlicer computes it and always gets the same answer, such as overhang angles from geometry. It is a skill when it is a judgement about evidence. Pair that skill with the tool that supplies the evidence (`view_render`, `slice_report`).
- **Large reference data.** When the model needs only one part of the data, make it a tool that returns the part an argument selects, as `settings_search` does. When the model needs to see all of the data before it can use any of it, make it text. The printer list described under [Input design](#input-design) is the case we measured: putting the whole list in the instructions worked better than the search tool.
- **A tool description versus a skill.** A tool description says when to call that one tool, in the words a person would use. A skill says how several tools fit together and what to conclude from their results. If a tool description has grown into a step-by-step procedure, move the procedure out into a skill.

When you are not sure, start with a skill. A skill is a text file that ships with the page. A tool costs the whole implementation sequence described below, including fakes, tests, and rebase evidence. Turn a skill into a tool only on the kind of evidence every tool starts from (see [Start with an eval failure](#start-with-an-eval-failure)). Either transcripts show the model getting the sequence wrong in ways that better text does not fix, or the action needs something only the app can give: all-or-nothing application, validation, or undo. The opposite sign exists too. If a skill has turned into exact values and calls in a fixed order, with no judgement between them, it should be written as a tool.

How something gets loaded into the model's context does not decide whether it is a skill or a tool. Today a skill costs its one line in the instructions, and a tool costs its definition on the first request of a conversation (see [Tool budget and tool shape](#tool-budget-and-tool-shape)). If we later use the model platform's deferred loading, both will cost one index line plus a load when needed. A skill could then be a deferred read-only tool: the tool's description is the skill's index line, and the tool's result is the skill's text. Tools and skills would then be found the same way and covered by the same eval.

## Prefer extending nouns over multiplying verbs

The settings workflow has four operations that do not change. They work on searchable data, whichever preset layer the values belong to:

```text
settings_search → settings_get → settings_preview_patch → settings_apply_patch
```

Do not create one tool per setting:

```text
set_layer_height
set_wall_loops
set_infill_density
set_support_angle
```

The same rule applies to presets, project objects, plates, warnings, and slice reports. Extend the typed records that describe them, and the size-limited tools that query them, before you create dozens of special-purpose actions.

A new operation deserves its own tool when it really differs from the existing ones in at least one of these ways: which action class it belongs to (read-only, mutation or destructive), whether it applies completely or not at all, how it starts, runs, and ends, or what its result means. `slice_start` is an example. It is not one more way to change settings. It starts OrcaSlicer work that runs in the background, and it completes in a different way from a settings change. That difference justifies a separate tool.

Split an operation into a preview tool and an apply tool only where OrcaSlicer may rewrite, replace, or refuse what the caller asked for. In those cases the caller has to see what would really happen before submitting the change. There are four such cases:

- settings, because OrcaSlicer's normalizer (the code that adjusts dependent settings after an edit) can rewrite values;
- preset selection, because OrcaSlicer may substitute a compatible preset, and dirty edits may be lost;
- topology changes such as cutting, splitting, or merging, because the caller needs to see the resulting pieces;
- preflight (the check before a print is sent), because of filament mapping and printer readiness.

Everywhere else, the coordinator computes and validates the resulting state as part of the call. If a preview would only repeat its input back, do not make it a tool.

Prefer read tools whose results have a fixed, typed shape. Avoid one general record search whose result changes shape. Each section of `workspace_inspect` keeps its own fixed shape. A generic `records_search` whose result shape depends on a `kind` argument would make the model guess what comes back.

## Tool budget and tool shape

The limit on the number of tools comes from how much the model has to load on each turn. It does not come from how much the app can do. The rules in this section rest on this evidence:

- OpenAI's function-calling guidance keeps fewer than 20 functions available at the start of a turn and defers the rest.
- Anthropic's [tool-writing guidance](https://www.anthropic.com/engineering/writing-tools-for-agents) asks for a few tools aimed at high-impact workflows. It names "tools that merely wrap existing software functionality or API endpoints" as the common error.
- A [domain-oriented MCP study](https://arxiv.org/html/2608.22063v1) measured three interfaces. A generic interface of thin tools scored below raw SQL. Tools shaped around the domain scored far above both.
- The "god tool", one tool that dispatches many unrelated actions through a single `action` parameter, is a documented anti-pattern.

The current five-tool MCP catalog measures about 1.9 KB per definition, and two thirds of that is output schema.

**How tools are loaded for now.** Both adapters load every registered tool on every turn. The in-app adapter sends the full function list with each request, and MCP `tools/list` returns the whole catalog. This is the simplest behaviour that is correct. It also keeps the tools array byte-identical through a chat, and OpenAI's prompt cache depends on exactly that. The budget is 20 definitions per turn. Going over 20 does not block anything. It is the point at which we start watching the measurements. The live regression test records definition bytes, input tokens, and cached tokens for each request. The journey catalog below goes over 20 definitions from its first milestone. We accept that until those measurements show the model selecting wrong tools or running short of context.

**The baseline, measured on Windows against the live app.**

| After milestone | In-app definitions | In-app bytes | MCP definitions | MCP bytes | MCP bytes through the offline helper recipe |
|---|---:|---:|---:|---:|---:|
| M0 | 7 | 3,145 | 5 | 3,147 | |
| M1 | 11 | 6,482 | 9 | 6,915 | |
| M2 | 18 | 12,721 | 16 | 13,911 | 13,894 |
| M3 | 20 | 19,331 | 21 | 22,661 | 22,639 |
| M4 | 20 | 21,594 | 21 | 24,923 | 24,901 |
| M5 | 25 | 29,148 | 26 | 33,013 | |

The M5 figure for MCP was read over two `tools/list` pages.

The `--live-agent` regression test, run at M0 and at M1, shows what those definitions cost in tokens. The first request of a conversation gets nothing from the cache and pays for the whole catalog: 803 tokens at M0 and 1,733 at M1. So four more definitions cost about 930 tokens, paid once. Every later request in the same conversation came back 77 to 92 per cent cached (at M1, 5,864 input tokens, of which 5,376 were cached). That is because the tools array is byte-identical through a chat, and the cache depends on that. At M2, the same test made eleven requests that took 2,875 to 6,763 input tokens, with 42 to 91 per cent cached. None of the eleven started with an empty cache, because an earlier run on the same day had already cached the shared beginning of the prompt. So that run did not measure the uncached cost at M2.

The conclusion is that a definition adds cost to the *first* request of each conversation, not to every turn. That is why going over the budget only makes us look at the measurements and does not block a tool. Two things would change this conclusion: a catalog so large that the first request of every conversation is expensive by itself, or transcripts that show the model choosing the wrong tool. Watch for both. Neither has appeared yet.

**The printer panel follows the same rule with its own set of tools.** The printer panel on Home runs its own conversation, `PrinterConversation`. That conversation is offered the five printer tools and the four settings tools. The list is the same in every mode (Add, Change, or Connect), so it stays byte-identical through a conversation and between conversations of the same mode.

What a call may do depends on the conversation. `PrinterConversation::preflight_tool` refuses every call that is not allowed, before any card is shown, with an error that says where to go instead. The rules are:

- Finding and adding a printer (`printer_identify`, `printer_add`) is allowed only in an Add conversation. A Change or Connect conversation is about a printer the person already has. An add there would save another printer and move the conversation onto it.
- `printer_change` is allowed only on the printer the conversation is about, once there is one. When the project uses that printer, its unsaved edits are dropped and listed in `discarded`; the panel's coordinator saves a version first, as the project's agent pane does for any change to the project. If that save fails, the call does not run and returns the coordinator's generic `local_save_failed`; there is no separate message for it.
- The connection tools are allowed only once there is a saved printer.
- The settings tools are allowed only with `scope: "printer"`, on the printer the conversation is about.

A Change conversation may be about a stock profile (one that ships with OrcaSlicer) that the project has selected. That happens when the person chooses Printer settings… in the header on a project that uses a stock profile. In that case, changes to the nozzle and to the connection are refused with `stock_profile`. Changes to settings are saved as a copy of the profile, and the conversation follows that copy from then on (`PrinterConversation::tool_settled`). A Change conversation is never turned into an Add conversation. The code that opens a Change conversation checks that the printer is there, and shows OrcaSlicer's error dialog when it is not.

The header's filament chat uses the same panel (`PrinterPanel::open_filament`). It is offered the four settings tools and nothing else, and it gets no project state. It has its own instructions. The page writes them in `AgentUI/src/filamentInstructions.ts` and sends them with `filament_instructions`. The page writes them from the facts the app sends as the session part of the panel's state (`PrinterPanel::filament_session_json`): the preset's name, its slot and material, and, for a stock filament, the copy that a change is saved as. The preflight check for this chat allows the settings tools only with `scope: "filament"` on that preset. It refuses an apply that has no `persistAs`, with `not_saved`, because the chat is gone when it closes. When a copy is saved in the preset's place, the chat is about that copy from then on, and the page writes the instructions again for the copy. Until 2026-09-29 the filament chat ran on the project assistant's instructions and tools. With those, the model read "make the nozzle 5 degrees hotter" as a request to print. In another run it set 205 °C when the value was 220 °C, without reading the value first.

We do not give each mode a different tool list, for two reasons. The conversation has to refuse disallowed calls anyway, whatever list the model is shown. And the way to restrict tools while keeping the list stable (`tool_choice` with `allowed_tools`) exists only on some providers. We would reconsider if transcripts showed the model reaching for a refused tool and then failing to recover. `tests/printer_prompt` measures this with the `change-add-another` case.

Two past changes explain the current design:

- Until 2026-09-23 the panel offered one tool per mode (7f66e75b07). The reason was to keep a printer lookup from clearing the pinned card of the printer a Change session was about. That card is gone (4d4f9e466b). Offering one tool per step also left an Add conversation with no tool to connect the printer it had just added (8334b3395b).
- On 2026-09-28 three tools were removed (b60fc2d8e6): `printer_manual_setup`, `printer_manual_connection` and `printer_setup_finish`. These were the tools that opened a window or closed the panel. OrcaSlicer's full printer list and a printer's settings are still reachable from the panel's own buttons, and the person closes the panel.

The app decides which tools are offered. The page writes the text: `AgentUI/src/printerInstructions.ts` holds the instructions, and `AgentUI/src/printerWords.ts` holds the notes the model reads after a tap. If a turn arrives before the page has written its instructions, the app refuses the turn. The app has no prompt of its own to send instead. The instructions for an Add conversation carry the whole printer list, about 28 KB. That text is always the same and sits at the start of the instructions, so it is cached. No printer conversation carries the project: no workspace state goes into the turn, and none goes into a tool result.

**Deferral, when the measurements say so.** Deferral means the model first sees only a short index and loads a tool's full definition when it needs it. Use the deferral feature that the model platform provides. Never write our own mechanism for switching tools in and out. For the in-app Agent, the platform feature is OpenAI's `tool_search` with `defer_loading: true` on the Responses API. It is documented for `gpt-5.4` and later. Whether it works on the pinned `gpt-5.4-mini` has not been verified. On the MCP side, clients defer by themselves, as Claude Code does. Tools would be deferred one toolset at a time (toolsets are the named groups in the candidate catalog), and OpenAI recommends fewer than ten functions per namespace. Do not build a tool that enables a toolset by rewriting the tools array. Every switch would break the prompt cache.

**Shape each tool as a domain operation.** A tool does one thing that a slicer user has a name for: place an object, lay out the plates, mark a region, set up the printer. Do not wrap each OrcaSlicer function in its own tool. Do not build a tool that interprets a list of unrelated commands. One operation may have several optional facets, as long as each facet has its own type and the tool rejects facets that conflict.

**Batch only where items share a shape.** Good examples are a list of setting changes, a list of intent fields, a list of annotations, and a list of layout rows with one row per object. A list that mixes transforms, imports, and deletes is the god tool described above. It is refused.

**Fold reads into sections and detail levels before adding a read tool.** A new read tool is justified in only two cases. Either its inputs are of a different kind (a search over a large catalog, an analysis of one object, a report on a sliced plate), or its result is an image.

**Let call logs decide consolidation.** If transcripts show that two tools are always called together, consider merging them. If two facets of one tool are never used together, consider splitting them. Make these decisions from eval evidence, not from a design discussion.

**Output schemas are not advertised over MCP.** No client was shown to use them, and they were two thirds of the catalog. `tools/list` no longer carries `outputSchema`. `validate_output` still checks every successful MCP result, so a result that breaks its contract still fails with a visible error. We measured the effect on one build, with the recipe near the end of this guide. With five tools, the catalog went from 9,723 bytes to 3,141, which is from about 2,430 tokens to 785. Only the MCP side saves anything. The in-app adapter has always sent `input_schema` as `parameters` and nothing else, so an in-app turn never carried output schemas. Restore the field only for a client that is shown to read it.

## Choose the narrowest honest scope

Do not put an enum value in a schema, such as one of `printer | filament | process | object`, unless every advertised value works in both the fake and the real workspace adapter and inherits values the way the documentation says.

For each proposed capability, answer:

1. Which OrcaSlicer owner holds the real state or performs the operation?
2. Is the command valid for the whole project, one plate, one object, one instance, or one preset layer?
3. What stable ID identifies the target, and when does that ID expire?
4. Which revision must still be current when the command executes?
5. Which existing OrcaSlicer undo snapshot covers the change, and how is the change undone, reset, or reverted? Or does ordinary project Undo not apply?
6. Which workspace change reasons tell readers that they must read again?
7. What happens if the user edits something in the GUI while the call is queued or running?

If any of these answers is unclear, the tool's contract is not ready.

## Reuse the one authority path

Every change to the project must take this route:

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

A tool adapter must not call `Plater`, `Model`, `PresetBundle`, wxWidgets, or slicing objects directly. Do not copy an OrcaSlicer operation into JusPrin. If the operation you need exists only inside OrcaSlicer's UI code, add the smallest possible seam to the class that currently owns the operation. Name the seam for what it does, and put nothing JusPrin-specific in it.

Any change to a file OrcaSlicer owns must follow [fork stewardship](fork-stewardship.md). Keep the diff small, and add lines instead of rewriting existing ones. Record why the seam belongs where you put it, identify the upstream lines you touched, and include the rebase evidence.

## JusPrin-owned project state

Some product state is not held anywhere in OrcaSlicer: the print intent, the pinned agent plan, semantic region annotations, physical facts the user confirmed about a printer, and the monitoring rules the user sets for automatic pausing. For this state, the "current Orca owner" step in the route above is a JusPrin store instead. Every other step stays the same. The rules are:

- **Storage.** State that belongs to a project (intent, plan, regions, conversation, and attachments) lives in the local project store, under a project ID that does not change. The current `ProjectStateDocument` is saved separately from model checkpoints. An imported project may contain the older `Auxiliaries/JusPrin/state.json` and its recovery mirror. Those are read only to migrate them. Three things remain fields of that document:
  - the print intent, `printIntent`. A write inserts or updates one field by name, and each field carries its provenance;
  - the plan, `plan`. A write replaces the whole plan;
  - the region annotations, `regions`. A write replaces the whole list, and each record is stamped with a seq when it changes.

  State that belongs to a printer (confirmed facts, monitoring policy) stays in app data, stored per printer identity, the way `PrinterFactsStore` keeps it. Confirmed facts carry an expiry, because the physical world changes without telling the app. See [Local autosave and version history](autosave-version-history.md).
- **Revision.** Every write has to signal that it happened, so that readers read again. Two questions are still **open**: which signal to use, and whether the signal invalidates actions that are already queued. They belong to the milestone that adds the first writer. The obvious answer would be to publish a `WorkspaceChangeReasons` bit and add that bit to `kInvalidatingReasons`. That cannot be built as described. `settings_apply_patch` requires its caller's `expectedRevision` to equal the current revision exactly. So if the revision goes up at any point between a client's preview and its apply, the apply fails as `stale_workspace`. An agent that recorded its plan between previewing and applying would make its own call fail. There is an existing example of the alternative: the edit feed (`WorkspaceEditHub`) reports a change without raising the revision. Decide separately for each kind of state. A region annotation generates OrcaSlicer artifacts, so it is a real change to the project. An answer about intent, or a statement of the plan, is not. In the code as built, writing a region record signals nothing by itself. The artifacts it generates are ordinary OrcaSlicer edits, and they raise the revision in the usual way. So an action that was queued before an annotation goes stale, exactly as it would after any other edit to the model.
- **Undo.** None of this state is in OrcaSlicer's undo stack. Results say `projectUndo: false`. Region annotations are the awkward case. They generate OrcaSlicer artifacts (modifier volumes, enforcers, blockers, paint), and those artifacts *are* in the undo stack. So project Undo can leave an annotation without its artifacts, or leave an artifact without its annotation. The `regions` section of `object_analyze` reports both situations, and `region_annotate` generates the artifacts again. The result of a place, divide, or repair lists the regions whose link to the geometry that operation broke.
- **Provenance (where a value came from).** Any field that records a fact about the world or about what the user wants also records its source: `file` (read from the project or a profile), `observed` (reported by a sensor or the printer, with a timestamp), `agent_inferred`, or `user_confirmed`. A tool marks a value `user_confirmed` only when the call is based on something the user explicitly said. Values the agent inferred stay `agent_inferred`. When a tool reads printer state, it reports the `configured` value and the `observed` value of the same fact separately, and it computes the `mismatches` between them itself. That comparison is not left to the model.

## Define the contract before the implementation

For every new tool, the registry entry, together with its contract and tests, must specify:

- a name that will not change, in lowercase with underscores;
- a short title, shown in the activity UI;
- a short description that says when to use the tool;
- a strict JSON Schema for the input, with `additionalProperties: false` where practical;
- a JSON Schema for the output;
- its `ActionClass`, which is trusted because it is set in the registry;
- whether it is offered to the in-app Agent, to MCP, or to both;
- what must be true for it to be available, such as an open FFF project (FFF is ordinary filament printing) or a selected object;
- whether the action is revision-sensitive, meaning it must fail when the workspace has changed since it was requested;
- whether it applies completely or not at all, and how it is really undone, reset, or reverted. Say so explicitly when normal project Undo does not apply;
- what happens when it is cancelled;
- the size limits on its output and how results are paged; and
- its errors, as structured codes that are specific to the task.

The registry cannot change while the app runs, and its entries always come in the same order. A tool adapter may leave tools out, based on the exposure each tool declares. An adapter must not change a tool's action class, validation, or behavior. Keep the list that MCP clients see stable. The only exception would be a server that also implements and tests the current protocol's notification for a changed tool list. When a tool cannot run because of the app's current state, report that from `tools/call` instead of removing the tool from the list.

`ToolDefinition` currently stores the name, title, description, input and output schemas, action class, exposure, availability, and handler. The other properties are not registry fields. Whether a tool is revision-sensitive, whether it is all-or-nothing, how it cancels, and how it appears in history are all determined by the coordinator and workspace code, and checked by their tests. `ToolAvailability` currently describes the request a tool is called from (`Always` or `ImportableAttachment`). It does not filter tools by the current state of the project.

Inputs are validated by the `valid_arguments` decoder that the registry has for each handler. Changing only the JSON Schema does not change which calls are accepted. Successful MCP output is checked by `validate_output`, which deliberately understands only a small set of schema keywords. Before you use a keyword it does not support, extend the validator and add tests. Do not assume that `validate_output` implements all of JSON Schema, or that every adapter validates output automatically.

### Naming and compatibility

- Prefer names such as `slice_start` and `presets_compare`.
- Do not rename a released tool without a strong reason. Treat names and schemas as public APIs.
- Add optional fields in a way that keeps existing callers working. Do not change the meaning or the type of an existing field.
- When a breaking change to a contract cannot be avoided, add a versioned replacement. Stop advertising the old definition after a documented migration period, and keep a targeted compatibility test for as long as both exist.
- Use one canonical name for a tool in every adapter. Do not keep separate OpenAI and MCP aliases unless a defect in a client is documented and tested.

## Input design

A tool's inputs should say what the caller wants and which thing it wants it done to. They should not describe how the request is transported or how the UI works.

- Send OrcaSlicer IDs as JSON strings, so that the full width of the native ID is preserved.
- Use canonical setting keys, and values in their normalized form.
- The first catalog had three in-app fixtures: `duplicate_object`, `import_model`, and `inspect_selection`. All three are retired. `plate_layout` replaced the first, `object_import` replaced the second, and the selection ids in the summary of `workspace_inspect` replaced the third. When the coordinator queues an action, it records the session and the revision. It invalidates queued actions when a relevant workspace event arrives, and it checks again before executing. A change to the selection alone does not invalidate an action that is aimed at a specific object, and does not redirect it.
- Some new operations read a value, change it, and write it back, and must detect anything that changed since the caller's earlier read. For such an operation, design explicit inputs for the expected session and revision in its own schema and decoder, and test them. A check made when the call is submitted does not prove that the caller's earlier read is still current. Do not quietly add required arguments to tools that already exist.
- Put changes that must succeed or fail together into one call.
- Make the caller say which thing it means. Do not fall back to a hidden default. The exception is a tool whose name clearly promises the current selection or the active plate.
- Reject unknown fields. Reject inputs that select the target in conflicting ways.
- A caller may pass a filesystem path only to a tool that does three things: it validates the exact absolute path, it restricts what kind of file the path may be and where, and it documents whether it reads, creates, or overwrites. The candidate catalog has three such tools: `object_import_file`, `project_open`, and `export_file`. No read-only tool accepts a path. In-app clients keep using attachments, which need no path.
- Do not make the model repeat data that the server can read for itself, from the real source, when the call executes.
- Do not put a large catalog into a schema as an enum. Offer a size-limited search and a way to fetch details instead.

**One exception, with its evidence: the printer list.** Working out which printer someone means from what they say takes the model's understanding. It is not a lookup. A search tool placed in front of the model did the job badly. `printer_catalog_search` required every word of the query to appear in a model's name, and it returned the first few matches in file order. As a result "voron 2.4 350mm" came back unsupported, "x1c" matched another manufacturer's printer, and "prusa" showed three of thirteen.

So the instructions for an Add session now carry every printer the panel offers, one per line, in the form `<catalogId> | <brand> <model> | <build volume>`. There are 383 lines. They come from `PrinterCatalog::panel_printers`, are sent to the page as the session's `context`, and are written into the instructions by `AgentUI/src/printerInstructions.ts`. `printer_identify` checks the ids that the model copies from the list.

This was measured on 2026-09-18, through the app's own request and tool results, on `gpt-5.4-mini`, with three runs of each case:

- the word test set: 138/150;
- the photo test set: 29/45;
- the handoff's own cases, with those four queries among them: 21/21.

The model tried 33 times to show more than three printers. Each attempt was refused with `too_many`, and the model then asked a question to narrow the choice. So no answer ever showed three of many. Two weak spots remain. One is a query that equals one model's name and is also the beginning of other models' names ("bambu a1": 0/3). The other is a photo with no readable name. The test sets and the runner are in `agent-docs/jusprin/spikes/printer-identify/`, which is uncommitted and sits beside the earlier spikes.

The list is always the same text at the start of the instructions, so it is cached. This approach is right only while the list stays at a few hundred lines.

The coordinator looks up the action class and the handler from the tool's name. Never accept either one from the call's arguments.

## Output design

Decide how large a response may be before you add fields to it. A full project tree, every setting definition, G-code statistics for every layer, or an unlimited list of warnings can each use more of the model's context than the entire tool catalog.

Every read tool should use one or more of:

- a level of detail (concise, default, or detail);
- explicit field selection;
- paging, with results always in the same order;
- a fixed maximum number of items;
- a summary first, followed by separate calls for the details the caller wants; or
- an explicit `truncated` flag and a cursor for fetching the rest.

A new tool that returns live state should include `sessionId` and `revision` when later calls need them to identify the project and to check that nothing has changed. `workspace_inspect` returns both, and returns the selection as ids. Results that are internal history records have their own schemas. MCP activity results also carry the action ID, the current session, and the current revision in `_meta["io.jusprin/activity"]`. When an event says that state changed, fetch a fresh snapshot. Never treat the event itself as the new state.

A successful MCP result contains `structuredContent` that is valid against the output schema, plus a text block that holds the same result serialized as text. An error uses the shared `{error: {code, message, details}}` envelope with `isError: true`. It does not use the success schema.

The bridge adjusts results for older clients:

- For legacy clients, it removes the result and cache fields that exist only in the modern protocol.
- For `2025-03-26`, it also leaves out `structuredContent`, and leaves `outputSchema` and `title` out of the catalog. The serialized text result is kept.
- For the later supported revisions, `structuredContent` and `title` are kept.

No negotiated version advertises `outputSchema` any more (see [tool budget and tool shape](#tool-budget-and-tool-shape)). The bridge still has its own code that strips the field, because the bridge also forwards catalogs from a live app of another version. These adjustments are deliberate, for compatibility. They are not conflicting definitions of the same tool.

### Image results

Three read tools in the candidate catalog return a picture: `project_attachment_read` (a reference image), `view_render` (the prepared or sliced scene), and `printer_camera_snapshot`. The result that the registry produces holds the encoded image and a structured block that describes it (dimensions, camera or layer range, capture time). Each adapter then presents that one result in its own format:

- MCP: an `image` content block next to the text and the structured result. `2025-03-26` gets the same block, because image content is older than structured content.
- OpenAI: the text and the structured block are the function result. The image is appended as an image input in the continuation, because a function result can only be text.

The two adapters present the same result in different formats. The tool itself does not behave differently. Both projections must be tested before the first tool that returns an image ships. Limit every image to 1280 pixels on the long edge and to 2 MB, so that one frame never takes up most of the context window. Never return an image the user has not allowed. A camera frame requires the privacy grant in Printer Configuration. Without it, the call fails with `camera_disabled`.

## Errors should teach the next valid call

When an invalid state is expected, handle it with ordinary control flow and return a structured error. Do not throw an exception. An error should say what the problem is and include only the data the Agent needs to correct the call.

The MCP errors today include `invalid_arguments`, `cancelled`, `workspace_unavailable` and `stale_workspace`. The MCP adapter converts the app's native `stale_revision` and `stale_id` into `stale_workspace`, with the expected and the current session and revision in the details. It converts `unavailable_operation` into `workspace_unavailable`. Protocol errors at the transport level are separate JSON-RPC errors. Native error codes and MCP error codes do not have to be spelled the same.

The settings tools return these correction details, each limited in size:

- unknown setting: the canonical code `unknown_setting`, the original key, and a short `suggestions` list;
- invalid enum value: the key, a diagnostic message, and the valid values in `allowed`;
- value out of range: the key, a diagnostic message, and the `min`/`max` bounds. Units are available from the setting's metadata;
- incompatible settings: the keys that conflict, and OrcaSlicer's reason;
- stale call: the expected and the current session/revision, plus an instruction to read again;
- unavailable operation: the requested scope has no active preset or target.

A failure that should be impossible (a broken invariant) must stay visible to diagnostics and to Sentry. Catch an error at the boundary between two layers only to recover from it, to translate a known error, or to add context that is essential.

## Read, mutation, and destructive policy

Decide what a tool may do from its action class in the registry, not from its name:

- **Read-only:** cannot change the project, a preset, anything on disk, the printer, or any product state that is saved.
- **Mutation:** changes the project, a preset, or saved product state.
- **Destructive:** may delete, overwrite, discard, print, export, or restore history.

The action class is information used for UI wording, MCP annotations, staleness checks, and the audit history. A valid tool call starts as soon as it has been validated. A tool that previews changes counts as read-only only if tests against both the fake and the real workspace adapter prove that it does none of these: make a preset dirty, raise the workspace revision, create history, cancel slicing, or write a file.

## Asynchronous operations

Make a tool asynchronous only when the real OrcaSlicer operation is asynchronous. Do not build a second engine for running jobs.

Slicing, and any long-running action added later, works like this:

1. The workspace command starts the work through its current OrcaSlicer owner and returns a stable action handle.
2. `ToolActivity` records that the action is running, and then its final state, with a limited amount of progress information.
3. When OrcaSlicer completes or fails, the workspace state advances or is invalidated.
4. After that event, the caller fetches the real slice report or the error.
5. Cancelling calls OrcaSlicer's real cancellation path, and reports whether the cancellation took effect before the work finished.

MCP progress notifications are optional. They belong to one request and exist only to show progress. The progress value must never go backwards, and a client must always have another way to learn the final result. Add the MCP Tasks extension only when a real client or eval needs durable calls that outlive one request. An operation taking several seconds is not enough reason.

The current coordinator runs an action in steps, on GUI ticks. It does not provide durable asynchronous jobs. The bridge accepts `notifications/cancelled` in both the legacy and the modern protocol versions. On receiving one, it closes the connection it was forwarding and suppresses any further output for that request. End of input (EOF) ends the bridge session. If the connection is lost before the final response, the client gets `connection_lost` with an **unknown outcome** warning. That is not proof that the mutation was cancelled. Never retry a mutation automatically after such a loss. Read the real state first.

## Tool exposure

By default, offer a tool through both adapters when both can supply the same inputs. Offer it through only one when the two sides get their context in different ways. The current cases:

- `workspace_inspect` is offered through both adapters. Either side can then get the identity of a newly completed slice after slicing, without relying on the context it was given at the start of the turn.
- Import from an attachment (`object_import`) is in-app only, and import from a path (`object_import_file`) is MCP only. In the first case the file reaches the app through the conversation. In the second it arrives as an explicit path. These are two separate definitions that each say exactly what they do, both built on one workspace command. They are not one tool that behaves differently for different callers.
- `intent_update` and `plan_set` are in-app only. The intent record and the pinned plan belong to the in-app chat. An external agent keeps what its user said, and its own plan, in its own conversation. MCP clients can still read both through `workspace_inspect` and `slice_report`. For the same reason, the in-app assistant's print guidance, which names these tools, is not part of the MCP server's instructions. That guidance is the "Intent and setup", "Preparation" and "Verification" paragraphs of the in-app prompt, written on the page in `AgentUI/src/projectInstructions.ts`.
- A future MCP diagnostics tool may be MCP only, if its purpose is to establish the external connection.
- The printer panel's five own tools carry only the `Printer` exposure. Neither adapter lists them. The panel's conversation offers all of them in every mode and refuses the calls that do not fit the conversation (see [Tool budget and tool shape](#tool-budget-and-tool-shape)). What the model reads back from one of these tools is the tool's own result. It is not wrapped in the host's workspace envelope.

Write the reason for a restricted exposure next to the registry definition, and test it. Do not use exposure to make one tool behave differently for different callers. If two adapters need different behavior, either improve the shared command or create two separate definitions that each say what they really do.

## When resources or skills are appropriate

Do not make anything depend on MCP resources until the clients we intend to support have shown that their models fetch resources reliably. A tool that returns a size-limited amount of data is what every client can be expected to handle. Resources may be added as an optional second way to get data that is large, readable, and stable.

Keep printing playbooks as skills the model can read (see [Skills versus tools](#skills-versus-tools)). Examples are reducing stringing, improving strength, and choosing a support strategy. Turn a workflow into an executable tool only when it corresponds to a named OrcaSlicer operation that always behaves the same way and has defined inputs, outputs, validation, and behavior for undo, reset, and cancellation. If many step-by-step workflows eventually qualify, build one workflow registry that the model can search, in preference to dozens of nearly identical tool definitions.

## Required implementation sequence for one new tool

1. Add or update the failing eval and the expected transcript.
2. Find the real OrcaSlicer owner. Record how it behaves: object lifetimes, threading, undo history, and events.
3. Define the typed inputs, outputs, and errors of the workspace command.
4. Implement the command in the fake workspace adapter, and write the workspace contract tests.
5. Implement the command in the real workspace adapter, through the smallest possible seam in the owner.
6. Prove that making the edit directly in the GUI and making it through the tool leave the same observable state.
7. Add the registry definition, the decoder and validator, the link to the code that executes the tool, and the output schema.
8. Add coordinator tests for action class, staleness, all-or-nothing behavior, cancellation, and the final activity state.
9. Add projection tests for every adapter that offers the tool.
10. Add protocol fixtures and output-limit tests.
11. Run a harness against the real app, and record one end-to-end transcript with an external client.
12. Update the catalog below, and attach the verification and rebase evidence to the PR.

Do not merge a registry entry that is backed only by the fake workspace.

## Test matrix

Every new capability needs tests at each layer it passes through:

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

If a layer does not apply, say why in the PR. If something cannot be tested, state that openly as a limitation. It is not permission to leave out evidence without saying so.

## Candidate catalog

This catalog lists the tools the finished product would have. It was worked out one product screen at a time from [Designing an AI-Piloted OrcaSlicer](orca-feature-discovery.md), and shaped by the rules under [tool budget and tool shape](#tool-budget-and-tool-shape). Nothing in it is implemented unless the catalog record above says so. Every row still needs what this guide requires of any tool: the eval failure, the trace of its owner, and the test matrix. The number that matters is how many definitions load per turn, not the total. The total here is 36. All of them load on every turn until the measurements under [tool budget and tool shape](#tool-budget-and-tool-shape) justify deferral. The toolsets below are the groups that would be deferred together when that day comes. Until then they only group tools for naming and for delivery.

These conventions apply to every row:

- Every read returns `sessionId` and `revision`.
- Every write that is revision-sensitive takes `expectedRevision`.
- A handle for a detected face or hole carries the revision it was computed at. After the revision moves, using the handle fails with `feature_expired`.
- Facts carry provenance, as defined under [JusPrin-owned project state](#jusprin-owned-project-state).
- Long-running rows follow [Asynchronous operations](#asynchronous-operations). They return an action handle, and the handle's state is read from the `slicing` section of `workspace_inspect`.
- Every read states a fixed maximum size and has a flag that says when the result was cut short.

In the Class column, **R** means read-only, **M** means mutation, and **D** means destructive.

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

Both adapters load every registered tool on every turn. MCP `tools/list` returns the whole catalog, and the catalog stays the same for a given build. The in-app adapter sends the whole function list with each request and keeps it byte-identical through a chat. The projection tests record how many bytes each adapter loads. The live regression test records input tokens and cached tokens for each request. Those numbers decide when deferral is adopted. The toolset table does not.

### `core`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `workspace_inspect` | R | The one read for current state. `sections` selects any of: `summary` (default: project identity and saved state, printer pointer, intent line, selection ids, plate and object counts, plan headline, top unresolved warning); `project` (description, designer, license, safety and usage notes, assembly, bill of materials, profile notes, attachment list, versions and recovery state, each with provenance); `intent` (the intent record with provenance and an `unanswered` list); `printer` (the selected printer: configured versus observed nozzle, plate, and filaments with timestamps, computed `mismatches`, `confirmedFacts`, job progress, temperatures, alerts, monitoring policy); `objects` (plates, objects, parts, instances, enabled, extruder, quantity, bounding box, override and region counts, print order); `plan` (the pinned plan, deviations from the profile, estimates when a slice is valid); `slicing` (state and action handle per plate, progress, invalidation); `level` concise or detail. | workspace snapshot; `MachineObject`; `PresetBundle`; JusPrin stores; `UndoRedo` | per section: 4 KB text fields, 64 attachments, 16 slots and alerts, 64 objects with cursor |
| `object_analyze` | R | Geometry facts for one object. `include` any of: `mesh` (dimensions, volume, health, units suspicion); `features` (planar face groups and cylindrical holes with revision-scoped handles); `orientations` (score each entry of `candidates`, or Orca's auto-orient candidates, for overhang area, support volume, bed contact, and which handles face down); `fit` (plate fit, overlaps, likely duplicates); `regions` (annotations and their `bindingLost` or `artifactsMissing` flags); `measure` (distances between two handles). | `TriangleMesh` statistics; `Measure` feature detection; `OrientJob` evaluation; `PartPlate` checks; JusPrin annotation store | 32 faces and holes by area; 8 candidates; 16 overlaps, duplicates, measurements; 32 regions |
| `settings_search` | R | Required `scope`; required `target` for `object`, `filament` and `printer`, forbidden for `process`; `writable` and `changedOnly` filters. `changedOnly` answers what differs from the owning preset or inherited value. | `print_config_def`; scoped edited preset or `ModelConfig`; dirty options | 25 per page |
| `settings_get` | R | Required `scope` and its exact target; origin reported as system, user preset, project edit, filament preset, printer preset, or object override. | scoped edited presets; `PresetBundle::filament_presets`; `ModelConfig` | 32 keys |
| `settings_preview_patch` | R | Required `scope` and its exact target; applies the write blacklist, dependencies, validation, persistence and undo semantics. | scope-specific config clone; `ConfigManipulation` where applicable; `Slic3r::validate` | 32 keys |
| `settings_apply_patch` | M | Required `scope` and the same target as preview. Optional `persistAs` is valid only for preset scopes and saves the result as a named user preset. | owning `Tab` and `PresetCollection`; `ModelConfig` for object | 32 keys |
| `intent_update` | M | Record what the user said about the print: a same-shape list of `{field, value}` over the intent record. Use `assumed` for agent inferences; only values grounded in the user's statement become `user_confirmed`. In-app only. `setupTitle` is the user's stated purpose as the chat-owned title of the setup card, with or without fields; it applies no setting. | JusPrin intent store | 32 fields, 120-character title |
| `plan_set` | M | In-app only. The agent's own statement: orientation rationale, strategy per concern, unverified assumptions, compromises and risks, confidence per decision, alternatives. | JusPrin plan store | 2 KB per field, 16 decisions |
| `slice_start` | M\* | Slice one plate or all; returns an action handle. Refuses to pre-empt a GUI-started slice unless `preempt: true`. | `Plater` reslice; `BackgroundSlicingProcess` | |
| `slice_report` | R | Check print report for a sliced plate, by `sections`: summary (time, filament per extruder as length, weight, cost); Orca warnings; supports (volume, contact area, contact with annotated regions or detected holes, removal-difficulty heuristics); seams against visible or forbidden faces; overhangs and bridges past thresholds; first-layer contact; islands; collisions and toolpath outside the bed; material changes and purge; prime tower; intent checks such as time over budget. | `Print` statistics; `GCodeProcessor` result; `PrintObject` supports and seams; JusPrin annotations | 32 findings per list by severity |

### `project`

| Tool | Class | Contract | Owner | Bound |
|---|---|---|---|---|
| `project_open` | D | Exactly one of `path`, `new: true`, or `versionId`. Explicit inputs for the choices Orca otherwise asks in dialogs: `loadProjectSettings` (project or keep), `unitConversion` (none, inches to millimetres); every dialog Orca raises while loading is answered with the least change and returned in the load report. Replaces the document once the autosave hook has saved the open one; unsaved preset edits are then dropped. A file opened as a project before returns `opened_before` (the project, and whether the file changed) until `openedBefore` says `existing` or `new`. | `Plater` load_project and load_files; backup restore | 32 import warnings |
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

Text embossing is left out until an eval needs it. When one does, embossing gets its own tool built on `EmbossJob`. It does not become a facet of `object_divide`.

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
| `print_send` | D | Send with a `preflightId`. Re-runs the preflight at execution; fails without sending if anything bound to the digest moved or blocking problems remain. Runs only on the person's request to send; preparing a print never sends. | `PrintJob`, `SendJob` | |
| `export_file` | D | G-code, sliced 3mf, project 3mf, STL, or preset bundle to a path. Validation surfaces license restrictions from project details in the result and on the activity card. | `Plater` export functions | |

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

Some reads and writes that were once planned as separate tools are now part of another tool.

Reads that became sections of `workspace_inspect`: project details, intent, printer state and mismatches, confirmed facts, monitoring policy, object manifest, plan, activity state, versions.

Reads that became part of `object_analyze`: region list, orientation scoring.

Writes that became part of another tool:

- `printer_facts_confirm` is now part of `printer_setup`;
- `presets_save` is now the `persistAs` option of `settings_apply_patch`;
- transform, arrange, object edits, and plate edits are now in `object_place` and `plate_layout`;
- `slice_layers` and `slice_gcode_read` are now in `slice_inspect`.

These are not tools until an eval demands them: editing project details (an action in the Project details drawer), writing the monitoring policy (an action in Printer Configuration), text embossing, and everything under "Deliberately not tools".

### Deliberately not tools

These will not be tools:

- one tool per setting, or one per bundle such as "make it stronger";
- printing advice;
- preferences, cloud, firmware, plugins, sync, and installing updates. These belong to the user, according to surface 2.12 of the product definition;
- manual axis movement, homing, detailed fan and light control, and printer file storage. These belong to the expert layer of Monitor;
- painting stroke by stroke, and the measure gizmo as a UI. The model reaches the same results through regions and through the handles that analysis returns;
- asking the user a question. The conversation does that;
- a second engine for running jobs, or the MCP Tasks extension;
- a single write tool that interprets a list of unrelated commands.

### Prerequisites the catalog imposes on the machinery

The catalog needs the following to be in place:

- `validate_output` supports `enum` (used for provenance, action state, region kind, and section names) and `maxLength`, so a row may use them. Inputs that allow several alternative forms, such as the facets of `object_place`, are checked by the decoder, not by the schema validator.
- Neither adapter needs new code for loading tools. The projection tests need to record definition bytes for each adapter, and the live regression test needs to record input and cached tokens for each request. With those numbers the deferral decision can rest on evidence.
- Three owners exist only inside OrcaSlicer's UI code. Each needs a seam with nothing JusPrin-specific in it before its tool can be built:
  - the mapping and readiness logic in `SelectMachineDialog`, needed by `preflight_check`;
  - the plane and circle detection in the Measure gizmo, needed by `object_analyze`;
  - offscreen rendering of the preview, needed by the preview mode of `view_render`.

  Import and open also need a way to make the choices that OrcaSlicer's dialogs ask for (`loadProjectSettings`, `unitConversion`, `oversized`) without showing the dialogs. This is the same kind of hazard as the settings normalizer.
- The JusPrin stores for intent and plan exist, in `ProjectStateDocument`. The stores for regions, confirmed facts, and monitoring policy do not. Each one is added together with the tool that writes to it. How a write signals that it happened is still open. The rules are under [JusPrin-owned project state](#jusprin-owned-project-state).
- Both adapters must be able to present images as described under [Image results](#image-results) before `project_attachment_read`, `view_render`, or `printer_camera_snapshot` ships.
- The Tier 1 security review described under [Proportional security growth](#proportional-security-growth).

### Evals that gate delivery

These evals must pass before the tools they need are delivered:

1. Open an STL with one object, state "decorative, front face visible, under five hours, PLA", and reach a sent job without editing any setting by hand. Needs `core`, `project`, `setup`, `geometry`, `output`. The transcript of this eval is also the first call log to use for consolidation: any two tools that are always called together are candidates to merge.
2. Take a model that has a precision hole and an overhang that needs a lot of support. Find the hole, annotate it as protected, show that supports no longer enter it after slicing again, and place the seam on a hidden face. Needs `regions` and `check`.
3. The user reports stringing on a Bambu printer. List the tests, generate a temperature tower, read the user's photo, propose a filament temperature, then preview it and apply it with `persistAs`. Needs `calibration` and the filament scope.
4. An edit at the same time: the user changes a setting in the GUI before an external agent's apply executes. The apply fails as stale, and the agent recovers with one preview and one apply. Applies to every toolset.
5. Measuring the load: for the full catalog, the live regression test reports definition bytes, input tokens, and cached tokens for each request, and the transcript is reviewed for cases where the model selected the wrong tool. These numbers decide whether deferral is adopted.

## Settings tools: contract and hazards

The settings tools are the worked example of everything in this guide. This
section records the parts of their design that stay true now that the work is
done. The implementation handoff that produced them is retired. Read this
section again before you change the write blacklist (the list of settings no
tool may change), and before you add any tool that changes a preset.

### Standing decisions

1. Settings are data. There are four generic tools that work on searchable
   records. There is never one tool per setting.
2. Every settings call requires `scope`. Its enum is exactly `process`,
   `object`, `filament`, or `printer`. A call without `scope` fails with
   `invalid_arguments`. There is no default, the scope is never inferred from
   `target`, and there is no alias and no compatibility mode.
3. What `target` must be depends on the scope, and no other forms are
   accepted:

   | `scope` | `target` | Meaning |
   |---|---|---|
   | `process` | forbidden | the active edited FFF process preset |
   | `object` | required `{objectId}` | that object's `ModelConfig` overrides |
   | `filament` | required `{preset}` | that filament preset, by name |
   | `printer` | required `{preset}` | that printer preset, by name |

   These rules apply to `settings_search`, `settings_get`,
   `settings_preview_patch`, and `settings_apply_patch`. A target that is
   extra, missing, empty, or of the wrong shape fails with
   `invalid_arguments`.

   A preset is named the way OrcaSlicer keys it: by its name, which is unique
   within its own collection. `Preset.hpp` says: "The preset name shall be
   unique across a single PresetCollection". Never name a preset by the label
   a menu shows. The header shows a stock printer's model, and
   `Preset::label` puts `"* "` in front of a preset with unsaved edits. Never
   name a preset by an alias either. If no visible preset has the given name,
   the call fails with `unknown_preset`, and the suggestions are the
   project's filaments or the printer in use.

   History: the first draft of this contract named a filament by its 1-based
   project slot, and edited only the selected printer. On 2026-09-29 Kenneth
   chose the preset's name for both. With names, a chat opened for one preset
   can change that preset, and a saved printer the project does not use can
   be changed, without selecting anything first.
4. The scope says which preset or object the settings belong to. It does not
   select anything. `printer_setup` remains the only tool that picks the
   project's printer, process, or filament. A settings call never changes
   nozzle topology (how many nozzles there are and of what kind), and never substitutes a compatible profile. A settings call
   selects a preset only in the two places where OrcaSlicer's own Save or
   edit code does. First, a copy saved with `persistAs` takes the original's
   place. Second, a filament that is not the one open in its tab is opened
   there, the way the sidebar's edit button opens it
   (`PlaterPresetComboBox::switch_to_tab`).
5. Metadata, parsing, and serialization all come from OrcaSlicer's own
   `print_config_def` and its config option classes. JusPrin keeps no second
   table of types, enum values, ranges, units, or aliases.
6. Search and read cover every setting definition that applies to the
   requested scope, and every one of them is writable except the keys on
   one blacklist (`blocked_setting` in `SettingsSupport.hpp`). Every record
   returned by search or read says whether the key is writable. Trying to
   change a blocked key returns `unsupported_setting_mutation`. A key goes
   on the blacklist for what a wrong value does outside the print:
   - it runs a program on the computer (`post_process`);
   - it says where a printer is on the network, or how to sign in to it;
   - it records which preset this is, what it inherits from and what it
     fits, so that a different value breaks the preset;
   - it is the printer's hardware (model, bed, nozzles, extruders), which
     decides which filaments and processes fit and how many values a
     per-extruder setting holds. Those change with `printer_setup` or
     `printer_change`.

   A setting does not go on the blacklist because OrcaSlicer asks about it
   or rewrites it. Those are handled for every key by the guards and the
   sweep described under
   [the process normalizer](#the-process-normalizer-can-open-dialogs-and-rewrite-values).

   History: until 2026-10-12 each scope had a reviewed allowlist (40 of 356
   process settings, 34 filament, 35 printer), and a key was added only
   after an audit of its own. The audit guarded against five things: a
   dialog, a silent rewrite, slicing and dirty state, Undo, and edits made
   at the same time. All five are now guarded for any key, so Kenneth
   replaced the allowlists with the blacklist.

   One exception to "read covers every definition": the seven printer keys
   that say where a print host is and how to sign in to it (`print_host`,
   `print_host_webui`, `printhost_apikey`, `printhost_cafile`,
   `printhost_user`, `printhost_password`, `printhost_port`). Search lists
   them, because a definition holds no value. `settings_get` never returns
   their values: each comes back under `unknownKeys` with the code
   `withheld_setting`. A tool result is sent to a model provider and saved
   with the conversation, and JusPrin's own connect flow collects the
   credential in a local form so that the model never sees it, then stores it
   in `printhost_apikey`. The list is `kWithheldSettings` in
   `SettingsSupport.hpp`, applied in `read_settings` of both workspace
   adapters, so the in-app and MCP tool adapters cannot differ. It is the
   list OrcaSlicer itself strips from G-code, project files and exported
   presets; the addresses are on it because a host URL may carry
   `user:password@`. All seven are on the write blacklist and must stay
   there: a patch returns the old value as `before`.
7. A batch is applied completely or not at all. It goes through
   `Tab::load_config`, or through the code that already owns that scope.
   Never change a config behind the visible preset UI and then imitate the
   notifications the UI would have sent.
8. Edits to process, filament, and printer presets are not in project Undo.
   Their results say `projectUndo: false` and return the previous values, so
   that a caller can submit a change that reverses the edit. Object
   overrides use one OrcaSlicer snapshot, and their results say
   `projectUndo: true`.
9. Every result repeats the validated `scope`, and gives the `presetName` of
   the preset the settings belong to (for an object, the process preset),
   the `sessionId`, and the `revision`. An apply also returns `savedAs`,
   which is the name it saved under or empty, and `presetDirty`.
10. When a preview finds invalid settings, the call still succeeds, with
   `valid: false`. Malformed input is a tool error.
11. In JSON, every value is a string in OrcaSlicer's canonical serialization.
   Callers may send numbers or booleans, and the decoder converts them.
   Every OrcaSlicer id is also a string in JSON.

This schema change breaks old callers on purpose. The interface does not need
a backward-compatibility period. Update the in-app adapter, the MCP
projection, the deterministic agent, the fixtures, and the eval prompts in
the same change. Do not add a protocol-version branch, a feature flag, an
alias, a fallback, or a migration path for the old forms, in which the scope
was missing or the object was implicit. After the change, every caller must
send the new contract.

### Scope-specific ownership

- **Process.** The settings belong to the active
  `prints.get_edited_preset()` and are changed through
  `Tab(Preset::TYPE_PRINT)`. The rules below continue to apply as
  implemented: the normalizer, validation, dirty state, binding an apply to
  its preview, and reversing a change with an inverse patch.
- **Object.** The settings are the `ModelConfig` of the selected
  `ModelObject`. The object is identified only by `target.objectId`. The
  project Undo behavior described below continues to apply as implemented.
- **Filament or printer, when the target is the preset being edited** (the
  one selected in its collection). The tool works on the preset's edited
  copy, through its `Tab`, as for the process. Reads include the preset's
  unsaved edits.
- **Filament, when the target is another preset.** The preset must be in one
  of the project's slots; otherwise the call fails with `not_in_project`. It
  can be changed only by saving, so a call without `persistAs` fails with
  `not_selected`. Apply opens the preset in the filament tab the way the
  sidebar's edit button does: `Tab::select_preset`, then `set_filament_idx`
  for its slot. Apply then loads the change and saves it through the tab. If
  the filament that is open in the tab has unsaved edits, that switch would
  ask about them. Apply drops them first, as `printer_setup` does, and
  preview lists them in a `discards_unsaved_edits` warning. The version
  history keeps them.
- **Printer, when the target is another preset.** This is a saved printer
  that the project does not use. It can only be saved in place. The write
  goes through `Printers::write_unselected_printer`, which is the write
  `change_named_printer_nozzle` already makes: it stores the difference from
  the parent preset and queues the change for the cloud. Saving a copy of it
  would quietly create a second printer that the project does not know
  about. So a different `persistAs` fails with `not_selected`.
- A printer's blocked keys are the ones that decide which filament and
  process fit, or how many extruders there are (bed shape, nozzle size and
  type, extruder count, printer model), its network address and sign-in,
  and its identity as a preset. Those are 43 of its 160 settings. Custom
  g-code stays writable for a printer and a filament, as it was.
- Some settings hold one value per extruder or per nozzle kind. Such a
  setting keeps its length. A patch with a different number of values fails
  with `invalid_setting_value`, because a shorter list would drop the rest.

`persistAs` is not valid for `object`. For the preset scopes it works like
OrcaSlicer's Save with the name typed in:

- The preset's own name overwrites the preset. If OrcaSlicer ships that
  preset, the call fails with `read_only_preset`, and the suggestion is
  `"<name> - Copy"`, the name SavePresetDialog offers.
- Another name that no preset has saves a copy, and the copy takes the
  original's place. For a filament, that happens in every project slot that
  used the original (`Sidebar::update_presets_from_to`).
- A name that another preset has fails with `name_taken`.
- A name that SavePresetDialog would refuse fails with
  `invalid_preset_name`.

Saving the preset that is being edited also saves its other unsaved edits, as
Save does. Preview warns about this with `saves_unsaved_edits` and lists
those edits. The card's title says where the change is saved ("; save it", or
"; save as \"<name>\""). Without `persistAs`, the tool changes only the
edited preset and leaves it dirty.

In two cases the filament or printer tab would open a dialog. Preview
refuses these cases:

- a max volumetric speed below 0.5
  (`ConfigManipulation::check_filament_max_volumetric_speed`);
- firmware retraction with a wipe that retracts less than 100% first
  (`TabPrinter::toggle_options`).

### Scope expansion acceptance criteria

- The registry offers the same required `scope` enum and the same target
  shape through the in-app adapter and the MCP adapter. Catalog tests pin
  both projections.
- Argument tests reject an omitted or unknown scope, and every target that is
  missing, extra, zero-based, mixed, or malformed. No test accepts the old
  call shape.
- Search, get, preview, and apply are each covered against the fake and the
  real workspace adapter, for all four scopes, including a key that can be
  read but not written.
- Filament tests cover three cases: a preset in a slot that is not open in
  its tab, a copy taking the original's slot, and a save that drops the
  unsaved edits of the filament open in the tab.
- Printer tests prove that changing a normal writable value updates the
  native field, the dirty marker, the slice invalidation, and the value read
  back. They also prove that the topology keys and the compatibility keys
  stay unwritable.
- Apply tests prove four things: a failed apply changes nothing, the activity
  text is right for each scope, a preset change can be reverted or reversed
  with an inverse patch, and project Undo applies only to object overrides.
- Evals and the deterministic agent's fixtures use explicit scopes before the
  old schema is removed. There is no phase in which both schemas are
  accepted.

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
| key on the write blacklist | `unsupported_setting_mutation` |
| read of a print host address or credential key | `withheld_setting`, per key under `unknownKeys`; the other keys are still read |
| requested object is gone, or no FFF printer or process is selected | `workspace_unavailable` |
| parse failure, bound violation, or layer height outside the printer's range | `invalid_setting_value` with `allowed` or bounds |
| a scope-specific dependency or validator rule conflicts | `incompatible_settings` with the conflicting keys |
| session, revision, target mapping, preset, or before value changed since preview | `stale_workspace` with expected and current |
| client cancelled or the app closed | `cancelled` |
| the batch call failed after validation | `execution_failed` |

Preview never fails because of what the patch contains. It reports the
problem in its result. Apply fails with the first blocking code and changes
nothing. Never turn an invariant failure into a success.

### Orca entry points the tools use

- **Applying a batch of process changes atomically.**
  `Tab::load_config(const DynamicPrintConfig&)` in `src/slic3r/GUI/Tab.cpp`
  compares the given config with the edited preset and sets every key that
  changed. It then runs `update_dirty()`, `reload_config()`, and `update()`
  once. `TabPrint::update()` runs the FFF normalizer and reaches
  `Plater::on_config_change`, which invalidates slicing the way a sidebar
  edit would. So an apply does this: build a config that holds only the
  changed keys, then call
  `wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff)`.
- **Batches of filament and printer changes.** These use the same
  `Tab::load_config`, on `Preset::TYPE_FILAMENT` or `Preset::TYPE_PRINTER`.
  Before that, preview must model that tab's dependencies, its consequences
  for compatibility, and the conditions under which it would open a dialog.
  A scope is not complete until a test against the real workspace adapter
  proves the native fields, the dirty marker, the slicing invalidation, and
  the values read back.
- **Parsing.** Clone the config for the scope and call
  `set_deserialize(key, text)` for each key. OrcaSlicer throws for an unknown
  key and for a value it cannot parse. The canonical value is
  `option->serialize()` after parsing.
- **Bounds and enums.** These come from `ConfigOptionDef` in
  `src/libslic3r/Config.hpp` (`type`, `label`, `category`, `tooltip`,
  `sidetext`, `min`, `max`, `mode`, `readonly`, `enum_values`,
  `enum_labels`). Treat `min` and `max` as absent when they are the float
  limits.
- **Validation.** Build the full configuration that would be in effect, with
  the cloned config for the scope put in place. Then run the validator that
  belongs to that owner. Process and object continue to use
  `Slic3r::validate(const FullPrintConfig&)`. Do not assume that process
  normalization covers the dependencies of filament or printer settings.
- **Dirty state.** Use the APIs of the `PresetCollection` that holds the
  preset: the one for dirty state and the one for the difference from the
  parent. The result must name the preset and say whether it is dirty after
  the call.

### The process normalizer can open dialogs and rewrite values

The normalizer is two functions in
`src/slic3r/GUI/ConfigManipulation.cpp`: `update_print_fff_config` and
`toggle_print_fff_options`. They run inside `TabPrint::update()` after every
batch. They evaluate every rule against the whole config as it stands after
the batch, not only against the keys that changed. Some rules open a modal
dialog and then rewrite a value. Other rules rewrite a value without telling
anyone. Both are a problem for a tool. A modal dialog opened while a tool is
executing blocks the GUI thread while the request waits. A silent rewrite
makes the tool's result wrong unless the rewrite is reported.

The tools deal with this in two ways.

First, preview evaluates the same rules against a clone of the config, using
the printer preset and the filament count that the adapter can read. A rule
that fires because of a value that was already there is therefore reported
too.

Second, the two kinds of rule are reported differently. A rule that would
open a dialog is a blocking issue. A rule that rewrites silently is a warning
that names the dependent key and its predicted value
(`normalized_dependency`). Apply then lists the actual rewrite under
`normalized`.

Preview does not copy OrcaSlicer's option tables. It runs OrcaSlicer's own
`ConfigManipulation` on the clone, with no presentation callbacks.

The table lists the rules a process patch can reach, as audited when the
tools were built and again on 2026-10-12. Line numbers drift, so search for
the condition text.

| Rule in Orca | Reached by | Effect | Preview must |
|---|---|---|---|
| `layer_height` at or below epsilon | `layer_height` | modal, reset to 0.2 | refuse with `invalid_setting_value` and the minimum |
| `layer_height` above the printer's `max_layer_height` when that exceeds 0.2 | `layer_height` | modal, clamp | refuse with `invalid_setting_value` and the printer's maximum |
| `seam_slope_type` not `None` and absolute `seam_slope_start_height` at or above `layer_height` | lowering `layer_height` | modal, start height reset to 0 | refuse with `incompatible_settings`; evaluate percent values through `get_abs_value`, 100% or above also trips it |
| `spiral_mode` on and not all of `wall_loops` 1, `top_shell_layers` 0, `sparse_infill_density` 0 | `spiral_mode` and those three keys | yes/no dialog. Yes rewrites nine keys: these three, `enable_support`, `enforce_support_layers`, `detect_thin_wall`, `overhang_reverse`, `timelapse_type`, `enable_wrapping_detection`. No turns `spiral_mode` off | refuse with `incompatible_settings` on `spiral_mode`; the message lists each key that differs with the value it needs |
| `sparse_infill_pattern` without multiline support while `fill_multiline` is above 1 | `sparse_infill_pattern` | silent: `fill_multiline` reset to 1 | predict and report as `normalized_dependency` |
| support-gap rounding block | none; it is inside `#if 0` | inactive | do not predict a change that does not happen |
| `support_style` not in the set for the support type (normal: default, grid, snug; tree: default, tree_slim, tree_strong, tree_hybrid, organic) | `enable_support`, `support_type`, `support_style` | silent: style reset to default | refuse with `invalid_setting_value` and the fitting styles in `allowed`; the patch is fixed by the caller, not rewritten |
| global `enable_support` while `detect_overhang_wall` is off | `enable_support` | silent: `detect_overhang_wall` turned on, once per query state | predict and report as `normalized_dependency` |
| `spiral_mode` on with support or thin walls | `spiral_mode`, `enable_support`, `detect_thin_wall` | yes/no dialog | refuse with `incompatible_settings` (the spiral row above) |
| fuzzy skin in an Extrusion or Combined mode without Arachne | `wall_generator` set to classic | yes/no dialog | refuse with `incompatible_settings` naming `fuzzy_skin_mode` |
| `Tab::on_value_change` for `support_type` in simple mode (style reset) | none: `Tab::load_config` never calls `on_value_change` | inactive for tools | nothing. Its confirmations are [handed to the caller as skipped confirmations](#skipped-confirmations) |
| `toggle_print_fff_options` writes (`enable_arc_fitting`, the slope segment length, `fill_multiline`, `overhang_reverse_threshold`) | none of the new keys directly; the prediction runs the real pass on the clone | silent | report whatever the clone shows |

`spiral_mode` is writable. A patch turns spiral vase on by sending it together with the values it needs, which is what answering Yes does; alone it is refused, because the tools never answer the dialog, and the refusal names each value that differs. Apart from that rule, `toggle_print_fff_options` reads `spiral_mode` only to show, hide, enable or disable fields. A plate that carries its own spiral setting (Plate Settings) overrides the process value when it is sliced; no tool writes that one, and `PartPlate::set_spiral_vase_mode` opens the same dialog.

No rule in either function reads the brim, skirt, seam, speed, surface-pattern, interface, contact-distance, threshold or shell-thickness keys, except to enable or disable those keys' own fields. The audit covered both functions and `on_value_change` in `Tab.cpp`, on 2026-09-16. Open-enum keys (the interface layer counts) carry no `allowed` list. Their enum values are shortcuts, and any number in bounds is accepted. A boolean has no numeric bounds.

Preview must check every condition that can currently open a dialog. That
includes values that were already invalid before the patch: ironing spacing,
first-layer height, XY and elephant-foot compensation, alternate-extra-wall,
infill-lock depth, and fuzzy-skin settings. Spiral mode also checks support,
enforced support layers, thin walls, overhang reversal, timelapse, and
wrapping detection.

The tests against the real workspace adapter assert three things. First, no
top-level dialog appears during apply under any row above. For the support,
brim and style checks, the test applies a tree-support patch with the
support page shown and reads the native fields back. Second, every silent
rewrite that preview predicted appears in the result with its actual value.
Third, a sweep (`verify_every_writable_setting` in
`tests/workspace/real_adapter_harness.cpp`) takes every setting of the
process, filament and printer scopes, one at a time, through preview, apply
and the inverse patch. A blocked key must be refused. Any other must either
be refused by a guard with a reason, or apply with no dialog, read back as
reported, mark the preset changed, and be put back by the inverse patch.
The sweep prints which settings it could not give another value (free-form
strings, points, and lists that hold no value). As of 2026-10-12 it applies
and restores 340 process, 92 filament and 112 printer settings with no
dialog and no failure.

The guards work on the whole configuration, not on the keys of the patch.
The 13 live dialog sites in `update_print_fff_config` each have a refusal in
`check_process_dialogs`. The filament tab has one on this route (max
volumetric speed on every update), in `check_filament_dialogs`; the
unsaved-changes prompt on switching filament is avoided by dropping the edits
first. The printer tab has one (firmware
retraction with wipe), in `check_printer_dialogs`.

If someone later finds a rule that a tool can trigger, that is a defect in
the preview. It is not behavior to accept. After an upstream update, read
both functions and the three tabs' `update` and `toggle_options` again for
new dialog sites, extend the guards and this table, and run the sweep.

### Skipped confirmations

A **skipped confirmation** is a confirmation OrcaSlicer asks of a person who
makes a change by hand, which a settings patch skips. It is not a refusal:
the patch is valid and applies as sent. Preview hands it to the caller as a
warning with code `skipped_confirmation`, and the caller settles it. (Until
2026-10-14 the code was `orca_question`; Kenneth asked for a name that says
what it is.) The refusals in the table above are the other kind: a change
OrcaSlicer would reset or block is refused, and nothing is applied.

`Tab::on_value_change` in `Tab.cpp` runs when a person changes one field. For
some settings it asks for confirmation first. `Tab::load_config` never runs
it, so a patch changes those settings and nobody is asked. `Tab::save_preset`
asks once more before it saves a filament whose first-layer temperature is
too far from the other layers' (`Tab::validate_filament_temperature_pairs`).

Kenneth decided this on 2026-10-12. The caller is an agent, and it settles
each skipped confirmation itself, one of three ways: go ahead; go ahead and tell the user;
or ask the user first, when the answer is theirs to give (it depends on what
the object is for, or it accepts a risk to their printer). No tool refuses
these changes, waits for the person to agree, or records that they did. The
description of `settings_preview_patch` tells the caller this.

Preview adds one warning per skipped confirmation, with code
`skipped_confirmation`:

- `key` is the setting the confirmation is about.
- `message` is the confirmation as the person would read it, whole. It comes
  from the translation catalog, so it is in the interface language.
- `suggestions` are the values OrcaSlicer's Yes would set, as `key=value`,
  leaving out any that already hold. A caller accepts by adding them to the
  patch.
- `min` and `max`, for layer height, are the nozzles' limits; for a
  first-layer temperature, the values OrcaSlicer saves without asking.

With `persistAs`, preview adds the save-time one too, one warning per
temperature pair, keyed by the first-layer setting
(`skipped_save_confirmations`); apply answers Continue for the save.

The warning leaves the patch valid, and the patch applies as sent. The apply
result does not repeat it.

The rules are `skipped_edit_confirmations` in `SkippedConfirmations.hpp`. They are a
copy of the conditions in `Tab.cpp`, and they look each message up in the
catalog piece by piece, as `Tab.cpp` does. Three things follow from how
OrcaSlicer asks:

- A confirmation is about a setting the patch really changes. A value the
  normalizer puts straight back (clumping detection on a printer without it)
  raises nothing.
- The condition is read from the values the patch leaves. A patch that sends
  the companion value with the change (smooth timelapse together with the
  prime tower) raises nothing.
- A state a person chose earlier by answering "do it anyway" raises nothing
  on a later patch that leaves those settings alone.

An object override raises the same confirmations as the process setting, because
OrcaSlicer's object panel runs the same handler.

| Setting | OrcaSlicer asks when the change leaves | Yes sets |
|---|---|---|
| `enable_prime_tower` off | smooth `timelapse_type`; and then a second confirmation if `enable_wrapping_detection` is on. Clumping detection alone raises nothing | nothing (No turns the tower back on) |
| `enable_prime_tower` on | `precise_z_height` on | nothing |
| `enable_wrapping_detection` on | the prime tower off | nothing |
| `precise_z_height` on | the prime tower on | nothing |
| `timelapse_type` smooth | the prime tower off | `enable_prime_tower=1` |
| `print_sequence` by object | a printer whose `printer_structure` is `i3` | nothing |
| `support_filament` | a filament OrcaSlicer counts as support material (`is_support_filament`, not strict) that is not soluble, in a project where no object uses TPU | nothing (No sets it back to 0) |
| `support_interface_filament` | a support material without all of top Z distance 0, interface spacing 0 and the interlaced rectilinear pattern; or a soluble interface over a base that is not | those three values, `independent_support_layer_height=0`, and sometimes `support_filament` |
| `make_overhang_printable` on | always | nothing |
| `sparse_infill_rotate_template` given where there was none | a pattern other than rectilinear, line, zigzag, cross zag or locked zag | nothing |
| `layer_height` | a height below the lowest `min_layer_height` or above the highest `max_layer_height` of the printer's nozzles | the nearer limit |
| `long_retractions_when_cut` (printer) or `filament_long_retractions_when_cut` (filament) on for an extruder | always. This one has only an OK button: OrcaSlicer informs, it does not ask | nothing |

The copy is held to the original by the workspace adapter harness. It makes
each change the way a field does (the value into the tab's config, then
OrcaSlicer's own `on_value_change`), with every dialog the handler opens
answered and written down, and compares those dialogs with the skipped
confirmations preview reported. `verify_skipped_confirmations` does this for a case per row and
for the cases that must raise nothing. `verify_every_writable_setting` does
it for every setting it applies, so a confirmation upstream adds, drops or
rewords fails the sweep with `Orca asks ..., preview forwarded ...` and the
setting's name. After an upstream update, run the harness and bring the
rules and this table back in line with `Tab.cpp`.

`on_value_change` also does a few things without asking, which a patch skips
and no tool does in its place. After a long retraction switch changes, it
recalculates the flush volumes when the "auto calculate flush" preference is
"all". It keeps `filament_diameter` and `pellet_flow_coefficient` in step
with each other. On a Bambu printer with several extruders it copies a
changed `min_layer_height` or `max_layer_height` to every extruder. The
printer tab's own field handler updates `thumbnails_format` from
`thumbnails`. Whether a patch should do any of these is undecided.

### Apply binds the previewed values

A settings change is previewed three times.

1. The client previews and sees the values before and after.
2. When the coordinator queues the action, it previews again on the GUI
   thread. An invalid patch therefore fails at once. The coordinator stores
   that result as the previewed set, and the activity title names the keys
   from it.
3. At execution, the adapter previews a third time, against the config as it
   is at that moment. It compares every "before" value with the previewed
   set. A value that has moved is `stale_workspace`. A blocking issue is
   `invalid_settings`. In both cases nothing is changed.

"Before" values supplied by the client are never trusted. Sometimes
OrcaSlicer normalizes a value differently from the preview. In that case the
result returns the actual value and lists the key under `normalized`. The
change did happen, so the call is a success, and the result says truthfully
what happened.

### Per-object overrides

A read with `scope: "object"` and `target: {"objectId": "..."}` returns the
value the object prints with. That is the object's own override where its
`ModelConfig` has one (`overridden: true`), and the process value otherwise.

A patch is checked against the process config with the object's overrides and
the patch applied on top. The checks are the same as for a process patch:
parsing, bounds, the conditions that would open a dialog, the support style
rule, and `Slic3r::validate`. A key that is neither a `PrintRegionConfig`
option nor a `PrintObjectConfig` option is refused with `unsupported_scope`.
That is ObjectList's own list for a whole object. No dependencies are
predicted. OrcaSlicer runs the normalizer only on the process preset and on
edits made in the object settings panel, so an override is stored as written.

Apply does the following:

1. takes one `Plater::TakeSnapshot` ("Change object settings");
2. writes each key with `ModelConfig::set_key_value`;
3. calls `ObjectList::object_config_options_changed`, so that the list shows
   the settings item;
4. calls `Plater::changed_object`, so that the slicing of that object is
   invalidated.

The result says `projectUndo: true`, and project Undo removes the override.
The activity names the object. The coordinator looks the object up in the
current snapshot twice, when the action is queued and when it executes, and
fails with `missing_object` when the object is gone. The adapter treats a
missing target as a broken precondition. No value in a patch removes an
override. Only Undo, or the object settings panel, does.

### Registry facts worth knowing

- `ToolExecutionCoordinator::execute` dispatches on `ToolHandler`, with one
  explicit block of code per tool. Add a block for each new tool. Do not
  replace the blocks with a generic dispatch table.
- `kInvalidatingReasons` is the mask of change reasons that make most queued
  actions stale. It includes `Settings`, so a queued action fails with
  `stale_revision` when the user edits a setting in the GUI.
  `settings_apply_patch` has its own mask, `Settings | Project`. The reason
  is something the M5 live eval showed: an agent annotated a region between
  its settings preview and its apply, and that model edit (and the slicing
  events after it) made every apply stale. A settings patch is already bound
  to the "before" values it previewed, and the apply checks those values
  again. So its `expectedRevision` only has to be no older than the last
  settings or project change. Model edits do not matter.
- `ToolRegistry::validate_output` accepts only these schema keywords: `type`,
  `properties`, `required`, `additionalProperties`, `items`, `minimum`,
  `maxItems`, `enum`, `maxLength`. Any other keyword throws. Before using a
  new keyword, extend the validator and add a test. `enum` is checked
  whatever the value's type. `maxLength` counts UTF-8 bytes, which matches
  the byte bounds this registry states everywhere else.

### Upstream seams the tool system owns

The tool system has made these changes to files OrcaSlicer owns:

- `src/CMakeLists.txt`: two added lines.
- `src/dev-utils/platform/unix/build_linux_image.sh.in`: eight added lines.
- `Plater::on_config_change`: one additive call at the end,
  `notify_project_state_changed(ProjectStateChangeReason::Settings)`.
- `GUI_Utils.hpp`: one added declaration, and two lines at the top of
  `DPIAware::ShowModal` (`answer_modal`, which is defined in fork code). If
  upstream changes that function, keep both.
- `Plater::priv::load_files`: a `FileLoadScope` as its first line, and a call
  after `load_config_model` that marks the settings that were actually
  applied.
- The file-open entry points: a `FileOpenScope`, so that choices made before
  loading begins are included.
- `Plater::load_project`: the two model metadata resets at its start were
  moved below its cancelable checks, so that an aborted open leaves the
  project intact. On a rebase, carry those resets to that later point.
- `FileLoads.hpp` is reached through `ProjectState.hpp`, a neutral
  forwarding header.
- `libslic3r/Orient.hpp` and `.cpp`: a new function `score_orientations`,
  added beside `orient()`. It reuses `AutoOrienter` and duplicates the five
  lines of `process()` that build OrcaSlicer's candidate set.

A warning about `Orient.cpp`. Current upstream (72774e5398) reworks it for
cooling-fan orientation and removes `OrientParamsArea`, which the adapter
copies from today's `OrientJob`. The text merges cleanly. But at that rebase,
`score_orientations` must be checked again against the new `AutoOrienter`,
and the adapter must build its parameters the way the new `OrientJob` does.

`Plater.cpp` is the fork's busiest file. When `Plater::on_config_change`
changes upstream, the natural resolution is "keep both". No line in
`Tab.cpp`, `ConfigManipulation.cpp`, or `PrintConfig.cpp` changes. The
normalizer hazards are handled in fork-owned code, by refusing the inputs
that would trigger them.

## Proportional security growth

Security measures should grow as the server is allowed to do more. Today the server starts automatically, listens only on the numeric loopback address, validates browser origins, and enforces request limits. It has no bearer authentication and no confirmation for each call. Any local process that can reach the socket can execute the offered tools, including mutations and the validated file operations. Origin validation protects against web pages in a browser. It does not identify local processes. Never make this endpoint reachable beyond loopback.

Because any local process can call the read tools, and because the in-app assistant sends tool results to its model provider, a read tool must not return a credential. The settings tools withhold the print host keys (see standing decision 6 under [Settings tools](#settings-tools-contract-and-hazards)). Before a new read tool returns a value taken from a preset, the app configuration, or a device record, check whether it can hold a secret.

Review the security design again before adding any of these: remote access, grants that last across restarts, more than one permission level, camera data, upload to a printer, or starting a physical print. At that point, evaluate authenticated pairing, durable credential storage, scopes per client, revocation, rate controls, a privacy review, and a dedicated audit log.

Paths supplied by a caller stay tightly restricted. `project_open` and `object_import_file` accept absolute paths that can be read. `export_file` requires the expected extension, a parent directory that already exists, and an explicit `overwrite` flag to replace a file. It refuses OrcaSlicer's data directory and its temporary project directories. `project_attachment_read` accepts only an attachment ID, which it resolves inside the project attachment directory. These restrictions limit accidental damage. They do not replace authenticating local clients.

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

The workspace adapter harness also runs on that Windows machine, and passes (617 checks, exit 0, as of 2026-09-17). Put the app's `src\Release` on `PATH` first; otherwise the exe cannot load the `api-ms-win-crt` DLLs. In PowerShell, read the exit code with `$LASTEXITCODE`:

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

Also run the shell harness in these modes:

- `--mcp` for direct HTTP;
- `--mcp-setup` for the isolated setup-command fixtures;
- no argument for the normal shell regression;
- `--stock` for stock behavior.

`--manual-mcp <dedicated-temporary-directory>` provides a disposable two-plate fixture for testing with real clients. Do not use the user's normal data directory. The checks for the Linux helper and launcher are documented in [the test README](../../tests/mcp/linux/README.md).

What has actually been run:

- native checks of reads, mutations, Undo, staleness, cancellation and shutdown;
- real Claude Code and Codex CLI sessions covering reads, writes, a restart on a changed port, and offline errors, using session-only configuration;
- Linux ARM64 container tests of the production helper, and of a compressed AppImage transport fixture. Those Linux tests are not a full GUI release image.

In one macOS test, Quit was attempted during a setup fixture. The app retained the monitor and later reaped the child processes that had timed out. That does not prove how the app behaves when the operating system forces a shutdown.

Still unverified:

- persistent one-click writes against real client installations;
- read, write, restart, and offline workflows with desktop clients, including whether local Cowork is available;
- complete Windows and Linux release packages;
- macOS universal signing and notarization;
- the remaining DPI and live-theme checks.

The development macOS bundle failed strict signature verification, and the existing CI signing step is restricted to the upstream OrcaSlicer repository. A successful local CLI test, a packaging rule, or the presence of signing identities does not mean that any of these gates has passed.

## Definition of done

A new tool is done when:

- it fixes a named eval failure;
- its stated scope is true, and it really acts through the OrcaSlicer owner of the state;
- the registry is the only place where it is defined;
- the coordinator owns its policy and its lifecycle (queuing, staleness, cancellation, completion);
- the real operation runs on the GUI thread, through OrcaSlicer's current owner;
- its results and errors are structured and limited in size;
- mutation history, staleness, events, and cancellation behave as documented;
- the evidence that applies passes: fake workspace, real workspace adapter, tool adapters, protocol, and shell;
- no unrelated OrcaSlicer behavior changes when the JusPrin feature is disabled; and
- the PR records any upstream seam and its rebase evidence.

Success is not measured by the number of tools. It is measured by whether a compact catalog reliably completes real printing tasks.
