# JusPrin v2 POC reference

The production branch intentionally leaves experimental code and evidence on `jusprin-v2-poc`. This index lets a future agent recover the exact plan, implementation, result, screenshot, log, or generated design artifact without importing the POC wholesale.

## Stable reference

- POC branch: `jusprin-v2-poc`
- Final audited POC commit: `9bba835b92`
- Shared production base: `8500fcdcca` (`v2.4.2`)

Read a historical file without switching branches:

```bash
git show 9bba835b92:agent-docs/orca-workspace-adapter-spike-results.md
```

Compare an experimental implementation with the production base:

```bash
git diff 8500fcdcca..9bba835b92 -- src/slic3r/GUI/JusPrin
```

Do not cherry-pick the full POC commit sequence. Its commits mix durable seams with probes, environment-variable startup policy, static UI data, evidence images, generated decks, and a self-test with known failures.

## Historical product and architecture documents

| Historical path on `jusprin-v2-poc` | Use it when |
|---|---|
| `agent-docs/orca-feature-discovery.md` | Tracing the complete source reasoning behind the canonical product definition |
| `agent-docs/native-ui-rewrite-plan.md` | Reviewing the original shell, gizmo, object pane, annotation, and Agent implementation sequence |
| `agent-docs/native-ui-risk-and-verification.md` | Reviewing the evidence-led risk method and original evidence ledger |
| `agent-docs/spike-webgl2-viewport.md` | Understanding why Electron/WebGL was investigated and then explicitly superseded |
| `agent-docs/native-ui-spike-linux-gcp-test-spec.md` | Reproducing Linux/X11/WebKitGTK platform validation or its GCP environment setup |
| `agent-docs/native-ui-spike-results.md` | Reviewing the native/WebView prototype results, platform gaps, resource observations, and WKWebView packaging lesson |
| `agent-docs/orca-workspace-adapter-spike.md` | Recovering the original minimum workspace contract and required hard events |
| `agent-docs/orca-workspace-adapter-spike-results.md` | Recovering command paths, runtime logs, stable-ID evidence, known history defects, and reproduction steps |
| `agent-docs/orca-invisible-legacy-ui-spike.md` | Understanding the prerequisite test for keeping the legacy GUI mapped and operational |
| `agent-docs/orca-invisible-legacy-ui-spike-results.md` | Reviewing concealment, focus, z-order, and top-level-window evidence |
| `agent-docs/orca-full-window-ui-coupling-spike.md` | Recovering the exact hard events and timeboxed full-window test plan |
| `agent-docs/orca-full-window-ui-coupling-spike-results.md` | Reviewing the working in-place shell, canvas, Slice/Preview, native gizmo, undo/redo, coupling, and rebase ledger |
| `agent-docs/orcaslicer-idiosyncrasies-lessons-learned.md` | Comparing the canonical integration guide with its complete historical wording and incident details |

## Historical evidence directories

- `agent-docs/orca-full-window-ui-coupling-spike-evidence/` contains Prepare, Preview, selection, orbit, pan, zoom, Move, Rotate, Undo, Redo, maximize, and legacy-regression screenshots and logs.
- `agent-docs/orca-invisible-legacy-ui-spike-evidence/` contains shell/probe screenshots, command logs, bounds, focus, z-order, and recovery evidence.

The native/WebView Spike 1 screenshots were visually inspected during its original run but were not committed. Its durable observations are in `agent-docs/native-ui-spike-results.md`.

## Historical workspace code

| Historical path | What it demonstrates | Why it remains POC-only |
|---|---|---|
| `src/slic3r/GUI/JusPrin/Workspace/Workspace.hpp` | Strong IDs, snapshots, explicit results, typed reasons, subscription lifetime, event coalescing | Revision, thread, reset, and production command semantics need refinement |
| `src/slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp` | GUI-independent contract fake | Will be migrated with the production contract rather than copied in this documentation step |
| `src/slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.*` | Real projection and commands against `Plater`, canvas, plates, selection, and history | Event coverage, history availability, and legacy-object-list dependencies are unresolved |
| `src/slic3r/GUI/JusPrin/Workspace/WorkspaceProbe.*` | Manual vertical workflow and readable event/state transcript | Separate development window with spike logging |
| `src/slic3r/GUI/JusPrin/Workspace/WorkspaceSelfTest.*` | In-process application test design and a reproduced undo-result defect | Current STL scenario exits with two known undo-related failures and uses `std::_Exit` |
| `src/slic3r/GUI/JusPrin/Workspace/SELFTEST.md` | Exact self-test setup, scenario, transcript, and ASan instructions | Documents the POC harness rather than the future production test target |
| `src/slic3r/GUI/JusPrin/Workspace/run_selftest.sh` | Convenience runner | Coupled to POC environment variables and build location |
| `tests/workspace/test_workspace_contract.cpp` | Seven fake-workspace cases and 41 assertions | Useful test seed, but it never exercises the real adapter |
| `resources/jusprin/selftest/selftest_cube.stl` | Deterministic 20 mm cube fixture | Move to test data only when the production harness is added |

