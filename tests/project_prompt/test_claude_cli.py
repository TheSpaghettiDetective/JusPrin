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


READ = 'Web search results for query: "A1 mini LAN Only Mode"\n\nLinks: [{"title":"Wiki","url":"https://wiki.example/lan"}]' \
       '\n\nLAN Only Mode is under Settings on the printer\'s screen.'


def cli_search():
    """The CLI's search, as output_items makes it from the result the CLI
    reports and the text the model read."""
    result = {"type": "web_search", "query": "A1 mini LAN Only Mode", "read": READ,
              "results": [{"tool_use_id": "srvtoolu_1", "content": [{"title": "Wiki", "url": "https://wiki.example/lan"}]},
                          "LAN Only Mode is under Settings on the printer's screen."]}
    [item] = cli.output_items([{"type": "tool_use", "id": "t1", "name": cli.WEB_SEARCH, "input": {"query": "x"}}, result])
    return item


class WebSearch(unittest.TestCase):
    def test_a_search_is_a_line_of_the_transcript(self):
        """The judge reads what the model read: the CLI's whole result, or
        every page OpenRouter cites; other items are not searches."""
        self.assertEqual(cli.web_search_line(cli_search()), "web search: A1 mini LAN Only Mode -> " + READ)
        pages = [{"type": "url", "url": f"https://wiki.example/{n}"} for n in range(7)]
        openrouter = {"type": "openrouter:web_search", "status": "completed",
                      "action": {"type": "search", "query": "A1 mini LAN Only Mode", "sources": pages}}
        self.assertEqual(cli.web_search_line(openrouter), "web search: A1 mini LAN Only Mode -> " +
                         ", ".join(page["url"] for page in pages))
        self.assertIsNone(cli.web_search_line({"type": "function_call", "name": "printer_add"}))
        self.assertIsNone(cli.web_search_line({"role": "assistant", "content": "Done."}))

    def test_a_search_is_replayed_as_the_clis_own(self):
        """A search in the history goes back as the CLI's WebSearch call and
        the result the model read, before the reply that used it."""
        reply = {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "Found it."}]}
        self.assertEqual(cli.messages([{"role": "user", "content": "where?"}, cli_search(), reply]),
                         [{"role": "user", "content": [{"type": "text", "text": "where?"}]},
                          {"role": "assistant", "content": [{"type": "tool_use", "id": "toolu_search_1", "name": cli.WEB_SEARCH,
                                                             "input": {"query": "A1 mini LAN Only Mode"}}]},
                          {"role": "user", "content": [{"type": "tool_result", "tool_use_id": "toolu_search_1",
                                                        "content": READ}]},
                          {"role": "assistant", "content": [{"type": "text", "text": "Found it."}]}])


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
        # The CLI's own search, as the app offers its provider's, then the
        # case's tools and nothing else.
        self.assertEqual(body["tools"][0]["name"], cli.WEB_SEARCH)
        self.assertEqual([plain(t) for t in body["tools"][1:]],
                         [{"name": cli.PREFIX + t["name"], "description": t["description"], "input_schema": t["parameters"]}
                          for t in tools])
        for text in texts(body):  # nothing of the developer's own Claude Code setup
            self.assertNotIn("CLAUDE.md", text)
            self.assertNotIn("Output Style", text)

    def test_a_search_in_the_history_is_resumed(self):
        """A history holding a search (the printer chat's next turn) resumes:
        the model gets the CLI's own call and the result it read before the
        reply that used it."""
        search = cli_search()
        items = [{"role": "user", "content": "where is LAN mode?"}, search,
                 {"role": "assistant", "content": "Under Settings, says the wiki."},
                 {"role": "user", "content": [{"type": "input_text", "text": "found it"}]}]
        fake = FakeMessages([{"type": "text", "text": "Good."}])
        os.environ[cli.TEST_ENDPOINT_ENV] = fake.url
        try:
            with cli.Run("INSTRUCTIONS", [], items, "claude-opus-5-5", "medium") as run:
                [reply], _ = run.next_response()
        finally:
            del os.environ[cli.TEST_ENDPOINT_ENV]
            fake.close()
        self.assertEqual(reply["content"][0]["text"], "Good.")
        sent = conversation(fake.bodies[0])
        self.assertEqual(sent[1], {"role": "assistant", "content": [
            {"type": "tool_use", "id": "toolu_search_1", "name": cli.WEB_SEARCH, "input": {"query": "A1 mini LAN Only Mode"}}]})
        self.assertEqual(sent[2]["content"][0]["type"], "tool_result")
        self.assertIn("LAN Only Mode is under Settings", json.dumps(sent[2]["content"][0]["content"]))
        self.assertEqual(sent[3]["content"][0]["text"], "Under Settings, says the wiki.")

    def test_a_point_mid_turn(self):
        """after-preview: the turn's calls and results are replayed as real
        turns, the last call's result comes through the stub, a read the model
        makes is answered from the recording, and its change ends the run."""
        request = runner.REQUESTS["after-preview"]
        preview = runner.last_result(request)["result"]
        verdict, detail, bodies = self.run_case(
            "after-preview", [tool("workspace_inspect", {})],
            [tool("settings_apply_patch", {"expectedSessionId": preview["sessionId"],
                                           "expectedRevision": preview["revision"], "scope": "process",
                                           "changes": {change["key"]: change["after"] for change in preview["changes"]}})])
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

    def test_a_refused_call_gets_the_apps_refusal_through_the_stub(self):
        """advice-infill: a settings_search with a sessionId it does not take
        reaches the model as the app's refusal, through the stub, and the
        reply that follows is the step."""
        verdict, detail, bodies = self.run_case(
            "advice-infill", [tool("settings_search", {"scope": "process", "query": "infill", "sessionId": "1"})],
            [{"type": "text", "text": "Use 15% infill."}])
        self.assertEqual(verdict, runner.PASS, detail)
        refused = json.loads(conversation(bodies[1])[-1]["content"][-1]["content"][0]["text"])
        self.assertEqual(refused["error"]["code"], "invalid_arguments")
        self.assertIn("Not a parameter of this tool: sessionId.", refused["error"]["message"])

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
