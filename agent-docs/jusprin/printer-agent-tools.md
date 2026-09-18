# The printer panel's two agent tools

Status: design, 2026-09-18. Not built. Supersedes the tool shapes in `printer-panel-fixes-handoff.md` WP1 (on branch `claude/printer-conversation-panel-af11cb`) and the tools in commit `0e55c4f9be`.

The printer panel on Home lets a person add a printer or change one they already have, by talking to the in-app agent. This document fixes who does what. **The model does the understanding. The app does everything it can already work out itself.** The model gets exactly two tools, one per mode. Each tool checks the model's decision against the real printer data, carries it out, and hands back facts the model needs for its next sentence. A tool never decides anything.

Paths are relative to the repo root. `J/` is `src/slic3r/GUI/JusPrin/`. The spikes (`agent-docs/jusprin/spikes/`), the test findings (`agent-docs/jusprin/printer-panel-findings.md`) and the panel's current code live on branch `claude/printer-conversation-panel-af11cb`, not on `jusprin-newui`.

---

## 1. The split

| Who | Does what |
| --- | --- |
| **The model** | Works out which printer the person has, from words or a photo. Works out what changed on a printer the person already has. Asks when it isn't sure. Explains, answers questions and suggests, in its own words. |
| **The app** | Everything it already knows: drawing a card when the person taps a printer, picks one from the browse list, or uses a network printer (matched by the model id the printer reports); the Add button; Undo after a change; the Photo button; the browse list; the always-visible "Browse the list · Set it up myself" doors. After each of these, the app adds one line to the conversation saying what happened, so the model can answer follow-ups. The app never tells the model which tool to call. |
| **`printer_identify`** (Add mode only) | Checks the printers the model named against the catalog, draws their cards, returns the facts to state. |
| **`printer_change`** (Change mode only) | Checks the change the model worked out against what this printer's profile allows, asks the person to confirm it, saves it, returns the printer as it now is. |

Retired: `printer_catalog_search`, `printer_propose`, `printer_suggest`. Suggestions are the model's words. Buttons the app can derive (Undo, Photo of the label, the manual-setup doors) are the app's.

### Rules for both tools

- Each mode offers **only its own tool**. Offering all tools in both modes let a Change conversation call the identify tool and clear the pinned card of an existing printer.
- **No project data in results.** Today every tool result carries the open project's whole workspace (plates, objects, selection, estimates, setting changes; `AgentHost::continue_after_tool`, `J/Agent/AgentHost.cpp` around line 1836). A printer session has nothing to do with the project. Its results carry only the tool's own result.
- **JSON results.** Unknown values are explicit `null`, so the model states nothing rather than filling a gap. Values are quotable as-is. Results are checkable against the declared output shape and testable, like every other tool in the app.
- **Errors say what to do next.** Every refusal names the problem and the facts needed to correct it.
- **No credentials through the model.** A printer's access code never appears in a tool's input or in the conversation (see 3.4).
- **No `planId`.** Neither tool joins grouped approvals; remove the field the shared registry adds to every change tool.
- **The description states the split** in the model's terms (quoted in 2.1 and 3.1).

---

## 2. `printer_identify`

### 2.1 What the model is told

At the start of every Add session, in the instructions:

- The whole printer list, one line per model: `<catalogId> | <build volume>`, built from `PrinterCatalog` (`J/PrinterSetup/PrinterCatalog.cpp`). The Orca Arena bundle is left out (Kenneth, 2026-09-17). About 15 KB, roughly 3,700 tokens, paid on the first request of each Add conversation.
- The rules, adapted from `agent-docs/jusprin/spikes/printer-naming/instructions.v2.txt` and `spikes/printer-photos/instructions.v2.txt`:
  - One printer fits: call the tool with it.
  - Two or three genuinely fit: call the tool with all of them, and ask in your reply what tells them apart.
  - **More than three fit: do not call the tool.** Ask one question that narrows it down and say where to look ("The Ender 3 comes in twelve versions. What does the label on the front say?"), or point to "Browse the list". Never show three of many.
  - A query that is the start of several model names is not a clear match, even when it equals one of them.
  - Nothing fits, or it isn't a filament printer (resin, laser): say so in one sentence. Do not call the tool. The panel's doors are always visible.
  - A photo: name a model only from a readable name or printed size; by shape alone, ask for a photo of the label. Never quote a label as read unless asking the person to confirm it.

