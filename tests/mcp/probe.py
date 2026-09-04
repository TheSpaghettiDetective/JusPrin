#!/usr/bin/env python3
"""Small, dependency-free 2026-07-28 MCP client for deliberate local testing.

This is a wire smoke client, not a substitute for the Phase 4 Inspector/production-client
and conformance tests.
"""

import argparse
from contextlib import closing
import http.client
import json
from urllib.parse import urlsplit

PROTOCOL_VERSION = "2026-07-28"


def call(endpoint, method, params=None):
    params = dict(params or {})
    params["_meta"] = {
        "io.modelcontextprotocol/protocolVersion": PROTOCOL_VERSION,
        "io.modelcontextprotocol/clientInfo": {"name": "jusprin-smoke", "version": "1"},
        "io.modelcontextprotocol/clientCapabilities": {},
        "progressToken": "smoke-progress",
    }
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
        "MCP-Protocol-Version": PROTOCOL_VERSION,
        "Mcp-Method": method,
    }
    if "name" in params:
        headers["Mcp-Name"] = params["name"]
    with closing(http.client.HTTPConnection(endpoint.hostname, endpoint.port, timeout=310)) as connection:
        connection.request("POST", endpoint.path, json.dumps({
            "jsonrpc": "2.0", "id": "smoke", "method": method, "params": params,
        }), headers)
        response = connection.getresponse()
        if response.getheader("Content-Type", "").startswith("text/event-stream"):
            for line in response:
                if line.startswith(b"data: "):
                    message = json.loads(line[6:])
                    if "result" in message or "error" in message:
                        return message
                    print("Progress:", message.get("params", {}).get("message", ""))
            raise RuntimeError("MCP stream closed without a terminal response")
        return json.loads(response.read())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="URL shown by the native MCP connection button")
    parser.add_argument("--duplicate-object-id", help="Request a duplicate; approve or reject it inside JusPrin")
    args = parser.parse_args()
    endpoint = urlsplit(args.url)
    if (endpoint.scheme != "http" or endpoint.hostname != "127.0.0.1" or
            not endpoint.port or endpoint.path != "/mcp" or endpoint.query or endpoint.fragment or endpoint.username):
        parser.error("Use the exact http://127.0.0.1:PORT/mcp local diagnostic URL")
    for method in ("server/discover", "tools/list"):
        print(json.dumps(call(endpoint, method), indent=2))
    workspace = call(endpoint, "tools/call", {"name": "workspace_inspect"})
    print(json.dumps(workspace, indent=2))
    print(json.dumps(call(endpoint, "tools/call", {"name": "inspect_selection"}), indent=2))
    if args.duplicate_object_id:
        session = workspace["result"]["structuredContent"]["sessionId"]
        print("Approve or reject the proposed duplicate in the JusPrin Agent panel.")
        print(json.dumps(call(endpoint, "tools/call", {
            "name": "duplicate_object",
            "arguments": {"sessionId": session, "objectId": args.duplicate_object_id},
        }), indent=2))


if __name__ == "__main__":
    main()
