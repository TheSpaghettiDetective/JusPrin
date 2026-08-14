# Spike: Can OrcaSlicer's 3D viewport run in WebGL2?

> [!IMPORTANT]
> **Superseded on 2026-08-12. Do not execute this spike.** Electron and a WebGL
> viewport are no longer under consideration. The current plan is
> [Native UI rewrite plan](native-ui-rewrite-plan.md). This document is retained
> only as a record of the discarded direction.

**Status:** Superseded — do not start
**Former deliverable:** `agent-docs/spike-webgl2-viewport-results.md`

## Why this plan was superseded

No WebGL spike was run and this document was not superseded because WebGL was
proven incapable. It was superseded because the product scope became clearer:
the proposed interface is a much smaller shell around OrcaSlicer's existing 3D
workspace, not a browser-owned replacement for the whole OrcaSlicer UI.

Keeping the current platform preserves the parts that are expensive and
uncertain to reproduce: `GLCanvas3D`, G-code preview, picking, selection,
gizmos, painter interactions, and their in-process connection to model state and
undo/redo. wxWidgets already supplies the required top-level tabs, fixed or
collapsible side panes, and buttons. The Agent conversation can still use
React/TypeScript in OrcaSlicer's existing `wxWebView`, without moving the 3D
workspace into a browser.

The Electron/WebGL direction would therefore add a renderer rewrite, a
cross-process state boundary, and new packaging and GPU behavior primarily to
deliver flat UI that the simplified wireframe no longer requires. Its main
benefit—using web technology for the entire interface—does not justify those
costs for the current product target.

The decision is now to retain C++17, wxWidgets, ImGui, OpenGL, and CMake, then
test the remaining implementation uncertainties directly: composing the new
shell around the real canvas and reorganizing one representative gizmo. See the
[native UI rewrite plan](native-ui-rewrite-plan.md).

| | Work | Cost | Needs OrcaSlicer code? | Can change the plan? |
|---|---|---|---|---|
| **Spike A** | Isolated render PoC — standalone Electron app loading an STL and a G-code file from disk | 3–4 days | **No** | **Yes — rejects the whole direction** |
| *(not a spike)* | Engine extraction, with the transfer path instrumented — §10 | Phase 1 | Yes | No |
| **Spike B** | Painting loop latency — calls the real `TriangleSelector` | ~1.5 weeks | Yes | Yes — Gate B |

Spike A is a throwaway project that touches nothing in this repository. **It can reject the
web-viewport direction in days**, before anyone writes C++ plumbing.

> **Corrections from earlier drafts, preserved so they aren't reintroduced:**
> 1. Native-OpenGL-under-Electron is **not** a proven fallback (§2.1).
> 2. The Electron decision **is** gated by this spike.
> 3. TypeScript does **not** need to reproduce 3MF bitstreams (§11) — C++ keeps owning
>    subdivision and serialization.
> 4. Measuring the engine→renderer transfer path is **not a spike** — it can't change the
>    decision, and it would build the same thing twice (§10).

---

## 1. Why this exists

We are planning a fork of OrcaSlicer that replaces its wxWidgets user interface with a
web-technology UI, keeping the C++ slicing engine untouched. The engine (`src/libslic3r/`,
248k lines) has zero wxWidgets dependency, so it survives as-is. The UI (`src/slic3r/`,
375k lines) is the rewrite target.

One question cannot be answered by reading code: **can a browser GPU stack handle
OrcaSlicer's actual rendering model?** Everything else in the plan is sound either way. This
one decision changes the architecture, so it goes first and it goes cheap.

All spike code is throwaway. Nothing built here ships.

## 2. Gates

Electron is **not** approved by Spike A alone.

| Gate | Condition | Meaning |
|---|---|---|
| — | **Spike A fails** | **Stop broad Electron work** — cheaply, in days. Spike Qt/QML vs. hybrid embedding. |
| **A** | Spike A passes | Electron is the **leading candidate**. Proceed with engine extraction, then Spike B. |
| **B** | Spike B passes, **or** a native/WASM geometry-service path is proven | **Commit** to Electron for the full UI rewrite. |
| — | Spike B fails *and* no geometry-service path proves out | **Reconsider Electron.** Do not commit. |

Note the risk asymmetry: under the C++-authoritative architecture in §11, Spike B does **not**
require reimplementing `TriangleSelector`, so its failure mode narrows to round-trip latency,
which has a known mitigation. Gate B is real but unlikely to fire.

