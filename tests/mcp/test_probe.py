import contextlib
import http.server
import io
import json
import threading
import unittest
from urllib.parse import urlsplit

import probe


class ProbeTests(unittest.TestCase):
    def test_json_and_request_scoped_sse(self):
        received = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                received.append((dict(self.headers), body))
                result = {"jsonrpc": "2.0", "id": body["id"], "result": {"resultType": "complete"}}
                self.send_response(200)
                streaming = body["method"] == "tools/call"
                self.send_header("Content-Type", "text/event-stream" if streaming else "application/json")
                self.end_headers()
                if streaming:
                    self.wfile.write(b'event: message\ndata: {"method":"notifications/progress","params":{"message":"Pending"}}\n\n')
                    self.wfile.write(b"event: message\ndata: " + json.dumps(result).encode() + b"\n\n")
                else:
                    self.wfile.write(json.dumps(result).encode())

        server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever)
        worker.start()
        try:
            endpoint = urlsplit(f"http://127.0.0.1:{server.server_port}/mcp")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                for method, params in (("server/discover", {}), ("tools/call", {"name": "workspace_inspect"})):
                    self.assertEqual(probe.call(endpoint, method, params)["result"]["resultType"], "complete")
            self.assertIn("Pending", output.getvalue())
            self.assertEqual(len(received), 2)
            for headers, body in received:
                self.assertEqual(headers["Mcp-Method"], body["method"])
                self.assertEqual(headers["MCP-Protocol-Version"], body["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"])
                self.assertNotIn("Authorization", headers)
            self.assertEqual(received[1][0]["Mcp-Name"], "workspace_inspect")
        finally:
            server.shutdown()
            worker.join()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
