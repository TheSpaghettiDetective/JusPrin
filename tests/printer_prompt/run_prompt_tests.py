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
The app streams its requests; this script does not. Nothing else differs.

Usage:
  OPENAI_API_KEY=... tests/printer_prompt/run_prompt_tests.py [--runs 10]
      [--must-pass N] [--case NAME] [--jobs 5] [--model gpt-5.4-mini] [--verbose]

Exit code 0 when every case passes at least --must-pass of its runs
(default: all of them), 1 otherwise. Needs python3 and node, and the page's
npm install (src/slic3r/GUI/JusPrin/AgentUI/node_modules) for esbuild.
"""

import argparse
import concurrent.futures
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ENDPOINT = os.environ.get("JUSPRIN_OPENAI_ENDPOINT", "https://api.openai.com/v1/responses")
MODEL = "gpt-5.4-mini"  # OpenAIResponsesConfig's default model
TOOLS = json.loads((HERE / "printer_tools.json").read_text())
MAX_REQUESTS_PER_TURN = 8


def render(session):
    """The page's system prompt and opening line for this session."""
    node = shutil.which("node")
    if node is None:
        sys.exit("node is required to render the printer panel's prompt")
    result = subprocess.run([node, str(HERE / "render_prompt.mjs")], input=json.dumps(session),
                            capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def post(body, key):
    request = urllib.request.Request(ENDPOINT, data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503) or attempt == 4:
                raise RuntimeError(f"OpenAI {error.code}: {error.read().decode(errors='replace')[:500]}")
        except urllib.error.URLError:
            if attempt == 4:
                raise
        time.sleep(2 ** attempt)


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
    if missing or extra:
        return {"code": "invalid_arguments",
                "message": f"Missing {missing}, unexpected {extra}. Nothing was proposed. "
                           "Check the arguments against this tool's parameters and call it again."}
    return None


class Conversation:
    """One run of a case: the chat as the app would hold it."""

    def __init__(self, case, prompt, key, model):
        self.case, self.instructions, self.key, self.model = case, prompt["instructions"], key, model
        self.history = [{"role": "assistant", "content": prompt["opening"]}]  # the page's opening line
        self.log = ["assistant: " + prompt["opening"]]
        self.waiting = None  # a card left up at the end of the last turn

    def say(self, words):
        """One turn: the person's words, then the model until it stops or a card waits."""
        self.log.append("person: " + words)
        # Writing while a card waits cancels it; the replay says so.
        if self.waiting is not None:
            self.history += self.replayed(self.waiting, {"state": "cancelled"})
            self.waiting = None
        turn = self.history + [{"role": "user", "content": [{"type": "input_text", "text": words}]}]
        self.history.append({"role": "user", "content": words})
        rejected = 0
        for _ in range(MAX_REQUESTS_PER_TURN):
            response = post({"model": self.model, "store": False, "parallel_tool_calls": False,
                             "instructions": self.instructions, "tools": TOOLS, "input": turn}, self.key)
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
                self.waiting = call  # a card is up: the app waits for the person
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
            "spools": [{"name": "Teal PLA", "material": "Bambu PLA Basic @BBL A1M", "colour": "#26A69A"}],
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


ADD = json.loads((HERE / "add_session.json").read_text())
CONNECT_OFFER = "Want to connect it so you can send prints straight to it?\nChoices: Connect it | Not now"


class AddCase:
    """Adding a printer, on the Add session the app sent in a recorded run: its
    full printer list, and the tools' recorded answers for the printers the
    case names. A call about any other printer is answered as untested and
    reported."""

    session = ADD["session"]

    def __init__(self):
        self.added = []
        self.lookups = []  # printer_identify calls, by turn
        self.turn = 0
        self.untested = []

    def tool(self, name, arguments):
        if name == "printer_identify":
            self.lookups.append(self.turn)
            ids = arguments.get("catalogIds", [])
            if not all(catalog_id in ADD["printer_identify"] for catalog_id in ids):
                self.untested += ids
                return {"error": {"code": "not_in_this_test", "message": "This test has no answer for " + ", ".join(ids) + "."}}, False
            return {"printers": [ADD["printer_identify"][catalog_id] for catalog_id in ids]}, False
        if name == "printer_add":
            catalog_id = arguments.get("catalogId", "")
            if catalog_id in self.added:
                return {"error": {"code": "already_added", "message": "This conversation already added it."}}, False
            if catalog_id not in ADD["printer_add"]:
                self.untested.append(catalog_id)
                return {"error": {"code": "not_in_this_test", "message": "This test has no answer for " + catalog_id + "."}}, False
            self.added.append(catalog_id)
            return ADD["printer_add"][catalog_id], False
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
        # Space at a line's end is not shown, and the page trims each choice.
        offered = "\n".join(line.rstrip() for line in reply.rstrip().splitlines()).endswith(CONNECT_OFFER)
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


CASES = {case.name: case for case in (ConnectBambu, AddNamedModel, AddChooseFromThree)}


# --- Runner ------------------------------------------------------------------


def run_once(case_class, prompt, key, model):
    case = case_class()
    conversation = Conversation(case, prompt, key, model)
    try:
        passed, detail = case.run(conversation)
    except Exception as error:  # a failed request is a failed run, reported as such
        passed, detail = False, f"ERROR {error}"
    return passed, detail, conversation.log


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--case", action="append", choices=sorted(CASES), help="run only this case (repeatable)")
    parser.add_argument("--runs", type=int, default=10, help="runs per case (default 10)")
    parser.add_argument("--must-pass", type=int, help="runs per case that must pass (default: all)")
    parser.add_argument("--jobs", type=int, default=5, help="runs in parallel (default 5)")
    parser.add_argument("--model", default=MODEL)
    parser.add_argument("--verbose", action="store_true", help="print every run's conversation, not only failures")
    args = parser.parse_args()
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        sys.exit("OPENAI_API_KEY is required")
    must_pass = args.runs if args.must_pass is None else args.must_pass

    ok = True
    for name in args.case or sorted(CASES):
        case_class = CASES[name]
        prompt = render(case_class.session)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda _: run_once(case_class, prompt, key, args.model), range(args.runs)))
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
