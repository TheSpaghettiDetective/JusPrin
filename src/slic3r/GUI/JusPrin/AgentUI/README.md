# JusPrin Agent UI

The local React/TypeScript application that renders the Agent conversation
pane inside `wxWebView`. It talks to OrcaSlicer exclusively through the
versioned `jusprin-agent-bridge` JSON protocol whose shared source of truth is
`resources/jusprin/agent/protocol.json`; the native side lives in
`src/slic3r/GUI/JusPrin/Agent/`.

The page renders native state and submits typed requests. It never owns an
editable copy of the project or conversation: the native `AgentHost` is
authoritative, and every reload re-runs the handshake and reconstructs the
page from the host's `state` message.

## Building

The C++ build does not require Node. The application loads the committed
single-file bundle at `resources/jusprin/agent/index.html`; rebuild and commit
that bundle whenever this package changes:

```bash
npm install
npm run build   # type-checks, then writes resources/jusprin/agent/index.html
```

The bundle must stay a single self-contained file (`vite-plugin-singlefile`):
WKWebView does not reliably load `file:` subresources.

## Testing

```bash
npm test        # vitest: protocol, bridge client, reducer, and DOM interaction tests
```

The deterministic conversation scenarios (`/fail`, `/flaky`, `/slow`) are
implemented natively in `DeterministicMockAgent.cpp` and asserted by
`tests/agent/test_agent_bridge.cpp`; the tests here exercise the page against
a scripted mock host playing the same protocol.

## Design

Colors resolve through the semantic tokens in
`resources/jusprin/ui/design-tokens.json` (imported at build time and applied
as CSS custom properties for the light and dark appearance the host reports).
Do not hard-code palette values in components.
