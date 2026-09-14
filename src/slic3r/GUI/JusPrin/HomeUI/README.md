# JusPrin Home UI

The local React/TypeScript application that renders the Home gallery and
printer column inside `wxWebView`. It talks to OrcaSlicer exclusively through
the versioned `jusprin-home-bridge` JSON protocol whose shared source of truth
is `resources/jusprin/home/protocol.json`; the native side lives in
`src/slic3r/GUI/JusPrin/Home/`.

## Building

CMake builds this page whenever an input changes (`npm ci` from the lockfile, then `npm run build`). The output is `resources/jusprin/home/index.html`; it is generated, not committed. Node/npm is required unless you configure with `-DJUSPRIN_BUILD_WEB=OFF` and supply that file yourself.

```bash
cmake --build build/arm64 --config RelWithDebInfo --target jusprin_web
```

The bundle must stay a single self-contained file (`vite-plugin-singlefile`):
WKWebView does not reliably load `file:` subresources.

To point the running app at a Vite dev server instead of the packaged file (hot
module reload):

```bash
cd src/slic3r/GUI/JusPrin/HomeUI && npm run dev
JUSPRIN_HOME_DEV_URL=http://localhost:5174 ./build/arm64/src/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
```

Adjust the binary path for your build tree and platform. The environment
variable is read once at construction; there is no settings UI for it.

## Testing

```bash
npm test        # vitest: protocol, store, and style-contract tests
```
