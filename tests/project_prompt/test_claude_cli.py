#!/usr/bin/env python3
"""The claude-cli backend against a fake Messages endpoint: what the model
would receive, and the step rules through real MCP calls. Runs the installed
Claude Code CLI with a dummy API key aimed at a local server, so it needs no
login, sends nothing anywhere and costs nothing. Skipped when `claude` is not
on PATH. Run it after a CLI update: the resumed session is the CLI's own
internal format.

Usage: python3 tests/project_prompt/test_claude_cli.py
"""

import http.server
import importlib.util
import json
import os
import pathlib
import shutil
import threading
import unittest

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("run_prompt_tests", HERE / "run_prompt_tests.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
cli = runner.claude_cli


class FakeMessages:
    """Answers each streamed request with the next scripted response (a list
    of content blocks) and keeps every request body."""

    def __init__(self, *responses):
        self.responses, self.bodies = list(responses), []
        fake = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_HEAD(self):
                self.send_response(200)
                self.end_headers()

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers.get("content-length", 0))))
                if not self.path.startswith("/v1/messages") or "count_tokens" in self.path or not body.get("stream"):
                    self.send_response(404)
                    self.end_headers()
                    return
                fake.bodies.append(body)
                blocks = fake.responses.pop(0) if fake.responses else [{"type": "text", "text": "(nothing scripted)"}]
                self.send_response(200)
                self.send_header("content-type", "text/event-stream")
                self.end_headers()

                def event(data):
                    self.wfile.write(f"event: {data['type']}\ndata: {json.dumps(data)}\n\n".encode())

                event({"type": "message_start", "message": {"id": f"msg_{len(fake.bodies)}", "type": "message",
                       "role": "assistant", "model": body["model"], "content": [], "stop_reason": None,
                       "usage": {"input_tokens": 10, "output_tokens": 1}}})
                for index, block in enumerate(blocks):
                    if block["type"] == "text":
                        event({"type": "content_block_start", "index": index, "content_block": {"type": "text", "text": ""}})
                        event({"type": "content_block_delta", "index": index,
                               "delta": {"type": "text_delta", "text": block["text"]}})
                    else:
                        event({"type": "content_block_start", "index": index, "content_block":
                               {"type": "tool_use", "id": f"toolu_{len(fake.bodies)}_{index}", "name": block["name"],
                                "input": {}}})
                        event({"type": "content_block_delta", "index": index,
                               "delta": {"type": "input_json_delta", "partial_json": json.dumps(block["input"])}})
                    event({"type": "content_block_stop", "index": index})
                stop = "tool_use" if any(block["type"] == "tool_use" for block in blocks) else "end_turn"
                event({"type": "message_delta", "delta": {"stop_reason": stop, "stop_sequence": None},
                       "usage": {"output_tokens": 5}})
                event({"type": "message_stop"})

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def close(self):
        self.server.shutdown()
        self.server.server_close()


def tool(name, arguments):
    return {"type": "tool_use", "name": cli.PREFIX + name, "input": arguments}


def plain(value):
    """A request's content without the CLI's cache markers."""
    if isinstance(value, list):
        return [plain(item) for item in value]
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items() if key != "cache_control"}
    return value


def conversation(body):
    """A request's messages without the CLI's own system turns (its
    environment note, and an empty one carrying the effort setting)."""
    return [message for message in plain(body["messages"]) if message["role"] != "system"]


def texts(body):
    for message in body["messages"]:
        content = message["content"]
        for block in [{"text": content}] if isinstance(content, str) else content:
            yield block.get("text") or json.dumps(block.get("content", ""))


class Split(unittest.TestCase):
    def test_every_recorded_point_splits(self):
        """Every point becomes a session and a last message, or a session
        ending on one deferred call; mid-turn points are the deferred ones."""
        for name, request in runner.REQUESTS.items():
            items = request["body"]["input"]
            history, last, deferred = cli.split(items)
            last_item = [item for item in items if item.get("role") != "developer"][-1]
            self.assertEqual(deferred is not None, last_item.get("type") == "function_call_output", name)
            self.assertEqual(last is None, deferred is not None, name)
            cli.messages(history + (last or []))  # every item has a Messages form


