# Native UI Spike 1 results

**Status:** macOS implementation demonstrated; Windows and Linux remain untested.

## Build tested

| Platform | Revision/build | Result |
|---|---|---|
| macOS 26.2, Apple silicon | `codex/native-ui-spike-1`, based on `12a77c4c26`; arm64 Release configuration | Passed with a local macOS 15.0 deployment target. The production 11.3 target did not compile against the installed macOS 26.5 SDK because unchanged OrcaSlicer Objective-C++ sources hit `-Werror=unguarded-availability-new`. |
| Windows | Not run | Untested: no Windows environment was available. |
| Linux / WebKitGTK 4.1 | Not run | Untested: no Linux environment was available. This is still required before calling the composition cross-platform demonstrated. |

The Agent package also passed `npm run typecheck` and `npm run build` with Vite 8.2.1. Its production output is a single local HTML file so WKWebView can load the React bundle from a `file:` URL without cross-origin subresource failures. `node_modules` is ignored and is not part of the result.

## macOS demonstration

The development shell was launched with `ORCA_NATIVE_UI_SPIKE=1` and an isolated test data directory. An empty startup project was populated with two real plates and two real mesh objects. The center remained OrcaSlicer's production `GLCanvas3D`; the right pane used WKWebView.

### Section 6 seam checks

| Check | Observed result |
|---|---|
| Left pane object to viewport | Passed. Selecting `Spike block` in the native list selected the real model and displayed OrcaSlicer's object information overlay. |
| Viewport object to left pane | Not conclusively tested. Computer-control coordinate targeting could not reliably address the OpenGL canvas on this multi-display setup. The production selection callback is wired to refresh the list, but that is code inspection rather than runtime evidence. |
| Collapse/restore while orbiting | Partial. Collapse and restore passed repeatedly without a WebView/GL resize artifact. Continuous simultaneous pointer orbiting could not be automated reliably. |
| Stream while orbiting/resizing | Partial. The typed JS-to-C++ command and incremental C++-to-JS event stream passed; chat stayed anchored at the bottom while the pane collapsed/restored and the window changed to the maximized layout. Simultaneous orbiting was not conclusively tested. |
| Canvas/chat/application focus and shortcuts | Partial. Focus moved between the native list, window, and chat input; Enter submitted a chat command. Clipboard, IME, canvas keyboard focus, and global shortcuts from inside the WebView were not exhaustively tested. |
| Prepare/Preview with Agent present | Passed in both directions. The existing Prepare and G-code Preview canvases switched while the fixed Agent pane and transcript remained present. |
| Slice button | Passed. The new button triggered the existing slice action and opened the production Preview with generated G-code. |
| Normal/high-DPI resize | Partial. Normal and maximized layouts on a Retina-capable Mac passed without visible GL/WebView seams. Explicit 100% and 200% display-mode changes were not performed. |

### Section 7 observations

- No visible rendering gap or stale region appeared when collapsing/restoring the native pane, switching views, or maximizing the window.
- The local page initially exposed two WKWebView-specific integration issues during testing: nested resource path construction and `file:` module/subresource loading. Both were reproduced before correction. The final page loaded from the app resources and rendered the React UI.
- Project state arrived from C++ as `2 plates · 2 objects`. JavaScript only rendered versioned native events and submitted versioned commands.
- Chat scroll anchoring remained at the newest streamed content during the tested pane and window layout changes.
- Startup worked after dismissing an unrelated missing Bambu network plug-in notice caused by the isolated test data directory. Full WebView reload behavior was not explicitly exercised.
- Light appearance passed. Dark appearance was not tested.
- Text entry and selection worked. Clipboard and IME behavior were not tested.
- No macOS-specific CSS was needed. The only macOS/WebKit-specific packaging adjustment was using a single-file bundle for reliable local loading.

## Memory and CPU snapshot

These are one-point development measurements, not a benchmark. The running OrcaSlicer process included two plates, two meshes, sliced G-code, the existing application WebViews, and the Spike 1 Agent pane, so the WebKit helper processes cannot be attributed exclusively to the new pane.

| State | OrcaSlicer process | WebKit helpers visible for the run |
|---|---|---|
| Idle after resize/slice | 906 MiB RSS, about 1.0% CPU | GPU 42 MiB; networking 21 MiB; WebContent helpers 28–64 MiB each |
| Simulated stream sample | 909 MiB RSS, about 0.9% CPU | The same helpers were at 0.0% CPU in the sampled instant; no material RSS jump was observed. |

## Screenshots

Screenshots were visually inspected through the local computer-control session for the resting Prepare shell, the collapsed/expanded shell, the maximized shell, streaming chat, and Preview. They were not added as repository artifacts because the automation session did not provide a stable repository-local capture path.

## Boundary recommendation

The tested macOS result does not require changing the proposed boundary. Keep the real viewport, project state, selection, slicing, and view switching in C++; keep the Agent conversation isolated in the fixed WebView. For production work, preserve the single-file or an equivalent registered-resource loading strategy and add an automated bridge contract test before expanding the command surface.

Do not treat this as cross-platform completion. Linux/WebKitGTK is the highest-priority remaining run, followed by Windows/WebView2, explicit IME/clipboard/shortcut checks, viewport-to-list selection, continuous orbit-plus-stream input, and 100%/200% scaling.
