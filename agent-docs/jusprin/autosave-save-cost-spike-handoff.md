# Handoff: measure how long saving a project blocks the app

**Written:** 2026-09-29 by a Claude session working with Kenneth. **Status:** not started.
**For:** an agent with no knowledge of the conversation that produced this.
**This file is a temporary working document.** Leave it untracked. Do not commit it and do not add it to any ignore file.

## What this is

JusPrin is a fork of OrcaSlicer, an open-source program that prepares 3D models for printing. JusPrin plans to save the open project automatically a few seconds after the person stops editing, instead of asking "save changes?" when a project closes. OrcaSlicer's save runs on the main thread, the one that also draws the window and handles input, so the app is frozen while a save runs. Nobody has measured how long that freeze is for a large project.

You measure it and say whether the plan needs a cheaper kind of write for frequent use. You produce numbers and a verdict. You do not build the autosave feature.

## Background

The planned design has three kinds of write. All three go through the same OrcaSlicer function, `Plater::export_3mf` in `src/slic3r/GUI/Plater.cpp`, with different option flags (`SaveStrategy` in `src/libslic3r/Format/bbs_3mf.hpp`).

| Kind of write | How it is called | What it writes | How often the design wants it |
|---|---|---|---|
| **Full save** | `Plater::save_project()`, which calls `export_3mf(path, SplitModel \| ShareMesh)` | Four thumbnails per plate, rendered with OpenGL. Every mesh, as compressed XML. All settings. Attached files. | A few seconds after each burst of edits, and on close |
| **Checkpoint** (a point-in-time copy kept so the person can go back) | `IWorkspace::export_project_archive(path)` in `src/slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.cpp`, which calls `export_3mf(path, Silence \| SplitModel \| ShareMesh \| SkipAuxiliary)` | The same, without attached files | Before each AI agent turn that changes the project, every 10 to 30 minutes of manual editing, before a restore |
| **Backup write** (OrcaSlicer's existing crash protection) | A timer calls `export_3mf(<backup folder>/.3mf, SaveStrategy::Backup)`; see `src/slic3r/GUI/MainFrame.cpp` line 674 | Settings and the layout of plates and objects. No thumbnails. No meshes: a background thread writes each object's mesh to its own file only when that mesh changes. | Every 10 seconds when something changed (the default) |

If a full save is fast enough at every project size, it can be the only frequent write. If not, the design falls back to two tiers: the backup write runs often, and full saves run only when the person is idle, on close, and at checkpoints.

## The question

For each of the three kinds of write: **how long is the main thread blocked, as a function of how big the project is and what it contains, and which step accounts for the time?**

Do not assume thumbnails or meshes are the cause. Measure the steps and report what you find.

## What is already known

| Fact | Source | Confidence |
|---|---|---|
| On a small two-plate project, a checkpoint took about 170 ms and was dominated by thumbnail rendering; a full save took about 180 ms and wrote 220 KB. | Notes from an earlier JusPrin feature, 2026-08-30. The code that measured it was later removed. | Indicative only. Build type and machine were not recorded. |
| A model of 199,080 triangles becomes 16.0 MB of XML, which compresses to 3.1 MB in the project file. | Measured 2026-09-29 with the command-line export. | Measured once |
| Meshes of different objects are written in parallel (`tbb::parallel_for` in `_add_model_file_to_archive`, `src/libslic3r/Format/bbs_3mf.cpp` line 7021). One very large object cannot use more than one core. | Code reading | Read, not measured |
| The backup write already logs its own duration under the label "backup cost" (`bbs_3mf.cpp` line 8811). | Code reading | Read |

## Thresholds for the verdict

These are proposed by the author of this handoff. Kenneth has not confirmed them, so report raw numbers as well.

| Main thread blocked for | Meaning |
|---|---|
| Under 100 ms | Not noticeable. Safe at any time. |
| 100 to 500 ms | Acceptable only while the person is idle. |
| Over 500 ms on the Medium fixture, or over 1 s on the Large fixture | The risk is real. The design needs the two-tier approach. |

## Fixtures

| Name | Contents | Why |
|---|---|---|
| Tiny | The two-plate cube project the harness already builds (`tests/workspace/real_adapter_harness.cpp` lines 399 to 414) | Compare with the earlier 170 to 180 ms |
| Typical | One model, `resources/handy_models/3DBenchy.drc` | The everyday case |
| Medium | 10 different objects of 100,000 triangles each, on 2 plates | A busy project |
| Large | 1 object of 2,000,000 triangles (about 100 MB as a binary STL file) | A scan or sculpt |
| Huge (optional) | 1 object of 5,000,000 triangles (about 250 MB) | Stress |
| Many | 200 different objects of 2,000 triangles each, on 10 plates | Cost that grows with plates and objects, not triangles |

The appendix has a script that generates a model with a chosen triangle count. Use a different seed per object so the meshes differ; identical meshes are stored once. Keep generated files in your scratch folder, not in the repository, and delete them when done.

## What to measure

For every fixture and every kind of write:

1. **Time the main thread is blocked.** Wrap the call in `std::chrono::steady_clock`. Do one cold run and five warm runs. Report the median and the worst.
2. **Bytes written.**
3. **Time per step.** `Plater::export_3mf` takes a progress callback as its fourth parameter (`Export3mfProgressFn`). It is called at each stage of the export. Record a timestamp at every call. The time before the first call is thumbnail rendering and preparation.
4. **For the backup write only:** after an edit that changes a mesh, how long until that object's mesh file appears in the backup folder. This does not block the app, but it is the window in which a crash loses the change.

Record the machine (the handoff author's is an Apple M2 Pro, 12 cores, 32 GB), the commit, and the build type.

**Build type trap.** This repository compiles the `RelWithDebInfo` build type with optimization turned off (`CMakeLists.txt` lines 533 to 549). Numbers from it overstate the cost. Measure a `Release` build. If you also report `RelWithDebInfo`, label it. Nobody has confirmed that the harness builds in `Release` on this machine; if it does not, say so and report what you could measure.

## How to run it

**Worktree.** Create your own. Never reuse an existing one under `.claude/worktrees/`, even if it looks idle; its owner may resume.

```bash
git -C /Users/kenneth/Projects/JusPrin worktree add -b spike/autosave-save-cost /Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost jusprin-newui
```

**Configure.** Use the cmake in `/Applications/CMake.app/Contents/bin`, not Homebrew's. A failed configure poisons the cache; delete the build folder before retrying. The two `llvm-ar` flags are required on a fresh build folder.

```bash
/Applications/CMake.app/Contents/bin/cmake -S <worktree> -B <worktree>/build/arm64 -G "Ninja Multi-Config" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_PREFIX_PATH="/Users/kenneth/Projects/JusPrin/deps/build/arm64/OrcaSlicer_dep/usr/local;/Users/kenneth/Projects/OrcaSlicer/deps/build/arm64/OrcaSlicer_dep/usr/local" -DSLIC3R_GUI=ON -DSLIC3R_STATIC=ON -DSLIC3R_PCH=ON -DBUILD_TESTS=ON -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib
```

**Build.** The target is `workspace_adapter_integration_harness`. Always pass the build folder as an absolute path: the shell's working directory resets between commands, and a relative path silently builds the main checkout instead.

```bash
/Applications/CMake.app/Contents/bin/cmake --build <worktree>/build/arm64 --config Release --target workspace_adapter_integration_harness
```

A fresh build is several hundred compile steps. A build started in the background is killed when the tool's timeout fires, and the log then ends with "interrupted by user". Start long builds detached with `nohup`, write to a log, and wait for the log to show the exit code. Do not end your turn while a build or a run is still pending; nothing resumes you.

**Where to put the probe.** `tests/workspace/real_adapter_harness.cpp` starts the real application with an isolated settings folder and runs its checks in one pass. Add a mode flag next to `--manual-stock` in `main` (line 869), and run your probe after the OpenGL check at line 390, because exporting before the canvas has initialized OpenGL crashes. Build each fixture with `new_project(true, true)` and `load_files(...)`, as lines 399 to 414 do. Print one line per measurement so results can be collected with `grep`.

**Harness facts.**

- It sets both the settings folder and the temporary folder to a fresh folder under the system temp directory. OrcaSlicer's backup folder is created inside it, under `orcaslicer_model/`.
- Its settings file is `tests/data/jusprin/harness.conf`. Backup is on by default with a 10 second interval (`src/libslic3r/AppConfig.cpp` lines 532 to 537).
- On macOS this harness must exit through its existing path. Closing its window instead crashes after the results are printed.
- `Plater::export_3mf` refuses any path that does not end in `.3mf`.

## Rules

- No change to an OrcaSlicer-owned file should be needed. OrcaSlicer-owned means anything outside `src/slic3r/GUI/JusPrin/` and `tests/`. If you believe one is needed, stop and explain why in the findings.
- Probe code stays on your spike branch. Do not merge it and do not push to `jusprin-newui`.
- Do not start, drive, or stop any other running JusPrin or OrcaSlicer instance.
- Report what happened, including anything that failed or could not be measured.

## Deliverable

One file, left untracked: `/Users/kenneth/Projects/JusPrin/agent-docs/jusprin/autosave-save-cost-findings.md`. It contains:

1. Machine, build type, commit.
2. A results table: fixture, kind of write, median and worst time, bytes.
3. The time per step for the Medium and Large fixtures.
4. The verdict against the thresholds above.
5. A recommendation: full save as the only frequent write, or two tiers, with the numbers that support it.
6. What could not be measured and why.
7. The spike branch name and the exact command that reruns the measurement.

## Out of scope

- Building autosave.
- Disk space used by checkpoints. A separate handoff covers it: `agent-docs/jusprin/autosave-checkpoint-storage-handoff.md`.
- Windows and Linux.
- Changing the exporter.

## A related defect, so it does not surprise you

On 2026-09-29 the handoff author reproduced this with the command-line export: when the disk is nearly full, the export reports success and replaces the existing good project file with a damaged one. With 1.5 MB free the result was a truncated file that is not a valid archive. With 40 KB free the result was a valid archive that is missing the mesh. Keep at least twice the fixture size free on the volume you write to, or your timings will describe failed writes.

## Appendix: model generator

Requires Python 3 with numpy. Usage: `python3 gen_stl.py out.stl <triangles> <seed>`.

```python
#!/usr/bin/env python3
"""Write a closed, bumpy sphere as a binary STL with roughly the requested triangle count.
The surface noise keeps the vertex text from compressing unrealistically well."""
import sys, struct
import numpy as np

def main(path, triangles, seed=1, radius=40.0):
    n = max(8, int(round((triangles / 2) ** 0.5)))      # n x n quads -> 2*n*n triangles
    rng = np.random.default_rng(seed)
    theta = np.linspace(0.0, np.pi, n + 1)
    phi = np.linspace(0.0, 2 * np.pi, n, endpoint=False)
    t, p = np.meshgrid(theta, phi, indexing="ij")
    r = radius + rng.normal(0.0, 0.15, size=t.shape)
    r[0, :] = radius; r[-1, :] = radius                 # single pole points
    x = r * np.sin(t) * np.cos(p); y = r * np.sin(t) * np.sin(p); z = r * np.cos(t) + radius
    v = np.stack([x, y, z], axis=-1).astype(np.float32)
    i0 = np.arange(n)[:, None]; j0 = np.arange(n)[None, :]; j1 = (j0 + 1) % n
    a = v[i0, j0]; b = v[i0 + 1, j0]; c = v[i0 + 1, j1]; d = v[i0, j1]
    tris = np.concatenate([np.stack([a, b, c], axis=2).reshape(-1, 3, 3),
                           np.stack([a, c, d], axis=2).reshape(-1, 3, 3)])
    e1 = tris[:, 1] - tris[:, 0]; e2 = tris[:, 2] - tris[:, 0]
    nrm = np.cross(e1, e2); ln = np.linalg.norm(nrm, axis=1)
    keep = ln > 1e-9; tris = tris[keep]; nrm = (nrm[keep] / ln[keep, None]).astype(np.float32)
    rec = np.zeros(len(tris), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"] = nrm; rec["v"] = tris
    with open(path, "wb") as f:
        f.write(b"jusprin spike fixture".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        rec.tofile(f)
    print(f"{path}: {len(tris)} triangles")

if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 1)
```
