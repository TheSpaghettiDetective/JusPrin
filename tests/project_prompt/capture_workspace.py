#!/usr/bin/env python3
"""Record read-only workspace sections from an explicitly selected isolated app."""
import argparse
import datetime
import hashlib
import json
import pathlib
import urllib.parse
import urllib.request

SECTIONS = ('summary', 'project', 'objects', 'intent', 'plan', 'slicing', 'printer')
IDENTITY = ('sessionId', 'revision', 'truncated')


def initial_context(summary):
    # OpenAIResponsesAgent::workspace_json, not the richer AgentHost UI context.
    return {**{key: summary[key] for key in
               ('sessionId', 'revision', 'projectName', 'printerPreset', 'filamentPreset')},
            'selectedObjectIds': summary['selection']['items'],
            'plates': [{'id': p['plateId'], 'name': p['name'], 'active': p['active'],
                        'sliced': p['sliced'],
                        'objects': [{'id': o['objectId'], 'name': o['name'],
                                     'instances': o['instanceCount']} for o in p['objects']['items']]}
                       for p in summary['plates']['items']]}


def replay(fixture, arguments):
    sections = arguments.get('sections', ['summary'])
    if (not isinstance(sections, list) or len(sections) > 8 or
            any(not isinstance(s, str) or s not in fixture['sections'] for s in sections)):
        return None  # Includes activities: a frozen log cannot describe this conversation.
    result = {key: fixture['sections']['summary'][key] for key in IDENTITY}
    for section in sections:
        recorded = fixture['sections'][section]
        result.update({key: value for key, value in recorded.items() if key not in IDENTITY})
        result['truncated'] |= recorded['truncated']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--discovery', required=True, type=pathlib.Path,
                        help='Discovery file of your isolated --manual-mcp harness, never the user app')
    parser.add_argument('--model', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    discovery = json.loads(args.discovery.read_text())
    url = urllib.parse.urlparse(discovery['url'])
    if url.scheme != 'http' or url.hostname != '127.0.0.1' or url.path != '/mcp':
        raise ValueError('Expected a local isolated MCP server')

    def inspect(sections):
        body = {'jsonrpc': '2.0', 'id': 'capture', 'method': 'tools/call',
                'params': {'name': 'workspace_inspect', 'arguments': {'sections': sections},
                           '_meta': {'io.modelcontextprotocol/protocolVersion': '2026-07-28',
                                     'io.modelcontextprotocol/clientCapabilities': {}}}}
        request = urllib.request.Request(discovery['url'], data=json.dumps(body).encode(), headers={
            'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
            'MCP-Protocol-Version': '2026-07-28', 'Mcp-Method': 'tools/call', 'Mcp-Name': 'workspace_inspect'})
        with urllib.request.urlopen(request, timeout=30) as response:
            rpc = json.load(response)
        if 'error' in rpc or rpc['result'].get('isError'):
            raise RuntimeError(rpc)
        return rpc['result']['structuredContent']

    sections = {name: inspect([name]) for name in SECTIONS}
    summary = sections['summary']
    if any((value['sessionId'], value['revision']) != (summary['sessionId'], summary['revision'])
           for value in sections.values()):
        raise ValueError('Workspace changed during capture; retry after it settles')
    if any(value['truncated'] for value in sections.values()):
        raise ValueError('Use a small workspace with untruncated responses')
    fixture = {'provenance': {'capturedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                             'appVersion': discovery['appVersion'],
                             'source': 'Existing native JusPrinShellHarness --manual-mcp, isolated datadir',
                             'model': args.model.name,
                             'modelSha256': hashlib.sha256(args.model.read_bytes()).hexdigest(),
                             'continuationContext': 'Omitted: MCP does not expose AgentHost full UI context'},
               'workspace': initial_context(summary), 'sections': sections}
    # Check that merging individual sections faithfully reproduces a real combined read.
    if replay(fixture, {'sections': list(SECTIONS)}) != inspect(list(SECTIONS)):
        raise ValueError('Combined response differs from section replay; workspace may have changed')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(fixture, indent=2) + '\n')
    print(f'Captured {len(sections)} sections, session {summary["sessionId"]}, revision {summary["revision"]}')


if __name__ == '__main__':
    main()
