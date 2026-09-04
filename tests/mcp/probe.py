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
    parser.add_argument("--changes", help='JSON process patch, e.g. {"wall_loops":4}; approve or reject in JusPrin')
    args = parser.parse_args()
    endpoint = urlsplit(args.url)
    if (endpoint.scheme != "http" or endpoint.hostname != "127.0.0.1" or
            not endpoint.port or endpoint.path != "/mcp" or endpoint.query or endpoint.fragment or endpoint.username):
        parser.error("Use the exact http://127.0.0.1:PORT/mcp local diagnostic URL")
    for method in ("server/discover", "tools/list"):
        print(json.dumps(call(endpoint, method), indent=2))
    workspace = call(endpoint, "tools/call", {"name": "workspace_inspect"})
    print(json.dumps(workspace, indent=2))
    print(json.dumps(call(endpoint, "tools/call", {
        "name": "settings_search", "arguments": {"query": "infill"},
    }), indent=2))
    print(json.dumps(call(endpoint, "tools/call", {
        "name": "settings_get", "arguments": {"keys": ["layer_height", "sparse_infill_density"]},
    }), indent=2))
    if args.changes:
        changes = json.loads(args.changes)
        preview = call(endpoint, "tools/call", {"name": "settings_preview_patch", "arguments": {"changes": changes}})
        print(json.dumps(preview, indent=2))
        content = preview["result"]["structuredContent"]
        if not content.get("valid"):
            return
        print("Approve or reject the process-settings patch in the JusPrin Agent panel.")
        print(json.dumps(call(endpoint, "tools/call", {
            "name": "settings_apply_patch",
            "arguments": {"changes": changes, "expectedSessionId": content["sessionId"], "expectedRevision": content["revision"]},
        }), indent=2))


if __name__ == "__main__":
    main()