**Ungated — proceed in parallel, do not block on this spike:** extracting the headless engine
daemon, exposing the engine over MCP, ccache and the CMake engine/GUI split.

### 2.1 If rendering fails, there is no easy fallback

The answer is **not** "put the native viewport under Electron." That path is unproven and
high-risk:

- Electron owns Chromium's window and compositor. An externally-owned native view is not part
  of Chromium's layer tree.
- **Consequence, and it is severe:** React and HTML cannot reliably composite *above* a native
  child view. Orca's viewport is dense with overlays — gizmo panels, manipulation fields,
  toasts, context menus. If those can't be web-rendered, you get a web UI *beside* a native
  viewport with every overlay staying ImGui indefinitely, removing most of the rewrite's value
  on the app's most important screen.
- Different event loop and window hierarchy; focus, keyboard, drag-and-drop, resize and DPI
  scaling all cross that boundary.
- macOS native-view attachment is platform-specific. Wayland makes foreign-window embedding
  and positioning particularly problematic.

That Orca today puts `wxWebView` and `wxGLCanvas` in one wxWidgets app proves nothing here —
those are two wx child windows sharing one event loop and one toolkit.

**If native embedding becomes the only apparently viable path, it needs its own spike, proven
independently on macOS, Windows and Wayland, before anything is committed.**

---

## 3. Acceptance criteria — one sign-off, needed before Spike A

*"Is the viewport good enough?"* is unanswerable without both **which machine** and **how good
there**. These are two halves of one decision and they trade against each other:

- Lower the hardware floor → thresholds get harder → more likely to fail
- Loosen the thresholds → weaker machines pass → but those users get a worse experience than
  they have today

Set independently, you can write a rigged spec without noticing: a very low floor with very
tight thresholds can never pass; a high floor with loose thresholds always passes. Neither
teaches anything. **Decide these together and sanity-check the pair.**

### 3.1 Machine class — a product decision

An earlier draft asserted "Intel Iris Xe." That was invented and is removed. State the intended
minimum supported configuration explicitly.

Bear in mind: OrcaSlicer today runs on modest hardware because native OpenGL is lean. A browser
stack carries overhead. **This choice may cost users who have no problems today.**

Measure on:
- Apple Silicon (ceiling)
- The chosen floor machine — integrated Windows GPU expected
- At least one Linux config, **including Wayland** — a large part of Orca's userbase, and where
  the native-embedding fallback is worst, so the highest-variance answers

At **1080p and 4K/high-DPI** both. Fill rate scales with pixels; Retina renders at 2×.

A pass means passing on the floor machine. Apple-Silicon-only numbers are not a result — say so
and mark the conclusion provisional.

### 3.2 Speed thresholds — relative to today's OrcaSlicer

Relative, not absolute: if the current app already stutters on a huge model, the new one may
stutter about as much. The question is "as good as what we have," not an invented number.

**Proposed. Adjust freely — but fix them before running anything.**

| Metric | Pass threshold |
|---|---|
| p95 frame time | ≤ 1.2× native baseline |
| p99 frame time | ≤ 1.3× native baseline |
| Time to first usable frame | ≤ 1.5× native baseline |
| Input latency (click → visual change) | within 30 ms of native baseline |
| Peak memory | ≤ 2× native baseline (Chromium overhead is expected) |

*Frame time* is how long one drawn image takes; at 60 fps that's ~16 ms. *p95* means 95 of 100
frames came in under that figure. Percentiles rather than average frame rate, because averages
hide stutter — 59 fast frames and one 500 ms freeze averages beautifully and feels broken.

### 3.3 Health gates — absolute, no negotiation

These do not move with hardware and need no coordination with §3.1.

| Gate | Requirement | Why absolute |
|---|---|---|
| WebGL context loss | **0 events** | The 3D view going blank is broken on any machine. Not a speed question. |
| Memory growth over 5-min stress run | **< 5%** after warmup | A leak is a leak regardless of hardware. |
| Worst single frame | **No frame over 100 ms** | Even if today's app stutters, a tenth-second freeze in the new one is unacceptable. |

### 3.4 Test data — blocker, escalate immediately if absent

`resources/handy_models/` is **inadequate** — everything in it is under 600 KB (3DBenchy,
Stanford Bunny, calibration cubes). It will pass trivially and prove nothing.

Required before Spike A:
- **One very heavy single mesh** — STL, high triangle count
- **One full plate of many separate objects** — a *different* bottleneck: draw-call count rather
  than triangle count. Do not skip this; it is a distinct failure mode.
