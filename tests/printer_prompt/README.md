# Printer panel prompt tests

The printer panel's assistant is a system prompt
(`src/slic3r/GUI/JusPrin/AgentUI/src/printerInstructions.ts`), five printer
tools and the four settings tools, and the chat. The header's filament chat
is the same panel with its own prompt (`filamentInstructions.ts`) and the
settings tools alone; its cases are the `filament-*` ones. `run_prompt_tests.py` sends the live model the same
requests the app sends. It answers the tools the way the app answers them for
a fixed printer, then checks what the model did. The app does not run.

On Kenneth's local machine, the keys are in
`~/.config/jusprin/prompt-test.env` (outside the repository, mode `600`). An
agent running this suite can load them for that command in a subshell:

```bash
( . "$HOME/.config/jusprin/prompt-test.env"; tests/printer_prompt/run_prompt_tests.py )
```

Do not print, copy into the repository, or include the key values in a command
or test log. The file exports `OPENROUTER_API_KEY` and `OPENAI_API_KEY`; the
runner reads them from its environment.

Prompt changes are evaluated on DeepSeek V4 Flash through OpenRouter, served
only by the hosts that answer the app's notes, and on no other model (decided
2026-09-28; see "Which model"). `OPENROUTER_API_KEY` is for that model,
`OPENAI_API_KEY` for the judge.

A run is one sample of a model that answers differently each time, so each case
runs several times, in parallel, and reports a count. By default one invocation
runs at most 100 conversations (`RUN_BUDGET`), shared by the cases it runs: 2
each for the 36 cases, and at most 20 for a case run alone. `--runs N` sets
the count per case instead; use it to look closer at one case. By default
every run must pass; `--must-pass N` lowers the bar. `--verbose` prints every
conversation, not only the failed ones.

## Judging a prompt change

Run the cases before and after the edit and compare the counts; "Baseline"
below has the counts to start from. The script renders the prompt from the
TypeScript source on every run, so nothing needs building. At the default
three runs a case catches a behaviour that fails often, not one that fails one
time in twenty; when a case fails, or to judge the edit it was written for,
run that case alone, where it gets 20 runs.
Differences of a few runs out of 20 are noise; look for a clear gap.

For a prompt-only edit, run the previous prompt with `--prompt-rev HEAD` (any
git revision works): the page's source is read as it was there, and the checkout
is left alone. The current change also removes three tools, so the earlier
prompt and tool list are no longer a valid control for the affected cases.
Run both versions at the same time when their tools and case expectations match,
so both meet the same hosts and the same day. A baseline recorded earlier is a
guide, not a control: the same prompt
scored 76, 76 and 74 of 84 in three runs, so compare against a before run made
alongside the after run.
Paired this way, a real effect shows plainly: on 2026-09-28 a two-line edit
took `add-not-now-done` from 19 to 3 of 20 while its own case improved.

Run every case, not only the one the edit is for. The three sessions share one
prompt, so a line written for one of them reaches the other two; a fix is one
whose own case improves while no other case drops.

