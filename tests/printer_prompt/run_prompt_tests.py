#!/usr/bin/env python3
"""Printer panel conversations against the live model, without the app.

The printer panel's assistant is a system prompt (printerInstructions.ts),
eight printer tools and the chat. This script sends the model the same
requests the app sends, answers the tools the way the app answers them for a
fixed printer, and checks what the model does. A case is a short scripted
conversation; each run of it is one sample of a model that answers
differently each time, so a case is run many times and the result is a count.

What the app sends, reproduced here:
  - instructions: rendered from printerInstructions.ts by render_prompt.mjs,
    so an edit to the prompt is tested without building anything;
  - tools: printer_tools.json, the app's own tool list for the printer panel
    (test_openai_responses_agent.cpp fails if it drifts from the registry);
  - input: earlier turns as the app replays them -- each assistant message's
    words, then the tool calls it made with their results, as function_call
    (no item id) and function_call_output -- then this turn's message, then
    this turn's calls and results (AgentHost::make_agent_request,
    OpenAIResponsesAgent::initial_input). A call the app refused before it ran
    (bad arguments) was never recorded, so it is not replayed.
The app streams its requests; this script does not. Nothing else differs,
except which model answers them.

Usage:
  OPENROUTER_API_KEY=... OPENAI_API_KEY=... tests/printer_prompt/run_prompt_tests.py
      [--runs N] [--must-pass N] [--case NAME] [--jobs 5] [--verbose]
      [--app | --model NAME [--endpoint URL] [--key-env NAME] [--provider HOST]...]

Prompts are judged on DeepSeek V4 Flash through OpenRouter, pinned to the hosts
that answer an app note (MODEL, PROVIDERS; decided 2026-09-28). --app runs the
app's own model instead, gpt-5.4-mini on OpenAI: a last check before a prompt
change ships. --model tries any other model through a Responses API endpoint,
with its key in the variable --key-env names. The judge stays on OpenAI
(OPENAI_API_KEY) whichever model answers, so counts are judged alike.

Without --runs, the cases run share RUN_BUDGET conversations: 5 runs each for
20 cases, fewer when there are more, never more than MAX_RUNS for one case.

Exit code 0 when every case passes at least --must-pass of its runs
(default: all of them), 1 otherwise. Needs python3 and node, and the page's
npm install (src/slic3r/GUI/JusPrin/AgentUI/node_modules) for esbuild.
"""

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
# The model prompts are judged on (decided 2026-09-28): DeepSeek V4 Flash on
# OpenRouter, served only by the hosts that answer an app note -- the others
# render the app's developer messages so that the model replies with nothing
# (README, "Another model").
MODEL = "deepseek/deepseek-v4-flash"
ENDPOINT = "https://openrouter.ai/api/v1/responses"
KEY_ENV = "OPENROUTER_API_KEY"
PROVIDERS = ["Alibaba", "Novita", "Baidu", "AtlasCloud"]
# The app's own model, for --app.
APP_MODEL = "gpt-5.4-mini"  # OpenAIResponsesConfig's default model
APP_ENDPOINT = os.environ.get("JUSPRIN_OPENAI_ENDPOINT", "https://api.openai.com/v1/responses")
# The judge stays on OpenAI whichever model answers, so counts are judged alike.
JUDGE_ENDPOINT = "https://api.openai.com/v1/responses"
JUDGE_MODEL = "gpt-5.5-2026-04-23"  # pinned, so a count moves only when the prompt does
# UTF-8 throughout: on Windows, Python otherwise reads text as the system code
# page and garbles "×" and curly quotes before the model sees them.
TOOLS = json.loads((HERE / "printer_tools.json").read_text(encoding="utf-8"))
MAX_REQUESTS_PER_TURN = 8
# Conversations one invocation runs by default, shared by the cases it runs,
# and the most one case gets from that share.
RUN_BUDGET = 100
MAX_RUNS = 20


def render(session, revision=None):
    """The page's system prompt and opening line for this session, from the
    checkout or, given a git revision, from the page's source at it."""
    node = shutil.which("node")
    if node is None:
        sys.exit("node is required to render the printer panel's prompt")
    result = subprocess.run([node, str(HERE / "render_prompt.mjs")] + ([revision] if revision else []), input=json.dumps(session),
                            capture_output=True, encoding="utf-8", check=True)
    return json.loads(result.stdout)


def post(body, key, endpoint=ENDPOINT):
    request = urllib.request.Request(endpoint, data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503) or attempt == 4:
                raise RuntimeError(f"OpenAI {error.code}: {error.read().decode(errors='replace')[:500]}")
        except (urllib.error.URLError, ConnectionError):  # ConnectionError: a reset mid-response
            if attempt == 4:
                raise
        time.sleep(2 ** attempt)


def judge(log, question):
    """A yes or no about the conversation's last reply, from a stronger model,
    for what no string test can check. Returns (yes, why)."""
    schema = {"type": "object", "additionalProperties": False, "required": ["answer", "why"],
              "properties": {"answer": {"type": "string", "enum": ["yes", "no"]}, "why": {"type": "string"}}}
    response = post({"model": JUDGE_MODEL, "store": False,
                     "instructions": "You read a chat between a 3D printing app's assistant and a person, and answer one "
                                     "question about it strictly as written, judging only what the messages say.",
                     "input": "\n".join(log) + "\n\nQuestion: " + question,
                     "text": {"format": {"type": "json_schema", "name": "verdict", "strict": True, "schema": schema}}},
                    os.environ["OPENAI_API_KEY"], JUDGE_ENDPOINT)
    text = "".join(output_text(item) for item in response.get("output", []) if item.get("type") == "message")
    verdict = json.loads(text)
    return verdict["answer"] == "yes", verdict["why"]


# What every reply of every case must hold to, whatever the case checks.
INTERNAL_TERMS = re.compile(r"\b(profiles?|presets?|catalog(ue)?|catalogId|deviceId|printer_[a-z_]+)\b", re.IGNORECASE)


def reply_problems(reply):
    """How a reply breaks the prompt's rules for every reply: a "Choices:" line
    the page would show as text or draw wrongly (replyChoices.ts), or internal
    words."""
    problems = []
    lines = reply.rstrip().splitlines()
    found = re.search(r"(^|\s)Choices:(.*)$", lines[-1]) if lines else None
    if found is None and "Choices:" in reply:
        problems.append("CHOICES NOT ON THE LAST LINE")
    if found is not None:
        choices = [choice.strip() for choice in found.group(2).split("|") if choice.strip()]
        if choices != ["Done"] and not 2 <= len(choices) <= 4:
            problems.append(f"{len(choices)} CHOICES")
        if any(len(choice) >= 30 for choice in choices):
            problems.append("LONG CHOICE")
    if INTERNAL_TERMS.search(reply):
        problems.append("INTERNAL TERM " + repr(INTERNAL_TERMS.search(reply).group(0)))
    return problems