- **One large G-code file** — multi-hour print, several million extrusion segments. Generate it
  by slicing in the existing app.
- For Spike B: one project with existing painted supports or MMU segmentation

**If this data hasn't been supplied, stop and ask.** Do not substitute calibration cubes.

### 3.5 Baseline — run the existing app as a measuring stick

Every §3.2 threshold is relative to native Orca on the same machine, model and resolution. This
is the only thing Spike A needs the existing app for, and it needs **no C++ development** — the
app is already built:

```bash
open build/arm64/src/RelWithDebInfo/OrcaSlicer.app
```

Rebuild only if necessary:

```bash
cmake --build build/arm64 --config RelWithDebInfo --target all --
```

Profile it externally — Instruments on macOS, PresentMon on Windows. No code changes needed for
frame pacing, GPU/CPU time or memory. The one metric external tools may not reach is semantic
input latency; if it can't be captured externally, **minimal native instrumentation is permitted
for that metric only** — note it in the results.

---

# Spike A — Isolated render PoC

**3–4 days. Standalone Electron project. No OrcaSlicer code, no CMake, no C++ build.**
This is the stage that can reject the direction.

## 4. Half-day smoke test first — before building anything rigorous

Spending two days on a benchmark harness and *then* discovering the heaviest mesh runs at 8 fps
on the floor machine wastes the harness.

So: load the heavy mesh in a rough WebGL2 page on the floor machine and eyeball the frame rate.

- **Catastrophically bad** → stop. Report it. No harness needed, Spike A is answered.
- **Plausible** → build the harness (§8) and get real numbers.

Same cheapest-decisive-thing-first logic as the spike ordering itself, applied one level deeper.

## 5. Mesh rendering

Binary STL is a header, a triangle count, and 50 bytes per triangle — roughly 50 lines of
TypeScript to parse. No exporter, no engine dependency.

**Method:** Electron + WebGL2, orbit camera, fixed scripted camera trace (§8). three.js or raw
WebGL2, whichever is quicker. Run against **both** §3.4 cases — the single heavy mesh *and* the
many-object plate, which stress different things.

## 6. G-code preview

Strong starting position. `libvgcode` (`src/libvgcode/`, 6,556 lines, no wxWidgets) ships **two**
shader backends and the ES one is WebGL2's exact shading language:

- **Port this:** [`ShadersES.hpp`](../src/libvgcode/src/ShadersES.hpp) — `#version 300 es`,
  `sampler2D` + `texelFetch` + `gl_InstanceID`, path data packed into 2D textures. WebGL2 *is*
  OpenGL ES 3.0; its shading language is GLSL ES 3.00.
- **Do NOT use:** [`Shaders.hpp`](../src/libvgcode/src/Shaders.hpp) — `#version 150` with
  `samplerBuffer`. Texture buffer objects **do not exist in WebGL2**. Dead end.

Packing to replicate, in [`ViewerImpl.cpp`](../src/libvgcode/src/ViewerImpl.cpp):

| Data | Format | Approx. lines |
|---|---|---|
| Positions | `GL_RGB32F` | 386, 391 |
| Heights / widths / angles | `GL_RGB32F` | 439, 444 |
| Colors | `GL_R32F` | 492, 497 |
| Segment indices | `GL_R32UI` | 550–556 |
| Option indices | `GL_R32UI` | 618–624 |

Texture dimension wrapping against `max_texture_size` is at 323–340 and ports directly. Data
layout for reference: [`PathVertex.hpp`](../src/libvgcode/include/PathVertex.hpp),
[`GCodeInputData.hpp`](../src/libvgcode/include/GCodeInputData.hpp).

Build the shared scaffolding first (Electron, WebGL2, camera, metrics), then mesh — cheap, and
it validates the scaffolding — then G-code, which carries the higher risk. Tens of millions of
texture-backed instanced segments is where memory limits and context loss actually live.

### 6.1 Getting vertex data without building anything

libvgcode renders `PathVertex` data, not raw G-code text — normally produced by `GCodeProcessor`
simulating the print to derive extrusion widths, roles and times. Two ways around it, in
preference order:

1. **Crude G-code parser in TypeScript.** Good enough for a *performance* number — the GPU does
   not care whether extrusion widths are physically accurate, only how many segments there are
   and how big the buffers get. Fastest path.
2. **One-time data dump** from the already-built app, saved as a binary blob and loaded from
   disk. Upgrade to this only if option 1 gives a marginal result, or for the §7 correctness
   comparison.

