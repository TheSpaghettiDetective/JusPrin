# Production shell Phase 1: implementation record

**Status:** Phase 1 of the Agent WebView handoff (usable production shell) —
implementation and evidence record.

## User-visible behavior delivered

- On startup in editor mode, the JusPrin shell replaces the stock chrome
  inside the existing main window: a compact status row on top (project name,
  unsaved-changes marker, printer/filament/plate summary, `Check print`, and
  `Slice`), the real Prepare / G-code Preview canvas in the center, and a
  fixed right pane that shows an honest "Agent is not available yet" state.
- The stock tab strip and Plater sidebar are hidden, not destroyed. Every
  stock behavior that reads the tab selection, and the stock
  Prepare/Preview switching flow, keep running unchanged underneath.
- `Slice` drives the same dispatch as the stock slice shortcut
  (`EVT_GLTOOLBAR_SLICE_PLATE` plus the Preview tab selection). `Check print`
  and `Back to Prepare` drive the stock tab selection, so AMS checks and view
  switching behave exactly as stock.
- The shell declines to install (keeping untouched stock presentation) when:
  the app is not in editor mode, app config `jusprin_shell` is `"0"`, the
  packaged design tokens are missing or malformed, or any install step fails.
- Colors resolve from `resources/jusprin/ui/design-tokens.json` (semantic
  light and dark palettes); the panes re-apply on system appearance change.

## Architecture choice (differs from the POC shell)

The POC (`FullWindowUiSpike`, commit `462e243b46` on `jusprin-v2-poc`)
reparented the Plater out of the Notebook into a fork-owned panel. This
implementation instead keeps the Notebook, its pages, and its selection flow
completely intact and only:

1. re-sizers the Notebook next to the new panes (pure wx layout calls from
   fork code, no upstream layout edits), and
2. hides the Notebook's tab-strip control (`GetBtnsListCtrl()->Hide()`);
   wxBookCtrlBase gives a hidden controller zero size, so pages get the full
   area (verified in the dep-tree wx source, `bookctrl.cpp`
   `GetControllerSize`).

This keeps every `m_tabpanel->GetSelection()`-based predicate (menu/shortcut
enabling, `can_delete`, slice status updates) and the stock page-changed side
effects working without modification.

## Files changed

Fork-owned (new):

- `src/slic3r/GUI/JusPrin/Shell/ShellController.{hpp,cpp}` — install/restore
  controller, the `attach_shell` bootstrap policy, static ownership slot.
- `src/slic3r/GUI/JusPrin/Shell/StatusRow.{hpp,cpp}` — compact status row and
  the Slice / Check print flow.
- `src/slic3r/GUI/JusPrin/Shell/AgentPane.{hpp,cpp}` — Agent-unavailable
  region.
- `src/slic3r/GUI/JusPrin/Shell/ShellTheme.{hpp,cpp}` — semantic token
  loading (throws on missing/malformed tokens; install then falls back).
- `src/slic3r/GUI/JusPrin/sources.cmake` — registration (fork-owned list).
- `tests/shell/{shell_harness.cpp,CMakeLists.txt,Info.plist.in}` — shell
  end-to-end harness.

Upstream-owned:

- `src/slic3r/GUI/MainFrame.cpp` — one include and one call
  (`JusPrin::attach_shell(*this, m_tabpanel, m_main_sizer);`) at the end of
  layout initialization in the constructor.
- `src/slic3r/GUI/Plater.hpp` / `Plater.cpp` — neutral persistent
  sidebar-presentation policy: `set_sidebar_available` /
  `is_sidebar_available` plus a guard line in `priv::enable_sidebar`,
  mirroring the existing `m_only_gcode` guard. Required because
  `select_view_3D`, `new_project`, and `load_project` all call
  `enable_sidebar(true)` and would remount the hidden sidebar (the trap the
  POC documented).
- `tests/CMakeLists.txt` — one `add_subdirectory(shell)` line.

No upstream line was deleted or rewritten; every edit is additive.

## Rebase evidence

Simulation: `git merge-tree --write-tree --merge-base=<pre-change HEAD>
upstream/main <change>` with upstream/main at `05b3c9053e` and the fork tree
at `4376b738b0` plus this change.

- `MainFrame.cpp`, `Plater.cpp`, `Plater.hpp`: **auto-merge cleanly** against
  current upstream.
- `tests/CMakeLists.txt`: content conflict at the tail of the subdirectory
  list (upstream also appends there). Natural resolution — keep both sides'
  added lines — is correct.
- `src/slic3r/GUI/JusPrin/sources.cmake`: modify/delete in the two-endpoint
  simulation only because upstream never had the file; in a real replay of
  fork commits it arrives with the earlier fork commit. Keep ours.

Function-level churn at the seam sites, measured `8500fcdcca..upstream/main`
(fork base to upstream `05b3c9053e`):

- `Plater::priv::enable_sidebar`: **0** upstream commits.
- MainFrame constructor "initialize layout from config" block: **0** upstream
  commits.