def dump(value):
    """JSON as the app writes it: compact, keys sorted."""
    return json.dumps(value, separators=(",", ":"), sort_keys=True, ensure_ascii=False)


def output_text(item):
    return "".join(part.get("text", "") for part in item.get("content", []) if part.get("type") == "output_text")


def invalid_arguments(tool, arguments):
    """The registry's check before a tool runs, as far as the schema says."""
    schema = next((t["parameters"] for t in TOOLS if t["name"] == tool), None)
    if schema is None:
        return {"code": "unknown_tool", "message": f'There is no tool named "{tool}" here. Nothing was proposed. Use one of the listed tools.'}
    missing = [name for name in schema.get("required", []) if name not in arguments]
    extra = [name for name in arguments if name not in schema.get("properties", {})]
    # ToolRegistry's valid_arguments for printer_connect: deviceId alone, or
    # hostType with a non-empty address.
    if tool == "printer_connect" and not extra and not (
            (set(arguments) == {"deviceId"} and arguments["deviceId"]) or
            (set(arguments) == {"hostType", "address"} and arguments["address"] and
             arguments["hostType"] in ("moonraker", "octoprint"))):
        return {"code": "invalid_arguments",
                "message": "The tool arguments do not match the registered contract. Give deviceId alone, or hostType with "
                           "the address the person gave. Without an address, ask the person for the one they open the "
                           "printer with in a browser, and call this once they give it."}
    if missing or extra:
        return {"code": "invalid_arguments",
                "message": f"Missing {missing}, unexpected {extra}. Nothing was proposed. "
                           "Check the arguments against this tool's parameters and call it again."}
    return None