Important workspace implementation commits:

- `4e278e3276` — initial workspace contract, adapter, fake, probe, Orca command seams, and tests.
- `44fa9bc4d9` — environment-gated real-adapter self-test.
- `d896c67d45` — fix for undo reporting success when nothing changed.
- `57aafffe71` — matching redo result check.
- `9fbf70f9a7` — restore legacy object-list refresh ownership to `Plater::rename_object`.

## Historical shell and canvas code

| Historical path | What it demonstrates | Why it remains POC-only |
|---|---|---|
| `src/slic3r/GUI/JusPrin/FullWindowUiSpike.*` | Reparented real `Plater`, fixed shell regions, real Slice/Preview, native Move/Rotate, and adapter-driven status | Hardcoded colors, printer data, static transcript, spike names/logging, incomplete actions, and no production WebView |
| `src/slic3r/GUI/JusPrin/GLCanvas3DWrapper.*` | Fork-owned RAII presentation policy and real gizmo activation | Good design seed, but production needs finer controls and shared-toolbar ownership |
| `src/slic3r/GUI/JusPrin/InvisibleLegacyUiSpike.*` | Separate opaque shell over a mapped legacy window | A prerequisite experiment, not the selected in-place production composition |

The corresponding generic and attachment changes are in:

- `src/slic3r/GUI/GLCanvas3D.{hpp,cpp}`
- `src/slic3r/GUI/GLToolbar.{hpp,cpp}`
- `src/slic3r/GUI/Gizmos/GLGizmosManager.{hpp,cpp}`
- `src/slic3r/GUI/MainFrame.cpp`
- `src/slic3r/GUI/Plater.{hpp,cpp}`
- `src/slic3r/GUI/GUI_ObjectList.cpp`
- `src/slic3r/CMakeLists.txt`
- `tests/CMakeLists.txt`

Important shell commits:

- `65fc3b56e1` — invisible legacy UI prerequisite implementation and evidence.
- `462e243b46` — in-place full-window shell and initial canvas presentation seam.
- `bf7a0c1e10` — product-neutral presentation options and fork-owned canvas wrapper.
- `782728ea7b` — rebase-risk and ownership lessons.

## Historical brand and design artifacts

All paths below are under `agent-docs/brand-kit-by-codex/` on `jusprin-v2-poc`.

- `outputs/JusPrin-Visual-Brand-Kit.pptx` and `outputs/JusPrin-Visual-Brand-Kit.pdf` — full brand foundation, logo system, color, typography, geometry, visual language, usage, and applications.
- `outputs/JusPrin-UI-Design-System.pptx`, `outputs/JusPrin-UI-Design-System.pdf`, and `agent-docs/brand-kit-by-codex/outputs/JusPrin-UI-Design-System.md` — complete product UI token and component guidance.
- `outputs/JusPrin-UI-Design-System-assets.zip` — packaged design deliverables.
- `outputs/JusPrin-horizontal-lockup.png` and `outputs/JusPrin-mark.{svg,png}` — curated rendered logo assets.
- `outputs/JusPrin-brand-tokens.{json,css}` and `outputs/JusPrin-UI-design-tokens.{json,css}` — source token files copied into production resource paths during documentation migration.
- `work/brand-kit/` and `work/ui-design-system-v2/` — deck builders, inspection data, source notes, rendered pages/slides, montages, template inspection, and intermediate archives.

Generated work directories, decks, PDFs, ZIPs, inspections, montages, and render trees are intentionally not production dependencies. Consult or regenerate them only for brand/design work.

## Agent WebView qualification

The POC documents describe and report a local React/TypeScript Agent-pane prototype, but the final `jusprin-v2-poc` tree does not contain that React package or its source. The full-window POC uses a static native transcript instead. Use the reported WebView behavior and packaging lessons as evidence, but implement the production Agent application fresh on `jusprin-newui`.

## Evidence qualifications to retain

- Native/WebView composition ran on macOS and Ubuntu/X11 with WebKitGTK and Mesa software rendering. Windows/WebView2, Wayland, physical-GPU Linux, and several input/IME checks remained incomplete.
- The in-place full-window shell, real Slice/Preview, selection, native Move/Rotate, and undo/redo workflow passed on macOS.
- The legacy Sidebar and `GUI_ObjectList` remained constructed and behaviorally relevant.
- The POC fake-workspace tests passed, but the real-adapter self-test retained two known failures around undoing a rename after a bare STL import.
- One unrelated placeholder-parser test crashed in the POC branch's broader test run; consult the result documents before treating historical test totals as a production baseline.