Every reply in every case is also held to the rules the prompt sets for all
replies: a `Choices:` line the page draws as intended (on the last line, two
to four choices under 30 characters; read as
`replyChoices.ts` reads it), and none of the internal words the prompt rules
out (profile, preset, catalog, a tool's name). A reply that breaks one fails
its run, whatever the case checks.

A few checks are about what a reply says, which no string test can read.
Those ask a stronger model (`JUDGE_MODEL`, pinned) one narrow yes-or-no
question about the conversation's last reply, and the run prints its reason.
Read the reasons of the runs that fail before trusting a count: a question
worded too broadly fails good replies.

## What keeps it faithful to the app

- **The system prompt and the opening line** come from the page's own source,
  through `render_prompt.mjs` and the page's esbuild. This needs node and the
  AgentUI npm install.
- **`printer_tools.json`** is the tool list the app sends in the printer panel.
  `test_openai_responses_agent.cpp` fails when it drifts from the registry.
  Regenerate it with
  `JUSPRIN_UPDATE_PRINTER_TOOLS=1 agent_bridge_tests '[printer]'`.
- **The chat history** is built the way `AgentHost` and `OpenAIResponsesAgent`
  build it: each earlier assistant message's words, then the tool calls it
  made with their results (replayed since `jusprin-newui` 115e2776bf); then
  this turn's message, tool calls and results. A call the app refused before
  it ran is not replayed, because the app never recorded it.
- **Tool answers** are what the app returned for the in-process fake Bambu
  printer, recorded from a real run.

The app streams its requests and this script does not, and the model that
answers them is the one prompts are evaluated on, not necessarily the one the
app is configured with (see "Which model").

## Cases

- `connect-bambu`: connect a saved Bambu Lab A1 mini that is on the network.
  Passes when the model opens `printer_connect`'s credential form for it. Before the app
  replayed earlier tool results, it failed about a third of the time: the model
  looked up the printer's id in one turn, and on "connect it" in the next it had
  no id and invented one, such as `<from_status>`.

- `add-named-model`: the person names one model ("bambu lab a1 mini"). Passes
  when that printer is added and the reply ends with the connect offer the
  prompt spells out, "Want to connect it so you can send prints straight to
  it?", with the choices Connect it and Not now.
- `add-choose-from-three`: "prusa mk4" fits three models, then the person
  chooses "Prusa MK4S". Passes when nothing is added before the choice, the
  chosen printer is, and the reply ends with the connect offer. A second
  lookup of the chosen printer draws a second picture card; it is reported,
  not failed.

- `add-then-undo`: the person names one model, it is added, then they tap Undo
  on its receipt. The app removes it, posts its note as a developer message,
  and starts a turn with nothing said (`PrinterConversation::undo_add`).
  Passes when that reply adds nothing, drops the connect offer and asks which
  printer they have. Without the prompt's rule about undone adds it passed 3 of
  20 (2026-09-24): the model mostly said "Okay, it's removed" and stopped, and
  some replies said "I removed" as if it had.
- `add-not-now-close`, `connect-verified-close`, `connect-leave-close`,
  `change-nozzle-close`: the four places the prompt says nothing is left to
  decide -- the person turns down connecting, the app's note says the
  connection is verified, they leave a failed connection for now, a nozzle
  change is saved. Each passes when that reply says the person can close the
  chat and offers no reply choices. The connect cases tap
  Submit the credential form and deliver the app's note as the app does: a developer
  message and no user message.
The cases above follow the main paths. The ones below are the rest of what
people say in each session -- questions the tools cannot answer, facts that
need nothing saved, requests that belong to another session -- and each checks
what must not happen as well as what should. The Change and Connect ones
answer the tools as `PrinterConversation` does for one saved printer
  (`SavedPrinterCase`), including its refusals (`preflight_tool`: no finding or
  adding outside an Add conversation, no change to another printer, no settings
  but that printer's), and fail on any call that adds, changes, connects or
  applies a settings change when that is not what was asked. The settings
tools read and check a fixed set of that printer's settings, as
`OrcaWorkspaceAdapter` answers for them: the printer is not the one the
project uses, so a change without `persistAs` is `not_selected`; a stock
profile (`stock`) refuses saving over itself with `read_only_preset`; bed size
and nozzle size are read-only.

- Change, about a saved Anycubic Kobra 3 (a print host, not connected):
  - `change-add-another`: "I also have a Prusa MK4S, can you add it too?" The
    app refuses adding here, so trying is not a failure; the reply is. Passes
    when it says another printer is added from Home and does not say this one
    was added. It measures whether the model recovers from the refusal, which
    is what would justify offering fewer tools in a Change conversation.
  - `change-ask-start-gcode`: "what's the current start g-code?" Passes when
    it is read with `settings_get` and quoted. Fails when the reply treats the
    printer as not yet added, says the setting needs a network connection (it
    is on this computer), or asks which printer this is. Reported 2026-09-28,
    before the settings tools were offered here: a reply offered to add the
    printer, and another, about a Bambu Lab A1's z offset, asked "Which printer
    is it?". That z-offset conversation passed 10 of 10 on DeepSeek with the
    split prompt, so it has no case of its own.
  - `change-edit-start-gcode`: "add G29 right after the G28 line in the start
    g-code". Passes when the change is applied and saved under the printer's
    own name, with G29 after G28.
  - `change-bed-size`: "make the bed 250 by 250", which the settings tools
    refuse. Passes when nothing is changed and the reply says the person can
    change it in the app's printer settings, without claiming to open them.
  - `change-ask-nozzle`: "what nozzle size is it set up for?" Passes when the
    reply says 0.4.
  - `change-loaded-filament`, `change-plate`: a spool loaded, a plate swapped.
    Passes when nothing is saved.
  - `change-unshipped-nozzle`: "I put a 0.3 nozzle on it". Passes when nothing
    is saved and the reply names 0.2, 0.4, 0.6 and 0.8.
  - `change-then-connect`: "can you connect it…?", then the address. Passes
    when the card opens for that address and no address was made up first.
- Change, about the settings OrcaSlicer ships for the Bambu Lab A1 mini,
  selected in the project (the header's Printer settings… on a project that
  uses them): `change-stock-copy`, "set the retraction length to 1 mm". Passes
  when the change is applied and saved as `Bambu Lab A1 mini 0.4 nozzle - Copy`,
  the copy `read_only_preset` suggests.
- Change, about a connected Bambu Lab A1 mini holding PLA and PETG:
  `change-ask-loaded`, "what filament is loaded right now?" Passes when the
  reply names both.
- Connect:
  - `connect-host-address`: a Voron given as "http://voron.local, I open it
    with Mainsail". Passes when the card opens for Moonraker at that address.
  - `connect-bambu-not-found`: no A1 mini in LAN mode on the network. Passes
    when the status is checked, no card opens, and the reply points to LAN
    mode or asks what the screen shows.
  - `connect-ask-why`: "do I have to connect it?" Passes when the reply says
    it is optional.
- Add:
  - `add-vague-description`: "the small bambu one". Passes when nothing is
    added before the person confirms.
  - `add-unsupported-printer`: a resin printer. Passes when nothing is shown
    or added and no other model is offered in its place.

Regressions: behaviour an earlier prompt or tool change fixed, found in the
history of `printerInstructions.ts` and the tools' wording. Each names the
commit that measured the failure, so a later edit made for something else
cannot quietly undo it.

- `add-many-fit`: "prusa" (7f66e75b07: the model tried to show more than three
  printers 33 times). Passes when no printer is looked up or added and the
  reply narrows it down or offers the full list.
- `add-name-starts-two`: "bambu a1", which begins the A1 and the A1 mini
  (7f66e75b07: 0 of 3 with the rule in place). Passes when nothing is added and
  the reply asks which. Its A1 answer is written from the profile, not
  recorded.
- `add-unshipped-nozzle`, and `change-unshipped-nozzle` above: a size the model
  does not ship (3633478c0e: the model listed the sizes and stopped). Passes
  when nothing is saved and the reply names the sizes and asks the person to
  check the nozzle.
- `add-full-list`: "show me the full printer list". Passes when the assistant
  says the person can browse it manually in the app, without claiming to open
  it or adding a printer. The earlier test required a tool to open the list;
  that result is not comparable after removing the tool.
- `change-ask-loaded-not-connected`: what is loaded on a printer that is not
  connected (492fdb5531). Passes when no filament is claimed as loaded.
- `connect-failed-ways-forward`: a print host that does not respond
  (c922876c66: a failure ended with no way forward). Passes when the waiting
  reply says how long it can take and the failure reply names the three ways
  forward.
- `connect-bambu-no-plugin`: connecting without Bambu's network plug-in
  (e2e1c6e7cc: "isn't available right now" with no reason). Passes when no
  card opens and the reply gives the plug-in as the reason.

The add cases use `add_session.json`, recorded from a `--printer-live` run: the
Add session the page was sent, including the full printer list, and what
`printer_identify` and `printer_add` returned for the printers the cases name.
Nothing checks that this recording stays current; record it again when the
catalog or those tools' answers change.

A new case is a class in `run_prompt_tests.py` that has a session payload, a
`tool()` that answers calls, and a `run()` that says the person's lines and
returns the outcome. Add it to `CASES`.

## Which model

The default is `deepseek/deepseek-v4-flash` on OpenRouter, pinned to Alibaba,
Novita, Baidu and AtlasCloud (`MODEL`, `PROVIDERS`). It was chosen on
2026-09-28 from four models run on these cases (29 then), three runs each, all
judged by the same `JUDGE_MODEL`:

| Model | Passed |
|---|---|
| xiaomi/mimo-v2.6-pro, hosts Xiaomi and GMICloud | 85/87 |
| gpt-5.4-mini (the app's) | 82/87 |
| deepseek/deepseek-v4-flash, pinned hosts | 79/87 |
| minimax/minimax-m3, hosts AtlasCloud and DeepInfra | 72/87 |

This measures the prompt, not the provider a person's app is set up with.
`--model`, `--endpoint`, `--key-env` and `--provider` try another model through
a Responses API endpoint, to compare models rather than to judge a prompt
change, such as:

```bash
OPENROUTER_API_KEY=... OPENAI_API_KEY=... tests/printer_prompt/run_prompt_tests.py \
    --model xiaomi/mimo-v2.6-pro --provider Xiaomi --provider GMICloud
```

A model other than the default starts with no hosts pinned. The judge stays on
OpenAI whichever model answers, so counts are judged alike.

Pin OpenRouter's hosts with `--provider` (repeatable). OpenRouter serves an
open-weight model from many hosts, and they render a message the model was not
trained on differently. The app's notes are `developer` messages, OpenAI's
convention: DeepSeek V4 has no such role in ordinary chat, and hosts that treat
it as a `system` message open no reply turn after it, so the model answers an
app note with nothing or with noise (2026-09-28: StreamLake, Parasail and Relace
empty 3 of 3, DeepInfra junk; Alibaba, Novita, Baidu and AtlasCloud correct).
Unpinned, those cases measure which host a request landed on. Check a new
model's hosts with a note as the last message before trusting its counts.

## The filament chat's cases (2026-09-29)

A filament chat about Bambu PLA Basic @BBL X1C, a filament OrcaSlicer ships
with two nozzle temperatures (one per nozzle kind), in slot 1
(`FilamentCase`, answered as `PrinterPanel::filament_preflight` and the
adapter answer): the settings tools alone, and a refusal of any other target
or of an apply without `persistAs`.

- `filament-hotter`: "make the nozzle 5 degrees hotter". Passes when the
  temperature is read first and 225,225 is applied for every layer, saved
  as the copy the facts name.
- `filament-ask-temperature`: "what nozzle temperature does it print at?"
  Passes when it is read and 220 is said, with no change.
- `filament-other-settings`: "also make the walls thicker". Passes when no
  change is applied and the reply says the print's settings are changed elsewhere.
- `filament-own-in-place`: a filament the person saved ("My PLA"), "lower the
  nozzle temperature to 210". Passes when 210,210 is applied for every
  layer, saved under its own name.

20 runs each: 19, 20, 20 and 20. The miss said "preset" before a tool call.
Before the rule that a temperature without layers named is every layer's,
the model asked whether the first layer should change too (6 and 12 of 20);
before it was told to say "settings", 5 of 20 replies said "preset" or
"profile". `--prompt-rev` does not apply to these cases before the commit
that adds `filamentInstructions.ts`.

## Baseline after offering the settings tools (2026-09-29)

The printer panel now also offers `settings_search`, `settings_get`,
`settings_preview_patch` and `settings_apply_patch`, answered by
`SavedPrinterCase.settings` as the app answers them. Change and Connect
sessions get a scope rule that includes a printer's settings and a section of
settings rules; a stock profile's facts name the copy a change is saved as
(`copyName`). The Add prompt is unchanged, though Add sessions are offered the
same tools. Full suite, three runs per case, beside the previous prompt
(`--prompt-rev HEAD`) with the same tools on the same hosts:

| Case | Previous prompt | This prompt |
|---|---|---|
| `change-ask-start-gcode` | 1 | 3 |
| `change-edit-start-gcode` | 0 | 1 |
| `change-stock-copy` | 0 | 3 |
| every other case, together | 82/87 | 83/87 |

The other cases' misses were single runs on both sides, mostly in different
cases, which is noise at three runs. Each settings case alone, 20 runs:
`change-ask-start-gcode` 20 and 20, `change-edit-start-gcode` 20 and 19 (the
miss said "preset"), `change-bed-size` 19 and 20 (the miss was this runner crashing on a string
`target`, since fixed), `change-stock-copy` 20 and
20 once the facts named the copy -- 11 before, when the model had to learn the
name from `read_only_preset` and often invented one.

DeepSeek V4 Flash sometimes sends `target` as a string of JSON. It comes in
bursts: in one full run both `change-edit-start-gcode` failures were eight
such calls in a row, while a later batch of 80 conversations had one, which
the model then sent correctly. The registry now says so when it happens
("target must be a JSON object, not a string holding one"); too few string
targets have been seen since to measure whether that ends the bursts.

## Historical baseline before removing the dialog and finish tools

The prompt before this tool removal, recorded in `dd7ca1b8ef`, run alongside
the one before it on 2026-09-28, three runs per case, with the app's refusals in place
(`PrinterConversation::preflight_tool`). The edit adds one sentence to the
Change and Connect scope rule: another printer is added with + Add printer on
Home. Every case not listed passed 3 of 3 in both runs.

| Case | Before | After |
|---|---|---|
| `add-many-fit` | 0 | 1 |
| `add-name-starts-two` | 2 | 2 |
| `add-not-now-done` | 3 | 1 |
| `add-then-undo` | 2 | 2 |
| `add-unsupported-printer` | 2 | 3 |
| `change-add-another` | 1 | 3 |
| `change-nozzle-done` | 3 | 2 |
| **Total** | **78/87** | **79/87** |

The Add rows differ by noise alone: the Add prompt is the same on both sides.
The `change-nozzle-done` drop is a Done offered beside another choice, seen
before the edit. `change-add-another` alone, 20 runs: 15 before and 20 after.
`change-ask-start-gcode` alone, 20 runs, before this edit: 20.
`add-name-starts-two` still fails in most runs: on "bambu a1" the model picks
the A1 without asking about the A1 mini.