class Conversation:
    """One run of a case: the chat as the app would hold it."""

    def __init__(self, case, prompt, key, model, endpoint=ENDPOINT, providers=None):
        self.case, self.instructions, self.key, self.model = case, prompt["instructions"], key, model
        self.endpoint = endpoint
        # OpenRouter's hosts only, none else: which host serves a request decides
        # how a message it does not expect, such as the app's developer notes, is
        # rendered for the model.
        self.routing = {"provider": {"order": providers, "allow_fallbacks": False}} if providers else {}
        self.history = [{"role": "assistant", "content": prompt["opening"]}]  # the page's opening line
        self.log = ["assistant: " + prompt["opening"]]
        self.waiting = None  # a card left up at the end of the last turn
        self.waiting_turn = None  # the input of the turn that card stopped

    def say(self, words):
        """One turn: the person's words, then the model until it stops or a card waits."""
        self.log.append("person: " + words)
        # Writing while a card waits cancels it; the replay says so.
        if self.waiting is not None:
            self.history += self.replayed(self.waiting, {"state": "cancelled"})
            self.waiting = None
        turn = self.history + [{"role": "user", "content": [{"type": "input_text", "text": words}]}]
        self.history.append({"role": "user", "content": words})
        self.respond(turn)

    def note(self, text):
        """The app's own line, as the developer, then the turn it starts
        (AgentHost::start_turn): the person has said nothing."""
        self.log.append("app: " + text)
        self.history.append({"role": "developer", "content": text})
        self.respond(list(self.history))

    def decide(self, result):
        """The person decides the waiting card; its result continues the turn
        that card stopped, as the app's continue_after_tool does."""
        call, turn = self.waiting, self.waiting_turn
        self.waiting = self.waiting_turn = None
        self.log.append("  card decided -> " + dump(result))
        turn.append({"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)})
        self.history += self.replayed(call, result)
        self.respond(turn)

    def respond(self, turn):
        """The model until it stops or a card waits."""
        rejected = 0
        for _ in range(MAX_REQUESTS_PER_TURN):
            response = post({"model": self.model, "store": False, "parallel_tool_calls": False,
                             "instructions": self.instructions, "tools": TOOLS, "input": turn, **self.routing},
                            self.key, self.endpoint)
            output = response.get("output", [])
            turn += output
            for reply in (output_text(item) for item in output if item.get("type") == "message"):
                if reply:
                    self.history.append({"role": "assistant", "content": reply})
                    self.log.append("assistant: " + reply)
            call = next((item for item in output if item.get("type") == "function_call"), None)
            if call is None:
                break
            arguments = json.loads(call.get("arguments") or "{}")
            self.log.append(f"tool: {call['name']} {json.dumps(arguments)}")
            problem = invalid_arguments(call["name"], arguments)
            if problem and rejected < 2:
                rejected += 1
                result, waits, recorded = {"state": "failed", "error": problem}, False, False
            else:
                (result, waits), recorded = self.case.tool(call["name"], arguments), True
            self.log.append("  -> " + dump(result))
            if waits:
                self.waiting, self.waiting_turn = call, turn  # a card is up: the app waits for the person
                break
            turn.append({"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)})
            if recorded:
                self.history += self.replayed(call, result)

    @staticmethod
    def replayed(call, result):
        return [{"type": "function_call", "call_id": call["call_id"], "name": call["name"], "arguments": call["arguments"]},
                {"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)}]


# --- Cases -------------------------------------------------------------------


class ConnectBambu:
    """Connect a saved Bambu Lab A1 mini that is on the network in LAN mode.

    Passes when the model opens printer_connect's card for that printer. The
    person confirms twice at most, as --printer-live's script does.
    """

    name = "connect-bambu"
    session = {
        "mode": "connect", "printerName": "Bambu Lab A1 mini", "blocks": [],
        "context": {"printer": {
            "name": "Bambu Lab A1 mini", "model": "Bambu Lab A1 mini", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
            "spools": [],
            "connected": False, "provider": "bambu"}},
    }
    # printer_connection_status as the app answered it for the fake printer.
    status = {"address": "", "candidates": [{"address": "127.0.0.1", "deviceId": "FAKE001", "name": "JusPrin Fake A1 mini"}],
              "hostType": "", "message": "", "nozzleMismatch": False, "provider": "bambu", "state": "not_configured"}

    def __init__(self):
        self.card = False
        self.refused = []
        self.other = []

    def tool(self, name, arguments):
        if name == "printer_connection_status":
            return self.status, False
        if name == "printer_connect":
            # The app accepts a listed printer's id or its name (preflight_tool).
            device = arguments.get("deviceId", "")
            if device in ("FAKE001", "JusPrin Fake A1 mini"):
                self.card = True
                return {"state": "pending"}, True
            self.refused.append(device)
            return {"error": {"code": "unknown_device",
                              "message": "Pass the deviceId of one of these printers in LAN mode: FAKE001 (JusPrin Fake A1 mini)."}}, False
        self.other.append(name)
        if name == "printer_manual_connection":
            return {"state": "opened"}, False
        return {"error": {"code": "not_in_this_test", "message": "This test does not answer " + name + "."}}, False

    def run(self, conversation):
        conversation.say("Yes, it is")
        if not self.card:
            conversation.say("Yes, connect it")
        detail = "card" if self.card else "NO CARD"
        if self.refused:
            detail += "; refused " + ", ".join(repr(device) for device in self.refused)
        if self.other:
            detail += "; also called " + ", ".join(self.other)
        return self.card, detail


ADD = json.loads((HERE / "add_session.json").read_text(encoding="utf-8"))
CONNECT_OFFER = "Want to connect it so you can send prints straight to it?\nChoices: Connect it | Not now"
CATALOG = {entry[0]: entry[1] for entry in ADD["session"]["context"]["printers"]}
# printer_identify's answers: the recorded ones, and the Bambu Lab A1 written
# from its profile (resources/profiles/BBL) in the same shape, for "bambu a1".
IDENTIFIED = {**ADD["printer_identify"],
              "BBL/Bambu Lab A1": {"alreadyYours": False, "brand": "Bambu Lab", "buildVolume": "256 × 256 × 256 mm",
                                   "catalogId": "BBL/Bambu Lab A1", "model": "A1", "nozzles": [0.2, 0.4, 0.6, 0.8],
                                   "assumed": {"filament": "Bambu PLA Basic @BBL A1", "nozzle": 0.4,
                                               "plate": "Textured PEI Plate"}}}


def unknown_nozzle(nozzles, model, nozzle):
    """PrinterConversation.cpp's unknown_nozzle refusal."""
    sizes = [f"{size:g}" for size in nozzles]
    return {"error": {"code": "unknown_nozzle",
                      "message": f"JusPrin supports {', '.join(sizes[:-1])} and {sizes[-1]} mm nozzles for {model}, but not "
                                 f"{nozzle:g} mm. Ask the person to check the nozzle marking or packaging; do not substitute "
                                 "a size."}}


class AddCase:
    """Adding a printer, on the Add session the app sent in a recorded run: its
    full printer list, and the tools' answers for the printers the case names,
    checked as PrinterConversation::identify and ::add check them. A call about
    any other printer is answered as untested and reported."""

    session = ADD["session"]

    def __init__(self):
        self.added = []
        self.lookups = []  # printer_identify calls that drew cards, by turn
        self.identify_calls = []  # every printer_identify call, drawn or refused
        self.manual_setup = 0
        self.turn = 0
        self.untested = []

    def tool(self, name, arguments):
        nozzle = arguments.get("nozzle")
        said = isinstance(nozzle, (int, float)) and nozzle > 0
        if name == "printer_identify":
            self.identify_calls.append(arguments)
            ids = list(dict.fromkeys(arguments.get("catalogIds", [])))
            unknown = [catalog_id for catalog_id in ids if catalog_id not in CATALOG]
            if unknown:
                return {"error": {"code": "unknown_printer", "message": f'"{unknown[0]}" is not on the printer list; copy it '
                                                                      "exactly from the printer list."}}, False
            if len(ids) > 3:
                return {"error": {"code": "too_many",
                                  "message": "More than three fit. Ask one question that narrows it down instead."}}, False
            if not all(catalog_id in IDENTIFIED for catalog_id in ids):
                self.untested += ids
                return {"error": {"code": "not_in_this_test", "message": "This test has no answer for " + ", ".join(ids) + "."}}, False
            for catalog_id in ids:
                if said and nozzle not in IDENTIFIED[catalog_id]["nozzles"]:
                    return unknown_nozzle(IDENTIFIED[catalog_id]["nozzles"], CATALOG[catalog_id], nozzle), False
            self.lookups.append(self.turn)
            return {"printers": [IDENTIFIED[catalog_id] for catalog_id in ids]}, False
        if name == "printer_add":
            catalog_id = arguments.get("catalogId", "")
            if catalog_id in self.added:
                return {"error": {"code": "already_added", "message": "This conversation already added it."}}, False
            if catalog_id not in ADD["printer_add"]:
                self.untested.append(catalog_id)
                return {"error": {"code": "not_in_this_test", "message": "This test has no answer for " + catalog_id + "."}}, False
            answer = ADD["printer_add"][catalog_id]
            if said and nozzle not in answer["printer"]["nozzles"]:
                return unknown_nozzle(answer["printer"]["nozzles"], CATALOG[catalog_id], nozzle), False
            self.added.append(catalog_id)
            return {"printer": {**answer["printer"], "nozzle": nozzle if said else answer["printer"]["nozzle"]}}, False
        if name == "printer_manual_setup":
            # The person closes OrcaSlicer's list without adding anything.
            self.manual_setup += 1
            return {"applied": False, "added": []}, False
        self.untested.append(name)
        return {"error": {"code": "not_in_this_test", "message": "This test does not answer " + name + "."}}, False

    def say(self, conversation, words):
        """The turn's last reply."""
        self.turn += 1
        start = len(conversation.log)
        conversation.say(words)
        replies = [line for line in conversation.log[start:] if line.startswith("assistant: ")]
        return replies[-1][len("assistant: "):] if replies else ""

    def outcome(self, printer, reply):
        # Space at a line's end is not shown, the page trims each choice, and a
        # blank line before the choices draws the same chips.
        offered = "\n".join(line.rstrip() for line in reply.rstrip().splitlines() if line.strip()).endswith(CONNECT_OFFER)
        passed = self.added == [printer] and offered
        detail = ("added" if self.added == [printer] else f"added {self.added or 'nothing'}") + \
                 ("; offered to connect" if offered else "; NO CONNECT OFFER: " + repr(reply[-120:]))
        if self.untested:
            detail += "; untested " + ", ".join(self.untested)
        return passed, detail


class AddNamedModel(AddCase):
    """The person names one model plainly. Passes when that printer is added
    and the reply ends with the connect offer and its two choices."""

    name = "add-named-model"

    def run(self, conversation):
        reply = self.say(conversation, "bambu lab a1 mini")
        return self.outcome("BBL/Bambu Lab A1 mini", reply)


class AddChooseFromThree(AddCase):
    """A name that fits three models, then the person's choice, as a tap on its
    chip sends it. Passes when nothing is added before the choice, the chosen
    printer is, and the reply ends with the connect offer. Looking the chosen
    printer up again draws a second picture card; it is reported, not failed."""

    name = "add-choose-from-three"

    def run(self, conversation):
        self.say(conversation, "prusa mk4")
        asked_first = not self.added
        reply = self.say(conversation, "Prusa MK4S")
        passed, detail = self.outcome("Prusa/Prusa MK4S", reply)
        if not asked_first:
            passed, detail = False, "ADDED BEFORE ASKING; " + detail
        if 2 in self.lookups:
            detail += "; looked it up again"
        return passed, detail


class AddThenUndo(AddCase):
    """The person names one model, it is added, and they tap Undo on its
    receipt: the app removes it and says so in a note, then the model answers
    with nothing said. Passes when that reply adds nothing, drops the connect
    offer, and asks which printer they have."""

    name = "add-then-undo"
    printer = "BBL/Bambu Lab A1 mini"

    def run(self, conversation):
        self.say(conversation, "bambu lab a1 mini")
        if self.added != [self.printer]:
            return False, f"setup: added {self.added or 'nothing'}"
        name = ADD["printer_add"][self.printer]["printer"]["name"]
        # The app's note, word for word (PrinterConversation::undo_add).
        self.added = []  # the printer is gone; another add of it is a new one
        self.turn += 1
        start = len(conversation.log)
        conversation.note(f"The person tapped Undo on the receipt: {name} is removed and is no longer one of their printers.")
        replies = [line[len("assistant: "):] for line in conversation.log[start:] if line.startswith("assistant: ")]
        reply = replies[-1] if replies else ""
        problems = []
        if self.added:
            problems.append(f"ADDED {self.added} AGAIN")
        if not reply:
            problems.append("NO REPLY")
        if "Connect it" in reply:
            problems.append("STILL OFFERS TO CONNECT")
        if "?" not in reply:
            problems.append("ASKED NOTHING")
        detail = "; ".join(problems) or "asked again: " + repr(reply[-120:])
        if self.untested:
            detail += "; untested " + ", ".join(self.untested)
        return not problems, detail


def last_reply(conversation, words=None, note=None):
    """The last thing the model wrote in one turn: the person's words, or the
    app's note."""
    start = len(conversation.log)
    if note is None:
        conversation.say(words)
    else:
        conversation.note(note)
    replies = [line for line in conversation.log[start:] if line.startswith("assistant: ")]
    return replies[-1][len("assistant: "):] if replies else ""


def offers_done(reply):
    """Whether the page draws Done as the reply's one chip, reading the reply
    as splitChoices (replyChoices.ts) does: "Choices:" starting a word on the
    last line, with something said before it."""
    lines = reply.rstrip().splitlines()
    found = re.search(r"(^|\s)Choices:(.*)$", lines[-1]) if lines else None
    if found is None:
        return False
    said = "\n".join(lines[:-1]) + lines[-1][:found.start()]
    return [choice.strip() for choice in found.group(2).split("|") if choice.strip()] == ["Done"] and said.strip() != ""


class Finishing:
    """The end of a case: once nothing is left to decide, the reply ends with
    Done alone, and "Done", as its chip sends it, closes the panel. Closing
    before that is a failure, whatever else happened."""

    finished = 0  # printer_setup_finish calls

    def finish(self, name):
        if name == "printer_setup_finish":
            self.finished += 1
            return {"state": "closed"}, False
        return None

    def end(self, conversation, reply, detail):
        offered = offers_done(reply)
        early = self.finished
        last_reply(conversation, "Done")
        closed = self.finished > early
        detail += "; offered Done" if offered else "; NO DONE OFFER: " + repr(reply[-120:])
        if early:
            detail += "; CLOSED BEFORE DONE"
        detail += "; closed on Done" if closed else "; NOT CLOSED ON DONE"
        return offered and not early and closed, detail


class AddNotNowDone(Finishing, AddCase):
    """The person adds a printer and turns down connecting it. Passes when the
    reply to "Not now" offers Done alone and Done closes the panel."""

    name = "add-not-now-done"

    def tool(self, name, arguments):
        return self.finish(name) or AddCase.tool(self, name, arguments)

    def run(self, conversation):
        self.say(conversation, "bambu lab a1 mini")
        reply = self.say(conversation, "Not now")
        detail = "added" if self.added == ["BBL/Bambu Lab A1 mini"] else f"added {self.added or 'nothing'}"
        return self.end(conversation, reply, detail)


class ConnectOutcome(Finishing, ConnectBambu):
    """Connect the saved A1 mini, as connect-bambu does, then the person taps
    Connect on the card and the app reports the outcome in a note."""

    def tool(self, name, arguments):
        return self.finish(name) or ConnectBambu.tool(self, name, arguments)

    def connect(self, conversation, outcome):
        """The model's reply to the app's note, or None when no card came."""
        ConnectBambu.run(self, conversation)
        if not self.card:
            return None
        conversation.decide({"state": "connecting", "message": "The app is checking the printer now and gives it up to 30 seconds."})
        return last_reply(conversation, note="Connection to Bambu Lab A1 mini " + outcome)


class ConnectVerifiedDone(ConnectOutcome):
    """Passes when the reply to a verified connection offers Done alone and
    Done closes the panel."""

    name = "connect-verified-done"

    def run(self, conversation):
        reply = self.connect(conversation, "verified.")
        if reply is None:
            return False, "NO CARD"
        return self.end(conversation, reply, "verified")


class ConnectLeaveDone(ConnectOutcome):
    """The connection fails and the person leaves it for now. Passes when the
    reply to that offers Done alone and Done closes the panel."""

    name = "connect-leave-done"

    def run(self, conversation):
        reply = self.connect(conversation, "failed: The printer did not respond. Check that it is on the network and try again.")
        if reply is None:
            return False, "NO CARD"
        reply = last_reply(conversation, "Leave it for now")
        return self.end(conversation, reply, "failed, left")


class ConnectAfterQuestion(Finishing, ConnectBambu):
    """The person writes while the card waits, as --printer-live does: the app
    cancels the card and the waiting turn goes on with that, then the question
    is a turn of its own. A cancelled card is not a failed connection. Passes
    when neither reply offers Done or closes the panel, and "connect it" then
    opens a fresh card."""

    name = "connect-after-question"

    def tool(self, name, arguments):
        return self.finish(name) or ConnectBambu.tool(self, name, arguments)

    def run(self, conversation):
        ConnectBambu.run(self, conversation)
        if not self.card:
            return False, "NO CARD"
        start = len(conversation.log)
        conversation.decide({"state": "cancelled"})
        cancelled = [line for line in conversation.log[start:] if line.startswith("assistant: ")]
        answered = last_reply(conversation, "where do I find the access code?")
        self.card = False
        last_reply(conversation, "ok, I have the code now. Connect it")
        offered = [reply for reply in cancelled + ["assistant: " + answered] if offers_done(reply[len("assistant: "):])]
        detail = "fresh card" if self.card else "NO FRESH CARD"
        if offered:
            detail += "; DONE OFFERED: " + repr(offered[0][-120:])
        if self.finished:
            detail += "; CLOSED"
        return self.card and not offered and not self.finished, detail


class ChangeNozzleDone(Finishing):
    """The person says the nozzle changed. Passes when it is changed to that
    size alone, and the reply offers Done alone and Done closes the panel."""

    name = "change-nozzle-done"
    printer = {"name": "Bambu Lab A1 mini", "model": "Bambu Lab A1 mini", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
               "spools": [], "connected": False}
    session = {"mode": "change", "printerName": printer["name"], "blocks": [],
               "context": {"printer": {**printer, "provider": "bambu"}}}

    def __init__(self):
        self.changes = []
        self.other = []

    def tool(self, name, arguments):
        if name == "printer_setup_finish":
            return self.finish(name)
        if name == "printer_change":
            self.changes.append(arguments)
            after = {**self.printer, "nozzle": arguments.get("nozzle", self.printer["nozzle"])}
            return {"changed": [{"field": "nozzle", "before": 0.4, "after": after["nozzle"]}], "printer": after}, False
        self.other.append(name)
        return {"error": {"code": "not_in_this_test", "message": "This test does not answer " + name + "."}}, False

    def run(self, conversation):
        reply = last_reply(conversation, "I put a 0.6 nozzle on it")
        changed = self.changes == [{"printerName": self.printer["name"], "nozzle": 0.6}]
        detail = "changed to 0.6" if changed else f"CHANGES {self.changes}"
        if self.other:
            detail += "; also called " + ", ".join(self.other)
        passed, detail = self.end(conversation, reply, detail)
        return passed and changed, detail


# --- Cases across the three sessions -----------------------------------------
#
# The cases above follow the main paths. These are the rest of what people
# say in each session: questions the tools cannot answer, facts that need
# nothing saved, and requests that belong to another session. Each checks
# both what should happen and what must not, so a prompt edit made for one
# session is measured against the others.

# What the model must never do while talking about a printer it already has.
ADDING = ("printer_identify", "printer_add", "printer_manual_setup")
MUTATIONS = ADDING + ("printer_change", "printer_connect", "printer_setup_finish")

KOBRA = {"name": "Anycubic Kobra 3", "model": "Anycubic Kobra 3", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
         "spools": [], "connected": False}
A1_MINI = {"name": "Bambu Lab A1 mini", "model": "Bambu Lab A1 mini", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
           "spools": [], "connected": False}
VORON = {"name": "Voron 2.4 350", "model": "Voron 2.4 350", "nozzle": 0.4, "nozzles": [0.4, 0.6], "spools": [],
         "connected": False}
# printer_connection_status for a print host with no address saved, as
# OrcaPrinterBackend reports it; Orca's default host type is OctoPrint.
HOST_NOT_CONFIGURED = {"address": "", "candidates": [], "hostType": "octoprint", "message": "", "nozzleMismatch": False,
                       "provider": "host", "state": "not_configured"}


class SavedPrinterCase:
    """A Change or Connect session about one saved printer, its tools answered
    as PrinterConversation answers them for it. Every call is recorded."""

    mode = "change"
    printer = KOBRA
    provider = "host"
    status = HOST_NOT_CONFIGURED
    needs_plugin = False  # a Bambu Lab printer on a computer without Bambu's network plug-in

    def __init__(self):
        self.calls = []
        self.printer = dict(self.printer)

    @classmethod
    def session_for(cls):
        printer = {**cls.printer, "provider": cls.provider}
        if cls.needs_plugin:
            printer["needsNetworkPlugin"] = True
        return {"mode": cls.mode, "printerName": cls.printer["name"], "blocks": [], "context": {"printer": printer}}

    def tool(self, name, arguments):
        self.calls.append((name, arguments))
        if name == "printer_change":
            return self.change(arguments), False
        if name == "printer_connection_status":
            return self.status, False
        if name == "printer_connect":
            return self.connect(arguments)
        if name == "printer_manual_connection":
            return {"state": "opened"}, False
        if name == "printer_setup_finish":
            return {"state": "closed"}, False
        return {"error": {"code": "not_in_this_test", "message": "This test does not answer " + name + "."}}, False

    def change(self, arguments):
        """PrinterConversation::change."""
        name, nozzle = arguments.get("printerName", ""), arguments.get("nozzle")
        if name != self.printer["name"]:
            return {"error": {"code": "unknown_printer", "message": f'No saved printer is named "{name}". The saved printer '
                                                                  f'this conversation is about is "{self.printer["name"]}".'}}
        if not isinstance(nozzle, (int, float)) or nozzle <= 0:
            return {"error": {"code": "nothing_to_change", "message": "Name the nozzle size that changed."}}
        if nozzle not in self.printer["nozzles"]:
            return unknown_nozzle(self.printer["nozzles"], self.printer["model"], nozzle)
        if nozzle == self.printer["nozzle"]:
            return {"error": {"code": "nothing_to_change", "message": f"That is already how {name} is set up."}}
        before, self.printer["nozzle"] = self.printer["nozzle"], nozzle
        return {"changed": [{"field": "nozzle", "before": before, "after": nozzle}], "printer": dict(self.printer)}

    def connect(self, arguments):
        """PrinterConversation::preflight_tool, then the card waits."""
        if self.needs_plugin:
            return {"error": {"code": "connection_unavailable", "message": self.status["message"]}}, False
        if self.provider == "host":
            if "hostType" not in arguments:
                return {"error": {"code": "address_needed", "message": "Pass hostType and the address the person gave."}}, False
            return {"state": "pending"}, True
        listed = [c for c in self.status.get("candidates", []) if arguments.get("deviceId") in (c["deviceId"], c["name"])]
        if not listed:
            return {"error": {"code": "unknown_device", "message": "No printer in LAN mode is on the network now; check again "
                                                                   "with printer_connection_status."}}, False
        return {"state": "pending"}, True

    def called(self, *names):
        return [(name, arguments) for name, arguments in self.calls if name in names]

    def forbidden(self, names=MUTATIONS):
        """A detail naming the calls this case must not make."""
        made = sorted({name for name, _ in self.called(*names)})
        return "CALLED " + ", ".join(made) if made else ""


def judged(conversation, question):
    """A detail for a judge's yes; empty for no."""
    yes, why = judge(conversation.log, question)
    return f"JUDGE YES to {question!r}: {why}" if yes else ""


def verdict(problems, fine):
    problems = [problem for problem in problems if problem]
    return not problems, "; ".join(problems) or fine


class ChangeAskStartGcode(SavedPrinterCase):
    """The person asks about a setting no tool reads (seen 2026-09-28: the reply
    offered to add the printer first). Passes when nothing is added or changed
    and the reply neither treats the printer as not yet added, nor says the
    setting needs a network connection (it is on this computer), nor asks which
    printer this is (seen the same day, a Bambu Lab A1 asked about z offset:
    "Which printer is it?")."""

    name = "change-ask-start-gcode"

    def run(self, conversation):
        last_reply(conversation, "what's the current start g-code?")
        return verdict([self.forbidden(),
                        judged(conversation, "Does the assistant's last message say or imply that this printer has not been "
                                             "added to the app yet, for example by offering to add it?"),
                        judged(conversation, "Does the assistant's last message say or imply that the printer has to be "
                                             "connected over the network before its start g-code can be seen?"),
                        judged(conversation, "Does the assistant's last message ask which printer this is, or say it "
                                             "needs to know the printer's model, although the conversation names it?")],
                       "answered without adding or blaming the connection")


class ChangeAskNozzle(SavedPrinterCase):
    """A question the facts answer. Passes when the reply says 0.4 and no tool
    changes anything."""

    name = "change-ask-nozzle"

    def run(self, conversation):
        reply = last_reply(conversation, "what nozzle size is it set up for?")
        return verdict([self.forbidden(), "" if "0.4" in reply else "NO 0.4: " + repr(reply[-120:])], "said 0.4")


class ChangeLoadedFilament(SavedPrinterCase):
    """Loading a spool is nothing to save. Passes when printer_change is not
    called and nothing else changes."""

    name = "change-loaded-filament"

    def run(self, conversation):
        last_reply(conversation, "I just loaded black PETG")
        return verdict([self.forbidden()], "saved nothing")


class ChangePlate(SavedPrinterCase):
    """The plate is each project's. Passes when nothing is saved."""

    name = "change-plate"

    def run(self, conversation):
        last_reply(conversation, "I switched to the textured PEI plate")
        return verdict([self.forbidden()], "saved nothing")


def refused_nozzle_problems(conversation, reply, nozzles):
    """After unknown_nozzle the reply names every size the model ships and asks
    the person to check the one on the printer. Without that rule the model
    listed the sizes and stopped (3633478c0e)."""
    missing = [f"{size:g}" for size in nozzles if f"{size:g}" not in reply]
    yes, why = judge(conversation.log, "Does the assistant's last message ask the person to check which nozzle size is on "
                                       "the printer, for example from the marking on the nozzle or its packaging?")
    return [f"SIZES NOT NAMED {missing}: {reply[-120:]!r}" if missing else "", "" if yes else "DID NOT ASK TO CHECK: " + why]


class ChangeUnshippedNozzle(SavedPrinterCase):
    """A size this model does not ship. Passes when nothing is saved, and the
    reply names every size it does ship and asks the person to check."""

    name = "change-unshipped-nozzle"

    def run(self, conversation):
        reply = last_reply(conversation, "I put a 0.3 nozzle on it")
        saved = self.printer["nozzle"] != KOBRA["nozzle"]
        return verdict([self.forbidden(ADDING + ("printer_connect", "printer_setup_finish")),
                        f"SAVED {self.printer['nozzle']}" if saved else ""] +
                       refused_nozzle_problems(conversation, reply, KOBRA["nozzles"]),
                       "saved nothing; named the sizes and asked")


class ChangeThenConnect(SavedPrinterCase):
    """Connecting, asked for from a Change session. Passes when the model asks
    for the address rather than inventing one, and the address the person then
    gives opens the card; nothing is added."""

    name = "change-then-connect"

    def run(self, conversation):
        last_reply(conversation, "can you connect it so I can send prints to it?")
        early = self.called("printer_connect")
        last_reply(conversation, "it's at 192.168.1.42")
        if not self.called("printer_connect"):
            last_reply(conversation, "it runs Moonraker")  # as --printer-live answers, if still asked
        cards = [a for _, a in self.called("printer_connect") if "192.168.1.42" in a.get("address", "") and "hostType" in a]
        return verdict([self.forbidden(ADDING + ("printer_change", "printer_setup_finish")),
                        f"CONNECT BEFORE THE ADDRESS {early}" if early else "",
                        "" if cards else f"NO CARD FOR THE ADDRESS {self.called('printer_connect')}"],
                       "asked, then opened the card")


class ChangeAskLoaded(SavedPrinterCase):
    """A connected printer reports what it holds. Passes when the reply names
    both materials it reports and nothing changes."""

    name = "change-ask-loaded"
    provider = "bambu"
    printer = {**A1_MINI, "connected": True,
               "spools": [{"name": "PLA Matte", "material": "PLA", "colour": "5F7D4FFF"},
                          {"name": "PETG HF", "material": "PETG", "colour": "000000FF"}]}

    def run(self, conversation):
        reply = last_reply(conversation, "what filament is loaded right now?")
        missing = [m for m in ("PLA", "PETG") if m not in reply]
        return verdict([self.forbidden(), f"NOT NAMED {missing}: {reply[-120:]!r}" if missing else ""], "named both")


class ConnectHostAddress(SavedPrinterCase):
    """A Klipper printer: the person gives the address and what they open it
    with. Passes when the card opens for Moonraker at that address within two
    replies, and nothing is added."""

    name = "connect-host-address"
    mode = "connect"
    printer = VORON

    def run(self, conversation):
        last_reply(conversation, "it's http://voron.local, I open it with Mainsail")
        if not self.called("printer_connect"):
            last_reply(conversation, "yes, connect it")
        cards = [a for _, a in self.called("printer_connect")
                 if a.get("hostType") == "moonraker" and "voron.local" in a.get("address", "")]
        return verdict([self.forbidden(ADDING + ("printer_change", "printer_setup_finish")),
                        "" if cards else f"NO MOONRAKER CARD {self.called('printer_connect')}"], "card for voron.local")


class ConnectBambuNotFound(SavedPrinterCase):
    """No Bambu Lab printer in LAN mode is on the network. Passes when the
    status is checked, no card opens, and the reply points to LAN mode on the
    printer or asks what they see."""

    name = "connect-bambu-not-found"
    mode = "connect"
    provider = "bambu"
    printer = A1_MINI
    status = {"address": "", "candidates": [], "hostType": "", "message": "", "nozzleMismatch": False,
              "provider": "bambu", "state": "not_configured"}

    def run(self, conversation):
        last_reply(conversation, "Yes, it is")
        yes, why = judge(conversation.log, "Does the assistant's last message tell the person where to turn LAN mode on, or "
                                           "ask what they see on the printer's screen?")
        return verdict([self.forbidden(), "" if self.called("printer_connection_status") else "NO STATUS CHECK",
                        "" if yes else "NO LAN MODE GUIDANCE: " + why], "checked; pointed to LAN mode")


class ConnectAskWhy(SavedPrinterCase):
    """A question before connecting. Passes when nothing runs and the reply
    says the printer prepares prints without a connection."""

    name = "connect-ask-why"
    mode = "connect"
    printer = VORON

    def run(self, conversation):
        last_reply(conversation, "do I have to connect it? what do I get from that?")
        yes, why = judge(conversation.log, "Does the assistant's last message make clear that connecting is optional, or that "
                                           "the printer can already be used to prepare prints without a connection?")
        return verdict([self.forbidden(), "" if yes else "NOT SAID OPTIONAL: " + why], "said it is optional")


class AddVagueDescription(AddCase):
    """A description one model fits. Passes when nothing is added before the
    person confirms, and the reply asks."""

    name = "add-vague-description"

    def run(self, conversation):
        reply = self.say(conversation, "the small bambu one")
        return verdict([f"ADDED {self.added}" if self.added else "", "" if "?" in reply else "ASKED NOTHING"],
                       "asked before adding" + ("; untested " + ", ".join(self.untested) if self.untested else ""))


class AddUnsupportedPrinter(AddCase):
    """A resin printer, which the list does not have. Passes when nothing is
    shown or added and no other printer is offered in its place."""

    name = "add-unsupported-printer"

    def run(self, conversation):
        self.say(conversation, "an Elegoo Saturn 4 Ultra")
        return verdict([f"ADDED {self.added}" if self.added else "", "SHOWED A PRINTER" if self.lookups else "",
                        judged(conversation, "Does the assistant's last message offer or suggest a specific different "
                                             "printer model as a substitute?")],
                       "said so; offered no substitute")


# --- Regressions: behaviour an earlier prompt or tool change fixed -----------
#
# Each names the commit that measured the failure. They keep the fix from
# being lost to a later edit made for something else.


class AddManyFit(AddCase):
    """A brand with many models (7f66e75b07: the model tried to show more than
    three printers 33 times until the rule and the too_many refusal). Passes
    when no printer is looked up or added and the reply narrows it down or
    offers the full list."""

    name = "add-many-fit"

    def run(self, conversation):
        self.say(conversation, "prusa")
        yes, why = judge(conversation.log, "Does the assistant's last message ask a question that narrows down which model "
                                           "it is, or offer the full list of printers?")
        return verdict([f"ADDED {self.added}" if self.added else "",
                        f"LOOKED UP {[c.get('catalogIds') for c in self.identify_calls]}" if self.identify_calls else "",
                        "" if yes else "DID NOT NARROW: " + why], "asked; showed nothing")


class AddNameStartsTwo(AddCase):
    """A name that equals one model and begins another: "bambu a1" is the A1
    and the A1 mini (7f66e75b07 measured it at 0 of 3 with the rule in place).
    Passes when nothing is added and the reply asks which of the two."""

    name = "add-name-starts-two"

    def run(self, conversation):
        self.say(conversation, "bambu a1")
        yes, why = judge(conversation.log, "Does the assistant's last message ask the person whether their printer is the "
                                           "Bambu Lab A1 or the A1 mini?")
        return verdict([f"ADDED {self.added}" if self.added else "", "" if yes else "DID NOT ASK WHICH: " + why],
                       "asked A1 or A1 mini")


class AddUnshippedNozzle(AddCase):
    """A named model with a size it does not ship. Passes when nothing is added,
    and the reply names the sizes it ships and asks the person to check
    (3633478c0e)."""

    name = "add-unshipped-nozzle"

    def run(self, conversation):
        reply = self.say(conversation, "bambu lab a1 mini with a 0.3 nozzle")
        return verdict([f"ADDED {self.added}" if self.added else ""] +
                       refused_nozzle_problems(conversation, reply, IDENTIFIED["BBL/Bambu Lab A1 mini"]["nozzles"]),
                       "added nothing; named the sizes and asked")


class AddFullList(AddCase):
    """The person asks for the whole list (0f382d4098 and 9089d6fee7 saw it
    answered in words instead in 2 to 9 runs of 20). Passes when the list is
    opened, and nothing is added."""

    name = "add-full-list"

    def run(self, conversation):
        self.say(conversation, "show me the full printer list")
        return verdict([f"ADDED {self.added}" if self.added else "", "" if self.manual_setup else "LIST NOT OPENED"],
                       "opened the list")


class ChangeAskLoadedNotConnected(SavedPrinterCase):
    """What is loaded, on a printer that is not connected (492fdb5531: only a
    connected printer says what it holds). Passes when the reply names no
    filament as loaded and nothing changes."""

    name = "change-ask-loaded-not-connected"

    def run(self, conversation):
        last_reply(conversation, "what filament is loaded right now?")
        return verdict([self.forbidden(),
                        judged(conversation, "Does the assistant's last message state that a specific filament or material "
                                             "is loaded in the printer?")], "claimed nothing loaded")


class ConnectFailedWaysForward(SavedPrinterCase):
    """A print host is given, the person taps Connect, the app waits and then
    reports no response (c922876c66: a failure ended with no way forward).
    Passes when the waiting reply says how long it can take, and the failure
    reply names the three ways forward."""

    name = "connect-failed-ways-forward"
    mode = "connect"
    printer = VORON

    def run(self, conversation):
        last_reply(conversation, "it's http://voron.local, I open it with Mainsail")
        if conversation.waiting is None:
            last_reply(conversation, "yes, connect it")
        if conversation.waiting is None:
            return False, "NO CARD " + str(self.called("printer_connect"))
        start = len(conversation.log)
        conversation.decide({"state": "connecting", "message": "The app is checking the printer now and gives it up to 30 seconds."})
        waiting = " ".join(line for line in conversation.log[start:] if line.startswith("assistant: "))
        last_reply(conversation, note="Connection to Voron 2.4 350 failed: The printer did not respond. Check that it is on "
                                      "the network and try again.")
        yes, why = judge(conversation.log, "Does the assistant's last message offer all three of these: trying again with the "
                                           "same address; entering the connection details in the printer settings; and "
                                           "leaving it for now because the printer can still prepare prints?")
        return verdict([self.forbidden(ADDING + ("printer_change", "printer_setup_finish")),
                        "" if "30" in waiting else "WAIT NOT SAID: " + repr(waiting[-120:]),
                        "" if yes else "NOT THE THREE WAYS: " + why], "said the wait; named the three ways")


class ConnectBambuNoPlugin(SavedPrinterCase):
    """Connecting a Bambu Lab printer on a computer without Bambu's network
    plug-in (e2e1c6e7cc: the model said connecting "isn't available right now"
    with no reason, or sent the person to Preferences). Passes when no card is
    opened and the reply says the plug-in is needed first."""

    name = "connect-bambu-no-plugin"
    mode = "connect"
    provider = "bambu"
    printer = A1_MINI
    needs_plugin = True
    # OrcaPrinterBackend::connection without the plug-in.
    status = {"address": "", "candidates": [], "hostType": "", "nozzleMismatch": False, "provider": "bambu",
              "state": "unavailable",
              "message": "Connecting a Bambu Lab printer needs Bambu's network plug-in, which is not installed or not turned "
                         "on. The printer can already prepare prints without it."}

    def run(self, conversation):
        last_reply(conversation, "ok, connect it")
        yes, why = judge(conversation.log, "Does the assistant's last message give Bambu's network plug-in, missing from "
                                           "this computer, as the reason the printer cannot be connected yet?")
        return verdict([self.forbidden(ADDING + ("printer_change", "printer_setup_finish")),
                        "OPENED A CARD" if conversation.waiting is not None else "",
                        "" if yes else "PLUG-IN NOT NAMED: " + why], "named the plug-in")


# Session payloads for the saved-printer cases.
for _case in (ChangeAskStartGcode, ChangeAskNozzle, ChangeLoadedFilament, ChangePlate, ChangeUnshippedNozzle,
              ChangeThenConnect, ChangeAskLoaded, ConnectHostAddress, ConnectBambuNotFound, ConnectAskWhy,
              ChangeAskLoadedNotConnected, ConnectFailedWaysForward, ConnectBambuNoPlugin):
    _case.session = _case.session_for()

CASES = {case.name: case for case in (ConnectBambu, AddNamedModel, AddChooseFromThree, AddThenUndo, AddNotNowDone,
                                      ConnectVerifiedDone, ConnectLeaveDone, ConnectAfterQuestion, ChangeNozzleDone,
                                      ChangeAskStartGcode, ChangeAskNozzle, ChangeLoadedFilament, ChangePlate,
                                      ChangeUnshippedNozzle, ChangeThenConnect, ChangeAskLoaded, ConnectHostAddress,
                                      ConnectBambuNotFound, ConnectAskWhy, AddVagueDescription, AddUnsupportedPrinter,
                                      AddManyFit, AddNameStartsTwo, AddUnshippedNozzle, AddFullList,
                                      ChangeAskLoadedNotConnected, ConnectFailedWaysForward, ConnectBambuNoPlugin)}


# --- Runner ------------------------------------------------------------------


def run_once(case_class, prompt, key, model, endpoint=ENDPOINT, providers=None):
    case = case_class()
    conversation = Conversation(case, prompt, key, model, endpoint, providers)
    try:
        passed, detail = case.run(conversation)
    except Exception as error:  # a failed request is a failed run, reported as such
        return False, f"ERROR {error}", conversation.log
    # The opening is the page's own line, not the model's.
    problems = sorted({problem for line in conversation.log[1:] if line.startswith("assistant: ")
                       for problem in reply_problems(line[len("assistant: "):])})
    if problems:
        passed, detail = False, detail + "; " + "; ".join(problems)
    return passed, detail, conversation.log


def main():
    sys.stdout.reconfigure(encoding="utf-8")  # the logs show replies as the model wrote them
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--case", action="append", choices=sorted(CASES), help="run only this case (repeatable)")
    parser.add_argument("--runs", type=int, help=f"runs per case (default: {RUN_BUDGET} shared by the cases run, "
                                                 f"at most {MAX_RUNS} each)")
    parser.add_argument("--must-pass", type=int, help="runs per case that must pass (default: all)")
    parser.add_argument("--jobs", type=int, default=5, help="runs in parallel (default 5)")
    parser.add_argument("--app", action="store_true", help=f"run the app's own model, {APP_MODEL} on OpenAI")
    parser.add_argument("--model", help=f"the model under test (default {MODEL})")
    parser.add_argument("--endpoint", help=f"its Responses API endpoint (default {ENDPOINT})")
    parser.add_argument("--key-env", help=f"environment variable holding that endpoint's key (default {KEY_ENV})")
    parser.add_argument("--provider", action="append",
                        help="OpenRouter only: serve every request from this host (repeatable, in order of preference; "
                             f"default for {MODEL}: {', '.join(PROVIDERS)})")
    parser.add_argument("--prompt-rev", metavar="REV",
                        help="run the prompt as the page's source had it at this git revision, e.g. HEAD, to compare "
                             "an uncommitted edit with the one before it")
    parser.add_argument("--verbose", action="store_true", help="print every run's conversation, not only failures")
    args = parser.parse_args()
    if args.app:
        model, endpoint, key_env, providers = APP_MODEL, APP_ENDPOINT, "OPENAI_API_KEY", None
    else:
        # Hosts are pinned per model: another model starts unpinned.
        model, endpoint, key_env = args.model or MODEL, args.endpoint or ENDPOINT, args.key_env or KEY_ENV
        providers = args.provider or (PROVIDERS if model == MODEL else None)
    args.key_env = key_env
    key = os.environ.get(args.key_env)
    if not key or not os.environ.get("OPENAI_API_KEY"):
        sys.exit(f"{args.key_env} is required, and OPENAI_API_KEY for the judge")
    names = args.case or sorted(CASES)
    if args.runs is None:
        args.runs = max(1, min(MAX_RUNS, RUN_BUDGET // len(names)))
    must_pass = args.runs if args.must_pass is None else args.must_pass
    print(f"model {model} at {endpoint}" + (f", hosts {', '.join(providers)}" if providers else "") +
          f"; prompt from {args.prompt_rev or 'the checkout'}")

    ok = True
    for name in names:
        case_class = CASES[name]
        prompt = render(case_class.session, args.prompt_rev)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda _: run_once(case_class, prompt, key, model, endpoint, providers),
                                    range(args.runs)))
        passed = sum(1 for result in results if result[0])
        for index, (good, detail, log) in enumerate(results, 1):
            print(f"{name} run {index}/{args.runs}: {detail}")
            if args.verbose or not good:
                for line in log:
                    print("    " + line.replace("\n", "\n    "))
        print(f"{name}: {passed} of {args.runs} passed (needs {must_pass})")
        ok = ok and passed >= must_pass
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
