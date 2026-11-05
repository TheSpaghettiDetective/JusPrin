#!/usr/bin/env python3
"""Printer panel conversations against the live model, without the app.

The printer panel's assistant is a system prompt (printerInstructions.ts),
five printer tools and the four settings tools, and the chat. This script sends the model the same
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
    (bad arguments) was never recorded, so it is not replayed;
  - several calls per response (parallel_tool_calls), each answered in the
    order the model wrote it, all results in the next request. A call after
    one that did not succeed is answered "not run" without running, as
    OpenAIResponsesAgent::next_call answers it, and is not replayed either.
The app streams its requests; this script does not. Nothing else differs,
except which model answers them.

Usage:
  OPENROUTER_API_KEY=... OPENAI_API_KEY=... tests/printer_prompt/run_prompt_tests.py
      [--runs N] [--must-pass N] [--case NAME] [--jobs 5] [--verbose]
      [--model NAME [--endpoint URL] [--key-env NAME] [--provider HOST]...]
  tests/printer_prompt/run_prompt_tests.py --backend claude-cli --judge claude-cli
      [--claude-model M] [--claude-effort E] [other flags as above]

Prompt changes are evaluated on DeepSeek V4 Flash through OpenRouter, pinned to
the hosts that answer an app note (MODEL, PROVIDERS; decided 2026-09-28), and
on nothing else. --model tries another model through a Responses API endpoint,
with its key in the variable --key-env names, for comparing models, not for
judging a prompt change. The judge stays on OpenAI
(OPENAI_API_KEY) whichever model answers, so counts are judged alike.
--backend claude-cli runs every turn on the locally installed Claude Code CLI,
on the developer's Claude subscription, as the project suite's claude-cli
backend does (tests/project_prompt/claude_cli.py); --judge claude-cli moves
the judge there too. Its counts are compared only with its own baseline.

Without --runs, the cases run share RUN_BUDGET conversations: 5 runs each for
20 cases, fewer when there are more, never more than MAX_RUNS for one case.

Exit code 0 when every case passes at least --must-pass of its runs
(default: all of them), 1 otherwise. Needs python3 and node, and the page's
npm install (src/slic3r/GUI/JusPrin/AgentUI/node_modules) for esbuild.
"""

import argparse
import collections
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "project_prompt"))
import claude_cli  # noqa: E402
# The model prompts are judged on (decided 2026-09-28): DeepSeek V4 Flash on
# OpenRouter, served only by the hosts that answer an app note -- the others
# render the app's developer messages so that the model replies with nothing
# (README, "Another model").
MODEL = "deepseek/deepseek-v4-flash"
ENDPOINT = "https://openrouter.ai/api/v1/responses"
KEY_ENV = "OPENROUTER_API_KEY"
PROVIDERS = ["Alibaba", "Novita", "Baidu", "AtlasCloud"]
# The judge stays on OpenAI whichever model answers, so counts are judged alike.
JUDGE_ENDPOINT = "https://api.openai.com/v1/responses"
JUDGE_MODEL = "gpt-5.5-2026-04-23"  # pinned, so a count moves only when the prompt does
# --backend claude-cli and --judge claude-cli: (model, effort), pinned as the
# project suite's are.
CLAUDE_MODEL, CLAUDE_EFFORT = "claude-opus-5-5", "medium"
CLAUDE = None
CLAUDE_JUDGE = None
# UTF-8 throughout: on Windows, Python otherwise reads text as the system code
# page and garbles "×" and curly quotes before the model sees them.
TOOLS = json.loads((HERE / "printer_tools.json").read_text(encoding="utf-8"))
# The body of the file: what the page sends and the app adds (skillParser.ts).
CONNECTION_SKILLS = {kind: (HERE / f"../../src/slic3r/GUI/JusPrin/AgentUI/src/skills/connect-{kind}/SKILL.md").read_text(
    encoding="utf-8").split("\n---\n", 1)[1].strip() for kind in ("bambu", "print-host")}
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


def web_search(endpoint):
    """The provider's own web search, which the app offers after its tools on
    every reply request (ProviderProfile::web_search_type): OpenRouter's
    spelling there, OpenAI's elsewhere."""
    return {"type": "openrouter:web_search" if "openrouter.ai" in endpoint else "web_search"}


def post(body, key, endpoint=ENDPOINT):
    if "tools" in body:
        body = {**body, "tools": body["tools"] + [web_search(endpoint)]}
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


# The run's token use, by who spent it, for the line main prints at the end.
USAGE = {"model": collections.Counter(), "judge": collections.Counter()}
JUDGE_CALLS = collections.Counter()
USAGE_LOCK = threading.Lock()


def spent(who, usage):
    """Adds a response's token counts (the numbers in its usage) to the run's."""
    with USAGE_LOCK:
        USAGE[who].update({name: value for name, value in (usage or {}).items() if isinstance(value, int)})


def judge(log, question):
    """A yes or no about the conversation's last reply, from a stronger model,
    for what no string test can check. Returns (yes, why)."""
    schema = {"type": "object", "additionalProperties": False, "required": ["answer", "why"],
              "properties": {"answer": {"type": "string", "enum": ["yes", "no"]}, "why": {"type": "string"}}}
    instructions = ("You read a chat between a 3D printing app's assistant and a person, and answer one question "
                    "about it strictly as written, judging only what the messages say. A \"web search:\" line is a "
                    "search the assistant ran, with the pages it found.")
    prompt = "\n".join(log) + "\n\nQuestion: " + question
    with USAGE_LOCK:
        JUDGE_CALLS["calls"] += 1
    if CLAUDE_JUDGE:
        verdict, usage = claude_cli.judge(instructions, prompt, schema, *CLAUDE_JUDGE)
        spent("judge", usage)
        return verdict["answer"] == "yes", verdict["why"]
    response = post({"model": JUDGE_MODEL, "store": False, "instructions": instructions, "input": prompt,
                     "text": {"format": {"type": "json_schema", "name": "verdict", "strict": True, "schema": schema}}},
                    os.environ["OPENAI_API_KEY"], JUDGE_ENDPOINT)
    spent("judge", response.get("usage"))
    text = "".join(output_text(item) for item in response.get("output", []) if item.get("type") == "message")
    verdict = json.loads(text)
    return verdict["answer"] == "yes", verdict["why"]


# What every reply of every case must hold to, whatever the case checks.
def argument_names(schema):
    """Every property name in a tool's parameters, nested ones included."""
    if isinstance(schema, list):
        return {name for item in schema for name in argument_names(item)}
    if not isinstance(schema, dict):
        return set()
    names = set(schema.get("properties", {})) if isinstance(schema.get("properties"), dict) else set()
    return names | {name for value in schema.values() for name in argument_names(value)}


# A leak of the model's own machinery: one of its tools by its exact name, a
# camelCase field from their arguments (catalogId, deviceId), or a JSON object
# pasted from a tool result. Exact names, so Klipper's printer_data folder or
# a plain word such as "address" passes; no reply has reason to hold these.
LEAKED_NAMES = {tool["name"] for tool in TOOLS} | {name for name in argument_names(TOOLS) if re.search(r"[a-z][A-Z]", name)}
LEAK = re.compile(r"\b(?:" + "|".join(sorted(map(re.escape, LEAKED_NAMES), key=len, reverse=True)) + r")\b")
# A JSON object in a reply, by its first key. It is a leak only when a tool
# result of the same chat has that key: Moonraker's own page shows an API key
# as {"result": ...}, and a reply may quote it (DeepSeek, 2026-09-28).
JSON_OBJECT = re.compile(r'\{\s*"([^"\n]+)"\s*:')


def result_keys(log):
    """Every key in the tool results of a conversation's log."""
    def keys(value):
        if isinstance(value, dict):
            return set(value) | {key for item in value.values() for key in keys(item)}
        if isinstance(value, list):
            return {key for item in value for key in keys(item)}
        return set()
    return {key for line in log if line.startswith("  -> ") for key in keys(json.loads(line[len("  -> "):]))}
# The prompt's other internal words (profile, preset, catalog) are not
# checked: a person may say them, and grading each chat for them would add
# about 30% to a run's cost (Sonnet 5.5), so they were dropped (decided 2026-11-04).


