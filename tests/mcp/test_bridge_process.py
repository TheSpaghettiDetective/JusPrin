"""Exercise the shipped executable over real stdin/stdout and local HTTP.

These wire/process tests complement the C++ coordinator and native model tests;
the fake HTTP peer here is not evidence of an actual AI client's integration.
"""
import base64
import http.server
import json
import os
from pathlib import Path
import queue
import select
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = Path(os.environ.get("JUSPRIN_TEST_BRIDGE", ROOT / "build/arm64/src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/jusprin-mcp"))
VERSION = "2026-07-28"


def request(identifier, method, params=None, modern=False):
    params = dict(params or {})
    if modern:
        params.setdefault("_meta", {}).update({"io.modelcontextprotocol/protocolVersion": VERSION,
                                              "io.modelcontextprotocol/clientCapabilities": {}})
    return {"jsonrpc": "2.0", "id": identifier, "method": method, "params": params}


class Child:
    def __init__(self, path, env=None, command=None):
        self.errors = tempfile.TemporaryFile()
        environment = dict(os.environ)
        environment.pop("JUSPRIN_MCP_URL", None)
        environment.pop("JUSPRIN_MCP_DISCOVERY", None)
        environment.update(env or {})
        self.process = subprocess.Popen([*(command or [str(BRIDGE)]), "--discovery", str(path)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.errors, env=environment)
        self.messages = queue.Queue()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        try:
            for line in self.process.stdout:
                self.messages.put(json.loads(line))
        except Exception as error:
            self.messages.put(error)

    def send(self, value):
        self.process.stdin.write(json.dumps(value).encode() + b"\n")
        self.process.stdin.flush()

    def receive(self):
        message = self.messages.get(timeout=3)
        if isinstance(message, Exception):
            raise message
        return message

    def initialize(self, version="2025-06-18"):
        self.send(request(0, "initialize", {"protocolVersion": version, "capabilities": {},
                                           "clientInfo": {"name": "process-test", "version": "1"}}))
        response = self.receive()
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return response

    def stop(self):
        if not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            result = self.process.wait(timeout=3)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.reader.join(timeout=2)
            self.process.stdout.close()
        self.errors.seek(0)
        diagnostic = self.errors.read().decode()
        self.errors.close()
        return result, diagnostic


class Peer:
    def __init__(self, path):
        self.requests = []
        self.pending = threading.Event()
        self.cancelled = threading.Event()
        self.release = threading.Event()
        self.sockets = []
        self.response_transform = None
        self.catalog_padding = ""
        peer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                peer.requests.append((dict(self.headers), body))
                result = {"resultType": "complete", "_meta": {"io.modelcontextprotocol/serverInfo": {"name": "jusprin", "version": "peer"}}}
                if body["method"] == "server/discover":
                    result.update({"supportedVersions": [VERSION], "capabilities": {"tools": {}}, "ttlMs": 0, "cacheScope": "private"})
                elif body["method"] == "tools/list":
                    result.update({"tools": [{"name": "live_catalog"}], "ttlMs": 0, "cacheScope": "private"})
                    if peer.catalog_padding:
                        result["tools"][0]["description"] = peer.catalog_padding
                else:
                    content = {"peer": "live", "name": body["params"]["name"]}
                    result.update({"content": [{"type": "text", "text": json.dumps(content)}], "structuredContent": content, "isError": False})
                response = {"jsonrpc": "2.0", "id": body["id"], "result": result}
                if body["method"] == "tools/call" and peer.response_transform:
                    response = peer.response_transform(response)
                self.send_response(200)
                if body.get("params", {}).get("name") == "hold":
                    self.send_header("Content-Type", "text/event-stream")
                    self.end_headers()
                    peer.sockets.append(self.connection)
                    progress = {"jsonrpc": "2.0", "method": "notifications/progress", "params": {
                        "progressToken": body["params"]["_meta"]["progressToken"], "progress": 0, "message": "Awaiting approval"}}
                    self.wfile.write(b": keepalive\r\nevent: message\r\n" + b"\r\n".join(
                        b"data: " + line.encode() for line in json.dumps(progress, indent=2).splitlines()) + b"\r\n\r\n")
                    self.wfile.flush()
                    peer.pending.set()
                    while not peer.release.wait(0.01):
                        readable, _, _ = select.select([self.connection], [], [], 0)
                        if readable and not self.connection.recv(1, socket.MSG_PEEK):
                            peer.cancelled.set()
                            return
                    try:
                        self.wfile.write(b"data: " + json.dumps(response).encode() + b"\n\n")
                        self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError):
                        pass
                else:
                    self.send_header("Content-Type", "application/json")
                    data = json.dumps(response).encode()
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_port}/mcp"
        path.write_text(json.dumps({"schemaVersion": 1, "url": self.url, "pid": os.getpid(), "instanceId": "peer",
                                    "appVersion": "peer-version", "protocolVersions": [VERSION], "startedAt": "2026-09-03T00:00:00Z"}))

    def stop(self):
        self.release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


