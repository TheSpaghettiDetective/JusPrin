# Handoff: fixing the printer conversation panel after user testing

Status: written 2026-09-17 for implementation in this worktree (`.claude/worktrees/printer-conversation-panel-af11cb`, branch at commit `0e55c4f9be` "Talk to the agent about a printer, where the printers column is"). Nothing here is built yet.

Commit `0e55c4f9be` replaced the old "Add a printer" dialog with a conversation panel in Home's printers column (wireframe 19b). A hands-on test with the real agent found 25 problems; the worst is that a first-time user who types their printer the way people talk is told it is unsupported, or is given another manufacturer's printer with confidence. This document says how to fix every problem that has a known solution. Each change names the finding it fixes (F1–F25, numbered as in `printer-panel-findings.md`) and how to prove it fixed.

Paths are relative to this worktree unless absolute. `J/` means `src/slic3r/GUI/JusPrin/`.

---

## 1. Read first

| Source | What it gives you |
| --- | --- |
| `agent-docs/jusprin/printer-panel-findings.md` | The 25 findings, ranked, with what was typed and what appeared. The "problem" half of this document. |
| `agent-docs/jusprin/spikes/printer-naming/README.md` | The mechanism that replaces catalog search: the whole printer list in the prompt, prompt v2 rules, and the answer schema. 50/50 on a 50-case word corpus; no invented ids in 129 calls. |
| `agent-docs/jusprin/spikes/printer-photos/README.md` | Photo rules (prompt v2): printed names and sizes are reliable, shape alone is not; ask for a photo of the label; the model's stated evidence is not reliable enough to show as fact. |
| Claude Design project `d981c5e2-20db-445f-9047-fa9e9a12522e` (https://claude.ai/design/p/d981c5e2-20db-445f-9047-fa9e9a12522e), file `Orca Printer Setup Wireframes.dc.html`, **turn 20** | Lo-fi wireframes of the fixed states, one panel per state: `20a` opener · `20b` card states · `20c` not in the list · `20d` browse · `20e` photos · `20f` after Add · `20g` change. Also read turn 19 (`19b`, `19b-add`, the placement), turn 18 (`18e` wizard, `18f` no agent), and the project's `CLAUDE.md` sections "Add a printer — after testing (turn 20)" and "Printer conversation placement (turn 19)". Turn 20 is marked PROPOSED there. |
| `/Users/kenneth/Projects/JusPrin/agent-docs/jusprin/printer-conversation-panel-handoff.md` (main checkout, uncommitted) | The original 19b handoff and Kenneth's decisions: the panel replaces the column; the real agent adds and changes printers; the old dialog is deleted; no agent → the embedded agent-setup page. Still binding. |
| `AGENTS.md`, `agent-docs/jusprin/design-system.md` ("Writing UI code"), `agent-docs/jusprin/fork-stewardship.md` | House rules: every colour, radius, font and control size from `resources/jusprin/ui/design-tokens.json`; error-handling discipline; how to touch OrcaSlicer-owned files. |

### Using the design project

You have the Claude Design MCP (`claude_design`, authorised with `/design-login`). Use it as the source of truth for layout, order and copy; this document does not repeat what the panels show, only what to build behind them and where the panels are wrong.

- **Read it at the start, not from memory.** The design agent was sent follow-ups after this document was written (restore Connect… and the Change header door in `20f`/`20g`, stop quoting photo evidence in `20e`, drop the duplicate browse chip in `20a`, and add `20h` no agent in the panel, `20i` "Printer settings…" on a shipped profile, `20j` failures). List the file's panels first; if `20h`–`20j` exist, they are the reference for WP8, WP7 and failure states. Re-check section 5's table against what you find; rows the design has since fixed no longer apply.
- **Read the whole file, then the parts you need.** `list_files`, then `get_file` on `Orca Printer Setup Wireframes.dc.html`, `support.js` and `CLAUDE.md`. The `.dc.html` is about 135 KB; extract the `<section class="dv-turn" id="t20">` block rather than reading all of it into context.
- **Render it to look at it.** Save the `.dc.html` and `support.js` into one folder, serve it over HTTP (`python3 -m http.server`), and open `…/Orca%20Printer%20Setup%20Wireframes.dc.html#t20`. A `file://` URL does not render. Use a viewport of at least 1400 px; the 20b and 20e rows are 960 px wide.
- **Read-only.** Do not write to the design project. Design decisions, and the PROPOSED → decided status of turn 20, are Kenneth's. If the design and the code cannot both be right, stop and ask him; record what you found in this document's section 5.
- **Lo-fi means structure, not style.** The wireframes use the Kalam sketch font and a beige palette. Take structure, order, copy and emphasis from them; take every colour, radius, font and size from `resources/jusprin/ui/design-tokens.json`. The design project also has a Modernist design-system bundle attached; ignore it here (its `CLAUDE.md` says hi-fi comes later).

Do not bump the Agent bridge protocol version for these changes. They are additive and pre-release; use the capabilities list (project rule).

---

## 2. Work packages

Ordered by impact. WP1 alone removes the four worst findings.

### WP1 — Identify printers from the whole catalog (F1, F2, F3, F21; enables F5)

**Problem.** `OrcaPrinterBackend::search_catalog` (`J/PrinterSetup/OrcaPrinterBackend.cpp:103`) keeps a model only when every word of the query is a substring of "vendor + model + build volume + model id", returns the first N in file order, and reports no total. The session instructions (`J/PrinterSetup/PrinterConversation.cpp`, `profile()`, around line 191) tell the agent to search "using the person's own words". Result: "voron 2.4 350mm" → not found although `Voron 2.4 350` ships; "x1c" → Orca Arena's X1 Carbon, stated as the one clear match; "prusa" → three CORE One variants presented as all Prusa's models (13 ship).

**Change.**
1. Remove `printer_catalog_search` from the printer session (`J/Agent/ToolRegistry.cpp`, around line 1432, `ToolExposure::Printer`). Delete `search_catalog` if nothing else uses it.
2. Put the whole catalog into the Add session's instructions, one line per model: `<catalogId> | <build volume>`. Build it from `OrcaPrinterBackend`'s folded models (`m_models`), using the app's own ids (`<vendor id>/<model id>`); the spike keyed on vendor folder and model name, same granularity, different spelling. About 3,700 tokens for 384 models.
3. Adopt the rules of `spikes/printer-naming/instructions.v2.txt` verbatim in spirit, including the one that fixed the last failures: **a query that is the start of more than one model name is not a clear match, even when it exactly equals one of them.** Add the photo rules from `spikes/printer-photos/instructions.v2.txt` (printed size is the strongest evidence; with no readable name, never propose one printer — show two or three and ask for the label; a clone uses the model it copies; say "I can't read it" rather than guess).
4. Replace `printer_propose` with one tool whose arguments are the spike's answer shape, so the panel's state *is* the answer:
   ```
   action:      "propose" | "ask" | "unsupported"
   catalogIds:  up to 3, copied from the list
   question:    string (required for ask; for propose with 2–3 ids)
   say:         one or two short sentences
   reason:      "not_listed" | "not_fdm"   (unsupported only; new, see WP2)
   nozzle, plate, filament, provenance        (as printer_propose has today)
   ```
   Keep "never adds the printer": adding stays the person's tap.
5. Validate every `catalogId` against the catalog. An unknown id is a tool error returned to the model so it can correct itself — not a crash and not a silent drop. (Expected model behaviour, recoverable; per `AGENTS.md` error discipline, surface it to the caller that can fix it.)
6. Tell the agent, in the instructions, to name vendors separately from models. (The case found in testing, two vendors shipping the same model name — Bambu Lab's and Orca Arena's X1 Carbon — is removed by hiding Orca Arena; WP2 item 1.)

**Done when.** In the built app, with the live agent:

| Typed | Expected |
| --- | --- |
| `voron 2.4 350mm` | PROPOSE Voron 2.4 350 |
| `x1c`, `x1 carbon` | PROPOSE Bambu Lab X1 Carbon (Orca Arena is hidden from this flow, WP2 item 1) |
| `bambu x1` | ASK among Bambu Lab X1 Carbon, X1 and X1E |
| `prusa` | ASK with up to three Prusa models and a question that separates them; never "I found three matches" |
| `the ender with the touchscreen` | ASK with two or three Ender cards and what tells them apart |
| `maybe ender 3 v2 or ender 3 s1` | ASK with those two |
| `i built it myself, it runs klipper on a btt octopus board` | UNSUPPORTED `not_listed`, or ASK offering the generic Klipper profile — never a loop of "tell me the exact model" |
| `my elegoo mars` | UNSUPPORTED `not_fdm` |

Add these as unit cases where the conversation layer can be tested without a model (the answer-shape handling and id validation), and keep the spike corpora (`spikes/printer-naming/corpus.jsonl`, `spikes/printer-photos/corpus.jsonl`) as the model-level regression set.

### WP2 — The panel draws the three answer shapes (F5, F12, F18, F2 label, F11 display, F19, F22)

Reference: turn 20, `20b` and `20c`.

1. **Card anatomy.** Brand on its own small uppercase line, model in bold below, build volume below that. Never concatenate vendor and model into one string (today `vendor_name + " " + model_name` at `J/PrinterSetup/PrinterConversation.cpp:350`, `:388`, `:561` and `J/PrinterSetup/OrcaPrinterBackend.cpp:150`, which produces "Bambulab Bambu Lab A1 mini"). **Orca Arena — Kenneth decided (2026-09-17): hide the whole bundle from the add-printer flow.** OrcaSlicer ships a vendor bundle `resources/profiles/OrcaArena.json` ("Orca Arena Printer": one machine model, `Orca Arena X1 Carbon`, `model_id` `orca_arena_x1c`, plus about 40 `@Arena X1C` filament files and its process files). Its purpose is undocumented. Leave the whole bundle out of the printer list in the agent's instructions (WP1), out of the in-panel browse list (WP3), and out of any filament or plate the flow proposes. Do not hide it anywhere else: "Set it up myself" (the ConfigWizard) and OrcaSlicer's own preset lists still show it, and do not edit or delete the files under `resources/profiles/` (OrcaSlicer-owned). Filter it in fork-owned code by vendor id, as one named constant with a comment saying why, and cover it with a test. There is no "community profile" concept: that label was invented in an earlier proposal and appears in turn 20's `20b`; do not build it.
2. **PROPOSE (one id).** One live card. The red primary "Add this printer" sits **on the card**, bottom right. "Not this one" is a quiet chip under the agent's sentence. No Add button docked at the panel foot (today it is: `J/PrinterSetup/PrinterConversation.cpp:243`).
3. **ASK, or PROPOSE with 2–3 ids.** Each card has its own quiet "This one". The question sits under the cards in one sentence. **No red Add anywhere in this state.**
4. **Collapsed.** When a card is rejected ("Not this one") or superseded by a newer answer, it shrinks to one grey line (`Not this one · Voron 2.4 350`) with no button, and the pinned card clears to `—` (F12: today the rejected printer stays pinned and addable). Only one live card at a time.
5. **UNSUPPORTED `not_listed`.** Under the agent's one sentence, the panel itself draws a two-row exit block: "Browse the full list ›" (WP3) and "Set it up myself — for a printer you built, or one that isn't listed ›" (the ConfigWizard printer page, prefilled with anything the conversation settled). The agent's wording must not be the only exit.
6. **UNSUPPORTED `not_fdm`.** One sentence ("JusPrin slices for filament printers; resin printers like the Mars aren't something it can set up."), no exit block.
7. **Activity line (F22).** While tools run, no empty avatar bubble. Show one grey line naming the work ("Looking through the printer list…") that disappears when the reply arrives. Today each tool call renders an empty assistant bubble (1–3 dots before each reply); find where `J/AgentUI/src/components/MessageList.tsx` renders an assistant message with no text and stop it.
8. **Pinned card values (F19).** Human words: `Textured PEI · assumed`, `PLA · assumed`, never preset ids like `Bambu PLA Matte @BBL A1M`. Plate is filled for every model that ships a default plate (it was blank for the Prusa MK3S). The first PROPOSE sentence explains the word: `"Assumed" means I guessed; change it here or in your first project.`

### WP3 — Browse the full list, inside the panel (F6, F23)

Reference: turn 20, `20d`.

- The Add panel's header link is renamed **"Browse the full list"**. It opens a list inside the panel, not the floating ConfigWizard dialog.
- Level 1: `‹ Back to chat`, a "Filter brands" field, one row per brand with its model count.
- Level 2: `‹ Brands`, `<BRAND> · <count>`, one row per model with picture and build volume. Picking a model returns to the chat and shows it as a PROPOSE card.
- Last row of level 2, set apart: **"Set it up myself — my printer isn't listed"** → the ConfigWizard printer page, prefilled.
- The same two labels everywhere these two doors appear: "Browse the full list" (in-panel list) and "Set it up myself" (the wizard).

### WP4 — Say what the flow is, and stop promising what it cannot do (F9, F20, F7 wording)

Reference: turn 20, `20a`.

- **Opener** (`PrinterConversation::opening_message`, Add mode), one message: `I'll add your printer so your projects slice for it; connecting comes later. What printer do you have? Say it any way: "bambu a1 mini", "the ender with the touchscreen", "not sure, the small one".` Then the photo tip, unchanged. Then "Found on your network" rows, unchanged.
- **Placeholder (F20).** One line, ellipsised, short: `e.g. "bambu a1 mini"`. It is clipped mid-word today.
- **Connection (F7).** The printer session has no connection tool and this build has no Connect… entry point. Add to both Add and Change instructions: the agent must not offer to connect the printer or give setup tutorials; if asked, it says in one sentence that connecting isn't something it can do from this panel. (Seen today: "I can help you with the simplest setup… tell me Windows, Mac, or Linux", while running on the user's Mac.) Building Connect… is out of scope (section 4).

