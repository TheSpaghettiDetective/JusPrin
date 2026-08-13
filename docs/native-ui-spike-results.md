# Native UI Spike 1 results

**Status:** Native/WebView boundary demonstrated on macOS and Ubuntu 24.04 under
X11 with Mesa llvmpipe software rendering. Windows, Linux/Wayland, Linux with a
physical GPU, and several input-specific checks remain untested or partial.

**Interpretation:** Spike 1 is now classified as an integration demonstration
and platform-QA exercise, not an architectural-feasibility spike. The underlying
technical nature was already supported by mature OrcaSlicer layout, GL canvas,
WebView, and bridge implementations. See
[`native-ui-risk-and-verification.md`](native-ui-risk-and-verification.md).

## Build tested

| Platform | Revision/build | Result |
|---|---|---|
| macOS 26.2, Apple silicon | `codex/native-ui-spike-1`, based on `12a77c4c26`; arm64 Release configuration | Passed with a local macOS 15.0 deployment target. The production 11.3 target did not compile against the installed macOS 26.5 SDK because unchanged OrcaSlicer Objective-C++ sources hit `-Werror=unguarded-availability-new`. |
| Windows | Not run | Untested: no Windows environment was available. |
| Ubuntu 24.04, X11, Mesa llvmpipe | `eb34893c96`; release-like AppImage from `./build_linux.sh -dsi` | Passed for the tested software-rendered X11 boundary. The native pane, production GL canvas, and WebKitGTK Agent pane operated together without a blocking product defect. This does not cover Wayland or a physical GPU. |

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

The demonstrated macOS and Linux/X11 results do not require changing the
proposed boundary. Keep the real viewport, project state, selection, slicing,
and view switching in C++; keep the Agent conversation isolated in the fixed
WebView. For production work, preserve the single-file or an equivalent
registered-resource loading strategy and add an automated bridge contract test
before expanding the command surface.

Do not treat this as full platform coverage. Windows/WebView2 is the
highest-priority remaining run, followed by Linux/Wayland and physical-GPU
coverage, explicit IME/clipboard/shortcut checks, viewport-to-list selection,
continuous orbit-plus-stream input, and physical high-DPI validation.

## Linux/X11 demonstration

The 2026-08-12 GCP run built the release-like AppImage incrementally after one
SSH transport interruption and launched it in a private XFCE/VNC X11 session.
The renderer was Mesa llvmpipe. Because this was software rendering, WebKitGTK
required `APPIMAGE_EXTRACT_AND_RUN=1` and
`WEBKIT_DISABLE_DMABUF_RENDERER=1` in that test environment.

Observed evidence:

- The native shell, real viewport, and Agent WebView started and remained
  usable together; no blocking boundary defect was found.
- Selection from the left list to the real viewport worked.
- Orbit, pane collapse/restore, resize, simulated streaming, Prepare/Preview,
  and existing Slice-to-G-code Preview worked in sequential stress testing.
- Fresh launches at 96 and 192 logical DPI worked. A fresh `Greybird-dark`
  launch produced matching dark native, GL, and Agent surfaces.
- Aggressive synthetic unmaximize/resize/maximize sequences emitted isolated
  non-fatal GTK assertions, but the shell recovered without a visible black GL
  region, WebView gap, or crash.
- The committed Agent bundle was present in the AppImage. The VM's Node.js
  18.19.1 was too old for the locked Vite 8 toolchain, so the web source was not
  rebuilt on that VM.

Limitations:

- The single-pointer harness did not demonstrate uninterrupted orbit while a
  second input simultaneously operated native controls.
- Global-shortcut routing was inconclusive, and no IME was configured.
- The run did not cover Wayland, a physical GPU, or hardware high-DPI output.
- Existing OrcaSlicer WebViews made individual WebKit-helper resource
  attribution unreliable.
- Canonical repository-local screenshots and complete build logs were not
  retained with this results document.

The detailed environment lessons and reproduction instructions are in
[`native-ui-spike-linux-gcp-test-spec.md`](native-ui-spike-linux-gcp-test-spec.md).

## Remaining Spike 1 verification

The architecture is not waiting on these checks. They are targeted closure of
platform and interaction coverage:

1. Run the packaged shell on Windows/WebView2.
2. Run Linux with a physical GPU and Wayland if both are supported target
   configurations.
3. Test clipboard, an unmistakable global shortcut, and IME composition on all
   three WebView backends.
4. Confirm viewport-to-list selection and true simultaneous orbit, streaming,
   resize, and pane operations with a human or multi-input harness.
5. Capture durable screenshots and logs for the remaining runs.

A failure changes the native/WebView boundary only if it is reproduced and
cannot be corrected with localized layout, focus, backend, packaging, CSS, or
bridge work.