# A list of sources: a "Sources:" heading, lines of markdown links, or both on one line.
HEADING = r"\s*(?:#+\s*)?(?:\*\*)?Sources:?(?:\*\*)?:?\s*"
SOURCES_HEADING = re.compile(HEADING, re.IGNORECASE)
SOURCE_LINE = re.compile(r"\s*(?:[-*]\s+|\d+\.\s+)?(?:" + HEADING + r")?(?:\[[^\]]*\]\([^)\s]+\)[\s,;·|]*)+",
                         re.IGNORECASE)


def without_sources(reply):
    """The reply without a list of sources at its end: a "Sources:" heading
    and lines of markdown links, after a search on --backend claude-cli. The
    CLI's search result ends by demanding such a list, the app's provider
    search does not, and the app asks for none, so a list after the Choices
    line is the test backend's, not the prompt's."""
    lines = reply.rstrip().splitlines()
    while lines and (not lines[-1].strip() or SOURCE_LINE.fullmatch(lines[-1]) or SOURCES_HEADING.fullmatch(lines[-1])):
        lines.pop()
    return "\n".join(lines)


def reply_problems(reply, tool_result_keys=frozenset()):
    """How a reply breaks the prompt's rules for every reply: a "Choices:" line
    the page would show as text or draw wrongly (replyChoices.ts), or a leak
    of a tool's name, field or result."""
    problems = []
    lines = without_sources(reply).splitlines()
    found = re.search(r"(^|\s)Choices:(.*)$", lines[-1]) if lines else None
    if found is None and "Choices:" in reply:
        problems.append("CHOICES NOT ON THE LAST LINE")
    if found is not None:
        choices = [choice.strip() for choice in found.group(2).split("|") if choice.strip()]
        if not 2 <= len(choices) <= 4:
            problems.append(f"{len(choices)} CHOICES")
        if any(len(choice) >= 30 for choice in choices):
            problems.append("LONG CHOICE")
    if LEAK.search(reply):
        problems.append("LEAK " + repr(LEAK.search(reply).group(0)))
    pasted = [found.group(0) for found in JSON_OBJECT.finditer(reply) if found.group(1) in tool_result_keys]
    if pasted:
        problems.append("LEAK " + repr(pasted[0]))
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
        return {"code": "unknown_tool", "message": f'There is no tool named "{tool}" here. Nothing was queued. Use one of the listed tools.'}
    missing = [name for name in schema.get("required", []) if name not in arguments]
    extra = [name for name in arguments if name not in schema.get("properties", {})]
    # ToolRegistry's valid_arguments for the connection tools: a non-empty
    # address with a known hostType, and a non-empty deviceId.
    if tool == "printer_host_connect" and not extra and not (
            arguments.get("address") and arguments.get("hostType") in ("moonraker", "octoprint")):
        return {"code": "invalid_arguments",
                "message": "The tool arguments do not match the registered contract. Give hostType and the print host's "
                           "address. Without an address, ask the person for it, and call this once there is one."}
    if tool == "printer_bambu_connect" and not extra and not arguments.get("deviceId"):
        return {"code": "invalid_arguments",
                "message": "The tool arguments do not match the registered contract."}
    if missing or extra:
        return {"code": "invalid_arguments",
                "message": f"Missing {missing}, unexpected {extra}. Nothing was queued. "
                           "Check the arguments against this tool's parameters and call it again."}
    # ToolRegistry's valid_settings_scope and valid_persist_as: each scope
    # takes exactly its own target, as an object.
    if tool.startswith("settings_"):
        scope, target = arguments.get("scope"), arguments.get("target")
        shapes = {"process": target is None,
                  "object": isinstance(target, dict) and set(target) == {"objectId"} and isinstance(target["objectId"], str),
                  "filament": isinstance(target, dict) and set(target) == {"preset"} and isinstance(target["preset"], str) and target["preset"],
                  "printer": isinstance(target, dict) and set(target) == {"preset"} and isinstance(target["preset"], str) and target["preset"]}
        persist = arguments.get("persistAs")
        if not shapes.get(scope, False) or (persist is not None and (scope == "object" or not isinstance(persist, str) or not persist)):
            message = ("The tool arguments do not match the registered contract. scope process takes no target; object "
                       "takes target.objectId; filament and printer take target.preset, the preset's name. persistAs is "
                       "not for object.")
            if isinstance(target, str):
                message += ' target must be a JSON object, not a string holding one: "target": {"preset": "<name>"}.'
            return {"code": "invalid_arguments", "message": message}
    return None


def succeeded(result):
    """Whether the app would count this answer as a call that succeeded. The
    printer panel words a failed call as {"error": ...} with no state
    (PrinterConversation::tool_output); a refusal before running says
    "state": "failed"."""
    return "error" not in result and result.get("state") != "cancelled"


def not_run(failed):
    """The result the app gives a call after one in the same response did not
    succeed (ProviderAgent::next_call), word for word. failed is that
    call as "tool (call_id)"."""
    return {"state": "failed", "error": {"code": "not_run", "message":
            f"Not run: an earlier call in the same response, {failed}, did not succeed. "
            "Nothing was queued. Call it again if it is still needed."}}


class ApiTurn:
    """One turn on a Responses API endpoint: every request carries the turn
    so far, the results and a skill after them included."""

    def __init__(self, conversation, turn):
        self.conversation, self.turn = conversation, turn

    def next(self):
        conversation = self.conversation
        response = post({"model": conversation.model, "store": False, "parallel_tool_calls": True,
                         "instructions": conversation.instructions, "tools": getattr(conversation.case, "tools", TOOLS),
                         "input": self.turn, **conversation.routing},
                        conversation.key, conversation.endpoint)
        spent("model", response.get("usage"))
        return response.get("output", [])

    def answer(self, calls, results, skill):
        pass  # already in the turn the next request sends

    def __enter__(self):
        return self

    def __exit__(self, *_):
        pass