What the fork now owns: product policy (everything under
`src/slic3r/GUI/JusPrin/`), plus one neutral seam (the sidebar-availability
policy) that upstream could plausibly accept.

## Automated verification

Build: `cmake --build build/arm64 --config RelWithDebInfo --target OrcaSlicer
workspace_contract_tests workspace_adapter_integration_harness
shell_integration_harness` — clean.

- `ctest --test-dir build/arm64/tests/workspace -C RelWithDebInfo`:
  **14/14 pass**.
- `JusPrinShellHarness` (shell mode, default): **40/40 checks pass, exit 0.**
  Covers: install state, strip hidden, Prepare main toolbar hidden with input
  disabled while the gizmo picker stays visible, sidebar policy holds against
  a stock `enable_sidebar(true)` call, rejected second install, deterministic
  two-plate/two-object fixture built from the repository STL, real canvas
  volumes and selection, Slice completing with a valid slice result in the
  real Preview, return to Prepare, resize, project replacement, and
  `detach_shell()` restoring the stock strip/sidebar/toolbar/layout.
- `JusPrinShellHarness --stock` (config `jusprin_shell=0`): **all checks
  pass, exit 0.** No shell constructed; stock strip and sidebar intact;
  fixture renders on the stock canvas.
- `JusPrinWorkspaceHarness` (pre-existing real-adapter suite, now running
  with the shell installed by default): **pass, exit 0** — the workspace
  adapter, selection, rename/duplicate/remove, undo/redo, and transform
  behaviors are unregressed under the shell.

Run the harnesses from the repo root:

```bash
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness
build/arm64/tests/shell/RelWithDebInfo/JusPrinShellHarness.app/Contents/MacOS/JusPrinShellHarness --stock
```

## Inherited failure (not a regression)

Exiting the application while model volumes are loaded can crash in
`~GLCanvas3D → reset_volumes → Selection::clear → Plater::canvas3D()`, which
re-enters the already-destroyed `Plater::priv` (null `p`). Crash reports with
the identical signature exist from 2026-08-28, before this change
(`~/Library/Logs/DiagnosticReports/JusPrinWorkspaceHarness-2026-08-28-*.ips`),
and the stock-mode harness reproduces it with the shell code inert. Both
harness scenarios end on an empty project to avoid the landmine; the defect
itself is untouched upstream teardown behavior.

## Manual acceptance status

Verified programmatically through the harness against authoritative state
(selection, plate slice validity, panel identity, layout state), and the real
`OrcaSlicer.app` was launched with an isolated `--datadir` and shipped
tokens; its log shows no shell-install failure, and the token file is present
in the packaged bundle.

The JusPrin v2 Figma file (`jo9J1sK9ZZ0vxncWnSp0vH`, Page 1 frames
`prepare-first-print-light`, `maker-at-work-tools-attach`, plus the Design
System page) was reviewed after the first implementation pass, and the status
row was realigned to it: Slice / Check print / Print as a centered action
group (Print present but disabled until the Send-preflight phase), and the
printer · material · plate pointer as a bordered chip on the right. The
frames contain no Agent-unavailable state and no dark variants; the honest
empty state and the dark palette come from this handoff and the token file,
as the handoff directs. Deliberate deviations pending a product decision:
the frames' `< Home` breadcrumb and `ORCA` wordmark are omitted (no Home
surface exists yet), and the project name + unsaved marker stay visible on
the left because the product definition requires them even though the frames
omit them.

Remaining for a human on macOS (display-level checks this environment could
not perform — screen capture permission is not granted, and pointer input to
the GL viewport is a known automation gap):

1. Orbit, pan, zoom, and click-select on the visible canvas.
2. Maximize/restore and multi-display moves.
3. Switch system light/dark appearance and confirm both palettes.
4. Side-by-side visual pass against the Figma frames.

Windows/WebView-free and Linux builds of the shell are unexercised; the code
uses only cross-platform wx and existing Orca APIs, but platform QA remains
open, matching the Phase 1 gate.

## Known Phase 1 gaps (deliberate)

- The status row refreshes on project-state events and tab changes; a preset
  change or a plain Save that does not produce a project-state event can
  leave the printer summary or dirty marker stale until the next event.
  Fixing this cleanly wants a preset-changed/dirty-changed notification on
  the project-state seam — a candidate Phase 2 addition.
- The Prepare canvas's stock main toolbar is hidden (rendering and hit
  testing) through the existing `CanvasPresentationController` seam — zero
  new upstream lines — and restored exactly on shell detach and at frame
  teardown. Add-model remains available via File > Import and drag-and-drop
  until the plates/objects rail ships. The gizmo picker and plate controls
  deliberately stay visible: hiding them before the shell's action strip
  (Phase 3+) would remove all transform access.
- The window top bar (`BBLTopbar` on Windows/Linux) is untouched — it owns
  window chrome and menus; restyling it is out of Phase 1 scope.