### WP5 — Photos (F13, F24, photo rules)

Reference: turn 20, `20e`.

- **Staged.** A 48 px square thumbnail inside the composer box, above the text line, with × on its corner; beside it the file name and "add a note, or just send". Today it sits above the box as a wide file row.
- **Sent.** The picture itself (about 120 px wide) is the user's message, with any words under it. Today it is a file-name chip.
- **Cleared on send (F13).** After sending, the composer is empty. Today the staged photo stays and would be re-sent with the next message; reproduced twice. Look at how `J/AgentUI/src/state/store.ts` and the host distinguish staged from sent attachments.
- **No readable name.** The agent answers ASK (never PROPOSE on shape alone) and says: `I can't read a model name in this photo. A photo of the label settles it: usually a sticker on the back, a plate under the frame, or the About page on the screen.` The panel adds a chip "Photo of the label" that opens the same picker as Photo.
- **Don't present photo evidence as fact.** The photo spike caught the model describing labels that were not legible. The agent names its conclusion ("Looks like the A1 Combo — the A1 with the four-spool AMS lite beside it"); it does not quote text it claims to have read unless it asks ("I read 'A1' on the nameplate — right?"). Put this in the instructions.

### WP6 — After "Add this printer" (F11 saved name, F16, F17)

Reference: turn 20, `20f`.

