# Headless Windows GL handoff

Verification machines without a GPU cannot run the 3D canvas on the system
OpenGL driver. This document records what that costs, how to make a build tree
render, and the one defect still open. Delete it once the open defect is closed.

## Why a Windows build tree renders a black canvas

OrcaSlicer's Windows launcher (`src/OrcaSlicer_app_msvc.cpp`) probes the system
`opengl32.dll` and, when it is missing or reports less than GL 2.0, loads
`<exe dir>\mesa\opengl32.dll` instead. A machine with no graphics driver reports
the 1.1 GDI generic implementation, so the Mesa path is always taken there.

Mesa's `opengl32.dll` is only a ~139 KB ICD stub. The implementation is
`libgallium_wgl.dll` (~59 MB), which the stub imports and which must sit in the
**executable's** directory — Windows resolves a dependent DLL against the loading
process's directory, not the importing DLL's.

Neither file is produced by CMake or checked into this repository, so **every
freshly configured build tree starts without them**. When `libgallium_wgl.dll`
is absent the stub fails to load, the process silently falls back to system
GL 1.1, and startup produces exactly one line:

```
GUI_App::post_init: glcontext not ready, postpone init
```

`GUI_App::post_init` postpones canvas initialization when the canvas is not yet
on screen or `make_current_for_postinit()` fails. The retry that follows is
inside `#ifdef __linux__`, so on Windows a single postponement is permanent: the
3D canvas stays black for the rest of the session with no further error. The
absence of a `got opengl version` line in `<datadir>\log\debug_*.log.0` is the
reliable tell.

This is a provisioning gap, not a source defect. No application code is wrong.

## Provisioning a build tree

Run `src/slic3r/GUI/JusPrin/Testing/provision-mesa-windows.ps1` against the
build directory after configuring it. Placement is not interchangeable:

| file | goes | because |
| --- | --- | --- |
| `libgallium_wgl.dll` | beside `orca-slicer.exe` | the stub imports it and Windows resolves imports against the process directory |
| `opengl32.dll` | in the `mesa\` subfolder | the launcher loads it by that exact relative path |

Putting `libgallium_wgl.dll` inside `mesa\` leaves the stub unresolvable. Putting
`opengl32.dll` beside the executable is worse: it then shadows the system GL for
every implicit import and the application dies at launch.

Confirm which GL is live by listing the process's loaded modules. A provisioned
tree shows `<exe dir>\mesa\opengl32.dll` and `<exe dir>\libgallium_wgl.dll`; an
unprovisioned one shows only `C:\Windows\SYSTEM32\OPENGL32.dll`.

## Selecting the software renderer

Run the application with `GALLIUM_DRIVER=llvmpipe`.

Left unset, Mesa selects its `d3d12` driver on WARP and the process dies during
the first canvas render (see the open defect below). The log names which driver
is live, and the GL version is a quick tell:

- `got opengl version 4.6 … llvmpipe (LLVM …)` — good;
- `got opengl version 4.2 … D3D12 (Microsoft Basic Render Driver)` — will crash.

`D3D12 (Microsoft Basic Render Driver)` is Mesa's own d3d12 gallium driver
running on WARP. It is not Microsoft's GLon12 mapping layer and not a real GPU.

## Open defect: first canvas render crashes under the d3d12 gallium driver

**Status.** Open. Worked around by `GALLIUM_DRIVER=llvmpipe`; the underlying
fault is not diagnosed.

**Symptom.** With GL initialized through Mesa's `d3d12` driver, the process
exits with code `0x80070057` the first time the canvas actually renders. There
is no error line, no crash dialog, and no Windows Application event-log entry.
The log simply stops:

```
post_init, finished init canvas3D
post_init, finished init imgui frame
post_init, start to render a first frame for test
```

That is the `plater_->canvas3D()->render(false)` call in `GUI_App::post_init`.

**Reproducible and deterministic.** Whenever the d3d12 path reaches a render, it
dies. Runs that appear to survive have not reached GL at all — a fresh datadir
stalls in first-run setup, which is visible as a much shorter log (roughly 540
lines versus 660 for a run that reaches the render). Check the log, not the exit
status.

**`slow_bootup` is not a fix.** Setting `"slow_bootup": "true"` in the datadir
config skips only the `post_init` test render. The application then reaches the
Home page, which has no GL canvas, and dies with the same `0x80070057` the moment
the 3D canvas first paints. It is useful for exercising non-canvas screens and
misleading for anything else.

**Already ruled out — do not redo this work.**

- *Not a broken driver or a broken machine.*
  `src/slic3r/GUI/JusPrin/Testing/windows-gl/wgl-smoke-test.c` loads the same two
  Mesa DLLs the launcher does, creates the same core 4.2 context the canvas asks
  for, and clears and swaps 30 frames. It exits cleanly under **both**
  `llvmpipe` and `d3d12`, in both legacy and core profiles. The d3d12 backend
  handles the context OrcaSlicer requests; re-run it before suspecting the
  driver or the machine.
- *Not Mesa reporting a handled error.* `MESA_LOG_FILE` and `MESA_DEBUG=1`
  produce no output before the process dies, which points at a hard fault rather
  than a driver error path.
- *Not the missing `libgallium_wgl.dll` above.* That defect produces a black
  canvas in a live process; this one kills the process. They were seen together
  only because fixing the first one is what lets execution reach the second.

**Where to pick it up.** The fault is something the first OrcaSlicer frame does
that the d3d12 backend rejects and llvmpipe tolerates — shader compilation,
framebuffer or texture format selection, or a vertex-array path are the obvious
candidates. Narrowing it needs a stack. No debugger is installed on the
verification machine; `cdb.exe` can be obtained without administrator rights by
downloading `https://aka.ms/windbg/download`, unpacking the `.msixbundle`, then
unpacking `windbg_win-x64.msix` and running `amd64/cdb.exe` directly. Set
`_NO_DEBUG_HEAP=1` or teardown crawls.

**Why it may not be worth fixing.** `llvmpipe` renders correctly and is the
configuration this fork verifies against. If the d3d12 path is never a supported
target, the honest resolution is to select `llvmpipe` explicitly rather than to
chase a WARP-only fault.

## Related latent behavior

Independent of either defect above: a postponed canvas initialization is
permanent on Windows and produces no user-visible error — one `[warning]` line
that does not say whether `IsShownOnScreen()` or `make_current_for_postinit()`
failed. Any machine that reaches `post_init` without usable GL therefore shows a
black canvas and no explanation. Changing that means touching
`src/slic3r/GUI/GUI_App.cpp`, an OrcaSlicer-owned file, so it needs the
justification and rebase evidence [fork stewardship](fork-stewardship.md)
requires. It was not needed to resolve the provisioning gap and has been left
alone.