Tool description:

> You decide which printer this is. This tool checks the printers you name against the app's printer list, shows them to the person, and returns the details to mention. It never picks a printer.

### 2.2 Input

```json
{ "catalogIds": ["<id>", "..."],   // 1 to 3, copied from the printer list
  "nozzle": 0.6 }                   // optional, mm, only when the person or a photo said so
```

Nothing else. The count carries the model's certainty: one id means sure (the card offers "Add this printer"); two or three mean unsure (each card offers "This one"). No `action`, `say`, `question`, `reason`, `plate`, `filament` or `provenance`: the model's words go in its reply, and "not supported" is not a tool call.

### 2.3 Output

```json
{ "printers": [
    { "catalogId": "<id>",
      "brand": "Prusa",
      "model": "MK3S",
      "buildVolume": "250 × 210 × 210 mm",
      "nozzles": [0.25, 0.4, 0.6, 0.8],
      "assumed": { "nozzle": 0.4, "plate": null, "filament": "Prusa Generic PLA" },
      "alreadyYours": false } ] }
```

| Field | Source |
| --- | --- |
| `brand` | The brand's file, `resources/profiles/<Vendor>.json`, its `name` |
| `model` | The model's file, `resources/profiles/<Vendor>/machine/<Model>.json`, with the brand not repeated |
| `buildVolume` | The machine settings for each nozzle size (`printable_area`, `printable_height`), following `inherits` |
| `nozzles` | The model file's `nozzle_diameter` |
| `assumed.nozzle` | What the person said, else 0.4 when the model ships 0.4, else the model's first size |
| `assumed.plate` | The model file's `default_bed_type`; **`null` when absent** (325 of 359 models have none) |
| `assumed.filament` | The machine preset's own `default_filament_profile` for that nozzle size (e.g. `Prusa MK3S 0.4 nozzle.json` → "Prusa Generic PLA"), following `inherits`. This is OrcaSlicer's own default (`PresetBundle.cpp` uses it when a printer is selected); 893 of 913 installable presets have one. `null` when absent. |
| `alreadyYours` | Whether one of the person's saved printers is this model |

The model uses the output to write its reply from facts ("I'll assume a 0.4 mm nozzle and PLA; this profile doesn't say which plate it ships with"), never from memory.

### 2.4 Errors

| Code | When | Message carries |
| --- | --- | --- |
| `unknown_printer` | An id is not on the list | The id; "copy it exactly from the printer list" |
| `too_many` | More than three ids | "Ask one question that narrows it down instead" |
| `unknown_nozzle` | The stated nozzle isn't one the model ships | The sizes it ships |

### 2.5 Approval

None. The tool saves nothing; the person's tap on "Add this printer" is the decision.

### 2.6 Fix in the catalog: the wrong filament field

`PrinterCatalog.cpp` (around line 143) takes the first entry of the model file's `default_materials` and falls back to `default_filament_profile` only when that list is empty. `default_materials` is the list of filaments the setup wizard pre-ticks, not a default; for the Prusa MK3S its first entry is "Prusa Generic ABS". Read `default_filament_profile` from the machine preset for the chosen nozzle instead, and save and display that same value.

---

## 3. `printer_change`

### 3.1 What the model is told

At the start of every Change session, in the instructions, the printer as it is now:
- name, brand and model;
- current nozzle, **and the nozzle sizes this model ships**;
- the loaded spools as a list (name, material, colour), not a summary;
- whether it is connected.

Today the model gets a one-line summary and not the nozzle sizes, so it learns them only from an error.

The rules: work out what physically changed from what the person says. Change only what they said changed. The plate belongs to each project, not the printer; say where to change it and don't call the tool. A nozzle material (hardened steel) isn't tracked; say so. Connecting isn't possible from this panel.

Tool description:

> You work out what changed on this printer from what the person says. This tool checks it against what this printer's profile allows, asks the person to confirm, saves it, and returns the printer as it now is. It never decides what changed.

### 3.2 Input

```json
{ "nozzle": 0.6,                       // optional, mm
  "spools": [                          // optional; the complete list as it is now
    { "name": "Teal PLA", "material": "PLA", "colour": "#2a9d8f" } ] }
```

At least one field. `spools` replaces the whole list, so the model restates every spool; it can, because the instructions give it the current list. No `accessCode` (3.4). No plate.

### 3.3 Output

