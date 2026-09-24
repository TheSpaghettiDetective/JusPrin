# Printer panel prompt tests

The printer panel's assistant is a system prompt
(`src/slic3r/GUI/JusPrin/AgentUI/src/printerInstructions.ts`), eight printer
tools, and the chat. `run_prompt_tests.py` sends the live model the same
requests the app sends. It answers the tools the way the app answers them for
a fixed printer, then checks what the model did. The app does not run.

```bash
OPENAI_API_KEY=... tests/printer_prompt/run_prompt_tests.py --runs 20
```

A run is one sample of a model that answers differently each time, so each case
runs many times, in parallel, and reports a count. 20 runs take about 20
seconds. By default every run must pass; `--must-pass N` lowers the bar.
`--verbose` prints every conversation, not only the failed ones.

## Judging a prompt change

Run the cases before and after the edit and compare the counts. The script
renders the prompt from the TypeScript source on every run, so nothing needs
building. Differences of a few runs out of 20 are noise; look for a clear gap.

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

The app streams its requests and this script does not; nothing else differs.

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

The add cases use `add_session.json`, recorded from a `--printer-live` run: the
Add session the page was sent, including the full printer list, and what
`printer_identify` and `printer_add` returned for the printers the cases name.
Nothing checks that this recording stays current; record it again when the
catalog or those tools' answers change.

A new case is a class in `run_prompt_tests.py` that has a session payload, a
`tool()` that answers calls, and a `run()` that says the person's lines and
returns the outcome. Add it to `CASES`.
