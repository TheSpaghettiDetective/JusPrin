"""Offline checks for the smoke harness's boundaries, not assistant prose."""
import unittest
from unittest.mock import patch

import run_prompt_tests as runner


def response(*items):
    return {"status": "completed", "output": list(items),
            "usage": {"input_tokens": 20, "output_tokens": 5}}


def call(name, arguments):
    return {"type": "function_call", "call_id": "test-call", "name": name,
            "arguments": runner.dump(arguments)}


class SmokeRunnerTest(unittest.TestCase):
    def test_skill_body_round_trip_and_stop_before_domain_result(self):
        prompt = {"instructions": "index", "coreInstructions": "core",
                  "skills": [{"name": "tune-print", "text": "# Actual skill body"}]}
        case = {"name": "tuning", "query": "Make it stronger", "expectedSkills": ["tune-print"]}
        requests = []

        def post(body, *_):
            requests.append(runner.json.loads(runner.dump(body)))
            if len(requests) == 1:
                return response(call("skill_read", {"name": "tune-print"}))
            return response(call("workspace_inspect", {}))

        with patch.object(runner, "post", side_effect=post):
            result = runner.run_once(case, prompt, "unused", "test-model", "unused", [], True)
        self.assertEqual(len(requests), 2)
        tool_output = runner.json.loads(requests[1]["input"][-1]["output"])
        self.assertEqual(tool_output["result"]["text"], "# Actual skill body")
        self.assertEqual(result["routing"], "observed")
        self.assertEqual(result["total_input_tokens"], 40)
        self.assertEqual(result["total_output_tokens"], 10)

    def test_an_initial_read_leaves_routing_pending(self):
        case = {"expectedSkills": ["tune-print"], "readOnly": True}
        passed, routing = runner.evaluate(case, [], ["workspace_inspect"], "", True)
        self.assertTrue(passed)
        self.assertEqual(routing, "pending")

    def test_advice_allows_reads_but_not_project_writes(self):
        case = {"readOnly": True}
        self.assertTrue(runner.evaluate(case, [], ["settings_get"], "", True)[0])
        self.assertFalse(runner.evaluate(case, [], ["settings_apply_patch"], "", True)[0])
        self.assertTrue(runner.evaluate(case, [], [], "Two useful related questions? And another?", True)[0])

    def test_repeated_skill_reads_hit_the_bound_without_claiming_completion(self):
        prompt = {"instructions": "index", "coreInstructions": "core",
                  "skills": [{"name": "tune-print", "text": "# Body"}]}
        case = {"name": "loop", "query": "Tune it", "expectedSkills": ["tune-print"]}
        with patch.object(runner, "post", return_value=response(call("skill_read", {"name": "tune-print"}))) as post:
            result = runner.run_once(case, prompt, "unused", "test-model", "unused", [], True)
        self.assertEqual(post.call_count, runner.MAX_REQUESTS_PER_TURN)
        self.assertFalse(result["passed"])


class CapturedWorkspaceTest(unittest.TestCase):
    def setUp(self):
        self.fixture = runner.json.loads((runner.HERE / "fixtures/tee-workspace.json").read_text())

    def test_section_projection_and_identity(self):
        identity = {key: self.fixture["sections"]["summary"][key]
                    for key in ("sessionId", "revision", "truncated")}
        self.assertEqual(runner.replay(self.fixture, {"sections": []}), identity)
        self.assertEqual(runner.replay(self.fixture, {}), self.fixture["sections"]["summary"])
        result = runner.replay(self.fixture, {"sections": ["objects", "printer"]})
        self.assertEqual(set(result), set(identity) | {"objects", "printer"})
        self.assertEqual(result["objects"], self.fixture["sections"]["objects"]["objects"])
        self.assertEqual(result["printer"]["confirmedFacts"], [])
        self.assertIsNone(runner.replay(self.fixture, {"sections": ["activities"]}))
        self.assertIsNone(runner.replay(self.fixture, {"sections": ["made-up"]}))

    def test_real_workspace_then_skill_then_unrecorded_read(self):
        prompt = {"instructions": "index", "coreInstructions": "core",
                  "skills": [{"name": "tune-print", "text": "# Actual body"}]}
        case = {"name": "tuning", "query": "Make it stronger", "expectedSkills": ["tune-print"]}
        requests = []
        outputs = [call("workspace_inspect", {"sections": ["printer", "objects"]}),
                   call("skill_read", {"name": "tune-print"}),
                   call("settings_get", {"scope": "process", "keys": ["wall_loops"]})]

        def post(body, *_):
            requests.append(runner.json.loads(runner.dump(body)))
            return response(outputs[len(requests) - 1])

        with patch.object(runner, "post", side_effect=post):
            result = runner.run_once(case, prompt, "unused", "test", "unused", [], True, self.fixture)
        self.assertEqual(result["routing"], "observed")
        self.assertEqual(result["stop_reason"], "unrecorded_read")
        self.assertEqual(result["workspace_reads"], 1)
        self.assertEqual(len(requests), 3)
        read = runner.json.loads(requests[1]["input"][-1]["output"])
        self.assertEqual(read["result"], runner.replay(self.fixture, {"sections": ["printer", "objects"]}))
        self.assertNotIn("workspace", read)  # Do not fake the full native UI context.
        self.assertEqual(runner.json.loads(requests[2]["input"][-1]["output"])["result"]["text"], "# Actual body")

    def test_mutation_is_never_replayed_and_violates_advice_scope(self):
        prompt = {"instructions": "index", "coreInstructions": "core", "skills": []}
        case = {"name": "advice", "query": "Why warp?", "readOnly": True}
        with patch.object(runner, "post", return_value=response(call("slice_start", {}))) as post:
            result = runner.run_once(case, prompt, "unused", "test", "unused", [], True, self.fixture)
        self.assertEqual(post.call_count, 1)
        self.assertFalse(result["passed"])
        self.assertEqual(result["stop_reason"], "mutation_boundary")

    def test_final_reply_after_inspection_does_not_leave_routing_pending(self):
        prompt = {"instructions": "index", "coreInstructions": "core", "skills": []}
        case = {"name": "tuning", "query": "Make it stronger", "expectedSkills": ["tune-print"]}
        outputs = [response(call("workspace_inspect", {})), response({"type": "message", "content": [
            {"type": "output_text", "text": "Which direction does the force act?"}]})]
        with patch.object(runner, "post", side_effect=outputs):
            result = runner.run_once(case, prompt, "unused", "test", "unused", [], True, self.fixture)
        self.assertEqual(result["routing"], "missed")  # An observation, not an automatic scope failure.
        self.assertTrue(result["passed"])


if __name__ == "__main__":
    unittest.main()