@unittest.skipUnless(BRIDGE.is_file(), "Build jusprin-mcp or set JUSPRIN_TEST_BRIDGE")
class BridgeProcessTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="jusprin-bridge-process-")
        self.path = Path(self.directory.name) / "discovery 打印.json"
        self.child = None
        self.peer = None

    def tearDown(self):
        if self.child is not None:
            self.assertEqual(self.child.stop()[0], 0)
        if self.peer is not None:
            self.peer.stop()
        self.directory.cleanup()

    def test_offline_versions_and_protocol_errors(self):
        for version in ("2025-03-26", "2025-06-18", "2025-11-25", "not-supported"):
            with self.subTest(version=version):
                self.child = Child(self.path)
                expected = "2025-06-18" if version == "not-supported" else version
                self.assertEqual(self.child.initialize(version)["result"]["protocolVersion"], expected)
                self.child.send(request(1, "tools/list"))
                result = self.child.receive()["result"]
                self.assertEqual(len(result["tools"]), 5)
                self.assertNotIn("ttlMs", result)
                self.child.send(request(2, "tools/call", {"name": "workspace_inspect"}))
                result = self.child.receive()["result"]
                self.assertTrue(result["isError"])
                self.assertEqual(json.loads(result["content"][0]["text"])["error"]["code"], "workspace_unavailable")
                self.assertEqual("structuredContent" in result, expected != "2025-03-26")
                self.assertEqual(self.child.stop()[0], 0)
                self.child = None

    def test_one_process_follows_app_start_stop_and_replacement(self):
        self.child = Child(self.path)
        self.child.initialize()
        original_pid = self.child.process.pid

        def inspect(identifier, expected):
            self.child.send(request(identifier, "tools/call", {"name": "workspace_inspect"}))
            result = self.child.receive()["result"]
            if expected == "offline":
                self.assertTrue(result["isError"])
                self.assertEqual(result["structuredContent"]["error"]["code"], "workspace_unavailable")
            else:
                self.assertFalse(result["isError"])
                self.assertEqual(result["structuredContent"]["peer"], "live")

        inspect(1, "offline")
        self.peer = Peer(self.path)
        inspect(2, "live")
        self.peer.stop()
        self.peer = None
        # Leave the real discovery file behind, with a live PID but a closed
        # endpoint. File/PID validation alone must not classify it as live.
        inspect(3, "offline")
        self.peer = Peer(self.path)
        inspect(4, "live")
        self.assertEqual(self.child.process.pid, original_pid)
        self.assertIsNone(self.child.process.poll())
        self.assertEqual(sum(body["method"] == "tools/call" for _, body in self.peer.requests), 1)

    def test_live_catalog_header_mirroring_and_version_warning(self):
        self.peer = Peer(self.path)
        self.child = Child(self.path)
        self.child.initialize()
        self.child.send(request(1, "tools/list"))
        self.assertEqual(self.child.receive()["result"]["tools"], [{"name": "live_catalog"}])
        self.child.send(request(2, "tools/call", {"name": "打印"}, modern=True))
        result = self.child.receive()["result"]
        self.assertEqual(result["structuredContent"]["name"], "打印")
        headers, body = self.peer.requests[-1]
        folded = {key.lower(): value for key, value in headers.items()}
        self.assertNotIn("authorization", folded)
        self.assertEqual(folded["mcp-name"], "=?base64?" + base64.b64encode("打印".encode()).decode() + "?=")
        self.assertEqual(folded["mcp-protocol-version"], body["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"])
        status, diagnostic = self.child.stop()
        self.child = None
        self.assertEqual(status, 0)
        self.assertIn("versions differ", diagnostic)

    def test_progress_and_terminal_sse(self):
        self.peer = Peer(self.path)
        self.child = Child(self.path)
        self.child.initialize()
        self.child.send(request(1, "tools/call", {"name": "hold", "_meta": {"progressToken": "progress-1"}}))
        self.assertEqual(self.child.receive()["params"]["progressToken"], "progress-1")
        self.peer.release.set()
        self.assertFalse(self.child.receive()["result"]["isError"])

    def test_malformed_peer_result_becomes_an_error_not_invalid_stdout(self):
        self.peer = Peer(self.path)
        self.peer.response_transform = lambda response: dict(response, result=[])
        self.child = Child(self.path)
        self.child.send(request(1, "tools/call", {"name": "read"}, modern=True))
        result = self.child.receive()["result"]
        self.assertIsInstance(result, dict)
        self.assertTrue(result["isError"])
        self.assertEqual(result["structuredContent"]["error"]["code"], "connection_lost")

    def test_client_cancellation_and_eof_close_http(self):
        for modern in (False, True):
            with self.subTest(modern=modern):
                self.peer = Peer(self.path)
                self.child = Child(self.path)
                if not modern:
                    self.child.initialize()
                self.child.send(request(1, "tools/call", {"name": "hold", "_meta": {"progressToken": 1}}, modern))
                self.assertEqual(self.child.receive()["method"], "notifications/progress")
                self.child.send({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 1}})
                self.assertTrue(self.peer.cancelled.wait(2))
                self.child.send(request(2, "ping"))
                self.assertEqual(self.child.receive()["id"], 2)
                self.assertEqual(self.child.stop()[0], 0)
                self.child = None
                self.peer.stop()
                self.peer = None

    def test_eof_with_pending_call(self):
        self.peer = Peer(self.path)
        self.child = Child(self.path)
        self.child.initialize()
        self.child.send(request(1, "tools/call", {"name": "hold", "_meta": {"progressToken": 1}}))
        self.child.receive()
        self.assertEqual(self.child.stop()[0], 0)
        self.child = None
        self.assertTrue(self.peer.cancelled.wait(2))

    def test_oversized_and_fragmented_input_recovers(self):
        self.child = Child(self.path)
        self.child.process.stdin.write(b"x" * (64 * 1024 + 10) + b"\n")
        self.child.process.stdin.flush()
        self.assertEqual(self.child.receive()["error"]["code"], -32600)
        data = json.dumps(request(1, "server/discover", modern=True)).encode() + b"\n"
        for index in range(0, len(data), 3):
            self.child.process.stdin.write(data[index:index + 3])
            self.child.process.stdin.flush()
        self.assertEqual(self.child.receive()["result"]["supportedVersions"], [VERSION])

    def test_url_override_replaces_a_missing_discovery_file(self):
        self.peer = Peer(self.path)
        self.path.unlink()
        self.child = Child(self.path, {"JUSPRIN_MCP_URL": self.peer.url})
        self.child.initialize()
        self.child.send(request(1, "tools/list"))
        self.assertEqual(self.child.receive()["result"]["tools"][0]["name"], "live_catalog")

    def test_eof_does_not_hang_behind_blocked_stdout(self):
        process = subprocess.Popen([str(BRIDGE), "--discovery", str(self.path)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            for index in range(12):
                process.stdin.write(json.dumps(request(index, "tools/list", modern=True)).encode() + b"\n")
            process.stdin.flush()
            time.sleep(0.1)  # let responses fill the unread OS pipe
            process.stdin.close()
            self.assertEqual(process.wait(timeout=3), 0)
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            process.stdout.close()

    def test_broken_stdout_exits_without_waiting_for_more_input(self):
        process = subprocess.Popen([str(BRIDGE), "--discovery", str(self.path)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            process.stdout.close()
            process.stdin.write(json.dumps(request(1, "server/discover", modern=True)).encode() + b"\n")
            process.stdin.flush()
            self.assertEqual(process.wait(timeout=3), 1)
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            process.stdin.close()

    def test_cancellation_discards_progress_queued_behind_backpressure(self):
        self.peer = Peer(self.path)
        self.peer.catalog_padding = "x" * 32768
        process = subprocess.Popen([str(BRIDGE), "--discovery", str(self.path)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        reader = None
        try:
            # Large replies fill stdout without flooding the peer's accept
            # queue. Await actual HTTP delivery before sending the next call.
            for index in range(8):
                process.stdin.write(json.dumps(request(100 + index, "tools/list", modern=True)).encode() + b"\n")
                process.stdin.flush()
                deadline = time.monotonic() + 3
                while sum(body["method"] == "tools/list" for _, body in self.peer.requests) < index + 1:
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.005)
            process.stdin.write(json.dumps(request(1, "tools/call", {"name": "hold", "_meta": {"progressToken": 1}}, True)).encode() + b"\n")
            process.stdin.flush()
            self.assertTrue(self.peer.pending.wait(2))
            time.sleep(0.1)  # the progress event is now behind the blocked pipe
            process.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 1}}).encode() + b"\n")
            process.stdin.write(json.dumps(request(999, "ping")).encode() + b"\n")
            process.stdin.flush()
            self.assertTrue(self.peer.cancelled.wait(2))
            messages = queue.Queue()
            def consume():
                for line in process.stdout:
                    messages.put(json.loads(line))
            reader = threading.Thread(target=consume, daemon=True)
            reader.start()
            progress = []
            while True:
                message = messages.get(timeout=3)
                if message.get("method") == "notifications/progress":
                    progress.append(message)
                if message.get("id") == 999:
                    break
            self.assertEqual(progress, [])
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
            if reader:
                reader.join(timeout=2)
            process.stdout.close()


if __name__ == "__main__":
    unittest.main()
