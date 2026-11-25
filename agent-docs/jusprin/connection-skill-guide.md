# Writing a connection skill

A connection skill is the text the app adds to a printer chat so the model can guide a person through connecting a printer: `AgentUI/src/skills/connect-bambu/SKILL.md` for Bambu Lab printers, and `connect-print-host/SKILL.md` for every printer that connects through a print host (Moonraker, OctoPrint and the other types `printer_host_connect` takes). Read this before writing a new one or changing one. The general rules for skills, and for when a skill becomes a tool, are in [Tool extension guide, Skills versus tools](mcp-tool-extension-guide.md#skills-versus-tools); this document adds only what connecting a printer needs.

Sentences that describe code or tests describe `jusprin-dev`. Rules marked **Proposed** are not agreed yet; the others carry Kenneth's words or the incident they came from.

## What the skill is for

Kenneth's test, 2026-11-02: the skill is good if the agent guides a person who is stuck and makes educated guesses it labels as guesses, until the printer is connected or the person chooses to leave it. It is not judged by whether a printer connected, by how many states it names, or by whether every sentence in it is true. A skill that is never wrong and never helps a stuck person fails the test.

Three times in three days a skill was written to the wrong half of that test. The first Bambu skill was a table from state to next step, and when a P1S connection failed it told the person to re-check an access code that was right. The second Bambu skill said "add no cause of your own", and the model repeated a false reason instead of guessing. The host skills said "you have not seen where Mainsail or Fluidd show the key", and on 2026-11-04 the model told a person who had no key to look in "Mainsail's settings page", which does not show it. Each time the writer kept "say nothing false" and dropped "help the stuck person".

## How the skill reaches the model

- The app chooses the skill from the saved printer's vendor: a Bambu Lab printer gets `connect-bambu`, any other gets `connect-print-host` (`PrinterConversation::connection_skill`). The model never picks it.
- It is in force as soon as the chat has a saved printer, before the person asks to connect: with `printer_add`'s result, or on the first request of a chat about a saved printer (Kenneth, 2026-11-04). A chat that never connects still carries it, so each skill opens by saying the person may not want to connect, and offers it once after a printer is added.
- It stays in the chat once sent (`AgentSkillUse::stays`).
- `issue_report_open` comes with either skill and is offered from the same request on (Kenneth, 2026-11-06). It opens a GitHub issue page in the person's browser with the model's summary filled in; the person reads it and submits it with their own account. A chat without a connection skill is never offered it.
- The skill opens with "The rules above still apply", so wherever the standing prompt (`AgentUI/src/printerInstructions.ts`) gives an exact sentence or an exact step, the prompt wins over the skill. On 2026-11-04 the prompt's finishing example "It is connected, so you can send prints straight to it" beat the skill's "Moonraker answered, and nothing more", with Klipper shut down. The prompt now keeps only the rules that hold for every printer: the tools act on this printer, connecting is optional, never ask for a credential in chat, and how to finish.

## Before writing: three audits

### 1. The person's stuck moments, not the app's states

Start from the moments a person with this connection type gets stuck, and write to those. The failure table (which result means which step) comes second. The three moments every connection type has:

- **Giving the address, or finding the printer.** What they open in a browser, which port, what if the name does not resolve; for Bambu, why the printer is not heard. The skill is in force before this moment, so it can guide it.
- **Being asked for a credential they do not have.** A Klipper owner who has never heard of an API key; a Bambu owner who cannot find the access code. The skill says, in order of cost to check, what is likeliest and what they can look at, each labelled as observed, documented or a guess. The 401-without-a-key case was the one place both 2026-11-04 Moonraker runs got stuck, when the skill had only "search for where their version shows it".
- **Being told it is connected.** What the app's test proved and did not prove, so the closing sentence does not promise what the app has not checked.

For each moment, write what a competent friend would say to someone standing at that screen. That sentence is the skill's job; the failure table supports it.

### 2. Information against knowledge, per state

Kenneth, 2026-11-02: for each failure, is the cause a lack or wrong *information* (a fact about this situation that only the app can know at that moment) or a lack or wrong *knowledge* (how things work)? Information belongs in the app's note or tool result; knowledge about connecting belongs in the skill; how to work in general belongs in the prompt.

Before writing a line of the skill, list the app's notes and tool results for this connection type and, for each, what the app already holds at that moment that the model would need. Where the result lacks it, the fix is to the result, and the skill is written against the result that will exist. Two gaps found this way on 2026-11-04 are fixed: the Moonraker result now says which key each test sent (none, the one typed in the form, or the saved one), and the verified result gives Klipper's state, which the app read and had only logged.

"Already holds" is the limit. A result forwards what the code has; it does not gain a new probe to tell causes apart ([How to fix a result that hides the failure](mcp-tool-extension-guide.md#how-to-fix-a-result-that-hides-the-failure), rule 2). After 2026-11-02 the Bambu failure note gained a look at the printer's port 8883 before and after each attempt, and the skill gained four cases built on what the port did. The failures it was built to explain came from a development build with Enable SSL(MQTT) off (see [Three kinds of statement](#three-kinds-of-statement-each-labelled)), and both were removed on 2026-11-16.

### 3. The standing prompt and the tool descriptions

List every sentence in `printerInstructions.ts` and in the connection tools' descriptions (`Agent/ToolRegistry.cpp`) that gives an exact wording, an exact step or a procedure for connecting. Each one either goes, becomes conditional, or is accepted knowing the skill cannot override it. On 2026-11-04 four such sentences caused failures the skill's facts should have prevented, and all four have since been removed: the exact offer after `printer_add` (asked a person who had already asked to connect), "say briefly to use that form" after a key was requested (told a person with no key to type one), the finishing example above, and the connect tool's "until they give one, ask for it instead of calling this" (made the model ask permission to try a port the person had just said to try).

This matches [Tool extension guide, A tool description versus a skill](mcp-tool-extension-guide.md#skills-versus-tools): a description that has grown into a step belongs in the skill. The skill work had added new text without sweeping the old. **Proposed:** the sweep is a required step of every connection skill change.

## Writing the skill

### Explain the mechanism as a chain

Number the steps the connection goes through, each depending on the one before, and say so: a step that worked says nothing about the ones after it. This is what let the model reason about failures the skill did not list on 2026-11-03 (an OctoPrint the person called Klipper; `https://` against a plain-http host). Keep the chain from the person's side as well as the app's: for Moonraker, the app reaches Moonraker at its port, and so does the person's browser, so Moonraker's own pages are at that same address and port. A skill that explained only the app's side sent the person to a port-80 URL for the key (2026-11-04).

### Three kinds of statement, each labelled

- **Observed on a real host or printer.** What the app's own test reported, and when. The guard tests in `tests/agent/test_printer_conversation.cpp` require every state the skill names in backquotes to have a dated run behind it. Write these as facts.
- **Documented.** What a maintainer's docs or FAQ say, named ("Mainsail's FAQ for this error says…"). Write these with their source. The evidence rule does not forbid them; leaving them out is what made the host skills silent about the one thing a stuck Klipper owner needs (a 401 from a printer on the same home network is usually fixed in Moonraker's `trusted_clients`, with no key at all).
- **A guess.** General knowledge nobody has checked on this setup. Write it as a guess and give the cheapest check.

An observation counts only from an app set up the way people get it. Every "real P1S" result before 2026-11-04 came from a development build with Enable SSL(MQTT) off in the app's settings, which logs in to the printer without TLS: code -1 with the right access code, and a failure about 24 seconds after the attempt began. A release build takes TLS from the printer, and with the setting on the same P1S connected in about a second. The skill, its guard and the prompt cases stated those artifacts as facts for two weeks. **Proposed:** before a run on a real printer is recorded as observed, the record says which build ran it and, for a development build, the settings that differ from a release build.

What not to write, each from an incident:

- One next step per state. The state-to-step table sent a person with the right code to re-check it (P1S, 2026-11-02).
- "Add no cause of your own" or anything that forbids reasoning. The model repeated a false reason (P1S, 2026-11-02). The guard bans the phrase.
- A menu path as fact (`Settings > WLAN`). The guard bans arrows and "Settings >".
- A menu path as a presupposition. "Search for where Mainsail shows the key" presupposes it does; the model filled it in with "Mainsail's settings page" (2026-11-04). Say what is known instead: Mainsail and Fluidd do not show it; the two ways to get it; when it is not needed.
- A sentence the standing prompt contradicts with an exact wording. The prompt wins; see audit 3.
- A fix as the single answer. "Restart the printer" became every chat's advice once it was the only untested suggestion listed (P1S, 2026-11-02).
- A cause read from a probe the app added to tell causes apart; see audit 2.

### Search: say what to look up and how to read it

Every chat has the provider's web search, except a chat on a model on this computer, which has none and is not told of one. Three things are known about it. Offered with no instruction, the model did not search once in four chats (2026-11-02). Told to search before guessing, it searched in two of seven chats and cited its source (2026-11-03). Told to search for the key, it found Moonraker's docs, which give the key page without a port, and handed the person a wrong URL (2026-11-04).

So the skill names the search for each stuck moment and says how to apply the result to this setup: what the docs assume (Moonraker's default port), what varies by version, and what the model should ask the person to confirm. "Search before you say" alone was not enough.

### The length

The skill is read on every request once it is in force. Today `connect-bambu` is about 50 lines and `connect-print-host` about 80. A stuck-moment section for the credential adds a paragraph, not a page.

## The app's part

The skill is only as good as the results it reads. For each connection type they carry:

- A failed connection: the host's or the plug-in's own words, forwarded unchanged ([How to fix a result that hides the failure](mcp-tool-extension-guide.md#how-to-fix-a-result-that-hides-the-failure)), plus what the app itself did: whether a credential was sent and which, and how long the test waited.
- A verified connection: what the app checked and anything it read on the way that bears on the next step, such as Klipper's state.

When the person has tried and wants the developers to know, the skill points them to `issue_report_open`. Nothing is sent from the app.

## Testing a connection skill

### Offline

- `tests/agent/test_printer_conversation.cpp` (`[connection-skill]`): which skill a chat gets and when it reaches the model; `issue_report_open` offered with it and never without; every state named in backquotes has a dated real run; banned phrases are absent.
- `AgentUI/src/skills.test.ts` and `wording.test.ts`: the file parses, the trigger is unique, the text calls the product "the app" and never says a file "arrived".
- The shell harness (`tests/shell/shell_harness.cpp`): `--print-host-check` for hosts and `--printer-connect-skill` for a Bambu printer run the app's own connection test against a real host or printer with no model, and are the source of every observed line. `--printer-setup` runs the printer chat against the in-process fake Bambu printer.

These prove which skill reaches the model and when, and that its facts were seen. They say nothing about whether its words help.

### Live, with Kenneth's test

The 2026-11-03 host chats ran against Docker hosts with a scripted person who had the key, took the port advice, and knew it was OctoPrint. All seven connected and nothing was learned about guidance. The 2026-11-04 chats ran a smaller model with a person who did not know what an API key was, and failed at every stuck moment.

So a live run of a connection skill (**Proposed**, partly built in `tests/printer_prompt/run_prompt_tests.py`):

- scripts a person who does **not** have the answer at each stuck moment: no key and no idea what one is; the Mainsail port; a `.local` name; Klipper shut down; the wrong host type;
- runs on the model the product offers by default, not only on the model the writer prefers;
- is judged by where the person ends up (`ended_badly` in `run_prompt_tests.py`), including the closing sentence, not by whether the printer connected;
- reads the transcript as the model saw it, every tool call, result and note in order, before any line of the skill is changed, and names for each failure the text the model saw or lacked.

Live runs cost money and need Kenneth's approval of a specific batch with a token estimate. At 20 runs per case, a difference of two or three passes between two versions of a skill is noise: the old and corrected Bambu skills passed 68 and 64 of 160 runs on 2026-11-16.

## Checklist before a connection skill is committed (Proposed)

1. The three stuck moments are written to, each with labelled guesses in order of cost.
2. Audit 2 is written down: for each result, what the app already holds and what the result says; the gaps are fixed or listed.
3. Audit 3 is written down: every exact connecting sentence in the prompt and the tool descriptions, and what was done with it.
4. Every state in backquotes has a dated real run, from a build set up the way people get it; the guard passes.
5. Every documented claim names its source; every guess says it is one; no menu is stated or presupposed.
6. Each search the skill asks for says what to search and how to apply the answer.
7. A stuck-person live run on the default model, approved and run, with the transcript read before any edit.

## Where the rules come from

- 2026-10-31, P1S: the app hid a cloud-mode printer and the model invented menus. Tools report what the app saw.
- 2026-11-01: "a general process that improves when more people are using it" (Kenneth); tool results carry what is true now, a skill carries how it works.
- 2026-11-02: the state-to-step table failed on the P1S; "guide the user… make educated guess"; "the goal here is not to connect my printer"; information against knowledge, and where each belongs; the four rules for a result that hides the failure.
- 2026-11-02 to 03: web search alone was never used; told to search, the model did, in some chats.
- 2026-11-03: the host skills were written from Docker evidence and tested with a person who knew the answers, and search was left out of them by mistake.
- 2026-11-04: two live Moonraker runs failed at each stuck moment; the skill moved to the moment a printer is saved, and the prompt's connecting steps were swept out. The same day, Enable SSL(MQTT) was found off in the development build used for every P1S run, but nothing that rested on those runs was corrected.
- 2026-11-06: `issue_report_open` offered with every connection skill (Kenneth).
- 2026-11-16: the P1S facts from the SSL-off build were corrected in the Bambu skill and its guard, and the port-8883 look, the skill passages built on it, and the second-failure report card, which saved a file nobody received, were removed.
