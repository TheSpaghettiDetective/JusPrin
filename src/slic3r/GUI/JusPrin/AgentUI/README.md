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

CMake builds this page whenever an input changes (`npm ci` from the lockfile, then `npm run build`). The output is `resources/jusprin/agent/index.html`; it is generated, not committed. Node/npm is required unless you configure with `-DJUSPRIN_BUILD_WEB=OFF` and supply that file yourself.

```bash
cmake --build build/arm64 --config RelWithDebInfo --target jusprin_web
```

The bundle must stay a single self-contained file (`vite-plugin-singlefile`):
WKWebView does not reliably load `file:` subresources.

To point the running app at a Vite dev server instead of the packaged file (hot
module reload):

```bash
cd src/slic3r/GUI/JusPrin/AgentUI && npm run dev
JUSPRIN_AGENT_DEV_URL=http://localhost:5173 ./build/arm64/src/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
```

Adjust the binary path for your build tree and platform. The environment
variable is read once at construction; there is no settings UI for it.

## Testing

```bash
npm test        # vitest: protocol, bridge client, reducer, and DOM interaction tests
```

The deterministic conversation scenarios (`/fail`, `/flaky`, `/slow`) are
implemented natively in `DeterministicMockAgent.cpp` and asserted by
`tests/agent/test_agent_bridge.cpp`; the tests here exercise the page against
a scripted mock host playing the same protocol.

### State matrices

`src/visual/` holds one state input per state on the Figma page **Agent UI ·
States & Components**: runtime and header states, chat
list, composer, messages, manufacturing history, the earlier-chat notice, and
the printer chat. Each is rendered by the production components, or by the
real `App` against a scripted host, so a state is covered only if real inputs
can produce it. `npm test` asserts every one renders; with an output path the
same test writes a review page of all of them at the 429 and 320 DIP dock
widths in both appearances:

```bash
PREVIEW_OUT=/tmp/agent-ui-states.html npx vitest run AgentUiStateMatrix
```

Open the page with `?show=<case>,<case>&mode=dark&dock=320` to narrow it to
the states being compared with a Figma node. The page proves rendering only;
look at the running app as well (`tests/shell`'s capture modes write PNGs of
the real WebView).

## Design

`tokens.ts` imports `resources/jusprin/ui/design-tokens.json` at build time
and writes it onto the root element as custom properties: `--<group>-<name>`
for the semantic colors of the appearance the host reports, and, once at
startup, `--radius-<name>` for each radius plus `--font-<role>` holding the
complete `font` shorthand of each type role and `--font-code` for the one
monospace role, used only for code, keys, paths and IDs. `styles.css` may use only those variables for
color, radius, and type, and only the spacing scale (4 through 48) for
padding, margin, and gap; `styles.test.ts` reads the stylesheet from disk and
fails on any literal outside that contract, naming the offending line.
