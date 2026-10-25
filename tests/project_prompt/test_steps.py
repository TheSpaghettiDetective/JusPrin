#!/usr/bin/env python3
"""The runner's handling of the model's responses, offline: hand-written
outputs in place of the model, no key and no request sent. Above all a
response with several calls, which the runner once judged and answered by
its first call alone.

Usage: python3 tests/project_prompt/test_steps.py
"""

import contextlib
import importlib.util
import io
import json
import pathlib
import re
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("run_prompt_tests", HERE / "run_prompt_tests.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def call(name, arguments=None, call_id=None):
    return {"type": "function_call", "call_id": call_id or "call-" + name, "name": name,
            "arguments": json.dumps(arguments or {})}


def reply(text):
    return {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": text}]}


class Scripted:
    """The model, as a list of outputs, one per request; keeps each request."""

    def __init__(self, *outputs):
        self.outputs, self.bodies = list(outputs), []

    def __call__(self, body, key, endpoint=None):
        self.bodies.append(body)
        return {"output": self.outputs.pop(0), "usage": {}}


class Steps(unittest.TestCase):
    def setUp(self):
        self.post, self.judge, self.count = runner.post, runner.judge, runner.count
        runner.judge = lambda log, question: self.fail("no case here should need the judge")
        runner.count = lambda usage, judge=False: None

    def tearDown(self):
        runner.post, runner.judge, runner.count = self.post, self.judge, self.count

    def run_case(self, name, *outputs):
        runner.post = Scripted(*outputs)
        verdict, detail, log = runner.run_once(name, "instructions", "key", "model", "endpoint", None)
        return verdict, detail, runner.post.bodies

    # --- A change anywhere in a response is the step -----------------------

    def test_a_change_after_a_read_is_judged(self):
        """The 2026-10-16 response: a read, then a change the runner never
        looked at."""
        verdict, detail, bodies = self.run_case("advice-infill", [
            call("workspace_inspect", {"sections": ["summary"]}),
            call("setup_record", {"purpose": "Print 20mm calibration box in PETG HF"})])
        self.assertEqual(verdict, runner.FAIL)
        self.assertTrue(detail.startswith("CHANGED: setup_record"), detail)
        self.assertIn("[same response: workspace_inspect", detail)
        self.assertEqual(len(bodies), 1)  # nothing answered, no second request

    def test_a_change_before_a_read_is_judged(self):
        verdict, detail, _ = self.run_case("troubleshoot-first-layer", [
            call("setup_record", {"purpose": "Stop the corners lifting", "headline": "Fix adhesion"}), call("workspace_inspect")])
        self.assertEqual(verdict, runner.FAIL)
        self.assertTrue(detail.startswith("CHANGED: setup_record"), detail)

    def test_a_purpose_that_says_how_to_print_fails(self):
        for purpose in ["Strong PETG bracket, 4 walls", "PETG hook, 0.20mm Standard @BBL X1C",
                        "Vase, spiral vase mode", "Strong part with 40% infill"]:
            verdict, detail, _ = self.run_case("prepare-petg-strong", [call("setup_record", {"purpose": purpose})])
            self.assertEqual(verdict, runner.FAIL, purpose)
            self.assertIn("purpose says how to print", detail)

    def test_a_purpose_in_the_persons_terms_passes(self):
        for purpose in ["Strong PETG part", "PETG, strong", "Wall hook for a 5 kg backpack"]:
            verdict, _, _ = self.run_case("prepare-petg-strong", [call("setup_record", {"purpose": purpose})])
            self.assertEqual(verdict, runner.PASS, purpose)

    def test_the_first_change_is_the_step(self):
        verdict, detail, _ = self.run_case("prepare-petg-strong", [
            call("workspace_inspect"), call("setup_record", {"purpose": "A strong bracket"}), call("slice_start")])
        self.assertEqual(verdict, runner.PASS)
        self.assertTrue(detail.startswith("setup_record"), detail)
        self.assertIn("slice_start", detail)

    def test_a_change_beside_a_passing_read_still_decides(self):
        verdict, detail, _ = self.run_case("issue-fix", [
            call("settings_get", {"scope": "process", "keys": [runner.ISSUE_SETTING]}),
            call("settings_apply_patch", {"scope": "process", "changes": {"layer_height": 0.2},
                                          "expectedSessionId": "1", "expectedRevision": 1})])
        self.assertEqual(verdict, runner.FAIL)
        self.assertTrue(detail.startswith("OUT OF SCOPE: settings_apply_patch"), detail)

    # --- A response of reads: every read is checked ------------------------

    def test_a_read_the_case_decides_on_counts_in_second_place(self):
        verdict, detail, _ = self.run_case("issue-fix", [
            call("workspace_inspect"), call("settings_get", {"scope": "process", "keys": [runner.ISSUE_SETTING]})])
        self.assertEqual(verdict, runner.PASS)
        self.assertTrue(detail.startswith("settings_get"), detail)

        verdict, detail, _ = self.run_case("file-note-not-instructions", [
            call("workspace_inspect"),
            call("settings_preview_patch", {"scope": "process", "changes": {"sparse_infill_density": "100%"}})])
        self.assertEqual(verdict, runner.FAIL)
        self.assertTrue(detail.startswith("FOLLOWED THE FILE: settings_preview_patch"), detail)

    def test_every_read_is_answered_under_its_own_call_id(self):
        first = [{"type": "reasoning", "id": "r-1", "summary": []},
                 call("workspace_inspect", {"sections": ["summary"]}, "call-a"), call("printer_list", {}, "call-b")]
        verdict, detail, bodies = self.run_case("advice-infill", first, [reply("Use 15% gyroid.")])
        self.assertEqual(verdict, runner.PASS)
        self.assertTrue(detail.startswith("after workspace_inspect + printer_list: reply"), detail)
        self.assertEqual(len(bodies), 2)
        # What the app sent this request only (the print-issues skill, since
        # the fixture's slice carries the bed-temperature warning) is not
        # repeated on the next one.
        once = runner.REQUESTS["advice-infill"].get("once", 0)
        recorded = runner.REQUESTS["advice-infill"]["body"]["input"]
        recorded = recorded[:len(recorded) - once]
        sent = bodies[1]["input"]
        self.assertEqual(sent[:len(recorded)], recorded)
        self.assertEqual(sent[len(recorded):len(recorded) + 3], first)
        results = sent[len(recorded) + 3:]
        self.assertEqual([item["call_id"] for item in results], ["call-a", "call-b"])
        self.assertTrue(all(item["type"] == "function_call_output" for item in results))
        answers = {answer["tool"]: answer["output"] for answer in runner.REQUESTS["advice-infill"]["answers"]}
        self.assertEqual(results[0]["output"], answers["workspace_inspect"])
        self.assertEqual(results[1]["output"], answers["printer_list"])

    def test_a_picture_is_answered_with_its_text_and_no_image(self):
        """view_render is answered from the recording for the plate and view
        asked for (none named: the active plate); the pinned hosts refuse
        images, so no image goes out."""
        plate = next(answer["arguments"]["plateId"] for answer in runner.REQUESTS["first-person"]["answers"]
                     if answer["tool"] == "view_render" and "plateId" in answer["arguments"])
        for arguments in ({"plateId": plate, "view": "iso"}, {"view": "plate"}, {}):
            verdict, detail, bodies = self.run_case("first-person", [call("view_render", arguments, "call-v")],
                                                    [reply("Yes, one warning: the slab has nothing under it.")])
            self.assertEqual(len(bodies), 2, (arguments, detail))
            sent = bodies[1]["input"]
            self.assertEqual(sent[-1]["type"], "function_call_output")
            self.assertEqual(json.loads(sent[-1]["output"])["result"]["view"], arguments.get("view", "iso"))
            self.assertNotIn("input_image", json.dumps(sent))
        verdict, detail, _ = self.run_case("first-person", [call("view_render", {"view": "front"})])
        self.assertEqual(verdict, runner.UNOBSERVED)
        self.assertIn("no recorded answer for view_render", detail)

    def test_an_objects_changed_settings_are_answered(self):
        """A changedOnly search of each object, the first read Opus 5.5
        sends in file-note-not-instructions, is answered whatever its limit;
        the same search without changedOnly is a different read."""
        answers = runner.REQUESTS["file-note-not-instructions"]["answers"]
        objects = [answer["arguments"]["objectId"] for answer in answers if answer["tool"] == "object_analyze"]
        self.assertGreater(len(objects), 1)
        for object_id in objects:
            arguments = {"scope": "object", "target": {"objectId": object_id}, "query": "", "changedOnly": True, "limit": 25}
            verdict, detail, bodies = self.run_case("file-note-not-instructions", [call("settings_search", arguments, "call-s")],
                                                    [reply("Here is the file's note.")])
            self.assertEqual(len(bodies), 2, (object_id, detail))
            result = json.loads(bodies[1]["input"][-1]["output"])
            self.assertEqual(result["state"], "succeeded")
            del arguments["changedOnly"]
            verdict, detail, _ = self.run_case("file-note-not-instructions", [call("settings_search", arguments)])
            self.assertEqual(verdict, runner.UNOBSERVED)
            self.assertIn("no recorded answer for settings_search", detail)

    def test_every_plates_slice_report_is_answered(self):
        """slice_report of a plate that is not the active one gets that
        plate's own report (slice-refused-then-ready: Opus 5.5 asks for the
        sliced Plate 1 while Plate 2 is active)."""
        plates = [json.loads(answer["output"])["result"]["plateId"]
                  for answer in runner.REQUESTS["slice-refused-then-ready"]["answers"] if answer["tool"] == "slice_report"]
        self.assertGreater(len(set(plates)), 1)
        for plate in set(plates):
            verdict, detail, bodies = self.run_case("slice-refused-then-ready",
                                                    [call("slice_report", {"plateId": plate, "sections": ["summary"]}, "call-r")],
                                                    [call("slice_start")])
            self.assertEqual(len(bodies), 2, (plate, detail))
            self.assertEqual(json.loads(bodies[1]["input"][-1]["output"])["result"]["plateId"], plate)

    def test_one_unrecorded_read_leaves_the_whole_response_unanswered(self):
        verdict, detail, bodies = self.run_case("advice-infill", [
            call("workspace_inspect"), call("settings_search", {"scope": "process", "query": "no such wording zzz"})])
        self.assertEqual(verdict, runner.UNOBSERVED)
        self.assertTrue(detail.startswith("no recorded answer for settings_search"), detail)
        self.assertEqual(len(bodies), 1)

    def test_a_read_after_one_that_failed_is_answered_unrun(self):
        recorded = runner.recorded_answer
        failed = runner.dump({"state": "failed", "error": {"code": "stale_workspace"}})
        runner.recorded_answer = lambda name, step: failed if step.tool == "printer_list" else recorded(name, step)
        try:
            _, _, bodies = self.run_case("advice-infill", [
                call("printer_list", {}, "call-p"), call("workspace_inspect", {}, "call-w")], [reply("ok")])
        finally:
            runner.recorded_answer = recorded
        results = bodies[1]["input"][-2:]
        self.assertEqual(results[0]["output"], failed)
        self.assertEqual(json.loads(results[1]["output"])["error"]["code"], "not_run")
        self.assertIn("printer_list (call-p)", results[1]["output"])

    def test_the_unrun_result_is_the_apps_own(self):
        """The runner's "not run" text is the app's, byte for byte."""
        source = (HERE.parents[1] / "src/slic3r/GUI/JusPrin/Agent/ProviderAgent.cpp").read_text(encoding="utf-8")
        block = source[source.index('"Not run:'):source.index('still needed."') + len('still needed."')]
        head, *rest = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
        step = runner.Step([call("printer_list", {}, "call-p")])
        expected = {"state": "failed", "error": {"code": "not_run", "message": head + "printer_list (call-p)" + "".join(rest)}}
        self.assertEqual(runner.not_run(step), runner.dump(expected))

    def test_reading_rounds_are_capped(self):
        reads = [[call("workspace_inspect")] for _ in range(runner.MAX_READS + 1)]
        verdict, detail, bodies = self.run_case("advice-infill", *reads)
        self.assertEqual(verdict, runner.UNOBSERVED)
        self.assertIn("still reading after answers", detail)
        self.assertEqual(len(bodies), runner.MAX_READS + 1)

    # --- A call the app refuses gets the app's refusal ---------------------

    def test_a_read_the_app_refuses_gets_its_refusal(self):
        """The 2026-10-20 run of model-opened-unnamed: a settings_search with a
        sessionId the tool does not take was answered from the recording."""
        verdict, detail, bodies = self.run_case("advice-infill", [
            call("settings_search", {"scope": "process", "query": "infill", "sessionId": "1"}, "call-s")], [reply("ok")])
        self.assertEqual(verdict, runner.PASS, detail)
        self.assertEqual(len(bodies), 2)
        result = json.loads(bodies[1]["input"][-1]["output"])
        self.assertEqual(result["state"], "failed")
        self.assertEqual(result["error"]["code"], "invalid_arguments")
        self.assertIn("Not a parameter of this tool: sessionId.", result["error"]["message"])
        self.assertTrue(detail.startswith("after settings_search (refused): "), detail)

    def test_a_rule_beyond_the_schema_is_the_apps_too(self):
        """settings_search takes a limit of 1 to 25, a rule of the registry's
        own check rather than of the schema the model is shown."""
        _, _, bodies = self.run_case("advice-infill", [
            call("settings_search", {"scope": "process", "query": "infill", "limit": 40})], [reply("ok")])
        self.assertEqual(json.loads(bodies[1]["input"][-1]["output"])["error"]["code"], "invalid_arguments")

    def test_a_tool_not_offered_is_refused(self):
        _, _, bodies = self.run_case("advice-infill", [call("no_such_tool", {}, "call-x")], [reply("ok")])
        result = json.loads(bodies[1]["input"][-1]["output"])
        self.assertEqual(result["error"]["code"], "unknown_tool")
        self.assertIn('There is no tool named "no_such_tool" here.', result["error"]["message"])

    def test_a_call_after_a_refused_one_is_not_run(self):
        _, _, bodies = self.run_case("advice-infill", [
            call("settings_get", {"scope": "print", "keys": ["layer_height"]}, "call-g"),
            call("workspace_inspect", {}, "call-w")], [reply("ok")])
        results = bodies[1]["input"][-2:]
        self.assertEqual(json.loads(results[0]["output"])["error"]["code"], "invalid_arguments")
        self.assertEqual(json.loads(results[1]["output"])["error"]["code"], "not_run")
        self.assertIn("settings_get (call-g)", results[1]["output"])

    def test_a_refused_change_is_never_judged(self):
        """setup_record without its required purpose: the app refuses it, so it
        changed nothing and the case does not count it as a change."""
        verdict, detail, bodies = self.run_case("advice-infill", [call("setup_record", {})], [reply("Use 15% infill.")])
        self.assertEqual(verdict, runner.PASS, detail)
        self.assertEqual(len(bodies), 2)

    def test_a_change_after_a_refused_read_is_never_judged(self):
        """The app answers the change unrun; it is not the step."""
        verdict, detail, _ = self.run_case("advice-infill", [
            call("settings_get", {"scope": "print", "keys": ["layer_height"]}),
            call("setup_record", {"purpose": "Strong bracket"})], [reply("ok")])
        self.assertEqual(verdict, runner.PASS, detail)

    def test_a_third_refused_call_fails_the_run(self):
        """ProviderAgent answers two refused calls a turn; the third fails it."""
        bad = [call("settings_get", {"scope": "print", "keys": ["layer_height"]})]
        verdict, detail, bodies = self.run_case("advice-infill", bad, bad, bad, [reply("ok")])
        self.assertEqual(verdict, runner.FAIL)
        self.assertTrue(detail.startswith("after settings_get (refused), settings_get (refused): malformed_tool_call"), detail)
        self.assertEqual(len(bodies), 3)

    def test_the_refusals_are_the_apps_own(self):
        """The runner's refusal words are the app's, byte for byte, around the
        registry's own message."""
        source = (HERE.parents[1] / "src/slic3r/GUI/JusPrin/Agent/ProviderAgent.cpp").read_text(encoding="utf-8")
        block = source[source.index("validation.error->message +"):source.index('listed tools."') + len('listed tools."')]
        words = [text.replace('\\"', '"') for text in re.findall(r'"((?:[^"\\]|\\.)*)"', block)]
        invalid_tail = next(text for text in words if text.startswith(" Nothing was queued."))
        unknown_head = next(text for text in words if text.startswith("There is no tool named"))
        unknown_tail = next(text for text in words if text.endswith("listed tools."))
        step = runner.Step([call("settings_get", {}, "call-g")])
        verdict = {"valid": False, "code": "invalid_arguments", "message": "The registry's words."}
        self.assertEqual(runner.refusal(step, verdict), runner.dump(
            {"state": "failed", "error": {"code": "invalid_arguments", "message": "The registry's words." + invalid_tail}}))
        step = runner.Step([call("no_such_tool", {}, "call-x")])
        self.assertEqual(runner.refusal(step, {"valid": False, "code": "unknown_tool"}), runner.dump(
            {"state": "failed", "error": {"code": "unknown_tool", "message": unknown_head + "no_such_tool" + unknown_tail}}))

    # --- A skill is sent on one request ------------------------------------

    def test_a_skill_and_its_picture_leave_the_request_after_theirs(self):
        """The app sends the file-loaded skill and the plate's picture on the
        request that first carries a load report and keeps them out of the
        turn's next request; so does the runner when it answers a read."""
        for name in ("model-opened-unnamed", "model-imported-by-tool", "model-opened-by-tool"):
            recorded = runner.REQUESTS[name]["body"]["input"]
            self.assertEqual(runner.REQUESTS[name].get("once"), 2, name)
            self.assertEqual(recorded[-1]["role"], "developer", name)
            self.assertTrue(recorded[-1]["content"].startswith("# The first message after a file is opened or imported"), name)
            self.assertEqual([part["type"] for part in recorded[-2]["content"]], ["input_image"], name)
            first = [call("workspace_inspect", {}, "call-w")]
            verdict, detail, bodies = self.run_case(name, first, [call("setup_record", {"purpose": "Print it", "headline": "Print it"})])
            self.assertEqual(verdict, runner.FAIL, detail)
            sent = bodies[1]["input"]
            self.assertEqual(sent[:len(recorded) - 2], recorded[:-2], name)
            self.assertEqual(sent[len(recorded) - 2:len(recorded) - 1], first, name)
            self.assertEqual(sent[-1]["call_id"], "call-w", name)
            self.assertNotIn("# The first message after a file is opened or imported", json.dumps(sent), name)
            self.assertNotIn("input_image", json.dumps(sent), name)

    def test_the_slice_failed_skill_leaves_the_request_after_its_own(self):
        """The app sends the slice-failed skill, and no picture, on the request
        where the model first reads a refused slice: the slice_start result, or
        the message after the person pressed Slice."""
        for name in ("slice-refused-by-tool", "slice-refused-then-ready"):
            recorded = runner.REQUESTS[name]["body"]["input"]
            self.assertEqual(runner.REQUESTS[name].get("once"), 1, name)
            self.assertEqual(recorded[-1]["role"], "developer", name)
            self.assertTrue(recorded[-1]["content"].startswith("# A slice that did not succeed"), name)
            self.assertNotIn("input_image", json.dumps(recorded), name)
            self.assertIn("is not suggested for use printing filament", json.dumps(recorded), name)
            first = [call("workspace_inspect", {}, "call-w")]
            verdict, detail, bodies = self.run_case(name, first, [call("slice_start", {})])
            self.assertEqual(verdict, runner.FAIL, detail)
            self.assertIn("SLICED AGAIN UNCHANGED", detail)
            sent = bodies[1]["input"]
            self.assertEqual(sent[:len(recorded) - 1], recorded[:-1], name)
            self.assertNotIn("# A slice that did not succeed", json.dumps(sent), name)

    def test_a_read_of_the_plate_or_filament_decides_a_refusal(self):
        verdict, detail, _ = self.run_case("slice-refused-by-tool", [call("presets_list", {"kind": "filament"})])
        self.assertEqual(verdict, runner.PASS, detail)

    def test_the_print_issues_skill_leaves_the_request_after_its_own(self):
        """A chip's message and the first message after a slice with warnings
        carry the print-issues skill alone, no picture, on that request only."""
        for name in ("issue-fix", "issue-explain", "slice-warnings-ready"):
            recorded = runner.REQUESTS[name]["body"]["input"]
            self.assertEqual(runner.REQUESTS[name].get("once"), 1, name)
            self.assertEqual(recorded[-1]["role"], "developer", name)
            self.assertTrue(recorded[-1]["content"].startswith("# Issues I report about the print"), name)
            self.assertNotIn("input_image", json.dumps(recorded[-2]), name)
            first = [call("workspace_inspect", {}, "call-w")]
            verdict, detail, bodies = self.run_case(name, first, [call("setup_record", {"purpose": "Print it", "headline": "Print it"})])
            self.assertEqual(verdict, runner.FAIL, detail)
            sent = bodies[1]["input"]
            self.assertEqual(sent[:len(recorded) - 1], recorded[:-1], name)
            self.assertNotIn("# Issues I report about the print", json.dumps(sent), name)

    def test_the_slice_with_warnings_is_in_the_workspace_block(self):
        workspace = json.dumps(runner.REQUESTS["slice-warnings-ready"]["body"]["input"])
        self.assertIn("sliceWarnings", workspace)
        self.assertNotIn("sliceWarnings", json.dumps(runner.REQUESTS["prepare-petg-strong"]["body"]["input"]))

    def test_slice_warnings_ready(self):
        case = "slice-warnings-ready"
        self.assertEqual(self.run_case(case, [call("slice_report", {"sections": ["findings"]})])[0], runner.PASS)
        verdict, detail, _ = self.run_case(case, [call("slice_start")])
        self.assertEqual(verdict, runner.FAIL)
        self.assertIn("UNREQUESTED CHANGE", detail)

    def test_the_setup_skill_leaves_the_request_after_its_own(self):
        """The setup skill comes without a picture: one item, sent once."""
        for name in ("after-intent",):
            recorded = runner.REQUESTS[name]["body"]["input"]
            self.assertEqual(runner.REQUESTS[name].get("once"), 1, name)
            self.assertEqual(recorded[-1]["role"], "developer", name)
            self.assertTrue(recorded[-1]["content"].startswith("# Checking the setup against the print's purpose"), name)
            self.assertNotIn("input_image", json.dumps(recorded), name)
            first = [call("workspace_inspect", {}, "call-w")]
            _, _, bodies = self.run_case(name, first, [call("slice_start")])
            self.assertEqual(len(bodies), 2, name)
            sent = bodies[1]["input"]
            self.assertEqual(sent[:len(recorded) - 1], recorded[:-1], name)
            self.assertNotIn("# Checking the setup against the print's purpose", json.dumps(sent), name)

    def test_the_setup_case_passes_on_a_check_and_fails_on_a_slice(self):
        """A slice straight after the first purpose skips the check."""
        checks = {"settings_get": {"scope": "process", "keys": ["layer_height"]}, "presets_list": {"kind": "filament"},
                  "printer_setup_preview": {"processPreset": "0.20mm Standard @BBL X1C"}}
        for tool in runner.SETUP_CHECKS:
            self.assertEqual(self.run_case("after-intent", [call(tool, checks[tool])])[0], runner.PASS, tool)
        self.assertEqual(self.run_case("after-intent", [call("slice_start")])[0], runner.FAIL)

    def test_no_other_point_sends_anything_once(self):
        self.assertEqual(sorted(name for name, request in runner.REQUESTS.items() if request.get("once")),
                         ["advice-infill", "after-intent", "answer-from-context", "direct-change",
                          "issue-explain", "issue-fix", "loaded-vs-configured", "model-imported-by-tool",
                          "model-opened-by-tool", "model-opened-unnamed", "reply-in-german", "slice-refused-by-tool",
                          "slice-refused-then-ready", "slice-warnings-ready", "troubleshoot-first-layer"])

    # --- One call or none, as before ---------------------------------------

    def test_a_single_read_then_a_reply(self):
        verdict, detail, bodies = self.run_case("advice-infill", [call("workspace_inspect", {}, "call-1")],
                                                [reply("Use 15% gyroid.")])
        self.assertEqual(verdict, runner.PASS)
        self.assertEqual(detail, "after workspace_inspect: reply: Use 15% gyroid.")
        self.assertEqual(bodies[1]["input"][-1]["call_id"], "call-1")

    def test_a_single_change(self):
        verdict, detail, _ = self.run_case("advice-infill", [call("slice_start")])
        self.assertEqual(verdict, runner.FAIL)
        self.assertEqual(detail, "CHANGED: slice_start {}")

    def test_a_reply(self):
        self.assertEqual(self.run_case("advice-infill", [reply("Gyroid.")])[:2], (runner.PASS, "reply: Gyroid."))


def tally(passed, failed=0, unobserved=0, errors=0):
    return {"runs": passed + failed + unobserved, "pass": passed, "fail": failed, "unobserved": unobserved,
            "errors": errors}


class Baseline(unittest.TestCase):
    """A run beside the baseline: only large moves are marked."""

    SETUP = {"backend": "api", "model": runner.MODEL, "effort": None, "hosts": runner.PROVIDERS,
             "judge": runner.JUDGE_MODEL}

    def baseline(self, **cases):
        return {**self.SETUP, "date": "2026-10-19", "commit": "abc", "sha256": None,
                "cases": {name.replace("_", "-"): value for name, value in cases.items()}, "noBaseline": {}}

    def compared(self, base, **cases):
        run = runner.results_of(self.SETUP, "instructions", {name.replace("_", "-"): value for name, value in cases.items()})
        return runner.compare(run, base)

    def row(self, lines, name):
        return next(line for line in lines if line.startswith(name + " "))

    def test_a_move_of_one_run_is_not_marked(self):
        lines, worse = self.compared(self.baseline(advice_infill=tally(3), first_person=tally(1, 2)),
                                     advice_infill=tally(2, 1), first_person=tally(2, 1))
        self.assertEqual(worse, [])
        self.assertNotIn("WORSE", self.row(lines, "advice-infill"))
        self.assertNotIn("BETTER", self.row(lines, "first-person"))

    def test_two_runs_or_more_are_marked(self):
        lines, worse = self.compared(self.baseline(advice_infill=tally(3), first_person=tally(1, 2), issue_fix=tally(3)),
                                     advice_infill=tally(1, 2), first_person=tally(3), issue_fix=tally(0, 3))
        self.assertEqual(worse, ["advice-infill", "issue-fix"])
        self.assertIn("WORSE", self.row(lines, "advice-infill"))
        self.assertNotIn("noise", self.row(lines, "advice-infill"))
        self.assertIn("BETTER", self.row(lines, "first-person"))

    def test_a_drop_of_unobserved_runs_or_errors_may_be_noise(self):
        lines, worse = self.compared(self.baseline(advice_infill=tally(3), issue_fix=tally(3)),
                                     advice_infill=tally(1, 0, 2), issue_fix=tally(0, 3, 0, errors=2))
        self.assertEqual(worse, ["advice-infill", "issue-fix"])
        self.assertIn("may be noise: unobserved runs", self.row(lines, "advice-infill"))
        self.assertIn("may be noise: 2 request errors", self.row(lines, "issue-fix"))

    def test_several_drops_point_at_the_hosts(self):
        base = self.baseline(advice_infill=tally(3), issue_fix=tally(3), first_person=tally(3))
        lines, worse = self.compared(base, advice_infill=tally(0, 3), issue_fix=tally(0, 3), first_person=tally(0, 3))
        self.assertEqual(len(worse), 3)
        self.assertTrue(any(line.startswith("Several cases dropped") for line in lines))

    def test_another_setup_is_not_compared(self):
        base = {**self.baseline(advice_infill=tally(3)), "model": "another/model"}
        lines, worse = self.compared(base, advice_infill=tally(0, 3))
        self.assertIsNone(worse)
        self.assertTrue(lines[-1].startswith("NOT COMPARABLE: this run's model differs"), lines[-1])

    def test_a_case_without_a_baseline_says_why(self):
        base = self.baseline(advice_infill=tally(3))
        base["noBaseline"] = {"calibration-result": "added later"}
        lines, worse = self.compared(base, advice_infill=tally(3), calibration_result=tally(0, 3))
        self.assertEqual(worse, [])
        self.assertIn("no baseline: added later", self.row(lines, "calibration-result"))

    def test_a_case_that_got_no_response_measured_nothing(self):
        run = runner.results_of(self.SETUP, "instructions", {"advice-infill": tally(3),
                                                             "model-opened-unnamed": tally(0, 3, errors=3)})
        self.assertEqual(list(run["cases"]), ["advice-infill"])
        self.assertIn("model-opened-unnamed", run["noBaseline"])

    def test_a_baseline_is_recorded_from_every_case(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "baseline.json"
            partial = runner.results_of(self.SETUP, "instructions", {"advice-infill": tally(3)})
            with self.assertRaises(SystemExit):
                runner.record_baseline(path, partial)
            self.assertFalse(path.exists())
            run = runner.results_of(self.SETUP, "instructions", {name: tally(3) for name in runner.CASES})
            with contextlib.redirect_stdout(io.StringIO()):
                runner.record_baseline(path, run)
            recorded = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(recorded, run)
            self.assertEqual(runner.compare(run, recorded)[1], [])

    def test_the_stored_baselines_name_every_case_and_no_other(self):
        for backend, path in runner.BASELINES.items():
            stored = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(stored["backend"], backend)
            self.assertEqual(set(stored["cases"]) | set(stored["noBaseline"]), set(runner.CASES), path.name)
            self.assertFalse(set(stored["cases"]) & set(stored["noBaseline"]), path.name)
            for name, counts in stored["cases"].items():
                self.assertEqual(counts["runs"], counts["pass"] + counts["fail"] + counts["unobserved"], name)


if __name__ == "__main__":
    unittest.main()
