#!/usr/bin/env python3
"""The project tools as a stdio MCP server, for run_prompt_tests.py's
claude-cli backend. The Claude Code CLI starts it; the model's calls reach it
as MCP tools/call requests, and each is passed to the runner over a Unix
socket, which answers with the tool's result (the recorded answer for a read)
or never answers (a change: the runner ends the run instead).

Usage (written into the --mcp-config the runner generates):
  claude_mcp_stub.py TOOLS_JSON SOCKET_PATH

TOOLS_JSON is the case's tool list in the app's Responses API form
(project_tools.json, or a recorded point's own list). Only the standard
library: the CLI may start it with any python3."""

import json
import socket
import sys


def answer(socket_path, name, arguments):
    """The runner's result for one call: the text the app would return, or
    a list of texts (a result followed by the app's notes)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.connect(socket_path)
        connection.sendall((json.dumps({"name": name, "arguments": arguments}) + "\n").encode())
        reply = connection.makefile("r", encoding="utf-8").readline()
    if not reply:
        raise ConnectionError("the runner closed the call without a result")
    return json.loads(reply)["output"]


def main():
    tools_path, socket_path = sys.argv[1], sys.argv[2]
    with open(tools_path, encoding="utf-8") as file:
        tools = [{"name": tool["name"], "description": tool["description"], "inputSchema": tool["parameters"]}
                 for tool in json.load(file)]
    out = sys.stdout

    def send(message):
        out.write(json.dumps(message, ensure_ascii=False) + "\n")
        out.flush()

    for line in sys.stdin:
        if not line.strip():
            continue
        message = json.loads(line)
        method, request_id = message.get("method"), message.get("id")
        if request_id is None:
            continue  # a notification (initialized, cancelled): nothing to answer
        if method == "initialize":
            result = {"protocolVersion": message["params"].get("protocolVersion", "2025-06-18"),
                      "capabilities": {"tools": {}}, "serverInfo": {"name": "jusprin-project-tools", "version": "1"}}
        elif method == "tools/list":
            result = {"tools": tools}
        elif method == "tools/call":
            params = message["params"]
            try:
                output = answer(socket_path, params["name"], params.get("arguments") or {})
                texts = output if isinstance(output, list) else [output]
                result = {"content": [{"type": "text", "text": text} for text in texts]}
            except (OSError, ValueError) as error:
                result = {"content": [{"type": "text", "text": f"stub error: {error}"}], "isError": True}
        elif method == "ping":
            result = {}
        else:
            send({"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": f"no method {method}"}})
            continue
        send({"jsonrpc": "2.0", "id": request_id, "result": result})


if __name__ == "__main__":
    main()