```json
{ "state": "applied",                  // or "declined" when the person keeps things as they were
  "changed": [ { "field": "nozzle", "before": 0.4, "after": 0.6 } ],
  "printer": { "name": "Bambu Lab A1 mini", "nozzle": 0.6, "nozzles": [0.2, 0.4, 0.6, 0.8],
               "spools": [ ... ], "connected": false } }
```

The model reports only what `changed` says happened, then may suggest in its own words ("with 0.6 you can print thicker layers"). On "declined" it says nothing changed.

### 3.4 Access codes stay out of the model

A printer's LAN access code is a credential. Today the panel's placeholder asks the person to type it into the chat, which sends it to the model provider. In Add mode nothing receives it (the page never sends `accessCode`, and `printer_change` refuses in Add mode); in Change mode it passes through the model into this tool. Found by reading the code on `claude/printer-conversation-panel-af11cb`, not by running it.

Instead, the panel shows an access-code field on the network printer's card (Add) and in the Change panel for a printer that came from the network. The field sends the code straight to the app (`SetupCommands::set_printer_access_code`). The model may say "enter the access code in the field below" and never sees the code.

### 3.5 Confirmation

A change asks the person first, on a card that states it in words:

```
Change nozzle
0.4 mm → 0.6 mm on Bambu Lab A1 mini
Every project that uses this printer slices for 0.6 mm.
                              Keep 0.4 mm   [Set 0.6 mm]
```

For spools, the card lists the spools before and after. No tool name on the card.

**When the card appears (Kenneth, 2026-09-18):**
- **A change the model worked out from what the person typed always shows the card.** The model is interpreting loose words ("i put a 0.6 on it", "I'm thinking of a 0.6", "the other one has a 0.6"), and the change affects every project that uses the printer. The card is where a misreading gets caught.
- **A tap on an app button that already names the exact change applies directly**, with no card. Today that is Undo. The app draws it after a change ("Nozzle set to 0.6 mm · Undo") because it knows the old value; the model is not asked to offer it. Undo goes through the same backend change, not through the model or this tool.

### 3.6 Errors

| Code | When | Message carries |
| --- | --- | --- |
| `nothing_to_change` | No field given | "Name the nozzle or the spools that changed" |
| `unknown_nozzle` | The size isn't one the model ships | The sizes it ships |
| `printer_gone` | The printer was renamed or removed while the card waited | The printer's name; "the panel has closed this printer" |

### 3.7 Side effects to remove (backend, not the tool's shape)

- **Changing a nozzle switches the open project's printer.** `Printers::change_named_printer_nozzle` selects the printer in the plater before saving (`J/Printers/NamedPrinters.cpp`, around line 333, on `claude/printer-conversation-panel-af11cb`), and `settle_unsaved_changes` can raise OrcaSlicer's unsaved-changes dialog. Changing a printer from Home must not change what the open project prints on. Read from the code, not run.
- **Adding a printer marked the open project modified** in testing (F17 in `printer-panel-findings.md`, on `claude/printer-conversation-panel-af11cb`). Probably the same cause.

---

## 4. Verification

- **Re-measure identification on the shipped model.** The spikes measured `gpt-5.6-terra` answering in JSON. The app is pinned to `gpt-5.4-mini` (`J/Agent/OpenAIResponsesAgent.hpp:58`) and answers through a tool call. Run both spike corpora (`spikes/printer-naming/corpus.jsonl`, `spikes/printer-photos/corpus.jsonl`) through the real tool path on `gpt-5.4-mini` before trusting this design. Add cases for "x1c", "voron 2.4 350mm", "prusa" (more than three fit), and "ender 3".
- **Record the footprint** per the tool guide (`agent-docs/jusprin/mcp-tool-extension-guide.md`): definition bytes for each mode's tool list, and input and cached tokens per request, for an Add and a Change conversation.
- **Update the tool guide.** It says every registered tool loads on every turn, lists no printer-panel exposure, and says no shipped tool skips the approval card. Record the printer session's per-mode tool list, the whole-list-in-the-prompt exception to "use bounded search" (with the spike as the evidence), and these two tools in the catalog record.
- **Tests:** each tool's input decoder (counts, unknown ids, nozzle sizes); the result carries no workspace; each mode offers only its tool; the access code never enters a tool input or the conversation; changing a nozzle leaves the open project's printer selection and modified flag untouched.
