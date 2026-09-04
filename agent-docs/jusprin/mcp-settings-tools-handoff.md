# Process-settings tools implementation handoff

**Status:** Implemented and verified on macOS, including the live in-app Agent and Codex CLI acceptance scenario. The MCP catalog contains five tools; legacy proving tools remain available only to the in-app Agent. Verification and commit order are recorded in [PR #33](https://github.com/TheSpaghettiDetective/JusPrin/pull/33).

This handoff is for an engineer who has not followed the MCP work. It is self-contained: it describes what exists today, what the settings tools must do, where in OrcaSlicer they must hook in, how to test them, and how to clean up afterwards. Earlier planning and verification records were deliberately removed from the repository, so do not look for them; the facts they established that still matter are restated here.

Read these before changing code:

- [the guide for adding JusPrin tools](mcp-tool-extension-guide.md), for the contract checklist every registry entry must satisfy;
- [OrcaSlicer integration guide](orca-integration-guide.md), for how fork code reaches live Orca owners;
- [engineering and verification method](engineering-method.md), for the evidence every phase must record;
- [fork stewardship](fork-stewardship.md), before touching any file outside `src/slic3r/GUI/JusPrin/`.

## Where the MCP work stands today

JusPrin has one tool system with two adapters. The in-app Agent page and external MCP clients both execute tools through the same registry, the same coordinator, and the same live-workspace boundary.

**Runtime.** `AgentHost` owns `McpRuntime`, which owns a loopback Streamable HTTP server on one Boost.Asio worker thread. The server binds `127.0.0.1`, tries port 47301 first and falls back to an OS-assigned port on collision. There is no bearer token; loopback binding and exact Origin matching are the protections, the same posture as Figma's and JetBrains' local servers. Every request carries the 2026-07-28 protocol revision in its metadata and mirrored headers; the server implements `server/discover`, `tools/list`, and `tools/call` only. Reads answer with JSON; approval-gated calls answer with a request-scoped SSE stream that ends with the terminal result. Closing that stream cancels the call. The shell owns the single 33 ms pump that drives the coordinator, so a page reload never suspends MCP work.

**Discovery.** On startup the runtime writes `<data_dir>/jusprin/mcp.json` beside the existing recovery directory, atomically and owner-readable, holding the live URL, process id, a per-runtime instance id, the app version, and the protocol versions. Its destructor removes the file only if the file still names its own instance id. Readers validate the file, the pid, and the loopback URL, and must probe liveness over HTTP before trusting it. On Linux the data directory follows `XDG_CONFIG_HOME` with a `~/.config` fallback; a portable `data_dir` folder beside the executable overrides all platforms.

**Bridge.** `jusprin-mcp` is a small executable built beside the application binary from wx-free sources only: the registry, the tool result renderers, the protocol codec, and the discovery file. It links no wxWidgets, no slicer library, and no OpenSSL. A harness launches it as a stdio server. It speaks the legacy `initialize` handshake to the harness by default, answers `server/discover` for modern harnesses, forwards everything to the running app over HTTP with synthesized 2026-07-28 metadata, relays progress, maps `notifications/cancelled` in both eras to closing the forwarded connection, and serves the catalog from the compiled registry when the app is closed while returning `workspace_unavailable` on calls. It never launches JusPrin. Environment overrides `JUSPRIN_MCP_URL` and `JUSPRIN_MCP_DISCOVERY` exist for developers. On Linux the AppImage launcher forwards `--mcp-bridge` to the packaged helper.

**Connect dialog.** The Agent pane's "Connect AI tools..." dialog lists Claude Code, Codex, Claude Desktop, Cowork, Cursor, and VS Code, marks which are detected, shows the exact command or JSON entry with a Copy button, and offers a confirmed one-click Connect that runs the harness CLI or edits the JSON file with a backup. A disclosure shows the live URL for direct HTTP use.

**Approval.** An MCP-originated mutation carries an explicit `ToolSource::Mcp` on its activity. The Agent page renders those in a project-level "External AI tools" section with Approve and Reject buttons that work independently of chat state. Real-client testing found and fixed the earlier gap where such cards did not render at all; tests must click rendered buttons, never a hidden decision hook.

**Catalog.** Five tools are exposed to MCP: `workspace_inspect`, a bounded read of project context; `settings_search`, paged process definitions; `settings_get`, current values and preset origin; `settings_preview_patch`, validation and normalization prediction without mutation; and `settings_apply_patch`, an approval-gated atomic patch. `inspect_selection` and `duplicate_object` remain in-app proving fixtures and are not advertised or callable over MCP. `import_model` is in-app only and three manufacturing-record tools are internal.

**Verification status.** The settings slice passed Codex CLI 0.153.0 search, read, preview, rendered rejection/approval/inverse, native sidebar stale-revision replay, invalid printer bound, and app-closed checks. The live in-app gpt-5.4-mini scenario also passed all four settings tools and approval decisions. The following older bridge verification remains separate: Claude Code 2.1.259 and Codex CLI 0.153.0 have completed live read, reject, approve, restart-with-port-change, and app-closed scenarios through the bridge with session-only configuration. Claude Desktop, Cursor, VS Code, and local Cowork sessions have not been exercised. Persistent one-click writes have not been tested against a real client. Windows is untested. The development bundle fails `codesign --verify --deep --strict` for a resources reason not yet attributed, so release signing of the nested helper is unverified. Codex needs `tool_timeout_sec` raised above its 60-second default for approval-gated calls.

**Upstream footprint of the existing work.** Two added lines in `src/CMakeLists.txt` and eight in `src/dev-utils/platform/unix/build_linux_image.sh.in`, all additive. Every other MCP source is fork-owned.

### Existing test assets and commands

```sh
/Applications/CMake.app/Contents/bin/cmake --build build/arm64 --config RelWithDebInfo --target all -- -j6
build/arm64/tests/agent/RelWithDebInfo/agent_bridge_tests.app/Contents/MacOS/agent_bridge_tests --order rand --warn NoAssertions
build/arm64/tests/workspace/RelWithDebInfo/workspace_contract_tests.app/Contents/MacOS/workspace_contract_tests
build/arm64/tests/workspace/RelWithDebInfo/JusPrinWorkspaceHarness.app/Contents/MacOS/JusPrinWorkspaceHarness
python3 -m unittest discover -s tests/mcp -v
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness            # normal shell
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --stock    # stock mode
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --mcp     # direct HTTP scenario
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --mcp-bridge
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --mcp-setup
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --manual-mcp /tmp/fixture-dir
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --live-agent
```

- `tests/agent/`: Catch2 suites for the bridge page contract, project state, coordinator, registry, MCP protocol, runtime, discovery, connections, and the bridge core; `mcp_test_client.hpp` is a nonblocking HTTP client and `mcp_test_directory.hpp` a temporary-directory fixture.
- `tests/workspace/`: the wx-free contract suite against `FakeWorkspace`, and the real-adapter harness against the live Orca model.
- `tests/mcp/`: a Python smoke client, a built-process suite that drives the real bridge over pipes against a controlled HTTP peer, and Linux packaging tests.
- `tests/shell/`: the native harness with the modes above. `--manual-mcp` keeps a real two-object fixture alive at a stable discovery path for real-client runs. `--dark-ui` and `--light-ui` are macOS appearance switches.

## Outcome

When this slice is complete:

- an external agent can search the active FFF process preset's settings, read current values with their origin, preview a multi-key change, and apply it atomically after approval in JusPrin;
- the in-app Agent has the same four tools through the same registry, with no adapter-specific code;
- an applied change is visible in Orca's settings UI, marks the preset dirty, invalidates slicing through Orca's normal path, and can be reverted with Orca's existing preset revert control or an explicit inverse patch;
- `settings_apply_patch` is the approval-path fixture for every MCP test, and `inspect_selection` and `duplicate_object` are no longer advertised to MCP;
- the MCP catalog is `workspace_inspect`, `settings_search`, `settings_get`, `settings_preview_patch`, and `settings_apply_patch`.

## Decisions already made

These are constraints, not open questions.

1. **The server is embedded in the live GUI process.** The project, selection, presets, history, and slicing state belong to the open OrcaSlicer window. There is no headless or shadow project.
2. **MCP is an adapter, not a second automation system.** Both adapters feed one coordinator and one workspace boundary. No adapter calls `Plater`, `Model`, `PresetBundle`, wxWidgets, or slicing objects directly.
3. **One registry defines each tool.** Names, titles, descriptions, schemas, action classes, exposure, validation, and handler association live once. Adapters project and filter; they never redefine.
4. **The coordinator trusts the registry, not the caller.** A request cannot label its own operation read-only.
5. **Settings are data.** Four generic tools over searchable records; never one tool per setting.
6. **Only the active FFF process preset's edited configuration is in scope.** No `scope` field, no printer, filament, plate, object, or modifier layers until each is a separately tested capability.
7. **Metadata, parsing, and serialization come from Orca's own `print_config_def` and config option machinery.** No parallel table of types, enum values, ranges, units, or aliases.
8. **Search and read cover every process-setting definition; mutation starts with a reviewed allowlist:** `layer_height`, `wall_loops`, `sparse_infill_density`, `sparse_infill_pattern`, `top_shell_layers`, `bottom_shell_layers`, and `brim_width`. Every search and read record says whether the key is writable. Other keys return `unsupported_setting_mutation`. Expand the list only after the real-adapter tests cover the option type, dependency behavior, and visible UI update for each new key.
9. **A batch applies whole or not at all, through one current Orca owner.** Never mutate the config behind the visible preset UI and imitate the notifications.
10. **Process preset edits are not in project Undo.** Results say so and return previous values so a caller can propose the inverse. No separate settings undo model.
11. **A preview that finds invalid settings is a successful call with `valid: false`.** Malformed input is a tool error.
12. **All values cross JSON as strings in their canonical Orca serialization.** Callers may send numbers or booleans; the decoder converts them before parsing. All Orca ids cross JSON as strings.
13. **Output is bounded and every tool has an output schema.** Results carry both `structuredContent` and the same JSON in one text block.
14. **Security posture is unchanged.** No token, loopback only, Origin validated, mutations approved in JusPrin. Nothing in this slice adds remote access.

## Settings specification

### Source model for every value

Each returned value states the active process preset name, the current canonical value, whether the key differs from the selected preset, whether it differs from the system parent, and the workspace session id and revision at read time. Do not claim full printer, filament, project, or object inheritance; the contract cannot prove it yet.

### Bounded read contracts

- `settings_search`: default 10 and maximum 25 matches, deterministic order, optional cursor.
- `settings_get`: at most 32 keys per call.
- `settings_preview_patch` and `settings_apply_patch`: at most 32 keys per patch.
- Descriptions: the short label plus a bounded description, never a whole help page.
- Every list result carries `items`, `nextCursor` when applicable, and `truncated`.

### Preview semantics

`settings_preview_patch` accepts a `changes` object keyed by canonical Orca setting key. It clones the current edited process config, parses each value through Orca's option machinery, applies all requested values to the clone, and validates the result. It returns `valid`, the normalized changes with before and after values, warnings and blocking issues, suggestions for unknown keys, allowed enum values or numeric bounds when available, and the session id and revision used. It changes no Orca state and publishes no event.

### Apply semantics

`settings_apply_patch` accepts the normalized changes plus `expectedSessionId` and `expectedRevision`. The coordinator also pins the proposal to the live snapshot. On approval the real adapter must:

1. recheck the session and revision;
2. re-run parsing and validation against the then-current config;
3. apply the whole batch or none of it through the existing preset and Tab path;
4. let Orca update dirty state and dependent UI, and invalidate background slicing as it normally does;
5. emit one coalesced workspace change with a `Settings` reason and the resulting revision;
6. return the previous values so the caller can propose the inverse, and state that project Undo does not cover the change.

### Error codes

| Condition | Code |
|---|---|
| key not in the definition table | `unknown_setting` with suggestions |
| key not a process option | `unsupported_scope` |
| readable key outside the allowlist, on apply or as a preview issue | `unsupported_setting_mutation` |
| parse failure, bound violation, or layer height outside the printer's range | `invalid_setting_value` with `allowed` or bounds |
| spiral-mode conflict or a validator message on a touched key | `incompatible_settings` with the conflicting keys |
| session or revision mismatch, or a before value moved since preview | `stale_workspace` with expected and current |
| user rejected in JusPrin | `approval_rejected` |
| client cancelled or the app closed | `cancelled` |
| no FFF project or no process preset | `workspace_unavailable` |
| the batch call failed after validation | `execution_failed` |
| malformed arguments | `invalid_arguments` |

Preview never fails for content reasons; it returns `valid: false` with issues. Apply fails with the first blocking code and mutates nothing. Never convert an invariant failure into success.

## Findings from the code that shape the implementation

### The atomic batch owner is `Tab::load_config`

`Tab::load_config(const DynamicPrintConfig&)` in `src/slic3r/GUI/Tab.cpp` is the batch path Orca uses when loading a config into a preset. It diffs the incoming config against the edited preset's config, sets every changed key, then calls `update_dirty()`, `reload_config()`, and `update()` once. For the process tab, `TabPrint::update()` runs the FFF config normalizer, refreshes dependent field states, and calls `MainFrame::on_config_changed`, which calls `Plater::on_config_change`. That last call applies the change to the Plater's config and lets Orca invalidate slicing the way it does for sidebar edits.

So apply is: build a `DynamicPrintConfig` holding only the changed keys, then `wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff)`. No per-key loop, no manual notification, no change to `Tab.cpp`.

### Hazard 1: the normalizer can open modal dialogs and rewrite values

Two functions in `src/slic3r/GUI/ConfigManipulation.cpp` run inside `TabPrint::update()` after every batch: `update_print_fff_config` and `toggle_print_fff_options`. They evaluate every rule against the whole post-batch config, not only rules about the changed keys. Some rules open a modal dialog and then rewrite a value; others rewrite silently. A modal dialog opened from inside a tool execution blocks the GUI thread while the MCP request waits, and a silent rewrite makes the result lie unless it is reported.

These are the rules reachable through the seven allowlisted keys as of this writing. Line numbers are approximate and will drift; search for the condition text.

| Rule in Orca | Reached by | Effect | Preview must |
|---|---|---|---|
| `layer_height` at or below epsilon | `layer_height` | modal warning, reset to 0.2 | refuse with `invalid_setting_value` and the minimum |
| `layer_height` above the printer preset's `max_layer_height` when that is greater than 0.2 | `layer_height` | modal warning, clamp | refuse with `invalid_setting_value` and the printer's maximum |
| `seam_slope_type` is not `None` and the absolute `seam_slope_start_height` is at or above `layer_height` | lowering `layer_height` | modal warning, `seam_slope_start_height` reset to 0 | refuse with `incompatible_settings` naming `seam_slope_type` and `seam_slope_start_height`; evaluate percent values through Orca too; 100% or above also trips this |
| `spiral_mode` on and not all of `wall_loops` 1, `top_shell_layers` 0, `sparse_infill_density` 0 | `wall_loops`, `top_shell_layers`, `sparse_infill_density` | yes/no dialog, several keys rewritten either way | refuse with `incompatible_settings` naming `spiral_mode` |
| Support-gap rounding block under `#if 0` | none in this build | inactive; `support_top_z_distance` is not rewritten | do not predict or apply a nonexistent change; verify the value stays unchanged |
| `sparse_infill_pattern` set to a pattern without multiline support while `fill_multiline` is above 1, in `toggle_print_fff_options` | `sparse_infill_pattern` | silent: `fill_multiline` reset to 1 | predict and report as `normalized_dependency`; apply lists it under `normalized` |

Two rules follow from this table. First, the preview evaluates the same rule set against the post-batch clone, using the printer preset and filament count the real adapter can read, so a rule that fires because of a pre-existing value is reported too. Second, dialog rules are blocking issues and silent rules are warnings that name the dependent key and its predicted value. The real-adapter test asserts that no top-level dialog appears during apply for any allowlisted key under every condition in the table, and that every silent rewrite the preview predicted appears in the result's `normalized` list with the actual value.

The table is the current audit, not a guarantee. Phase 1 re-reads both functions for every key they read, adds any rule an allowlisted key can reach, and records the audit in the pull request. A rule found later that a tool can trigger is a defect in the preview, not accepted behavior.

### Implementation audit against the current checkout

The Phase 1 source audit corrected three assumptions in the table above:

- The support-gap rounding block is inside `#if 0` in `ConfigManipulation.cpp`. The implementation and fake use the active multiline reset as their silent-normalization fixture; they do not invent a support-gap change. The support-gap value remains unchanged.
- A percent scarf start height can trigger the dialog at 100% or above. Preview uses Orca's `get_abs_value` for both absolute and percent values.
- Every active dialog predicate must be checked, including pre-existing invalid ironing spacing, first-layer height, XY compensation, elephant-foot compensation, alternate-extra-wall settings, infill-lock depth, and fuzzy-skin settings. Spiral mode also checks support, enforced support layers, thin walls, overhang reversal, timelapse, and wrapping detection.

After these predicates pass, preview invokes Orca's `ConfigManipulation` on its cloned config with no presentation callbacks. This reuses the authoritative silent-normalization rules, including multiline support, instead of copying their option tables. `TabPrint::update` skips option toggles when there is no active process page or the Dependencies page is active; prediction follows that condition using the public field/page lookup.

Live in-app verification also exposed a stateless OpenAI continuation defect: after the first tool call, the adapter discarded the original user request, workspace context, and earlier tool results. A regression reproduced a three-item continuation where five items were required. The adapter now retains the complete turn input and appends each response and tool result, while dispatching only the newest response's tool calls. This is shared conversation transport behavior, not a second settings implementation.

Real-adapter verification has demonstrated a successful batch before explicitly opening the process settings page, a single coalesced `Settings` event and revision, unchanged project Undo availability, native field updates and preset revert, inverse patches, stale confirmed values, and no modal calls in the tested cases. Runtime proof and the full completion gate remain required below.

### Hazard 2: no change reason exists for settings

`WorkspaceChangeReasons` in `src/slic3r/GUI/JusPrin/Workspace/Workspace.hpp` and `ProjectStateChangeReason` in `src/slic3r/GUI/JusPrin/Workspace/ProjectState.hpp` have `Selection`, `Contents` or `Objects`, `History`, `Transform`, `Plates`, and `Project`, and no `Settings`. Nothing publishes a workspace change when a preset value changes. The fork's notification seam already has additive `notify_project_state_changed` calls inside `Plater.cpp`. Add `Settings = 1u << 6` to both enums and one additive call at the end of `Plater::on_config_change`. That is the only Orca-owned line this slice touches.

### Validation and metadata entry points

- Parsing: clone the edited config, then `set_deserialize(key, text)` per key inside a try block. Orca throws for unknown keys and unparseable values; translate those to `unknown_setting` and `invalid_setting_value`.
- Canonical value: `option->serialize()` after parsing.
- Bounds and enums: `ConfigOptionDef` in `src/libslic3r/Config.hpp` exposes `type`, `label`, `full_label`, `category`, `tooltip`, `sidetext`, `min`, `max`, `mode`, `readonly`, `enum_values`, and `enum_labels`. Treat `min` and `max` as absent when they are the float limits.
- Configuration validation: `Slic3r::validate(const FullPrintConfig&)` in `src/libslic3r/PrintConfig.cpp` returns a map from key to message. Build the input from `wxGetApp().preset_bundle->full_config()` with the patched process keys applied, then apply that into a `FullPrintConfig`. Messages for touched keys are blocking issues; messages for untouched keys are warnings, because the preset was already invalid before the patch.
- Slicing-time checks in `Print::validate` are not part of preview. Orca's own UI defers them to slicing too.
- Dirty state: `PresetCollection::current_dirty_options()` gives keys that differ from the selected preset, and `current_different_from_parent_options()` gives keys that differ from the system parent. The preset name is `prints.get_edited_preset().name`. The Tab's per-option revert buttons read the same data, so they keep working after a tool apply.

### Registry and adapter facts

- `ToolExecutionCoordinator::execute` in `src/slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.cpp` dispatches on `ToolHandler` with explicit blocks. Add four values and four blocks; do not add a generic dispatch table.
- The coordinator's `kInvalidatingReasons` is a single global mask covering contents, transform, plates, history, and project. Add `Settings` to it. A pending proposal of any kind then fails with `stale_revision` when the user edits a setting in the GUI, which is conservative and consistent with how a content edit already behaves. A per-tool mask is a later refinement if evals show friction. Independently, apply receives the before values the user saw at approval time and re-checks them, so an edit to a touched key is caught even if the revision guard changes later.
- The coordinator's `propose` path validates argument shape only. For `settings_apply_patch` it must also run a preview at proposal time, on the GUI thread, so that an invalid patch fails immediately without an approval card, and so that the before and after values the user approves are the ones apply later checks. Those confirmed changes are stored in the activity's normalized arguments; the approval title and the card read them from there.
- `ToolRegistry::validate_output` in `ToolRegistry.cpp` uses a closed schema vocabulary in `matches_schema`: `type`, `properties`, `required`, `additionalProperties`, `items`, `minimum`, `maxItems`. Any other keyword throws. New output schemas stay inside that vocabulary; add `maximum` to the validator if a schema needs it, with a test.
- `ToolRegistry::approval_title` currently ignores the arguments. The apply tool needs a title that names the keys, so make the function use the normalized arguments for that handler.
- The OpenAI adapter renders its tool list from the registry. Live verification found that optional search arguments and the dynamic patch map cannot use OpenAI strict mode: the API rejected the patch schema with HTTP 400 `invalid_function_parameters`. The projection preserves each canonical schema and derives `strict: false` for schemas with optional or open object properties; closed, fully required schemas retain strict mode. Registry argument validation and approval are unchanged. The bridge's offline catalog is compiled from the same registry, so the catalog changes propagate to it automatically. The tests that hard-code the three-tool catalog are listed in the cleanup section.
- Shared result renderers live in `src/slic3r/GUI/JusPrin/Agent/ToolResults.cpp` with `kToolListLimit` of 64 and `kToolLabelLimit` of 256 bytes. Reuse them.

## Design

### Workspace contract

Add wx-free types to `Workspace.hpp`. Names may follow nearby style; keep the fields.

```cpp
struct SettingDefinition {
    std::string key, type, label, category, description, unit;
    std::optional<double> min, max;
    std::vector<std::string> enum_values, enum_labels;
    bool writable{false};
};
struct SettingValue {
    std::string key, value;           // canonical Orca serialization
    bool differs_from_preset{false};
    bool differs_from_system{false};
};
struct SettingsQuery { std::string text; std::size_t limit{10}; std::string cursor; };
struct SettingsSearchResult { std::vector<SettingDefinition> items; std::string next_cursor; bool truncated{false}; };
struct SettingsReadResult { std::vector<SettingValue> items; std::vector<std::string> unknown_keys; };
struct SettingsPatch { std::map<std::string, std::string> changes; };   // key -> value text
struct SettingIssue {
    std::string key, code, message;
    std::vector<std::string> allowed, suggestions;
    std::optional<double> min, max;
};
struct SettingChange { std::string key, before, after; };
struct SettingsPreview {
    bool valid{false};
    std::vector<SettingChange> changes;        // only keys whose value would change
    std::vector<SettingIssue> issues, warnings;
    std::string process_preset;
};
```

Extend `WorkspaceSetup` with `process_preset` and `process_preset_dirty`. Add `Settings` to `WorkspaceChangeReasons`.

Add to `IWorkspace`:

```cpp
virtual SettingsSearchResult search_settings(const SettingsQuery& query) const = 0;
virtual SettingsReadResult   read_settings(const std::vector<std::string>& keys) const = 0;
virtual SettingsPreview      preview_settings(const SettingsPatch& patch) const = 0;
// `confirmed` is the preview the user approved: the coordinator captured it at
// proposal time. Apply re-previews the same patch and refuses if any before
// value moved or any blocking issue exists. `applied` is output only.
virtual CommandResult        apply_settings(const SettingsPatch& patch,
                                            const std::vector<SettingChange>& confirmed,
                                            SettingsPreview& applied) = 0;
```

The flow is: the client calls `settings_preview_patch` and sees before and after values; the client calls `settings_apply_patch`; the coordinator previews again at proposal time and stores those changes as the confirmed set on the activity; the user approves that set; at execution the coordinator passes the confirmed set into `apply_settings`, which previews a third time against the then-current config and compares every before value. A moved before value is `stale_workspace`. The client-supplied `changes` are never trusted as before values; only the coordinator's own preview is.

`apply_settings` fills `applied` with the actual before and after values read back from Orca after the batch. Where Orca normalized a value differently from the preview, the after value is the actual one and the key is listed in `applied.warnings` with code `normalized`. The mutation happened, so this is a success with an honest report, not an error.

`CommandResult` gains `WorkspaceError::InvalidSettings` for blocking issues found at apply time and `WorkspaceError::StaleSettings` for a moved before value; the executor already holds the structured issues from its own preview call and maps the second to `stale_workspace`.

### Fake workspace

`FakeWorkspace` in `src/slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp` carries a fixture table of about a dozen definitions covering each type the allowlist needs: a float with bounds, an integer, a percent, an enum with labels, a boolean, a string, and a read-only key, plus the dependent keys the hazard table names. It stores values as strings, implements search ranking, preview, and apply with the same rules as the real adapter, and mirrors one dialog rule and one silent rule from the Hazard 1 table, the spiral-mode conflict and the active multiline reset, so `incompatible_settings`, `normalized_dependency`, and the `normalized` result list are testable without Orca. It publishes `Settings` on apply and nothing on preview, and its `apply_settings` enforces the confirmed before values.

### Real adapter

`OrcaWorkspaceAdapter` in `src/slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.cpp` implements the four commands on the GUI thread:

1. **search**: iterate `Preset::print_options()`, look up each in `print_config_def`, rank deterministically by exact key, key prefix, label substring, then description substring, tie-break by key, page with an opaque `offset:N` cursor, default 10, maximum 25.
2. **read**: for each key, definition plus current value from the edited preset, `differs_from_preset` and `differs_from_system` from the dirty accessors, and `writable` from the allowlist. Unknown keys are returned separately with suggestions.
3. **preview**: the pipeline in the order given under "Validation and metadata entry points", then every rule in the Hazard 1 table evaluated against the post-batch clone with the printer preset and filament count read from the preset bundle, then `Slic3r::validate`, then the diff. Keys whose parsed value equals the current value are dropped from `changes` and reported as unchanged in `warnings`. No Orca state changes and no event is published.
4. **apply**: wrap in `ProjectStateTransaction` so nested notifications coalesce into one event. Re-run preview against the current config. If any before value differs from `confirmed`, return `StaleSettings`; if any blocking issue exists, return `InvalidSettings`; neither mutates. Otherwise build the diff config and call `Tab::load_config`. Read back every touched key and every dependent key the preview predicted, fill `applied`, and return success. The transaction commits one `Settings` change with one new revision.

### Registry entries

All four are exposed `InApp | Mcp`, availability `Always`, with handlers `SettingsSearch`, `SettingsGet`, `SettingsPreviewPatch`, `SettingsApplyPatch`. The first three are `ReadOnly`; the last is a mutation requiring approval.

| Tool | Input | Output |
|---|---|---|
| `settings_search` | `query` string, optional `limit` 1 to 25, optional `cursor` | `items`, `nextCursor`, `truncated`, `processPreset`, `sessionId`, `revision` |
| `settings_get` | `keys`, 1 to 32 strings | `items` with `key`, `value`, `type`, `label`, `unit`, `differsFromPreset`, `differsFromSystem`, `writable`; `unknownKeys` with suggestions; `processPreset`, `sessionId`, `revision` |
| `settings_preview_patch` | `changes` object, 1 to 32 keys, values string, number, or boolean | `valid`, `changes` with `before` and `after`, `issues`, `warnings`, `processPreset`, `sessionId`, `revision` |
| `settings_apply_patch` | `changes`, `expectedSessionId`, `expectedRevision` | `applied`, `changes` with actual values, `normalized` keys, `processPreset`, `processPresetDirty`, `projectUndo` always `false`, `sessionId`, `revision` |

Descriptions: one sentence on when to use the tool, one on the precondition, and for apply one saying that the change waits for approval in JusPrin and is not undone by project Undo. Keep every description under `kToolLabelLimit`.

Approval title for apply: "Change N process settings: key1, key2, ..." truncated to the label limit. The existing activity card renders it; no page change is required in this slice.

Argument validation in `valid_arguments`: shape only. Key existence and value validity are the executor's job so that the model receives structured `issues` rather than a schema rejection. For `settings_apply_patch` the coordinator additionally runs `preview_settings` at proposal time: an invalid patch fails the proposal at once with the issues, and a valid one stores the previewed changes as the confirmed set the user approves.

### Executors and results

Put the JSON renderers in `ToolResults.cpp` beside the existing ones so both adapters produce identical JSON. The four executor blocks in the coordinator call the workspace, render, and set the result, mapping conditions to the error-code table above.

### Discovery text

The `instructions` string returned by `server/discover` in `src/slic3r/GUI/JusPrin/Mcp/McpProtocol.cpp`, which the bridge also uses in its legacy `initialize` reply, gains one sentence describing the search, get, preview, apply workflow and the approval wait.

## Implementation sequence

Complete each phase with tests before starting the next.

### Phase 1: contract, fake, and change reason

1. Add the types, the four commands, `Settings` in both reason enums, and the `WorkspaceSetup` fields.
2. Implement them in `FakeWorkspace` with the fixture table and the two mirrored normalizer rules.
3. Extend `tests/workspace/test_workspace_contract.cpp` with the fake-workspace cases below.
4. Add the one-line Plater seam and record the rebase evidence required by fork stewardship.

### Phase 2: registry, executors, and coordinator tests

1. Add the four definitions, handlers, argument validation, and the argument-aware approval title.
2. Add the renderers in `ToolResults.cpp` and the executor blocks in the coordinator.
3. Add `Settings` to `kInvalidatingReasons`.
4. Extend `tests/agent/test_tool_registry.cpp`, `test_tool_coordinator.cpp`, `test_openai_responses_agent.cpp`, and the MCP suites with the cases below.

### Phase 3: real adapter

1. Implement the four commands in `OrcaWorkspaceAdapter` and the setup fields.
2. Extend `tests/workspace/real_adapter_harness.cpp` with the real-adapter cases below.
3. Extend the shell harness so the `--mcp`, `--mcp-bridge`, and `--live-agent` scenarios exercise the settings tools end to end.

### Phase 4: real-client proof

Run the scenario under "Real clients" with Codex CLI using the bundled bridge and the `--manual-mcp` fixture. The user narrowed acceptance testing to Codex CLI only on 2026-09-04; Claude Code and GUI clients are not gates for this slice. Record versions, commands, and structured results in the pull request.

### Phase 5: cleanup

Follow the section "Cleanup of the legacy MCP tools" below, as its own commit.

## Required tests

### Fake workspace and contract

- Search bounds, deterministic order, and cursor paging; an invalid cursor is `invalid_arguments`.
- Unknown key suggestions; unsupported scope.
- Type, enum, and range errors with the allowed values or bounds in the issue.
- The spiral-mode conflict produces `incompatible_settings`; the multiline reset produces a `normalized_dependency` warning in preview and a `normalized` entry with the actual value after apply. Support-gap rounding remains inactive.
- Preview mutates nothing and publishes no event.
- Apply is atomic: one revision, one `Settings` event, all keys or none.
- Apply with a confirmed set whose before value no longer matches returns `StaleSettings` and mutates nothing.
- A key equal to its current value is reported unchanged; an all-unchanged patch returns `NoChange`.
- Applying the inverse patch restores every key.
- Project replacement invalidates a pending settings proposal.

### Registry and coordinator

- Deterministic ordering and unique names now include the four tools; the OpenAI projection matches the registry.
- Output schemas validate every renderer result, and the validator throws on any keyword outside its vocabulary.
- Approval title names the keys, and the activity's normalized arguments carry the confirmed before and after values from the proposal-time preview.
- An invalid patch fails at proposal with structured issues and never creates an approval card.
- Reject and cancel do not mutate.
- A `Settings` change between proposal and execution fails the proposal with `stale_revision`.
- A before value that moved between preview and apply fails with `stale_workspace`.
- One failing key leaves every key unchanged.
- The in-app Agent and MCP observers receive the same terminal activity.

### Real Orca adapter

- Values and metadata come from the active edited process preset.
- Applied values appear in the process tab fields and the sidebar dirty indicator.
- A sliced plate reports `sliced: false` after apply.
- No top-level dialog appears during apply for any allowlisted key under every condition in the Hazard 1 table: layer height at zero, at and above the printer's maximum, lowered below an absolute scarf-seam start height, wall loops or top layers or infill density on a spiral-mode preset, and an infill pattern change on a multiline preset.
- The multiline reset is predicted by preview and reported under `normalized` with the actual value after apply. The compiled-out support-gap rounding leaves its value unchanged.
- An approved inverse patch restores the pre-batch values.
- Orca's preset revert control clears the dirty state after a tool apply.
- Project Undo availability is unchanged by an apply.
- A GUI edit between preview and apply produces `stale_workspace`.
- A GUI edit publishes `Settings` with one new revision.

### Protocol, bridge, and shell

- The MCP catalog lists the four tools with canonical schemas; the offline bridge catalog matches.
- `settings_apply_patch` over MCP returns SSE progress and the terminal result after approval, and `approval_rejected` after rejection.
- The shell `--mcp-bridge` scenario runs search, get, preview, reject, approve, inverse, and stale-by-GUI-edit against the real model, deciding through the rendered Approve and Reject buttons.

### Real clients

```text
open JusPrin with a sliced two-object project
→ search "infill", read layer_height and sparse_infill_density
→ preview a two-key patch, receive valid with before and after
→ apply, reject in JusPrin, values unchanged
→ apply, approve in JusPrin, values changed, plate unsliced, preset dirty
→ apply the inverse, approve, values restored together
→ edit wall_loops in the sidebar, replay the old revision, receive stale_workspace
→ preview layer_height above the printer maximum, receive invalid_setting_value with the bound
→ close JusPrin, receive workspace_unavailable
```

Record client versions and any timeout settings. Check counts and codes in the structured tool results, not in the model's prose.

## Cleanup of the legacy MCP tools

### Gate

Do not start until all of these are recorded in the pull request:

- Phases 1 to 3 pass on macOS with the commands listed above.
- Phase 4 passes with Codex CLI, including rejection, approval, inverse, and stale cases with counts checked in structured tool results.
- `settings_apply_patch` has replaced `duplicate_object` as the mutation fixture in every MCP test asset named below.

### What changes

1. **Registry exposure.** `inspect_selection` and `duplicate_object` become `InApp` only. Two flags in `ToolRegistry.cpp`. Nothing is deleted: the in-app Agent, its deterministic mock, its live harness, and its UI tests keep using both, and `IWorkspace::duplicate_object` stays because the contract and adapter tests exercise it as a workspace command.
2. **Catalog assertions.** Update every place that hard-codes the three-tool MCP catalog or calls the fixtures over MCP: `tests/agent/test_tool_registry.cpp` exposure lists, `tests/agent/test_mcp_runtime.cpp` catalog size and its `inspect_selection` call, `tests/agent/test_mcp_protocol.cpp` fixtures that use `inspect_selection`, `tests/agent/test_mcp_bridge.cpp`, `tests/shell/shell_harness.cpp` catalog check and MCP calls, `tests/mcp/probe.py`, `tests/mcp/test_bridge_process.py`, and `tests/mcp/test_linux_package.py` catalog sets. Read tools over MCP use `workspace_inspect` or `settings_get`; the mutation uses `settings_apply_patch`.
3. **Discovery text.** The `instructions` string in `McpProtocol.cpp` and the bridge's `initialize` reply describe the settings workflow instead of object duplication.
4. **Documents.** Update the example rows in [the tool extension guide](mcp-tool-extension-guide.md) so they show the settings tools, update the "Where the MCP work stands today" section of this document to the five-tool catalog, and add the guide to `agent-docs/jusprin/README.md`.
5. **Commit order.** The existing bridge work first as it stands, then the settings slice, then this cleanup as its own commit, so each commit's evidence describes the catalog it shipped with.

### What deliberately does not change

- `duplicate_object` and `inspect_selection` stay available to the in-app Agent until an in-app eval decides otherwise. The extension guide's eval rule applies to them from that point.
- If evals later show users asking for copies, the replacement is an instance-count tool, because instances are Orca's real operation. That is a separate slice.
- The internal manufacturing-record tools are untouched.

## Fork stewardship

Production sources are fork-owned under `src/slic3r/GUI/JusPrin/`. The one Orca-owned change is one additive `notify_project_state_changed(ProjectStateChangeReason::Settings)` line at the end of `Plater::on_config_change`. `Plater.cpp` is the fork's busiest file, so measure the churn of that function against upstream before choosing the exact line, run the merge simulation with both endpoint commits, and record that the natural resolution is "keep both". No line in `Tab.cpp`, `ConfigManipulation.cpp`, or `PrintConfig.cpp` changes; the normalizer hazards are handled by refusing the inputs in fork-owned code.

## Completion gate

This slice is complete only when all of these are true:

- the four tools exist once, in the registry, and both adapters render them from it;
- every value, bound, enum, and message comes from Orca's definitions and validator;
- apply goes through `Tab::load_config` inside one transaction and publishes one `Settings` revision;
- no modal dialog can be reached through any allowlisted key and every silent rewrite is predicted and reported, proven by the real-adapter test against the audited rule table;
- apply enforces the before values the user approved, proven by the fake and real-adapter stale tests;
- fake, registry, coordinator, real-adapter, protocol, bridge, and shell tests pass;
- Codex CLI completes the real-client scenario;
- the cleanup commit has landed with the five-tool catalog and updated documents.

## Explicit non-goals

- Printer, filament, plate, object, or modifier settings.
- Expanding the write allowlist beyond the seven keys without the real-adapter coverage decision 8 requires.
- Preset switching, saving, or creating presets.
- Slicing, export, arrange, transforms, or printer dispatch.
- A settings undo model separate from Orca's preset dirty and revert behavior.
- Rendering before-and-after diffs in the Agent page. The existing card is sufficient for v1.
- Closing the open gates of the bridge work listed under "Verification status". They are tracked separately and do not block this slice.

## Open questions to settle in Phase 1

- Whether `Tab::load_config` is safe to call while the process tab page has never been shown in the JusPrin shell. `reload_config` guards on the active page, and the tabs are created at startup, but confirm with the real-adapter test before relying on it.
- Whether `Plater::on_config_change` is called exactly once per `Tab::load_config` under the JusPrin shell, or whether `update_dirty` also reaches it. The transaction wrapper makes the answer harmless for revision counting, but the test should assert one event either way.
- Whether the Hazard 1 table is complete. Re-read `update_print_fff_config` and `toggle_print_fff_options` for every key they read, including rules keyed on printer type or filament count, and add any rule an allowlisted key can reach. Record the audit in the pull request.