@unittest.skipIf(shutil.which("claude") is None, "the Claude Code CLI is not installed")
class ThroughTheCli(unittest.TestCase):
    def run_case(self, name, *responses):
        fake = FakeMessages(*responses)
        os.environ[cli.TEST_ENDPOINT_ENV] = fake.url
        try:
            verdict, detail, _log = runner.run_once_claude(name, "INSTRUCTIONS", "claude-opus-5-5", "medium")
        finally:
            del os.environ[cli.TEST_ENDPOINT_ENV]
            fake.close()
        return verdict, detail, fake.bodies

    def assertIsolated(self, body, tools):
        self.assertEqual(body["system"][-1]["text"], "INSTRUCTIONS")
        self.assertEqual([plain(t) for t in body["tools"]],
                         [{"name": cli.PREFIX + t["name"], "description": t["description"], "input_schema": t["parameters"]}
                          for t in tools])
        for text in texts(body):  # nothing of the developer's own Claude Code setup
            self.assertNotIn("CLAUDE.md", text)
            self.assertNotIn("Output Style", text)

    def test_a_point_mid_turn(self):
        """after-preview: the turn's calls and results are replayed as real
        turns, the last call's result comes through the stub, a read the model
        makes is answered from the recording, and its change ends the run."""
        request = runner.REQUESTS["after-preview"]
        preview = runner.last_result(request)["result"]
        verdict, detail, bodies = self.run_case(
            "after-preview", [tool("workspace_inspect", {})],
            [tool("settings_apply_patch", {"expectedSessionId": preview["sessionId"],
                                           "expectedRevision": preview["revision"], "changes": {}, "scope": "process"})])
        self.assertEqual(verdict, runner.PASS, detail)
        self.assertEqual(len(bodies), 2)
        self.assertIsolated(bodies[0], runner.TOOLS)
        history, _, deferred = cli.split(request["body"]["input"])
        expected = cli.messages(history)
        sent = conversation(bodies[0])
        self.assertEqual(sent[:len(expected)], expected)
        result = sent[len(expected)]["content"][0]
        self.assertEqual(result["tool_use_id"], deferred[0]["call_id"])
        self.assertEqual([block["text"] for block in result["content"]], deferred[1])
        answered = conversation(bodies[1])[-1]["content"][-1]
        recorded = next(answer["output"] for answer in request["answers"] if answer["tool"] == "workspace_inspect")
        self.assertEqual([block["text"] for block in answered["content"]], [recorded])

    def test_a_point_on_a_new_message(self):
        """model-opened-unnamed: the person's message goes in whole, the
        plate's picture as an image and the skill as a developer message, and
        the subscription judge reads the reply."""
        runner.CLAUDE_JUDGE = ("claude-opus-5-5", "medium")
        try:
            verdict, detail, bodies = self.run_case(
                "model-opened-unnamed", [{"type": "text", "text": "That looks like a rabbit. What is it for?"}],
                [{"type": "tool_use", "name": cli.STRUCTURED_OUTPUT, "input": {"answer": "yes", "why": "a rabbit"}}])
        finally:
            runner.CLAUDE_JUDGE = None
        self.assertEqual(verdict, runner.PASS, detail)
        self.assertIsolated(bodies[0], runner.TOOLS)
        _, last, _ = cli.split(runner.REQUESTS["model-opened-unnamed"]["body"]["input"])
        sent = conversation(bodies[0])
        self.assertEqual(sent, cli.messages(last))
        self.assertEqual([block["type"] for block in sent[0]["content"]], ["text", "text", "image", "text"])
        self.assertTrue(sent[0]["content"][-1]["text"].startswith("<developer_message>\n"))
        self.assertEqual(bodies[1]["system"][-1]["text"].split(",")[0], "You read a chat between a 3D printing app's assistant and a person")


if __name__ == "__main__":
    unittest.main()