Either way the C++ side is a one-time data extraction, never a build dependency.

### 6.2 Known gotcha — read before debugging float textures

Upstream already hit a driver bug. From `ViewerImpl.cpp:904`:

> On some graphic cards texture buffers using GL_RGB32F format do not work

Their fix: pad to `GL_RGBA32F` with one unused float per element — see the `Vec4` typedef and
`extract_pos_and_or_hwa()` just below it. They applied it to the desktop texture-buffer path;
the ES path still uses `RGB32F` 2D textures.

In WebGL2 `RGB32F` is valid for `texImage2D` and `texelFetch` needs no filtering, so it may work.
**If float textures produce garbage or fail to allocate, pad to `RGBA32F` before investigating
anything else.** Known landmine, known workaround — don't lose a day.

## 7. Correctness spot-checks

Performance alone can yield a fast, wrong viewer. Screenshots plus a few numerical assertions
against the running native app are enough at this weight:

- Mesh orientation, normals, transparency, clipping planes
- Layer filtering and layer range selection
- Extrusions vs. travels vs. wipes
- G-code color modes (feedrate, tool, extrusion role)
- Picking correctness — right facet under the cursor at varied camera angles

## 8. Benchmark harness — the one thing that must not be throwaway

The rendering code is disposable. **The measurement harness is the deliverable.** Without it you
cannot compare three machines, re-run after a fix, or trust the numbers — and an agent cannot
iterate autonomously.

- **Fixed, scripted traces** — deterministic camera path and layer-scrubbing sequence, identical
  across machines and between native baseline and WebGL2. Hand-orbiting is not reproducible.
- **One-command execution**
- **JSON metric output** — every metric in §3.2 and §3.3, machine-readable
- **Automatic screenshots** at fixed trace points, for the §7 comparison
- **Environment metadata in results** — hardware, OS, GPU and driver version, Electron and
  Chromium version, resolution, and which test file was used

Run a **short repeatable benchmark** for iteration, plus a **2–5 minute stress pass** for memory
growth, context loss and thermal/stutter behavior. A 30-second test misses all three.

## 9. Gate A: stop and report

Report Spike A and get an explicit go/no-go. If it fails, the next action is the
Qt/QML-vs-hybrid spike in §2.1 — **not** Spike B.

---

# Not a spike — instrument the engine extraction

## 10. Measure the transfer path while you build it

Earlier drafts had this as a spike. It shouldn't be. Transfer cost affects *time to first frame*,
not sustained frame rate, and a slow load has known fixes — streaming, chunked upload, worker
decode. It cannot change the architecture decision. And if Spike A passes, this path gets built
as production code anyway, so running it as a spike means building it twice.

Fold it into Phase 1 (engine extraction) as an instrumented milestone. Record the same §3.2
metrics as you go, and watch peak memory in particular — several copies of a large buffer alive
simultaneously is the one thing here that could bite on an 8 GB machine.

### 10.1 The boundary has two hops, and only the second is hard

```
C++ engine  →  Electron main/preload  →  Chromium renderer  →  WebGL upload
```

JavaScript cannot mmap, and the Chromium renderer is sandboxed. A design that only gets bytes
into the main process is **half a solution**.

**Recommended: framed binary payload over a local socket.** C++ child writes framed binary →
Electron main reads it → payload **transferred**, not cloned, to the renderer → renderer builds
typed views and uploads to GPU. Cross-platform, no native addon, no sandbox concessions.

**mmap or shared memory is acceptable only if the renderer-access mechanism is also
implemented** — e.g. a native addon using `napi_create_external_arraybuffer`, which must run in
the renderer process and therefore requires sandbox concessions. Implement both halves or don't
claim it.

**Two gotchas that will silently corrupt the measurement:**
- `postMessage` **without** a transfer list is a structured clone — a full copy. Easy to write by
  accident; it doubles cost invisibly. Verify the buffer actually detached.
- Node socket reads land in **pooled** `Buffer`s. Transferring the underlying `ArrayBuffer` can
  move the whole pool slab or fail. Use an unpooled allocation or copy into a standalone
  `ArrayBuffer` — and count that copy.

Instrument the stages separately — encode, transfer, copy, decode, GPU upload.

---

# Spike B — Painting loop latency

**~1.5 weeks. Only after Gate A and a working transfer path.** Feeds Gate B. This is the first
stage that calls into the OrcaSlicer codebase.

