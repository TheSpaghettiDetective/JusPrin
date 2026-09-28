# Printer panel prompt tests

The printer panel's assistant is a system prompt
(`src/slic3r/GUI/JusPrin/AgentUI/src/printerInstructions.ts`), eight printer
tools, and the chat. `run_prompt_tests.py` sends the live model the same
requests the app sends. It answers the tools the way the app answers them for
a fixed printer, then checks what the model did. The app does not run.

```bash
OPENROUTER_API_KEY=... OPENAI_API_KEY=... tests/printer_prompt/run_prompt_tests.py
```

Prompts are judged on DeepSeek V4 Flash through OpenRouter, served only by the
hosts that answer the app's notes (decided 2026-09-28; see "Which model").
`OPENROUTER_API_KEY` is for that model, `OPENAI_API_KEY` for the judge.

A run is one sample of a model that answers differently each time, so each case
runs several times, in parallel, and reports a count. By default one invocation
runs at most 100 conversations (`RUN_BUDGET`), shared by the cases it runs: 3
each for the 28 cases, and at most 20 for a case run alone. `--runs N` sets
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

Run the prompt before the edit with `--prompt-rev HEAD` (any git revision
works): the page's source is read as it was there, and the checkout is left
alone. Run both at the same time, so both meet the same hosts and the same
day. A baseline recorded earlier is a guide, not a control: the same prompt
scored 76, 76 and 74 of 84 on the default model in three runs, and 80 and 78
with `--app`, so compare against a before run made alongside the after run.
Paired this way, a real effect shows plainly.

Run every case, not only the one the edit is for. The three sessions share one
prompt, so a line written for one of them reaches the other two; a fix is one
whose own case improves while no other case drops.

Before a prompt change ships, run it once more with `--app`: the app's own
model, gpt-5.4-mini on OpenAI. The two models fail in different places, and
people talk to the app's.

Every reply in every case is also held to the rules the prompt sets for all
replies: a `Choices:` line the page draws as intended (on the last line, two
to four choices under 30 characters, or Done alone; read as
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

The app streams its requests and this script does not. By default a different
model answers them (see "Which model"); `--app` sends them to the app's own.

## Cases

- `connect-bambu`: connect a saved Bambu Lab A1 mini that is on the network.
  Passes when the model opens `printer_connect`'s card for it. Before the app
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
- `add-not-now-done`, `connect-verified-done`, `connect-leave-done`,
  `change-nozzle-done`: the four places the prompt says nothing is left to
  decide -- the person turns down connecting, the app's note says the
  connection is verified, they leave a failed connection for now, a nozzle
  change is saved. Each passes when that reply offers Done as its one choice,
  read as the page reads it, and the person's "Done" then calls
  `printer_setup_finish`, which it must not call before. The connect cases tap
  Connect on the card and deliver the app's note as the app does: a developer
  message and no user message. Before the finishing rule, 0 of 20 offered Done
  in each; stated only as a general rule, the leave and change replies, whose
  own rules say how they end, offered it in 11 and 15 of 20.
- `connect-after-question`: the person writes while the credential card waits,
  as `--printer-live` does. The app cancels the card and the waiting turn goes
  on with `{"state": "cancelled"}`, then the question is a turn of its own.
  Passes when neither reply offers Done and "connect it" then opens a fresh
  card. The finishing rule first made the cancelled reply read as leaving
  connecting for now, with Done, in 11 of 20; the rule's own exception for a
  cancelled card brought that to 1 of 40.

The cases above follow the main paths. The ones below are the rest of what
people say in each session -- questions the tools cannot answer, facts that
need nothing saved, requests that belong to another session -- and each checks
what must not happen as well as what should. The Change and Connect ones
answer the tools as `PrinterConversation` does for one saved printer
(`SavedPrinterCase`), and fail on any call that adds, changes, connects or
closes when that is not what was asked.

- Change, about a saved Anycubic Kobra 3 (a print host, not connected):
  - `change-ask-start-gcode`: "what's the current start g-code?", a setting no
    tool reads. Fails when the reply treats the printer as not yet added, says
    the setting needs a network connection (it is on this computer), or asks
    which printer this is. Reported 2026-09-28: a reply offered to add the
    printer, and another, about a Bambu Lab A1's z offset, asked "Which printer
    is it?".
  - `change-ask-nozzle`: "what nozzle size is it set up for?" Passes when the
    reply says 0.4.
  - `change-loaded-filament`, `change-plate`: a spool loaded, a plate swapped.
    Passes when nothing is saved.
  - `change-unshipped-nozzle`: "I put a 0.3 nozzle on it". Passes when nothing
    is saved and the reply names 0.2, 0.4, 0.6 and 0.8.
  - `change-then-connect`: "can you connect it…?", then the address. Passes
    when the card opens for that address and no address was made up first.
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
- `add-full-list`: "show me the full printer list" (0f382d4098, 9089d6fee7:
  answered in words in 2 to 9 runs of 20). Passes when the list is opened.
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

The app only talks to OpenAI, so this measures the prompt, not a feature.
`--app` runs the app's model; `--model`, `--endpoint`, `--key-env` and
`--provider` try any other model through a Responses API endpoint, such as:

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

## Baseline

The prompt at a9ec0277f5, 2026-09-28, three runs per case. Every case not
listed passed 3 of 3 on both models.

| Case | Default model | `--app` |
|---|---|---|
| `add-many-fit` | 1 | 3 |
| `add-name-starts-two` | 0 | 0 |
| `add-then-undo` | 2 | 3 |
| `add-vague-description` | 3 | 1 |
| `change-ask-start-gcode` | 3 | 2 |
| `change-then-connect` | 2 | 3 |
| `connect-after-question` | 1 | 3 |
| `connect-failed-ways-forward` | 2 | 3 |
| **Total** | **74/84** | **78/84** |

`change-ask-start-gcode` alone, 20 runs: 18 on the default model, 19 with
`--app`. `add-name-starts-two` fails nearly every run on both models: on
"bambu a1" the model picks the A1 without asking about the A1 mini.
