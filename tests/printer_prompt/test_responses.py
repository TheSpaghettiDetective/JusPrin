#!/usr/bin/env python3
"""The runner's handling of the model's responses, offline: hand-written
outputs in place of the model, no key and no request sent. Above all a
response with several calls, of which the runner once answered only the
first.

Usage: python3 tests/printer_prompt/test_responses.py
"""

import importlib.util
import json
import pathlib
import re
import unittest

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("run_prompt_tests", HERE / "run_prompt_tests.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

PROMPT = {"instructions": "instructions", "opening": "Which printer is it?"}
FILAMENT = "Bambu PLA Basic @BBL X1C"
KOBRA = runner.KOBRA["name"]


def call(name, arguments=None, call_id=None):
    return {"type": "function_call", "call_id": call_id or "call-" + name, "name": name,
            "arguments": json.dumps(arguments or {})}


def reply(text):
    return {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": text}]}


def apply_start_gcode(persist_as=None, call_id=None):
    arguments = {"scope": "printer", "target": {"preset": KOBRA}, "expectedSessionId": "1", "expectedRevision": 7,
                 "changes": {"machine_start_gcode": "G28 ; home all axes\nG29\nG1 Z5 F5000 ; lift nozzle"}}
    if persist_as:
        arguments["persistAs"] = persist_as
    return call("settings_apply_patch", arguments, call_id)


class Scripted:
    """The model, as a list of outputs, one per request; keeps each request."""

    def __init__(self, *outputs):
        self.outputs, self.bodies = list(outputs), []

    def __call__(self, body, key, endpoint=None):
        self.bodies.append(json.loads(json.dumps(body)))  # as sent, before the runner adds to its input
        return {"output": self.outputs.pop(0)}


class Responses(unittest.TestCase):
    def setUp(self):
        self.post, self.judge = runner.post, runner.judge
        runner.judge = lambda log, question: self.fail("no case here should need the judge")

    def tearDown(self):
        runner.post, runner.judge = self.post, self.judge

    def run_case(self, case_class, *outputs):
        """A run as run_once makes it, with the case it ran and the requests
        it sent."""
        kept = []

        class Kept(case_class):
            def __init__(self):
                super().__init__()
                kept.append(self)

        runner.post = Scripted(*outputs)
        passed, detail, self.log = runner.run_once(Kept, PROMPT, "key", "model")
        bodies = runner.post.bodies
        self.assertTrue(all(body["parallel_tool_calls"] is True for body in bodies))
        self.assertEqual(runner.post.outputs, [], "every scripted output is used")
        return passed, detail, kept[0], bodies

    @staticmethod
    def results(body):
        return {item["call_id"]: json.loads(item["output"]) for item in body["input"] if item.get("type") == "function_call_output"}

    # --- The connection skill ----------------------------------------------

    def test_a_print_host_chat_has_its_skill_from_the_first_request(self):
        """A chat about a saved Klipper printer: the print host skill is in
        the first request, before anything is called, and is not sent again,
        as PrinterConversation::connection_skill gives it."""
        passed, detail, case, bodies = self.run_case(
            runner.ConnectHostAddress,
            [call("printer_host_connect", {"hostType": "moonraker", "address": "http://voron.local"})],
            [reply("Type its API key into the form if it asks for one.")])
        self.assertTrue(passed, detail)
        skills = [[item["content"] for item in body["input"] if item.get("role") == "developer"
                   and item.get("content") in runner.CONNECTION_SKILLS.values()] for body in bodies]
        self.assertEqual(skills, [[runner.CONNECTION_SKILLS["print-host"]]] * 2)

    def test_an_added_printer_brings_its_skill_with_the_add_result(self):
        """An add chat: no skill before printer_add, then the one for the
        added printer's vendor right after its result."""
        passed, detail, case, bodies = self.run_case(
            runner.AddNamedModel,
            [call("printer_add", {"catalogId": "BBL/Bambu Lab A1 mini"})],
            [reply("I set it up with the 0.4 mm nozzle.\n" + runner.CONNECT_OFFER)])
        self.assertTrue(passed, detail)
        first, second = [[item.get("content") for item in body["input"] if item.get("role") == "developer"] for body in bodies]
        self.assertNotIn(runner.CONNECTION_SKILLS["bambu"], first)
        self.assertEqual(second[-1], runner.CONNECTION_SKILLS["bambu"])
        self.assertEqual(bodies[1]["input"][-2]["type"], "function_call_output")

    def test_an_add_chat_that_checks_the_connection_does_not_crash(self):
        """An add case names its printer by catalogue id, a string. A
        connection check there once crashed the runner looking the printer's
        vendor up as if it were a saved printer."""
        passed, detail, case, bodies = self.run_case(
            runner.AddThenUndo, [call("printer_connection_status", {})], [reply("Which printer do you have?")])
        self.assertFalse(passed)
        self.assertTrue(detail.startswith("setup:"), detail)

    def test_a_search_is_in_the_transcript_the_judge_reads(self):
        """The provider's own search is an output item of its own; the
        transcript shows its query and pages before the reply that uses it."""
        search = {"type": "openrouter:web_search", "status": "completed",
                  "action": {"type": "search", "query": "Moonraker API key location",
                             "sources": [{"type": "url", "url": "https://docs.example/moonraker"}]}}
        _, _, _, bodies = self.run_case(
            runner.ConnectHostAddress,
            [call("printer_host_connect", {"hostType": "moonraker", "address": "http://voron.local"})],
            [search, reply("Type its API key into the form if it asks for one.")])
        log = self.log
        at = log.index("web search: Moonraker API key location -> https://docs.example/moonraker")
        self.assertTrue(log[at + 1].startswith("assistant: Type its API key"), log[at:at + 2])

    def test_the_next_turn_replays_a_search(self):
        """A search stays in the history the next turn sends, where the
        response put it, as the app sends a response's items back."""
        search = {"type": "openrouter:web_search", "status": "completed",
                  "action": {"type": "search", "query": "A1 mini LAN Only Mode",
                             "sources": [{"type": "url", "url": "https://wiki.example/lan"}]}}
        passed, detail, _, bodies = self.run_case(
            runner.ConnectBambu,
            [search, reply("Is it in LAN Only Mode? It is under Settings on its screen, says Bambu's wiki.\n"
                           "Choices: Yes | No")],
            [call("printer_connection_status", {}, "call-s"), call("printer_bambu_connect", {"deviceId": "FAKE001"}, "call-ok")],
            [reply("Enter the access code from the printer's screen in the form.")])
        self.assertTrue(passed, detail)
        next_turn = bodies[1]["input"]
        at = next_turn.index(search)
        self.assertEqual(next_turn[at + 1]["role"], "assistant")
        self.assertTrue(next_turn[at + 1]["content"].startswith("Is it in LAN Only Mode?"))

    def test_on_the_cli_a_skill_follows_the_results_as_their_text(self):
        """--backend claude-cli: the turn goes in once, each response's
        results are its tool results, and the skill that follows them is
        further text of the last one, as the app's notes after a result."""
        outputs = [[call("printer_add", {"catalogId": "BBL/Bambu Lab A1 mini"})],
                   [reply("I set it up with the 0.4 mm nozzle.\n" + runner.CONNECT_OFFER)]]
        answered = []

        class Run:
            def __init__(self, instructions, tools, items, model, effort):
                answered.append(("start", items[-1]["content"]))

            def next_response(self):
                return outputs.pop(0), {}

            def answer(self, calls, results):
                answered.append(("answer", [call["name"] for call in calls], results))

            def close(self):
                pass

        real_run, runner.claude_cli.Run, runner.CLAUDE = runner.claude_cli.Run, Run, ("claude-opus-5-5", "medium")
        try:
            passed, detail, _ = runner.run_once(runner.AddNamedModel, PROMPT, "key", "model")
        finally:
            runner.claude_cli.Run, runner.CLAUDE = real_run, None
        self.assertTrue(passed, detail)
        self.assertEqual(outputs, [])
        [(_, names, results)] = [entry for entry in answered if entry[0] == "answer"]
        self.assertEqual(names, ["printer_add"])
        result, skill = results[-1]
        self.assertIn("printer", json.loads(result))
        self.assertEqual(skill, runner.claude_cli.developer_block(runner.CONNECTION_SKILLS["bambu"])["text"])

    # --- Every call of a response runs -------------------------------------

    def test_a_change_after_a_read_runs(self):
        """A read and the change after it in one response: both run, in order,
        and both results go back in one request. The runner once answered the
        read alone, and the change never reached the simulator."""
        changes = {"nozzle_temperature": "225,225", "nozzle_temperature_initial_layer": "225,225"}
        first = [{"type": "reasoning", "id": "r-1", "summary": []},
                 call("settings_get", {"scope": "filament", "target": {"preset": FILAMENT},
                                       "keys": ["nozzle_temperature", "nozzle_temperature_initial_layer"]}, "call-a"),
                 call("settings_apply_patch", {"scope": "filament", "target": {"preset": FILAMENT}, "changes": changes,
                                               "expectedSessionId": "1", "expectedRevision": 7,
                                               "persistAs": FILAMENT + " - Copy"}, "call-b")]
        passed, detail, case, bodies = self.run_case(runner.FilamentHotter, first,
                                                     [reply("It now prints at 225 °C, saved as your own copy.")])
        self.assertTrue(passed, detail)
        self.assertEqual([name for name, _ in case.calls], ["settings_get", "settings_apply_patch"])
        self.assertEqual(len(bodies), 2)
        sent = bodies[1]["input"]
        start = len(sent) - len(first) - 2
        self.assertEqual(sent[start:start + len(first)], first)
        outputs = sent[start + len(first):]
        self.assertEqual([(item["type"], item["call_id"]) for item in outputs],
                         [("function_call_output", "call-a"), ("function_call_output", "call-b")])
        self.assertEqual(json.loads(outputs[1]["output"])["savedAs"], FILAMENT + " - Copy")

    def test_a_call_after_a_failed_one_is_not_run(self):
        """The first change fails (no persistAs on a printer the project does
        not use), so the second is answered not_run and never reaches the
        simulator; the model calls it again in its next response."""
        passed, detail, case, bodies = self.run_case(
            runner.ChangeEditStartGcode,
            [apply_start_gcode(None, "call-1"), apply_start_gcode(KOBRA, "call-2")],
            [apply_start_gcode(KOBRA, "call-3")],
            [reply("Added G29 after G28 and saved it.")])
        self.assertTrue(passed, detail)
        self.assertEqual([arguments.get("persistAs") for arguments in case.applied()], [None, KOBRA])
        results = self.results(bodies[1])
        self.assertEqual(results["call-1"]["error"]["code"], "not_selected")
        self.assertEqual(results["call-2"], {"state": "failed", "error": {"code": "not_run", "message":
                         "Not run: an earlier call in the same response, settings_apply_patch (call-1), did not succeed. "
                         "Nothing was queued. Call it again if it is still needed."}})
        self.assertTrue(self.results(bodies[2])["call-3"]["applied"])

    def test_a_refused_call_stops_the_calls_after_it(self):
        """A call to no tool is refused before it runs, which is a call that
        did not succeed: the change after it is not run."""
        passed, detail, case, bodies = self.run_case(
            runner.ChangeAskNozzle,
            [call("printer_details", {}, "call-x"), call("printer_change", {"printerName": KOBRA, "nozzle": 0.6}, "call-y")],
            [reply("It is set up for a 0.4 mm nozzle.")])
        self.assertTrue(passed, detail)
        self.assertEqual(case.calls, [])
        results = self.results(bodies[1])
        self.assertEqual(results["call-x"]["error"]["code"], "unknown_tool")
        self.assertEqual(results["call-y"]["error"]["code"], "not_run")
        self.assertIn("printer_details (call-x)", results["call-y"]["error"]["message"])

    def test_calls_after_a_success_keep_running(self):
        """A connection's outcome is a success: nothing after it is held back."""
        passed, detail, case, bodies = self.run_case(
            runner.ConnectBambu,
            [call("printer_bambu_connect", {"deviceId": "FAKE001"}, "call-c"), call("printer_connection_status", {}, "call-s")],
            [reply("Enter the access code from the printer's screen in the form.")])
        self.assertTrue(passed, detail)
        results = self.results(bodies[1])
        self.assertEqual(results["call-c"], runner.ConnectBambu.outcome)
        self.assertEqual(results["call-s"], runner.ConnectBambu.status)

    # --- What later turns replay -------------------------------------------

    def test_the_next_turn_replays_the_calls_that_ran(self):
        """Every call that ran is replayed in the next turn, each with its
        result, in order; the one answered not_run is not, since the app never
        ran or recorded it."""
        passed, detail, case, bodies = self.run_case(
            runner.ConnectBambu,
            [call("printer_connection_status", {}, "call-s"), call("printer_bambu_connect", {"deviceId": "<from_status>"}, "call-bad"),
             call("printer_bambu_connect", {"deviceId": "FAKE001"}, "call-unrun")],
            [reply("I couldn't find it. Is it in LAN mode?\nChoices: Yes | No")],
            [call("printer_bambu_connect", {"deviceId": "FAKE001"}, "call-ok")],
            [reply("Enter the access code from the printer's screen in the form.")])
        self.assertTrue(passed, detail)
        self.assertEqual(case.refused, ["<from_status>"])
        self.assertEqual(self.results(bodies[1])["call-unrun"]["error"]["code"], "not_run")
        next_turn = bodies[2]["input"]
        self.assertEqual(next_turn[-1], {"role": "user", "content": [{"type": "input_text", "text": "Yes, connect it"}]})
        replayed = [(item["type"], item["call_id"]) for item in next_turn if "call_id" in item]
        self.assertEqual(replayed, [("function_call", "call-s"), ("function_call_output", "call-s"),
                                    ("function_call", "call-bad"), ("function_call_output", "call-bad")])
        self.assertTrue(all("id" not in item for item in next_turn if item.get("type") == "function_call"))

    # --- One call or none, as before ---------------------------------------

    def test_a_single_call_then_a_reply(self):
        passed, detail, case, bodies = self.run_case(
            runner.ChangeNozzleClose,
            [call("printer_change", {"printerName": "Bambu Lab A1 mini", "nozzle": 0.6}, "call-1")],
            [reply("Saved: it now has a 0.6 mm nozzle. You can close this chat.")])
        self.assertTrue(passed, detail)
        self.assertEqual(bodies[1]["input"][-1]["call_id"], "call-1")

    def test_a_reply_ends_the_turn(self):
        passed, detail, _, bodies = self.run_case(runner.ChangeLoadedFilament, [reply("Nothing to save for that.")])
        self.assertTrue(passed, detail)
        self.assertEqual(len(bodies), 1)

    # --- The app's own words and rule --------------------------------------

    def test_the_unrun_result_is_the_apps_own(self):
        """The runner's "not run" text is the app's, word for word."""
        source = (HERE.parents[1] / "src/slic3r/GUI/JusPrin/Agent/ProviderAgent.cpp").read_text(encoding="utf-8")
        block = source[source.index('"Not run:'):source.index('still needed."') + len('still needed."')]
        head, *rest = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
        expected = {"state": "failed", "error": {"code": "not_run", "message": head + "printer_add (call-p)" + "".join(rest)}}
        self.assertEqual(runner.dump(runner.not_run("printer_add (call-p)")), runner.dump(expected))

    def test_what_counts_as_succeeded(self):
        self.assertTrue(runner.succeeded({"state": "dismissed", "message": "The person closed the form."}))
        # A test that ran and failed is a call that succeeded: the app counts
        # the call's own state, not the test's.
        self.assertTrue(runner.succeeded({"state": "failed", "message": "The print host test failed: HTTP 404: Not Found"}))
        self.assertTrue(runner.succeeded({"printers": []}))
        self.assertFalse(runner.succeeded({"error": {"code": "unknown_device", "message": "..."}}))
        self.assertFalse(runner.succeeded({"state": "failed", "error": {"code": "invalid_arguments"}}))
        self.assertFalse(runner.succeeded({"state": "cancelled"}))

    def test_a_source_list_after_the_choices_line_does_not_count(self):
        # As the CLI's search had the model write it after a search, 2026-11-04.
        reply = ("Do you open a Mainsail or Fluidd page to control it?\n\n"
                 "Choices: Yes, I have Rinkhals | No, it's stock | Not sure\n\n"
                 "Sources:\n"
                 "- [Rinkhals on GitHub](https://github.com/rinkhals-community/Rinkhals/)\n"
                 "- [SimplyPrint: Setting Up Your Anycubic Kobra 3](https://simplyprint.io/setup-guide/anycubic/kobra-3.md)\n")
        self.assertEqual(runner.reply_problems(reply), [])
        self.assertEqual(runner.reply_problems("Done?\nChoices: Yes | No\n**Sources:** [Bambu Wiki](https://wiki.bambulab.com/)"), [])
        self.assertTrue(runner.offers_done("All set.\nChoices: Done\n\nSources:\n- [Bambu Wiki](https://wiki.bambulab.com/)"))

    def test_other_text_after_the_choices_line_still_counts(self):
        for after in ("Let me know which.",
                      "Sources: Rinkhals on GitHub",
                      "- [Rinkhals](https://github.com/rinkhals-community/Rinkhals/) says it adds Moonraker."):
            with self.subTest(after=after):
                reply = "Do you open a Mainsail page?\nChoices: Yes | No\n" + after
                self.assertEqual(runner.reply_problems(reply), ["CHOICES NOT ON THE LAST LINE"])

    def test_a_leak_is_a_tool_name_field_or_result_the_tools_use(self):
        # Klipper's own folder was failed as a tool name before, 2026-11-04.
        keys = runner.result_keys(["tool: printer_host_connect {}", '  -> {"message":"Refused.","state":"failed"}'])
        self.assertEqual(keys, {"message", "state"})
        # Moonraker's own page, as a DeepSeek reply quoted it, 2026-09-28.
        for reply in ("The file is ~/printer_data/config/moonraker.conf.",
                      "Check the address field in Mainsail.",
                      "Macros use {braces} for parameters.",
                      'The page shows your key as {"result": "8ce6ae5d..."}.'):
            with self.subTest(reply=reply):
                self.assertEqual(runner.reply_problems(reply, keys), [])
        for reply, leaked in (("I ran printer_host_connect for you.", "printer_host_connect"),
                              ("Its deviceId is 01P00A.", "deviceId"),
                              ("Connect with hostType moonraker.", "hostType"),
                              ('The app said {"state": "failed"}.', '{"state":')):
            with self.subTest(reply=reply):
                self.assertEqual(runner.reply_problems(reply, keys), ["LEAK " + repr(leaked)])


if __name__ == "__main__":
    unittest.main()