**What it tests:** when the user drags a brush across the model, does paint appear under the
cursor fast enough to feel like painting rather than a laggy remote desktop — given that the
geometry work stays in C++ in another process. Brush aesthetics, tool layout and workflow are
ordinary UI work, not spike material.

## 11. C++ keeps owning subdivision and serialization

**TypeScript does not need to reproduce 3MF bitstreams.** An earlier draft required that. It was
wrong, contradicted the authoritative-C++-state model, and would have imported large
unnecessary risk.

The serialization boundary already sits entirely inside the library we are keeping:

- `TriangleSelector::serialize()` produces the subdivision data
  ([`TriangleSelector.hpp:354`](../src/libslic3r/TriangleSelector.hpp:354))
- `FacetsAnnotation::set()` stores it ([`Model.cpp:3455`](../src/libslic3r/Model.cpp:3455))
- `get_triangle_as_string()` encodes it for 3MF export
  ([`Model.cpp:3473`](../src/libslic3r/Model.cpp:3473)) — carrying upstream's own warning:
  *"Used for 3MF export, changing it may break backwards compatibility !!!!!"*
- Consumers are exclusively `src/libslic3r/Format/3mf.cpp` and `bbs_3mf.cpp` (2170–2176,
  2808–2835, 4916–4918) — all inside libslic3r
- The painter gizmo's only `serialize()` call is behind an ImGui debug button
  (`GLGizmoPainterBase.cpp:1768`)

**Requirement:** the authoritative C++ `TriangleSelector` state must round-trip through 3MF and
open identically in stock Orca. Any transient TypeScript preview must reconcile to that state
and **must never participate in serialization.**

Under this architecture the round-trip is correct largely by construction — treat it as a cheap
sanity check, not the central risk. The central risk is latency (§12).

## 12. Test the simplest architecture first

Do **not** start with speculative TypeScript painting, and do not test a synchronous round trip
per pointer event — nobody would ship either. Test in this order, stopping as soon as it's fast
enough:

1. WebGL does picking and renders the brush cursor
2. UI sends **batched** hit points, facet IDs and brush parameters
3. Existing C++ `TriangleSelector` performs subdivision and selection
4. C++ returns changed render geometry, or a compact delta
5. WebGL updates the display
6. **Only if measured latency requires it**, add speculative TypeScript painting

If steps 1–5 are fast enough, the reconciliation problem never exists. Reaching for speculative
local state first would reimplement subdivision semantics twice and manufacture a divergence
risk this ordering avoids.

**Measure steps 1–5 on the floor machine, not the Mac.** A fast Apple Silicon result will
wrongly conclude "no speculation needed."

## 13. What Spike B must demonstrate

Orca's painting is far more than pick-a-triangle-and-recolor.
[`TriangleSelector`](../src/libslic3r/TriangleSelector.hpp) at line 44 has "power to recursively
subdivide the triangles and make the selection finer." `select_patch()` takes a `cursor_radius`
and a `triangle_splitting` flag; `select_by_seed_fill()` exists; the painter base has
`m_cursor_radius` and `get_clipping_plane_in_volume_coordinates()`.

Demonstrate **all** of these through the §12 loop, or the result proves picking, not painting:

1. Radius brush affecting many facets per stroke
2. Visibility / clipping-plane rules respected
3. Recursive facet subdivision (computed by C++, displayed by WebGL)
4. One fill operation (seed fill)
5. An undoable stroke delta
6. Delta-driven display update without a full mesh re-upload

**Correctness:** painting matches native Orca for the same stroke on the same mesh; undo/redo
restores identical state; the §11 3MF round-trip sanity check passes.

## 14. Why this one spike covers the whole `Gizmos/` question

The painters are **4,446 lines** — `GLGizmoPainterBase` (1,905), `GLGizmoMmuSegmentation`
(1,276), `GLGizmoFdmSupports` (898), `GLGizmoSeam` (367) — about 14% of the ~31,900 lines of
`.cpp` in `Gizmos/`. Spike B tests them directly; the rest generalizes by argument.