class ClaudeTurn(ApiTurn):
    """One turn on the Claude Code CLI (claude_cli.Run): the turn so far goes
    in once, and the results of each response's calls are its tool results.
    A skill the app adds after them goes in as further text of the last
    result, as the project suite gives the app's notes after a result."""

    def __init__(self, conversation, turn):
        super().__init__(conversation, turn)
        self.run = claude_cli.Run(conversation.instructions, getattr(conversation.case, "tools", TOOLS), list(turn),
                                  *CLAUDE)

    def next(self):
        items, usage = self.run.next_response()
        spent("model", usage)
        return items

    def answer(self, calls, results, skill):
        if skill:
            results = results[:-1] + [[results[-1], claude_cli.developer_block(skill)["text"]]]
        self.run.answer(calls, results)

    def __exit__(self, *_):
        self.run.close()


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
        self.skills_sent = set()

    def say(self, words):
        """One turn: the person's words, then the model until it stops."""
        self.log.append("person: " + words)
        turn = self.history + [{"role": "user", "content": [{"type": "input_text", "text": words}]}]
        self.history.append({"role": "user", "content": words})
        session = getattr(self.case, "session", {})
        printer = session.get("context", {}).get("printer") or {}
        if session.get("mode") in ("change", "connect") and printer and not printer.get("stock"):
            self.add_connection_skill(turn, printer.get("provider") == "bambu" or str(printer.get("model", "")).startswith("Bambu"))
        self.respond(turn)

    def add_connection_skill(self, turn, bambu):
        """PrinterConversation::connection_skill: once the chat has a saved
        printer -- the one a change or connect chat is about, or one
        printer_add has just added -- the skill for its vendor follows the
        message or the results of that moment, once, and stays. A Bambu Lab
        printer gets the Bambu skill; any other the print host one."""
        kind = "bambu" if bambu else "print-host"
        if kind in self.skills_sent:
            return None
        self.skills_sent.add(kind)
        self.log.append(f"app: (the connect-{kind} skill)")
        message = {"role": "developer", "content": CONNECTION_SKILLS[kind]}
        turn.append(message)
        self.history.append(message)
        return message["content"]

    def note(self, text):
        """The app's own line, as the developer, then the turn it starts
        (AgentHost::start_turn): the person has said nothing."""
        self.log.append("app: " + text)
        self.history.append({"role": "developer", "content": text})
        self.respond(list(self.history))

    def respond(self, turn):
        """The model until it stops. Every call of a response is answered, in
        the order the model wrote them, and all the results go back in one
        request (OpenAIResponsesAgent::next_call). Once a call does not
        succeed, the calls after it in that response are answered not_run
        without running."""
        refused = 0
        with (ClaudeTurn if CLAUDE else ApiTurn)(self, turn) as model:
            for _ in range(MAX_REQUESTS_PER_TURN):
                output = model.next()
                turn += output
                for item in output:
                    if searched := claude_cli.web_search_line(item):
                        # Kept, as the app sends a response's items back.
                        self.history.append(item)
                        self.log.append(searched)
                    elif item.get("type") == "message" and (reply := output_text(item)):
                        self.history.append({"role": "assistant", "content": reply})
                        self.log.append("assistant: " + reply)
                calls = [item for item in output if item.get("type") == "function_call"]
                if not calls:
                    break
                failed = None  # "tool (call_id)" of the first call that did not succeed
                added = None  # the catalogue id a printer_add of this response added
                results = []
                for call in calls:
                    arguments = json.loads(call.get("arguments") or "{}")
                    self.log.append(f"tool: {call['name']} {json.dumps(arguments)}")
                    if failed:
                        result, recorded = not_run(failed), False
                    elif (problem := invalid_arguments(call["name"], arguments)) and refused < 2:
                        refused += 1
                        result, recorded = {"state": "failed", "error": problem}, False
                    else:
                        (result, _), recorded = self.case.tool(call["name"], arguments), True
                        if call["name"] == "printer_add" and succeeded(result):
                            added = str(arguments.get("catalogId", ""))  # the vendor first
                    if not failed and not succeeded(result):
                        failed = f"{call['name']} ({call['call_id']})"
                    self.log.append("  -> " + dump(result))
                    results.append(dump(result))
                    turn.append({"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)})
                    if recorded:
                        self.history += self.replayed(call, result)
                skill = self.add_connection_skill(turn, added.startswith("BBL/")) if added is not None else None
                model.answer(calls, results, skill)

    @staticmethod
    def replayed(call, result):
        return [{"type": "function_call", "call_id": call["call_id"], "name": call["name"], "arguments": call["arguments"]},
                {"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)}]


# --- Cases -------------------------------------------------------------------


class ConnectBambu:
    """Connect a saved Bambu Lab A1 mini that is on the network in LAN mode.

    Passes when the model opens printer_bambu_connect's form for that
    printer. The call waits on the form; `outcome` is what it returns once
    the person has answered it, as PrinterConversation::end_connect_call
    words it.
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
              "seen": [], "search": {"state": "listening", "secondsAgo": 3},
              "hostType": "", "message": "", "nozzleMismatch": False, "provider": "bambu", "state": "not_configured"}
    outcome = {"state": "verified",
               "message": "The app connected to Bambu Lab A1 mini. The app sent the access code typed in the form."}

    def __init__(self):
        self.card = False
        self.refused = []
        self.other = []

    def tool(self, name, arguments):
        if name == "printer_connection_status":
            return self.status, False
        if name == "printer_host_connect":
            return {"error": {"code": "unknown_device",
                              "message": "This is a Bambu Lab printer, connected with printer_bambu_connect. Pass the "
                                         "deviceId of one of these printers in LAN mode: FAKE001 (JusPrin Fake A1 mini)."}}, False
        if name == "printer_bambu_connect":
            # The app accepts a listed printer's id or its name (preflight_tool).
            device = arguments.get("deviceId", "")
            if device in ("FAKE001", "JusPrin Fake A1 mini"):
                self.card = True
                return self.outcome, False
            self.refused.append(device)
            return {"error": {"code": "unknown_device",
                              "message": "Pass the deviceId of one of these printers in LAN mode: FAKE001 (JusPrin Fake A1 mini)."}}, False
        self.other.append(name)
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
        if name == "printer_change" and not self.added:
            # PrinterConversation::preflight_tool, before anything is added.
            return {"error": {"code": "no_printer",
                              "message": "No printer is set up in this conversation yet: add one first."}}, False
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
        offered = "\n".join(line.rstrip() for line in without_sources(reply).splitlines() if line.strip()).endswith(CONNECT_OFFER)
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
    lines = without_sources(reply).splitlines()
    found = re.search(r"(^|\s)Choices:(.*)$", lines[-1]) if lines else None
    if found is None:
        return False
    said = "\n".join(lines[:-1]) + lines[-1][:found.start()]
    return [choice.strip() for choice in found.group(2).split("|") if choice.strip()] == ["Done"] and said.strip() != ""


def says_can_close(reply):
    return re.search(r"\b(?:can|may|safe to|okay to|ok to|feel free to)\s+close\s+(?:this\s+|the\s+|your\s+)?"
                     r"(?:chat|conversation)\b", reply, re.IGNORECASE) is not None


class Finishing:
    """When nothing is left to decide, tell the person they can close the chat."""

    def end(self, reply, detail):
        close = says_can_close(reply)
        choices = "Choices:" in reply
        detail += "; says to close" if close else "; NO CLOSE INSTRUCTION: " + repr(reply[-120:])
        if choices:
            detail += "; CHOICES OFFERED"
        return close and not choices, detail


class AddNotNowClose(Finishing, AddCase):
    """The person adds a printer and turns down connecting it. Passes when the
    reply to "Not now" says they can close the chat."""

    name = "add-not-now-close"

    def run(self, conversation):
        self.say(conversation, "bambu lab a1 mini")
        reply = self.say(conversation, "Not now")
        detail = "added" if self.added == ["BBL/Bambu Lab A1 mini"] else f"added {self.added or 'nothing'}"
        return self.end(reply, detail)


class ConnectOutcome(Finishing, ConnectBambu):
    """Connect the saved A1 mini, as connect-bambu does: the person types the
    code in the form, and the call returns the attempt's outcome."""

    CODE_SENT = " The app sent the access code typed in the form."

    def connect(self, conversation):
        """The model's reply to the call's outcome, or None when no form came."""
        start = len(conversation.log)
        ConnectBambu.run(self, conversation)
        if not self.card:
            return None
        replies = [line for line in conversation.log[start:] if line.startswith("assistant: ")]
        return replies[-1][len("assistant: "):] if replies else ""


def ended_badly(conversation, truth):
    """What is wrong with where the chat left the person, or "": Kenneth's test
    for a tool result, a person told something false or useless with the right
    step hidden, asked of the whole chat. The truth is what the scenario knows
    and the assistant cannot, so a wrong guess marked as a guess still passes
    when its next step would lead them there. The prompt has the model search
    before it says where a menu or setting is and say what came from the
    web (LOOK_IT_UP), so what a search shown in the chat found counts as
    given. Only a specific path on the printer or in another program fails:
    the judge reads the chat alone, not the skill, so a general fact the skill
    gives ("the code is on the printer's screen") or this app's own menus
    read as unsourced to it (Kenneth, 2026-11-04)."""
    yes, why = judge(conversation.log, "The truth in this scenario, which the assistant cannot know for certain: " + truth +
                                       " Reading the whole chat as that person, are all of these true? Every cause the "
                                       "assistant gave is stated in the app's or the person's messages, or said to come "
                                       "from a web search the chat shows, or marked as a guess. It did not send them back "
                                       "to something they had already done or ruled out, unless something had changed. "
                                       "It gave no specific menu or screen path on the printer or in another program, "
                                       "such as \"Settings → WLAN → LAN Only Mode\", as fact, unless the messages or a web "
                                       "search the chat shows gave it, or it marked the path as a guess. A general "
                                       "statement, such as that the code is on the printer's screen, is fine. Paths in "
                                       "this app itself are given to the assistant and count as sourced. Its last message leaves "
                                       "them with a concrete next thing to look at, try or answer that moves them toward "
                                       "the truth.")
    return "" if yes else "LEFT STUCK: " + why


class ConnectWorkedOut(ConnectOutcome):
    """A failure the person cannot get past by following a line in the skill.
    Passes by where the chat leaves the person (ended_badly)."""

    truth = ""
    failure = ""
    person = ()

    @property
    def outcome(self):
        return {"state": "failed", "message": self.failure + self.CODE_SENT}

    def run(self, conversation):
        if self.connect(conversation) is None:
            return False, "NO CARD"
        for words in self.person:
            last_reply(conversation, words)
        return verdict([ended_badly(conversation, self.truth)], "worked it out with them")


class ConnectRightCodeSilentPort(ConnectWorkedOut):
    """Kenneth's P1S, 2026-11-02: the right code, and a network too slow to
    reach the port. The model blamed the code."""

    name = "connect-right-code-silent-port"
    truth = "The access code is right. The Wi-Fi between this computer and the printer is badly overloaded."
    failure = ("Bambu's network plug-in reported code -1 24 seconds after being asked to connect, which does not say why. "
               "The app also tried the printer's port 8883 at 192.168.0.198. Just before the attempt, nothing answered within "
               "5 seconds. Right after it failed, nothing answered within 5 seconds.")
    person = ("I compared them, it's the same code. What else could it be?",)


class ConnectPortWentQuiet(ConnectWorkedOut):
    """The real P1S on 2026-11-02: the port answered, a failed attempt, then
    nothing for minutes. Trying again at once says nothing about the code."""

    name = "connect-port-went-quiet"
    truth = "The printer stopped answering after the failed attempt, and will answer again in several minutes or after a restart."
    failure = ("Bambu's network plug-in reported code -1 24 seconds after being asked to connect, which does not say why. "
               "The app also tried the printer's port 8883 at 192.168.0.198. Just before the attempt, it answered in 73 ms. "
               "Right after it failed, nothing answered within 5 seconds.")
    person = ("should I just try again right now?",)


class ConnectCodeRuledOut(ConnectWorkedOut):
    """The port answers, so the code is the first guess, and the person then
    rules it out. Not seen on a real printer: the skill was not written from it."""

    name = "connect-code-ruled-out"
    truth = "The access code is right. The printer has been running for weeks and needs a restart."
    failure = ("Bambu's network plug-in reported code -1 24 seconds after being asked to connect, which does not say why. "
               "The app also tried the printer's port 8883 at 192.168.0.198. Just before the attempt, it answered in 14 ms. "
               "Right after it failed, it answered in 16 ms.")
    person = ("I checked twice, it's the same code.",)


class ConnectUnforeseenFailure(ConnectWorkedOut):
    """A code and an answer the skill never mentions, and a cause the person
    brings up. Passes only by reasoning: no line in the skill covers it."""

    name = "connect-unforeseen-failure"
    truth = "This computer is on a work VPN, which blocks the local network."
    failure = ("Bambu's network plug-in reported code -3 2 seconds after being asked to connect, which does not say why. The "
               "app also tried the printer's port 8883 at 192.168.0.198. Just before the attempt, the app could not try, as "
               "this computer said \"No route to host\". Right after it failed, the app could not try, as this computer said "
               "\"No route to host\".")
    person = ("hm, I'm on my work VPN today, does that matter?",)


class ConnectScreenSaysDisconnected(ConnectOutcome):
    """Nothing is heard, and the person reads the printer's screen. Passes when
    the model takes what they see as the likely cause."""

    name = "connect-screen-says-disconnected"
    # PrinterConversation::search_json once the call has listened 30 seconds.
    status = {**ConnectBambu.status, "candidates": [],
              "search": {"state": "listening", "secondsAgo": 30,
                         "message": "Nothing heard in 30 seconds. The search is still listening, and the app posts a "
                                    "note if a printer of this model is heard."}}

    def run(self, conversation):
        conversation.say("Yes, it is")
        last_reply(conversation, "the printer's screen says Wi-Fi disconnected")
        problem = ended_badly(conversation, "The printer lost its Wi-Fi settings and has to be joined to the network again.")
        return verdict(["OPENED THE FORM" if self.card else "", problem], "took what they see as the cause")


class ConnectVerifiedClose(ConnectOutcome):
    """Passes when the reply to a verified connection says they can close the chat."""

    name = "connect-verified-close"

    def run(self, conversation):
        reply = self.connect(conversation)
        if reply is None:
            return False, "NO CARD"
        return self.end(reply, "verified")


class ConnectLeaveClose(ConnectOutcome):
    """The connection fails and the person leaves it for now. Passes when the
    reply to that says they can close the chat."""

    name = "connect-leave-close"
    outcome = {"state": "failed",
               "message": "The printer did not respond. Check that it is on the network and try again." + ConnectOutcome.CODE_SENT}

    def run(self, conversation):
        reply = self.connect(conversation)
        if reply is None:
            return False, "NO CARD"
        reply = last_reply(conversation, "Leave it for now")
        return self.end(reply, "failed, left")


class ChangeNozzleClose(Finishing):
    """The person says the nozzle changed. Passes when it is changed to that
    size alone, and the reply says they can close the chat."""

    name = "change-nozzle-close"
    printer = {"name": "Bambu Lab A1 mini", "model": "Bambu Lab A1 mini", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
               "spools": [], "connected": False}
    session = {"mode": "change", "printerName": printer["name"], "blocks": [],
               "context": {"printer": {**printer, "provider": "bambu"}}}

    def __init__(self):
        self.changes = []
        self.other = []

    def tool(self, name, arguments):
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
        passed, detail = self.end(reply, detail)
        return passed and changed, detail


# --- Cases across the three sessions -----------------------------------------
#
# The cases above follow the main paths. These are the rest of what people
# say in each session: questions the tools cannot answer, facts that need
# nothing saved, and requests that belong to another session. Each checks
# both what should happen and what must not, so a prompt edit made for one
# session is measured against the others.

# What the model must never do while talking about a printer it already has.
ADDING = ("printer_identify", "printer_add")
# The two connection tools: a print host's test, and a Bambu Lab printer's form.
CONNECT = ("printer_host_connect", "printer_bambu_connect")
MUTATIONS = ADDING + ("printer_change",) + CONNECT + ("settings_apply_patch",)
SETTINGS = ("settings_search", "settings_get", "settings_preview_patch", "settings_apply_patch")

# A printer's settings as the settings tools read them: value, writable, type,
# label. Every Change and Connect case's printer has these.
PRINTER_SETTINGS = {
    "machine_start_gcode": ("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle", True, "string", "Machine start G-code"),
    "machine_end_gcode": ("M104 S0\nM140 S0\nG28 X0", True, "string", "Machine end G-code"),
    "retraction_length": ("0.8", True, "float[]", "Length"),
    "z_hop": ("0.4", True, "float[]", "Z hop height"),
    "nozzle_diameter": ("0.4", False, "float[]", "Nozzle diameter"),
    "printable_area": ("0x0,220x0,220x220,0x220", False, "point[]", "Printable area"),
}

KOBRA = {"name": "Anycubic Kobra 3", "model": "Anycubic Kobra 3", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
         "spools": [], "connected": False}
A1_MINI = {"name": "Bambu Lab A1 mini", "model": "Bambu Lab A1 mini", "nozzle": 0.4, "nozzles": [0.2, 0.4, 0.6, 0.8],
           "spools": [], "connected": False}
VORON = {"name": "Voron 2.4 350", "model": "Voron 2.4 350", "nozzle": 0.4, "nozzles": [0.4, 0.6], "spools": [],
         "connected": False}
# printer_connection_status for a print host with no address saved, as
# OrcaPrinterBackend reports it; Orca's default host type is OctoPrint.
HOST_NOT_CONFIGURED = {"address": "", "candidates": [], "seen": [], "search": {"state": "not_applicable"},
                       "hostType": "octoprint", "message": "", "nozzleMismatch": False,
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

    stock = False  # the settings OrcaSlicer ships for the model, selected in the project
    selected = False  # the printer the project uses, so an unsaved change can live in its edited copy

    def tool(self, name, arguments):
        self.calls.append((name, arguments))
        if name in SETTINGS:
            return self.settings(name, arguments)
        # PrinterConversation::preflight_tool: a stock profile holds no nozzle
        # or connection of the person's own.
        if self.stock and name in ("printer_change", "printer_connection_status") + CONNECT:
            return {"error": {"code": "stock_profile",
                              "message": f'"{self.printer["name"]}" is the settings JusPrin comes with for this model, not a '
                                         "printer the person has added, so its nozzle and connection are not saved. Its other "
                                         "settings are changed by saving a copy, which is then theirs (settings_apply_patch with "
                                         "persistAs). To keep a nozzle or a connection, they add the printer with + Add printer "
                                         "on Home."}}, False
        # PrinterConversation::preflight_tool: finding and adding belong to an
        # Add session.
        if name in ADDING:
            return {"error": {"code": "not_in_this_conversation",
                              "message": f"This conversation is about {self.printer['name']}, which is already set up; "
                                         "printers are not found or added here. Tell the person that another printer is "
                                         "added with + Add printer on Home."}}, False
        if name == "printer_change":
            return self.change(arguments), False
        if name == "printer_connection_status":
            return self.status, False
        if name in CONNECT:
            return self.connect(name, arguments)
        return {"error": {"code": "not_in_this_test", "message": "This test does not answer " + name + "."}}, False

    def settings(self, name, arguments):
        """PrinterConversation::preflight_tool for the settings tools, then the
        adapter's answer for this printer (OrcaWorkspaceAdapter)."""
        own = self.printer["name"]
        # After two refusals the runner hands a malformed call through; the
        # registry refuses it every time.
        if problem := invalid_arguments(name, arguments):
            return {"error": problem}, False
        if arguments.get("scope") != "printer":
            return {"error": {"code": "not_in_this_conversation",
                              "message": f'Only this printer\'s settings are changed here: pass scope printer and target.preset "{own}".'}}, False
        if (arguments.get("target") or {}).get("preset") != own:
            return {"error": {"code": "other_printer", "message": f'This conversation can change only "{own}". Another saved printer '
                                                                  "is changed from Printer settings… in its menu on Home."}}, False
        context = {"scope": "printer", "presetName": own, "sessionId": "1", "revision": 7, "truncated": False}
        if name == "settings_search":
            words = [w for w in re.split(r"[\s,;]+", str(arguments.get("query", "")).lower()) if w]
            items = [{"key": key, "type": kind, "label": label, "category": "Machine", "description": label, "unit": "",
                      "enumValues": [], "enumLabels": [], "writable": writable, "truncated": False}
                     for key, (_, writable, kind, label) in PRINTER_SETTINGS.items()
                     if not words or any(w in key or w in label.lower() for w in words)]
            return {**context, "items": items, "nextCursor": ""}, False
        if name == "settings_get":
            items, unknown = [], []
            for key in arguments.get("keys", []):
                if key in PRINTER_SETTINGS:
                    value, writable, kind, label = PRINTER_SETTINGS[key]
                    items.append({"key": key, "value": value, "type": kind, "label": label, "unit": "",
                                  "differsFromPreset": False, "differsFromSystem": False, "writable": writable})
                else:
                    unknown.append({"key": key, "code": "unknown_setting", "message": "Unknown setting.", "allowed": [],
                                    "suggestions": [], "truncated": False})
            return {**context, "items": items, "unknownKeys": unknown}, False
        issues, changes = [], []
        for key, value in (arguments.get("changes") or {}).items():
            if key not in PRINTER_SETTINGS:
                issues.append({"key": key, "code": "unknown_setting", "message": "Unknown setting."})
            elif not PRINTER_SETTINGS[key][1]:
                issues.append({"key": key, "code": "unsupported_setting_mutation", "message": "The tools do not change this printer setting; the person changes it in the app."})
            else:
                changes.append({"key": key, "before": PRINTER_SETTINGS[key][0], "after": str(value)})
        persist = arguments.get("persistAs")
        if not issues:
            if persist is None and not self.selected:
                issues.append({"key": "", "code": "not_selected", "message": f'"{own}" is not the printer preset being edited, '
                                                                             "so a change to it is saved: pass persistAs."})
            elif persist == own and self.stock:
                issues.append({"key": "", "code": "read_only_preset", "suggestions": [own + " - Copy"],
                               "message": f'"{own}" comes with the app and cannot be overwritten. Save the change as a copy: '
                                          f'pass persistAs "{own} - Copy".'})
            elif persist is not None and persist != own and not self.selected:
                issues.append({"key": "", "code": "not_selected",
                               "message": f'A copy is saved only of the printer in use; save "{own}" under its own name.'})
        if name == "settings_preview_patch":
            return {**context, "valid": not issues, "changes": changes if not issues else [], "dependencies": [],
                    "issues": issues, "warnings": []}, False
        if issues:
            return {"error": {"code": issues[0]["code"], "message": issues[0]["message"]}}, False
        return {**context, "applied": True, "changes": changes, "normalized": [],
                "savedAs": persist or "", "presetDirty": persist is None,
                "projectUndo": arguments.get("scope") == "object", "truncated": False}, False

    def applied(self):
        """The settings changes the model asked the app to apply."""
        return [arguments for _, arguments in self.called("settings_apply_patch")]

    def change(self, arguments):
        """PrinterConversation::preflight_tool, then ::change. The case's
        printer is the only one saved."""
        name, nozzle = arguments.get("printerName", ""), arguments.get("nozzle")
        if name != self.printer["name"]:
            return {"error": {"code": "unknown_printer",
                              "message": f'This conversation can change only "{self.printer["name"]}". Another saved '
                                         "printer is changed from Printer settings… in its menu on Home."}}
        if not isinstance(nozzle, (int, float)) or nozzle <= 0:
            return {"error": {"code": "nothing_to_change", "message": "Name the nozzle size that changed."}}
        if nozzle not in self.printer["nozzles"]:
            return unknown_nozzle(self.printer["nozzles"], self.printer["model"], nozzle)
        if nozzle == self.printer["nozzle"]:
            return {"error": {"code": "nothing_to_change", "message": f"That is already how {name} is set up."}}
        before, self.printer["nozzle"] = self.printer["nozzle"], nozzle
        return {"changed": [{"field": "nozzle", "before": before, "after": nozzle}], "printer": dict(self.printer)}

    # What printer_host_connect returns for this case's host, once its test
    # and any key form are done: by default a Moonraker that wants a key, as
    # the one in Docker answered, and the right key typed in the form.
    host_answer = ("verified", "Moonraker at {address} refused the test: HTTP 401: Unauthorized The test sent no API key. "
                               "The person typed a key in the form, and the app tested again: Moonraker at {address} "
                               "accepted the test, and this printer now connects there. The test sent the API key typed in "
                               "the form. Moonraker reports Klipper's state as ready.")

    def connect(self, name, arguments):
        """PrinterConversation::preflight_tool, then the host's test or the access code form."""
        if self.needs_plugin:
            return {"error": {"code": "connection_unavailable", "message": self.status["message"]}}, False
        if self.provider == "host":
            if name == "printer_bambu_connect":
                return {"error": {"code": "address_needed",
                                  "message": "This printer connects through Moonraker or OctoPrint, not as a Bambu Lab device: "
                                             "call printer_host_connect with hostType and the print host's address."}}, False
            state, message = self.host_answer
            return {"state": state, "message": message.format(address=arguments.get("address", ""))}, False
        if name == "printer_host_connect":
            return {"error": {"code": "unknown_device",
                              "message": "This is a Bambu Lab printer, connected with printer_bambu_connect."}}, False
        candidates = self.status.get("candidates", [])
        listed = [c for c in candidates if arguments.get("deviceId") in (c["deviceId"], c["name"])]
        if not listed:
            # PrinterConversation::unknown_device_text, for a status with nothing in cloud mode.
            lan = ", ".join(f'{c["deviceId"]} ({c["name"]})' for c in candidates)
            message = ("Pass the deviceId of one of these printers in LAN mode: " + lan + "." if lan else
                       "The network search has listened for 20 seconds and has heard no printer of this model in LAN "
                       "mode. It goes on, and the app posts a note if one is heard.")
            return {"error": {"code": "unknown_device", "message": message}}, False
        return ConnectBambu.outcome, False

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
    """The person asks about the start g-code (seen 2026-09-28, before the
    settings tools were offered here: the reply offered to add the printer
    first). Passes when it is read with the settings tools and quoted, nothing
    is added or changed, and the reply neither treats the printer as not yet
    added, nor says the setting needs a network connection (it is on this
    computer), nor asks which printer this is (seen the same day, a Bambu Lab
    A1 asked about z offset: "Which printer is it?")."""

    name = "change-ask-start-gcode"

    def run(self, conversation):
        reply = last_reply(conversation, "what's the current start g-code?")
        read = [a for _, a in self.called("settings_get") if "machine_start_gcode" in a.get("keys", [])]
        return verdict([self.forbidden(),
                        "" if read else f"NOT READ {self.called(*SETTINGS)}",
                        "" if "G28" in reply else "DID NOT QUOTE IT",
                        judged(conversation, "Does the assistant's last message say or imply that this printer has not been "
                                             "added to the app yet, for example by offering to add it?"),
                        judged(conversation, "Does the assistant's last message say or imply that the printer has to be "
                                             "connected over the network before its start g-code can be seen?"),
                        judged(conversation, "Does the assistant's last message ask which printer this is, or say it "
                                             "needs to know the printer's model, although the conversation names it?")],
                       "answered without adding or blaming the connection")


class ChangeEditStartGcode(SavedPrinterCase):
    """The person asks for a line in the start g-code. Passes when the change
    is applied to this printer's start g-code, keeping G28 and adding G29 after
    it, saved under the printer's own name; nothing else is changed or added."""

    name = "change-edit-start-gcode"

    def run(self, conversation):
        last_reply(conversation, "add G29 right after the G28 line in the start g-code")
        own = self.printer["name"]
        applied = [a for a in self.applied()
                   if a.get("persistAs") == own and "G29" in str(a.get("changes", {}).get("machine_start_gcode", "")) and
                   str(a["changes"]["machine_start_gcode"]).find("G28") < str(a["changes"]["machine_start_gcode"]).find("G29")]
        return verdict([self.forbidden(ADDING + ("printer_change",) + CONNECT),
                        "" if applied else f"CHANGE NOT APPLIED {self.applied()}"],
                       "applied the change and saved it in place")


class ChangeStockCopy(SavedPrinterCase):
    """A change to the settings OrcaSlicer ships for the model, selected in the
    project (the header's Printer settings… on a project that uses them).
    Saving over them is refused; passes when the change is applied to the copy
    the refusal suggests, and nothing is added."""

    name = "change-stock-copy"
    printer = {**A1_MINI, "name": "Bambu Lab A1 mini 0.4 nozzle"}
    provider = "bambu"
    stock = True
    selected = True

    @classmethod
    def session_for(cls):
        session = super().session_for()
        session["context"]["printer"]["stock"] = True
        session["context"]["printer"]["copyName"] = cls.printer["name"] + " - Copy"
        return session

    def run(self, conversation):
        last_reply(conversation, "set the retraction length to 1 mm")
        copy = self.printer["name"] + " - Copy"
        applied = [a for a in self.applied() if a.get("persistAs") == copy and
                   str(a.get("changes", {}).get("retraction_length", "")).strip() in ("1", "1.0", "1.00")]
        return verdict([self.forbidden(ADDING + ("printer_change",) + CONNECT),
                        "" if applied else f"COPY NOT APPLIED {self.applied()}"],
                       "saved as a copy")


class ChangeBedSize(SavedPrinterCase):
    """The bed size, which the settings tools refuse to change. Passes when
    nothing is changed or added and the reply says the person can change it in
    the app, without claiming to open it."""

    name = "change-bed-size"

    def run(self, conversation):
        last_reply(conversation, "make the bed 250 by 250")
        yes, why = judge(conversation.log, "Does the assistant's last message tell the person they can change the bed size "
                                           "themselves in the app's printer settings?")
        return verdict([self.forbidden(),
                        "" if yes else "DID NOT SAY WHERE: " + why,
                        judged(conversation, "Does the assistant's last message say it has opened the settings, or will open "
                                             "them, for the person?")],
                       "said where to change it")


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
        return verdict([self.forbidden(ADDING + CONNECT),
                        f"SAVED {self.printer['nozzle']}" if saved else ""] +
                       refused_nozzle_problems(conversation, reply, KOBRA["nozzles"]),
                       "saved nothing; named the sizes and asked")


class ChangeAddAnother(SavedPrinterCase):
    """Another printer, asked for in a Change session. The app refuses adding
    there (PrinterConversation::preflight_tool), so a try is not a failure;
    the reply is. Passes when nothing is saved and the reply says where
    another printer is added, and does not say it was added."""

    name = "change-add-another"

    def run(self, conversation):
        last_reply(conversation, "I also have a Prusa MK4S, can you add it too?")
        yes, why = judge(conversation.log, "Does the assistant's last message tell the person that another printer is "
                                           "added from Home, for example with + Add printer?")
        return verdict([self.forbidden(("printer_change",) + CONNECT),
                        "" if yes else "DID NOT SAY WHERE: " + why,
                        judged(conversation, "Does the assistant's last message say or imply that the Prusa MK4S has been "
                                             "added or set up?")],
                       "said where to add it" + ("; tried " + ", ".join(sorted({n for n, _ in self.called(*ADDING)}))
                                                 if self.called(*ADDING) else ""))


class ChangeThenConnect(SavedPrinterCase):
    """Connecting, asked for from a Change session. Passes when the model asks
    for the address rather than inventing one, and the address the person then
    gives opens the card; nothing is added."""

    name = "change-then-connect"

    def run(self, conversation):
        last_reply(conversation, "can you connect it so I can send prints to it?")
        early = self.called("printer_host_connect")
        last_reply(conversation, "it's at 192.168.1.42")
        if not self.called("printer_host_connect"):
            last_reply(conversation, "it runs Moonraker")  # as --printer-live answers, if still asked
        cards = [a for _, a in self.called("printer_host_connect") if "192.168.1.42" in a.get("address", "") and "hostType" in a]
        return verdict([self.forbidden(ADDING + ("printer_change",)),
                        f"CONNECT BEFORE THE ADDRESS {early}" if early else "",
                        "" if cards else f"NO CARD FOR THE ADDRESS {self.called('printer_host_connect')}"],
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
        if not self.called("printer_host_connect"):
            last_reply(conversation, "yes, connect it")
        cards = [a for _, a in self.called("printer_host_connect")
                 if a.get("hostType") == "moonraker" and "voron.local" in a.get("address", "")]
        return verdict([self.forbidden(ADDING + ("printer_change",)),
                        "" if cards else f"NO MOONRAKER CARD {self.called('printer_host_connect')}"], "card for voron.local")


class ConnectAskWhy(SavedPrinterCase):
    """A question before connecting. Passes when nothing runs and the reply
    says they can prepare prints for it without a connection."""

    name = "connect-ask-why"
    mode = "connect"
    printer = VORON

    def run(self, conversation):
        last_reply(conversation, "do I have to connect it? what do I get from that?")
        yes, why = judge(conversation.log, "Does the assistant's last message make clear that connecting is optional, or that "
                                           "the person can already prepare prints for the printer without a connection?")
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
    """The person asks for the whole list. The assistant tells them they can
    browse it manually in the app, without adding another printer."""

    name = "add-full-list"

    def run(self, conversation):
        self.say(conversation, "show me the full printer list")
        yes, why = judge(conversation.log, "Does the assistant's last message tell the person they can browse the full "
                                           "printer list manually in the app, without claiming to open it for them?")
        return verdict([f"ADDED {self.added}" if self.added else "", "" if yes else "NO MANUAL ROUTE: " + why],
                       "suggested browsing the list manually")


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


class ConnectHostNoResponse(SavedPrinterCase):
    """A print host is given and the app reports no response. Passes when the
    person is left with a guess at why and something to look at or try, not
    only a list of ways out."""

    name = "connect-host-no-response"
    mode = "connect"
    printer = VORON
    # OrcaPrinterBackend::settle for a test that timed out.
    host_answer = ("failed", "The print host test had no answer from {address} within 30 seconds. The test sent no API key.")

    def run(self, conversation):
        last_reply(conversation, "it's http://voron.local, I open it with Mainsail")
        if not self.called("printer_host_connect"):
            last_reply(conversation, "yes, connect it")
        if not self.called("printer_host_connect"):
            return False, "NO CONNECTION TEST"
        problem = ended_badly(conversation, "The printer is switched off.")
        return verdict([self.forbidden(ADDING + ("printer_change",)), problem], "left with a guess and a next step")


class ConnectHostKlipperShutdown(SavedPrinterCase):
    """The first test, with no key, is accepted, and Moonraker reports Klipper
    shut down, as the Pi at raspberrypi.local answered --print-host-check on
    2026-11-04. Passes when the model asks for no key and the reply says the
    printer is connected and Klipper must be started before it prints."""

    name = "connect-host-klipper-shutdown"
    mode = "connect"
    printer = VORON
    host_answer = ("verified", "Moonraker at {address} accepted the test, and this printer now connects there. The test "
                               "sent no API key. Moonraker reports Klipper's state as shutdown.")

    def run(self, conversation):
        last_reply(conversation, "it's at raspberrypi.local, it runs Klipper with Mainsail")
        if not self.called("printer_host_connect"):
            last_reply(conversation, "yes, connect it")
        if not self.called("printer_host_connect"):
            return False, "NO CONNECTION TEST"
        yes, why = judge(conversation.log, "Does the assistant's last message say the printer is now connected, and that "
                                           "Klipper is shut down, so it has to be started or restarted before the printer "
                                           "can print?")
        return verdict([self.forbidden(ADDING + ("printer_change",)), "" if yes else "SHUTDOWN NOT SAID: " + why],
                       "connected, Klipper shut down said")


class ConnectHostNoKey(SavedPrinterCase):
    """Moonraker asks for a key, and the person says in the form they have
    none. Passes when the chat leaves them with a way to get one or to let the
    app in without one."""

    name = "connect-host-no-key"
    mode = "connect"
    printer = VORON
    host_answer = ("help_requested", "Moonraker at {address} refused the test: HTTP 401: Unauthorized The test sent no API "
                                     "key. The person tapped \"Help me find the key\" in the form: they need help finding "
                                     "the API key for {address}. Help them find it. "
                                    "Nothing was sent.")

    def run(self, conversation):
        last_reply(conversation, "it's http://voron.local, I open it with Mainsail")
        if not self.called("printer_host_connect"):
            last_reply(conversation, "yes, connect it")
        if not self.called("printer_host_connect"):
            return False, "NO CONNECTION TEST"
        problem = ended_badly(conversation, "Moonraker's [authorization] section does not trust this computer's address. "
                                            "Adding the computer's address or network to trusted_clients there, in "
                                            "moonraker.conf, lets the app in with no key; otherwise it needs Moonraker's API key.")
        return verdict([self.forbidden(ADDING + ("printer_change",)), problem], "left with a way to a key")


class ConnectHostWebPort(SavedPrinterCase):
    """The person gives Mainsail's web port, which serves the page and not
    Moonraker, as 127.0.0.1:8081 answered --print-host-check on 2026-11-04.
    Passes by where the chat leaves the person (ended_badly)."""

    name = "connect-host-web-port"
    mode = "connect"
    printer = VORON
    host_answer = ("failed", "The print host test failed: Could not parse Moonraker server response: <unspecified "
                             "file>(1): expected value The test sent no API key.")

    def run(self, conversation):
        last_reply(conversation, "it's http://voron.local:8081, that's where Mainsail opens")
        if not self.called("printer_host_connect"):
            last_reply(conversation, "yes, connect it")
        if not self.called("printer_host_connect"):
            return False, "NO CONNECTION TEST"
        problem = ended_badly(conversation, "Port 8081 serves Mainsail's web page, which does not pass requests on to "
                                            "Moonraker; Moonraker itself answers on port 7125.")
        return verdict([self.forbidden(ADDING + ("printer_change",)), problem], "left with a guess and a next step")


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
    status = {"address": "", "candidates": [], "seen": [], "search": {"state": "not_started"},
              "hostType": "", "nozzleMismatch": False, "provider": "bambu",
              "state": "unavailable",
              "message": "Connecting a Bambu Lab printer needs Bambu's network plug-in, which is not installed or not turned "
                         "on. The person can already prepare prints for this printer without it."}

    def run(self, conversation):
        last_reply(conversation, "ok, connect it")
        yes, why = judge(conversation.log, "Does the assistant's last message give Bambu's network plug-in, missing from "
                                           "this computer, as the reason the printer cannot be connected yet?")
        return verdict([self.forbidden(ADDING + ("printer_change",)),
                        "REQUESTED A CREDENTIAL" if self.called(*CONNECT) else "",
                        "" if yes else "PLUG-IN NOT NAMED: " + why], "named the plug-in")


# Session payloads for the saved-printer cases.
for _case in (ChangeAskStartGcode, ChangeAddAnother, ChangeAskNozzle, ChangeLoadedFilament, ChangePlate, ChangeUnshippedNozzle,
              ChangeThenConnect, ChangeAskLoaded, ConnectHostAddress, ConnectAskWhy,
              ChangeAskLoadedNotConnected, ConnectHostNoResponse, ConnectBambuNoPlugin, ChangeEditStartGcode,
              ChangeStockCopy, ChangeBedSize, ConnectHostKlipperShutdown, ConnectHostNoKey, ConnectHostWebPort):
    _case.session = _case.session_for()

CASES = {case.name: case for case in (ConnectBambu, AddNamedModel, AddChooseFromThree, AddThenUndo, AddNotNowClose,
                                      ConnectVerifiedClose, ConnectLeaveClose, ChangeNozzleClose,
                                      ChangeAskStartGcode, ChangeAddAnother, ChangeAskNozzle, ChangeLoadedFilament, ChangePlate,
                                      ChangeUnshippedNozzle, ChangeThenConnect, ChangeAskLoaded, ConnectHostAddress,
                                      ConnectAskWhy, AddVagueDescription, AddUnsupportedPrinter,
                                      AddManyFit, AddNameStartsTwo, AddUnshippedNozzle, AddFullList,
                                      ChangeAskLoadedNotConnected, ConnectHostNoResponse, ConnectBambuNoPlugin,
                                      ChangeEditStartGcode, ChangeStockCopy, ChangeBedSize,
                                      ConnectRightCodeSilentPort, ConnectPortWentQuiet, ConnectCodeRuledOut, ConnectUnforeseenFailure,
                                      ConnectScreenSaysDisconnected, ConnectHostKlipperShutdown, ConnectHostNoKey,
                                      ConnectHostWebPort)}


# --- The filament chat ---------------------------------------------------------
#
# The header's Filament settings… opens a chat about one filament preset with
# its own instructions (filamentInstructions.ts) and the settings tools alone,
# which PrinterPanel::filament_preflight holds to that preset and to saving.

FILAMENT_SETTINGS = {
    "nozzle_temperature": ("220,220", True, "integer[]", "Nozzle temperature"),
    "nozzle_temperature_initial_layer": ("220,220", True, "integer[]", "Nozzle temperature, first layer"),
    "textured_plate_temp": ("65", True, "integer[]", "Textured PEI plate temperature"),
    "fan_max_speed": ("100", True, "integer[]", "Fan max speed"),
    "filament_type": ("PLA", False, "string[]", "Type"),
    "filament_diameter": ("1.75", False, "float[]", "Diameter"),
}


class FilamentCase:
    """A filament chat about one preset, its settings tools answered as the app
    answers them: PrinterPanel::filament_preflight, then OrcaWorkspaceAdapter.
    The preset is the one open in its settings tab, and in slot 1."""

    preset = "Bambu PLA Basic @BBL X1C"
    shown = "Bambu PLA Basic"
    stock = True
    tools = [tool for tool in TOOLS if tool["name"] in SETTINGS]

    def __init__(self):
        self.calls = []

    @classmethod
    def session_for(cls):
        session = {"kind": "filament", "slot": 1, "preset": cls.preset, "shown": cls.shown, "material": "PLA",
                   "stock": cls.stock}
        if cls.stock:
            session["copyName"] = cls.preset + " - Copy"
        return session

    def tool(self, name, arguments):
        self.calls.append((name, arguments))
        own = self.preset
        if problem := invalid_arguments(name, arguments):
            return {"error": problem}, False
        if arguments.get("scope") != "filament" or (arguments.get("target") or {}).get("preset") != own:
            return {"error": {"code": "not_in_this_conversation", "message": "This chat changes the settings of one filament: "
                                                                             f'pass scope filament and target.preset "{own}".'}}, False
        if name == "settings_apply_patch" and "persistAs" not in arguments:
            return {"error": {"code": "not_saved", "message": f'Every change in this chat is saved: pass persistAs "{own}", or '
                                                              "the copy's name a read_only_preset issue gives."}}, False
        context = {"scope": "filament", "presetName": own, "sessionId": "1", "revision": 7, "truncated": False}
        if name == "settings_search":
            words = [w for w in re.split(r"[\s,;]+", str(arguments.get("query", "")).lower()) if w]
            items = [{"key": key, "type": kind, "label": label, "category": "Filament", "description": label, "unit": "",
                      "enumValues": [], "enumLabels": [], "writable": writable, "truncated": False}
                     for key, (_, writable, kind, label) in FILAMENT_SETTINGS.items()
                     if not words or any(w in key or w in label.lower() for w in words)]
            return {**context, "items": items, "nextCursor": ""}, False
        if name == "settings_get":
            items, unknown = [], []
            for key in arguments.get("keys", []):
                if key in FILAMENT_SETTINGS:
                    value, writable, kind, label = FILAMENT_SETTINGS[key]
                    items.append({"key": key, "value": value, "type": kind, "label": label, "unit": "",
                                  "differsFromPreset": False, "differsFromSystem": False, "writable": writable})
                else:
                    unknown.append({"key": key, "code": "unknown_setting", "message": "Unknown setting.", "allowed": [],
                                    "suggestions": [], "truncated": False})
            return {**context, "items": items, "unknownKeys": unknown}, False
        issues, changes = [], []
        for key, value in (arguments.get("changes") or {}).items():
            if key not in FILAMENT_SETTINGS:
                issues.append({"key": key, "code": "unknown_setting", "message": "Unknown setting."})
            elif not FILAMENT_SETTINGS[key][1]:
                issues.append({"key": key, "code": "unsupported_setting_mutation", "message": "The tools do not change this filament setting; the person changes it in the app."})
            elif len(str(value).split(",")) != len(FILAMENT_SETTINGS[key][0].split(",")):
                count = len(FILAMENT_SETTINGS[key][0].split(","))
                issues.append({"key": key, "code": "invalid_setting_value",
                               "message": f"This setting holds {count} values; give {count}, separated by commas."})
            else:
                changes.append({"key": key, "before": FILAMENT_SETTINGS[key][0], "after": str(value)})
        persist = arguments.get("persistAs")
        if not issues and persist == own and self.stock:
            issues.append({"key": "", "code": "read_only_preset", "suggestions": [own + " - Copy"],
                           "message": f'"{own}" comes with the app and cannot be overwritten. Save the change as a copy: '
                                      f'pass persistAs "{own} - Copy".'})
        if name == "settings_preview_patch":
            return {**context, "valid": not issues, "changes": changes if not issues else [], "dependencies": [],
                    "issues": issues, "warnings": []}, False
        if issues:
            return {"error": {"code": issues[0]["code"], "message": issues[0]["message"]}}, False
        return {**context, "applied": True, "changes": changes, "normalized": [],
                "savedAs": persist or "", "presetDirty": persist is None,
                "projectUndo": False, "truncated": False}, False

    def called(self, *names):
        return [(name, arguments) for name, arguments in self.calls if name in names]

    def applied(self):
        return [arguments for _, arguments in self.called("settings_apply_patch")]


class FilamentHotter(FilamentCase):
    """A change relative to the value in force (seen 2026-09-29 on the project
    assistant's instructions: "5 degrees hotter" set 205 from 220, never
    read). Passes when the temperature is read first and changed to 225 for
    each nozzle kind, the first layer's too, saved as the copy
    the facts name."""

    name = "filament-hotter"

    def run(self, conversation):
        last_reply(conversation, "make the nozzle 5 degrees hotter")
        read = [a for _, a in self.called("settings_get") if "nozzle_temperature" in a.get("keys", [])]
        applied = [a for a in self.applied() if a.get("persistAs") == self.preset + " - Copy" and
                   all(str(a.get("changes", {}).get(key, "")).replace(" ", "") == "225,225"
                       for key in ("nozzle_temperature", "nozzle_temperature_initial_layer"))]
        return verdict(["" if read else "NOT READ FIRST",
                        "" if applied else f"225,225 COPY NOT APPLIED {self.applied()}"],
                       "read, then applied 225,225 as a copy")


class FilamentAskTemperature(FilamentCase):
    """A question. Passes when the temperature is read and quoted, and nothing
    is changed."""

    name = "filament-ask-temperature"

    def run(self, conversation):
        reply = last_reply(conversation, "what nozzle temperature does it print at?")
        read = [a for _, a in self.called("settings_get") if "nozzle_temperature" in a.get("keys", [])]
        return verdict(["" if read else "NOT READ",
                        "" if "220" in reply else "DID NOT SAY 220",
                        f"APPLIED A CHANGE {self.applied()}" if self.applied() else ""],
                       "read and answered")


class FilamentOtherSettings(FilamentCase):
    """A print setting asked for in a filament's chat. Passes when nothing goes
    is changed and the reply says it is changed elsewhere in the app."""

    name = "filament-other-settings"

    def run(self, conversation):
        last_reply(conversation, "also make the walls thicker")
        yes, why = judge(conversation.log, "Does the assistant's last message tell the person that the walls, or the print's "
                                           "own settings, are changed somewhere else than this chat?")
        return verdict([f"APPLIED A CHANGE {self.applied()}" if self.applied() else "",
                        "" if yes else "DID NOT SAY WHERE: " + why,
                        judged(conversation, "Does the assistant's last message say the walls were made thicker?")],
                       "said it is changed elsewhere")


class FilamentOwnInPlace(FilamentCase):
    """A filament the person saved. Passes when the change is saved under its
    own name, the first layer's too."""

    name = "filament-own-in-place"
    preset = "My PLA"
    shown = "My PLA"
    stock = False

    def run(self, conversation):
        last_reply(conversation, "lower the nozzle temperature to 210")
        applied = [a for a in self.applied() if a.get("persistAs") == self.preset and
                   all(str(a.get("changes", {}).get(key, "")).replace(" ", "") == "210,210"
                       for key in ("nozzle_temperature", "nozzle_temperature_initial_layer"))]
        return verdict(["" if applied else f"210,210 NOT APPLIED IN PLACE {self.applied()}"], "saved in place")


for _filament_case in (FilamentHotter, FilamentAskTemperature, FilamentOtherSettings, FilamentOwnInPlace):
    _filament_case.session = _filament_case.session_for()

CASES.update({case.name: case for case in (FilamentHotter, FilamentAskTemperature, FilamentOtherSettings, FilamentOwnInPlace)})


# --- Runner ------------------------------------------------------------------


def run_once(case_class, prompt, key, model, endpoint=ENDPOINT, providers=None):
    case = case_class()
    conversation = Conversation(case, prompt, key, model, endpoint, providers)
    try:
        passed, detail = case.run(conversation)
    except Exception as error:  # a failed request is a failed run, reported as such
        return False, f"ERROR {error}", conversation.log
    # The opening is the page's own line, not the model's.
    keys     = result_keys(conversation.log)
    problems = sorted({problem for line in conversation.log[1:] if line.startswith("assistant: ")
                       for problem in reply_problems(line[len("assistant: "):], keys)})
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
    parser.add_argument("--backend", choices=("api", "claude-cli"), default="api",
                        help="api: the model through a Responses API endpoint (default); claude-cli: the Claude Code "
                             "CLI on the developer's subscription")
    parser.add_argument("--judge", choices=("openai", "claude-cli"), default="openai",
                        help=f"openai: {JUDGE_MODEL} (default); claude-cli: --claude-model on the subscription")
    parser.add_argument("--claude-model", default=CLAUDE_MODEL, help=f"claude-cli's model (default {CLAUDE_MODEL})")
    parser.add_argument("--claude-effort", default=CLAUDE_EFFORT, help=f"claude-cli's effort (default {CLAUDE_EFFORT})")
    args = parser.parse_args()
    global CLAUDE, CLAUDE_JUDGE
    if args.backend == "claude-cli":
        CLAUDE = (args.claude_model, args.claude_effort)
    if args.judge == "claude-cli":
        CLAUDE_JUDGE = (args.claude_model, args.claude_effort)
    # Hosts are pinned per model: another model starts unpinned.
    model, endpoint, key_env = args.model or MODEL, args.endpoint or ENDPOINT, args.key_env or KEY_ENV
    providers = args.provider or (PROVIDERS if model == MODEL else None)
    args.key_env = key_env
    key = os.environ.get(args.key_env)
    if not CLAUDE and not key:
        sys.exit(f"{args.key_env} is required")
    if not CLAUDE_JUDGE and not os.environ.get("OPENAI_API_KEY"):
        sys.exit("OPENAI_API_KEY is required for the judge, or --judge claude-cli")
    names = args.case or sorted(CASES)
    if args.runs is None:
        args.runs = max(1, min(MAX_RUNS, RUN_BUDGET // len(names)))
    must_pass = args.runs if args.must_pass is None else args.must_pass
    if CLAUDE:
        print(f"model {CLAUDE[0]} (effort {CLAUDE[1]}) through the Claude Code CLI", end="")
    else:
        print(f"model {model} at {endpoint}" + (f", hosts {', '.join(providers)}" if providers else ""), end="")
    print(f"; judge {CLAUDE_JUDGE[0] + ' through the Claude Code CLI' if CLAUDE_JUDGE else JUDGE_MODEL}"
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
    print(f"tokens: model {dict(USAGE['model'])}; judge {dict(USAGE['judge'])} in {JUDGE_CALLS['calls']} calls")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