- **Saved name.** The model name alone: `Bambu Lab A1 mini`; a second one becomes `Bambu Lab A1 mini 2` (`Printers::add_named_printer` already de-duplicates with a suffix; the input is what's wrong).
- **Order.** The new printer's card is at the top of Home's printers column, highlighted once. Today it is appended at the bottom (order comes from `Printers::named_printers()`, the preset collection's order).
- **Receipt strip.** Above the list, dismissible with ×: `<model> added · <nozzle>, <plate>, <filament> — assumed · Not connected yet · Change`. "Change" opens that printer's conversation (Change mode). No Connect… link until connecting exists (section 4).
- **The open project must not become modified (F17).** After adding, the window title changed from "Untitled" to "*Untitled", so the user is later asked to save a project they never touched. Seen once, immediately after Add; not isolated. Likely cause: `SetupCommands::install_and_select_printer` switches the selected preset, which OrcaSlicer counts as a project change. Reproduce first, then fix at the layer that owns the decision.

### WP7 — The Change flow (F8, F15, F25)

Reference: turn 20, `20g` (and `20i` if drawn).

- **Confirmation card (F15).** Today: "Change this printer / printer_change · jusprin-native / Waiting for your approval / Approve / Reject". Make it:
  ```
  Change nozzle
  0.4 mm → 0.6 mm on Bambu Lab A1 mini
  Every project that uses this printer slices for 0.6 mm.
                               Keep 0.4 mm   [Set 0.6 mm]
  ```
  No tool name. Buttons name the outcome, never Approve/Reject. This needs the change tool to hand the card a human summary (before → after, printer name, consequence); the generic `ToolActivityCard` shows only title, tool and server.
- **After applying (F25).** The pinned card's Nozzle row reads `0.6 mm · changed` (already works). The card collapses to a grey line (`Set 0.6 mm · Change nozzle`), and the panel adds one system line: `Nozzle set to 0.6 mm`. Add an "Undo" chip only if an inverse change exists; do not offer one that fails.
- **Printer settings… from Prepare (F8) — status unverified, skipped 2026-09-18.** `J/Shell/PrinterMenu.cpp` passes `printer.nickname` to `ShellController::open_printer_conversation`; the claim was that the nickname is empty when the selected printer is a shipped profile rather than a saved printer, sending the panel to **Add** mode. Reading `SetupCommands::current_printer()` (`SetupCommands.cpp:56-79`) during WP7 found this does not hold today: an unnamed printer's `nickname` falls back to the preset's `printer_model` config option, then to the preset's own label, so it is empty only when no printer preset is loaded at all (which already reads "No printer selected" in the menu, `PrinterMenu.cpp:51`). Screen was locked, so this could not be checked live either. Kenneth said to skip it rather than build a fix for a bug that may no longer reproduce; re-verify against current code (and live, once reachable) before doing this one. If it still opens Add mode for a shipped profile, the fix stands as written below: open Change mode with the pinned card filled from the selected profile, one line `This printer isn't in your list yet`, and a primary chip "Add it to my printers".
- **Header door — verified 2026-09-18, no fix needed.** Keep the Change panel's right-hand header link "Set it up myself", which opens OrcaSlicer's printer settings window for this printer (Kenneth's decision). The built app shows the link in Change mode; where it leads was not clicked during testing, so check it. Turn 20's `20g` omits the link by mistake. Traced the code instead of a live click (screen was locked): `handle_page_message`'s `manual_setup` branch in Change mode calls `IPrinterBackend::open_printer_settings(m_printer_name)` (`PrinterConversation.cpp`), which in `OrcaPrinterBackend` calls `Printers::open_named_printer_settings` (`NamedPrinters.cpp:292-300`) -- it selects the named preset and calls `SetupCommands::open_settings_tab(Preset::TYPE_PRINTER)`, OrcaSlicer's own printer settings tab. This is the correct destination; no code change needed here.

### WP8 — No agent set up (F4)

Reference: 18f in the same design file; `20h` if drawn.

Today the panel opens the normal Add conversation with the composer disabled ("The Agent is not available") and nothing to do. Kenneth decided this state shows the same embedded agent-setup page the old dialog used (an `AgentWebView` loaded with `?embedded=1`; see `J/AgentUI/src/main.tsx`, and the `CallAfter` teardown note in `J/PrinterSetup/PrinterPanel.cpp`). The same applies when the only agent is an external AI tool connected over MCP. Keep the parts that need no agent live beside it: "Found on your network" rows and "Browse the full list". When setup completes, the panel becomes the Add opener (or the Change opener).

### WP9 — Duplicated assistant text (F14)

Once, a reply rendered twice, concatenated with no space: `…and I'll match it.Okay—tell me…`. Seen after tapping "Not this one". Reproduce before fixing (house rule: no fix for a bug you cannot reproduce); suspect the stream-to-message merge in the Agent page or host.

### WP10 — Leaving the conversation (F10)

"‹ Printers" mid-conversation silently discards the thread. **Kenneth decided (2026-09-17): confirm before discarding.** "Every opening is a new session" stays.

- When the person taps "‹ Printers" and the thread holds anything they typed or sent (a message, a photo, a picked card), ask before closing: `Leave this conversation? What you've told me about this printer will be lost.` with `Leave` and `Stay`. With nothing typed yet, close without asking.
- The same applies wherever else the panel closes a conversation with content: opening "Set it up myself", and opening another printer's conversation from Home while one is open.
- After a printer is added, closing needs no confirmation: nothing is lost.
- There is no wireframe for this prompt. Use the app's existing confirmation pattern and tokens; don't invent a new dialog style.

### WP11 — Failure states (only if `20j` exists in the design project)

Not one of the 25 findings: the test never hit them. If `20j` is drawn, build what it shows. Otherwise leave today's behaviour and list these as open for Kenneth: the agent request fails; a network printer stops answering between "Use this" and Add. The nearest rule in the design log is turn 14's, written for the "Connect AI tools" flow: errors one at a time, each with its fix. Whether it applies here is Kenneth's call; keep the pinned card and existing cards in place either way.

---

## 3. Verification

Unit and component tests as the house requires (Vitest for AgentUI and HomeUI, Catch2 for the conversation and backend). Prove each guard test fails first by injecting the violation.

Then drive the built app — the findings were only visible there:

1. Build (see `AGENTS.md`), then copy the data directory so nothing touches the real one:
   `ditto "$HOME/Library/Application Support/JusPrin2" <scratch>/datadir`, clear `recent` entries in `<scratch>/datadir/JusPrin2.conf`.
2. Launch the binary directly with an isolated home:
   `HOME=<scratch>/home <worktree>/build/arm64/src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/OrcaSlicer --datadir <scratch>/datadir`
   The copied config carries the OpenAI key under `jusprin_agent`; delete that entry for the no-agent run (WP8).
3. The app's bundle id is `io.jusprin.JusPrin`. Background clicks into the web views only register while the app is frontmost (`osascript -e 'tell application "System Events" to set frontmost of (first process whose unix id is <pid>) to true'`). Web-rendered popup menus (the printer card's "…") need display-level clicks.
4. Run every row of the WP1 table, plus: a photo with a readable name and one without (WP5); Add then check the list order, receipt and window title (WP6); nozzle change from a printer card and "Printer settings…" from the Prepare header with a shipped profile selected (WP7); the no-agent run (WP8).
5. For each state you built, put a screenshot of the running panel beside the matching turn-20 panel rendered from the design project, and check structure, order, copy and emphasis one by one. Record any deliberate difference in section 5.
6. Check light and dark, and Windows (the Windows VM `jusprin-win-test` is set up for GUI testing; Windows text renders about 9% larger, so re-check the 250–320 px panel for clipping).

---

## 4. Out of scope

- **Connecting a printer (Connect…).** Not built. The decided design keeps Connect… on the printer card and in the printer menu; add it to the receipt strip when it exists.
- **The STARTER MODELS row after Add** (turn 18). A separate feature.
- **Shape-only photo accuracy** (5–8 of 15 in the spike) and **run-to-run variance** of the model's answers. No known fix; the design limits their cost (ASK never carries a primary Add; a photo without a readable name asks for the label).

---

## 5. Where turn 20 and this document differ

As of 2026-09-17, before the design agent acted on the follow-ups listed in section 1. Re-check each row against the live file.

| Turn 20 | This document | Why |
| --- | --- | --- |
| `20b` ASK example: Bambu Lab X1 Carbon vs "COMMUNITY PROFILE · ORCA ARENA" | Orca Arena hidden; no community label | Kenneth's decision; the label was invented |
| `20a` shows "Browse the full list" twice (header link and a chip) | Header link only | Same door twice on one screen |
| `20e` "sent": "The nameplate reads A1, …" | Name the conclusion; don't quote claimed readings | Photo spike: the model reports labels that are not legible |
| `20e` no-name candidate cards have no build volume | Every card shows build volume | One card anatomy |
| `20g` Change header has no right-hand door | Keep "Set it up myself" → OrcaSlicer printer settings | Kenneth's decision |
| `20f` omits Connect… | Omit Connect… in the build only | Not built; the design keeps it |

## 6. Open questions for Kenneth

1. WP7: does a nozzle change need confirming at all? Turn 20 records this as open. This document keeps the confirmation because it checks the agent's reading of loose words before a change that affects every project.