| Category | Lines | Interaction | Risk |
|---|---|---|---|
| **Painters** | 4,446 | Per-triangle geometry, continuously, at pointer rates | **High — this is Spike B** |
| Click-to-place (SLA supports, brim ears, hollow) | ~3,300 | Same pattern, one click per point, not a continuous stroke | Low |
| Transform (move, scale, rotate) | ~2,500 | Update a transform matrix, re-render. No geometry work. | Very low |
| Cut, simplify | ~6,500 | Heavy geometry, but modal — set parameters, apply, wait | Low (latency doesn't matter) |
| Text, SVG, emboss | ~7,700 | Generate geometry from fonts and vectors. Also modal. | Low |
| Measure | 2,671 | Picking and display, no mesh modification | Low |

Painting is the only category combining per-triangle geometry work with continuous pointer-rate
interaction. If it works, the rest do. If it doesn't, the fallback — moving geometry logic to a
native or WASM service — is scoped to those 4,446 lines, not the whole directory.

---

## 15. Decision matrix

| Result | Decision |
|---|---|
| Spike A fails on mesh or G-code rendering | **Stop broad Electron work.** Spike Qt/QML vs. hybrid embedding |
| Spike A passes; Spike B meets thresholds | **Gate B met.** Proceed with Electron and a web-owned viewport |
| Spike A passes; §12 steps 1–5 too slow, speculative preview recovers it | Gate B met. Design the batching/state boundary carefully in the production protocol |
| Spike A passes; painting fails either way | Investigate WebGL rendering plus a native or WASM geometry service — **see caveat** |
| Painting fails **and** no geometry-service path proves out | **Reconsider Electron.** Do not commit to the full rewrite |
| Only native embedding appears viable | Prove embedding independently on macOS, Windows and Wayland (§2.1) first |

**Caveat on the WASM geometry service.** Compiling `TriangleSelector` to WASM is *also*
unproven — Eigen dependency, recursive tree structures, bitstream serialization, unknown
performance under WASM SIMD. Promising direction, not a known-good fallback. If the spike lands
here it needs its own mini-spike. The skepticism that disqualified native-embedding-as-safe-
fallback (§2.1) applies equally.

## 16. Reporting

Write `agent-docs/spike-webgl2-viewport-results.md` with:
- Full measurements — **native baseline alongside WebGL2**, every machine, every resolution,
  percentiles not averages, JSON attached. Do not summarize away bad numbers.
- Each §3.2 and §3.3 threshold marked pass/fail with the actual figure
- Which matrix row fired, and the recommendation
- Correctness comparison results with screenshots
- Anything found that changes the wider plan
- What could not be tested, and why

**Report the §4 smoke test the day it runs, and Spike A as soon as it concludes.** Spike A is
only 3–4 days and it gates everything else.

Four failure modes that produce a confidently wrong answer. If any is forced on you, say so
plainly rather than qualifying it quietly:
- Reporting only Apple Silicon numbers
- Substituting bundled calibration models for real worst-case ones
- Reporting a WebGL2 number with no native baseline beside it
- Adjusting a §3.2 threshold after seeing the result

## 17. Out of scope

- Designing the production engine↔UI RPC protocol
- **Tests of the spike's rendering code** — that code is disposable. This does **not** apply to
  the §8 measurement harness, which must be repeatable and machine-readable.
- Any change to `src/libslic3r/` or `src/slic3r/`, beyond throwaway data dumps and the narrow
  input-latency instrumentation allowed in §3.5
- Reimplementing `TriangleSelector` subdivision or serialization in TypeScript (§11)
- React, UI chrome, layout, styling
- Fixing or refactoring `libvgcode` — read it, copy from it, leave it alone
- Cross-platform packaging

## 18. File reference

Spike A needs only the top four rows, all read-only.

| Path | Lines | Relevance |
|---|---|---|
| `src/libvgcode/src/ShadersES.hpp` | — | **Port these.** `#version 300 es`, WebGL2-compatible |
| `src/libvgcode/src/Shaders.hpp` | — | Do not use. Desktop `samplerBuffer` path |
| `src/libvgcode/src/ViewerImpl.cpp` | — | Texture packing to replicate; RGB32F gotcha at 904 |
| `src/libvgcode/include/PathVertex.hpp` | — | Vertex data layout to imitate |
| `src/libslic3r/TriangleSelector.hpp` | — | Spike B. **Keep, call, do not port** |
| `src/libslic3r/Model.cpp` | 3455, 3473 | `FacetsAnnotation` — C++-side serialization boundary |
| `src/libslic3r/Format/3mf.cpp` | 2808–2835 | 3MF export of painted state. Do not touch |
| `src/slic3r/GUI/Gizmos/GLGizmoPainterBase.cpp` | 1,905 | Brush, clipping, seed fill behavior (Spike B) |
| `src/slic3r/GUI/GLCanvas3D.cpp` | 10,254 | Current native viewport — the baseline to measure |
| `resources/handy_models/` | — | Bundled models. **Too small for this spike** |
