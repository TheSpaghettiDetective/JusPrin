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
        passed, detail, log = runner.run_once(Kept, PROMPT, "key", "model")
        bodies = runner.post.bodies
        self.assertTrue(all(body["parallel_tool_calls"] is True for body in bodies))
        self.assertEqual(runner.post.outputs, [], "every scripted output is used")
        return passed, detail, kept[0], bodies

    @staticmethod
    def results(body):
        return {item["call_id"]: json.loads(item["output"]) for item in body["input"] if item.get("type") == "function_call_output"}

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
        """credential_requested is a success: nothing after it is held back."""
        passed, detail, case, bodies = self.run_case(
            runner.ConnectBambu,
            [call("printer_connect", {"deviceId": "FAKE001"}, "call-c"), call("printer_connection_status", {}, "call-s")],
            [reply("Enter the access code from the printer's screen in the form.")])
        self.assertTrue(passed, detail)
        results = self.results(bodies[1])
        self.assertEqual(results["call-c"]["state"], "credential_requested")
        self.assertEqual(results["call-s"], runner.ConnectBambu.status)

    # --- What later turns replay -------------------------------------------

    def test_the_next_turn_replays_the_calls_that_ran(self):
        """Every call that ran is replayed in the next turn, each with its
        result, in order; the one answered not_run is not, since the app never
        ran or recorded it."""
        passed, detail, case, bodies = self.run_case(
            runner.ConnectBambu,
            [call("printer_connection_status", {}, "call-s"), call("printer_connect", {"deviceId": "<from_status>"}, "call-bad"),
             call("printer_connect", {"deviceId": "FAKE001"}, "call-unrun")],
            [reply("I couldn't find it. Is it in LAN mode?\nChoices: Yes | No")],
            [call("printer_connect", {"deviceId": "FAKE001"}, "call-ok")],
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
        source = (HERE.parents[1] / "src/slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.cpp").read_text(encoding="utf-8")
        block = source[source.index('"Not run:'):source.index('still needed."') + len('still needed."')]
        head, *rest = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
        expected = {"state": "failed", "error": {"code": "not_run", "message": head + "printer_add (call-p)" + "".join(rest)}}
        self.assertEqual(runner.dump(runner.not_run("printer_add (call-p)")), runner.dump(expected))

    def test_what_counts_as_succeeded(self):
        self.assertTrue(runner.succeeded({"state": "credential_requested", "message": "waiting"}))
        self.assertTrue(runner.succeeded({"printers": []}))
        self.assertFalse(runner.succeeded({"error": {"code": "unknown_device", "message": "..."}}))
        self.assertFalse(runner.succeeded({"state": "failed", "error": {"code": "invalid_arguments"}}))
        self.assertFalse(runner.succeeded({"state": "cancelled"}))


if __name__ == "__main__":
    unittest.main()
