# Handoff: how much disk do project checkpoints need, and can OrcaSlicer's backup layout make them cheap?

**Written:** 2026-09-29 by a Claude session working with Kenneth. **Status:** not started.
**For:** an agent with no knowledge of the conversation that produced this.
**This file is a temporary working document.** Leave it untracked. Do not commit it and do not add it to any ignore file.

## What this is

JusPrin is a fork of OrcaSlicer, an open-source program that prepares 3D models for printing. JusPrin plans to keep point-in-time copies of the open project, called checkpoints here, so a person can go back to an earlier state, including the state before the AI agent changed something. The simplest checkpoint is a full copy of the project file, which repeats every 3D mesh each time. Kenneth asked: **if checkpoints used the approach of OrcaSlicer's crash backup, which avoids rewriting meshes, would disk use still be a problem?**

You answer that with measurements, prove that the storage scheme you recommend can actually be restored, and propose a retention rule. You do not build the checkpoint feature.

## Decisions already made

Kenneth agreed to these on 2026-09-29. Do not reopen them.

1. JusPrin never saves over a file it did not write (another slicer's project, a download, a plain model file). It opens such a file as a draft copy.
2. Checkpoints live in the user settings folder, keyed by a project id. They are not stored inside the project file.
3. Settings edits belong to the project by default. Saved presets change only by an explicit act.

## Preliminary answer

This comes from reading code. Nothing here has been measured.

- **The backup cannot be the history as it stands.** It is one live folder that OrcaSlicer overwrites in place, empties on every full save, and deletes when the project closes. A checkpoint has to be a copy that never changes.
- **Its layout is the useful part.** It keeps small data (settings, the arrangement of plates and objects) apart from large data (one mesh file per object), and it rewrites a mesh file only when that object's mesh changes. A checkpoint store built the same way would keep each distinct version of a mesh once.
- **Expected result.** Disk use would grow with the number of edits that change a mesh, not with the number of checkpoints. Sessions that only move objects and change settings would cost kilobytes per checkpoint. Sessions that repeatedly cut, repair, or paint a large object would still cost one full mesh per change.

Your job is to replace "expected" with numbers.

## How the backup works

Read from `src/libslic3r/Format/bbs_3mf.cpp` (class `_BBS_Backup_Manager`, line 8591) and `src/libslic3r/Model.cpp` (`Model::get_backup_path`, line 959). Confirm each row by watching the folder during a live session.

The folder is `<temporary folder>/orcaslicer_model/<date>/<time>#<process id>#<model id>/`.

| Item in the folder | What it is | Written by |
|---|---|---|
| `.3mf` | A small archive: settings, plates, object list and placement. No meshes, no thumbnails. | The main thread, on a timer, every 10 seconds by default when something changed |
| `3D/Objects/<object name>_<backup id>.model` | One file per object. It is itself a small zip archive that holds the object's mesh as XML. Paint data (supports, seams, colors) is stored per triangle inside it. | A background thread, a few seconds after that object's mesh changes |
| `Auxiliaries/` | Files attached to the project. JusPrin keeps its conversation state here, in `Auxiliaries/JusPrin/state.json`. | OrcaSlicer and JusPrin |
| `origin.txt` | The path of the project file this session was opened from | The project loader |
| `lock.txt` | The process id of the owner | `Model::get_backup_path` |

| Event | What happens to the folder |
|---|---|
| A whole object is moved, rotated, or scaled | Nothing is written to `3D/Objects/`. Only the next `.3mf` changes. |
| A part inside an object is moved, or a mesh is cut, repaired, replaced, or painted | That object's mesh file is rewritten, in place |
| The project is saved in full | `.3mf` and everything in `3D/Objects/` are deleted (`RemoveBackup` task, line 8857). From then on the saved project file is the base and the folder holds only what changed since. |
| The project is closed, or another is opened | The whole folder is deleted |
| The app crashes | The folder stays. On the next start OrcaSlicer offers to restore from it. |

When OrcaSlicer restores from this folder it reads `.3mf`, and for any mesh the archive refers to but does not contain, it looks first in the folder and then in the file named by `origin.txt` (`_extract_from_archive`, line 2381).

**Consequence for checkpoints.** A checkpoint that holds only the small archive depends on mesh files that OrcaSlicer will overwrite or delete, and on a project file that the next save will replace. A checkpoint store must therefore keep its own copy of every mesh version that any retained checkpoint refers to.

## Questions to answer

1. **Is the description above correct?** Watch the backup folder during a real session and record what is written, when, and how large.
2. **Is an unchanged object's mesh file byte-for-byte identical from one write to the next?** Storing each mesh once depends on this. Test it in the backup folder and inside full project archives. In a full save, object ids are numbered in order, so adding an unrelated object may change the bytes of an unchanged one. In a backup write, ids come from a stable per-object backup id (`Model::get_object_backup_id`, `Model.cpp` line 606). Also test a rename: the object name is part of the file name.
3. **How much disk does each scheme below use** across the editing sessions below, per checkpoint and in total?
4. **Can the scheme you recommend be restored?** See "Restore round trip".
5. **What retention rule and size cap follow from the numbers?** Give worked examples for a small, a medium, and a large project over a week of use.
6. **How long does creating a checkpoint block the main thread under each scheme?** A separate spike measures write time in depth (`agent-docs/jusprin/autosave-save-cost-spike-handoff.md`). Use its findings file if it exists; otherwise a rough number is enough.

## Storage schemes to compare

| Scheme | A checkpoint is | How it is produced |
|---|---|---|
| **Full copy** (baseline) | A complete project archive | `IWorkspace::export_project_archive(path)` in `src/slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.cpp` line 1760 |
| **Backup-style** | A small archive of settings and layout, plus references to mesh files kept once each, named by a hash of their content | `Plater::export_3mf(path, SaveStrategy::Backup)` for the small archive, and copies of the backup folder's mesh files |
| **Full copy, split afterwards** | The same references and mesh store, but made by taking a full archive apart | Write a full archive, then move each entry into a store named by content hash, and keep a list of entries per checkpoint |

You may add a scheme if the evidence points to a better one. Say why.

## Editing sessions to simulate

Run each session on three project sizes. The appendix has a script that generates a model with a chosen triangle count.

| Project | Contents |
|---|---|
| Small | 1 object, 50,000 triangles |
| Medium | 10 different objects, 100,000 triangles each, on 2 plates |
| Large | 1 object of 2,000,000 triangles, plus 5 objects of 50,000 |

| Session | Edits | Checkpoints |
|---|---|---|
| Layout | 40 edits: move, rotate, scale, change a setting, move an object to another plate | One after every 4 edits |
| Mesh changes | Cut an object in two, repair one, delete one, import one, merge two | One after each |
| Painting | Paint supports on the largest object in 5 separate strokes | One after each stroke |
| Mixed | 30 layout edits with 3 mesh changes spread among them | One after every 3 edits |

The fork's workspace interface can perform these edits without simulating mouse input. See `IWorkspace` in `src/slic3r/GUI/JusPrin/Workspace/Workspace.hpp` line 1334: `place_object`, `lay_out`, `apply_settings`, `divide_object`, `repair_object`, `merge_objects`, `delete_items`, `import_objects`, `apply_regions`. If painting cannot be scripted, say so and measure the other three.

## Restore round trip

A checkpoint that cannot be restored is worth nothing. For the scheme you recommend, show this sequence working on the Medium project:

1. Take a checkpoint. Record the state: object count, each object's triangle count and placement, plate membership, and the settings in force.
2. Change a mesh, delete an object, and save the project in full. These are the three events that destroy what a backup-style checkpoint would otherwise depend on.
3. Rebuild a loadable project from the store, load it, and compare with the recorded state.

**Two ways to load a rebuilt project, and their traps.** Both are read from code. Neither was run for this handoff.

| Way | What it does | Traps |
|---|---|---|
| `Plater::reset(false)`, then `Plater::load_files({archive}, LoadModel \| LoadConfig \| Silence)`, then `Plater::clear_undo_redo_stack_main()` | Loads an ordinary archive. An earlier JusPrin feature used exactly this and measured 0.8 to 0.9 s on a small project. The code is in git history: `git show 67c190a40c:src/slic3r/GUI/JusPrin/Workspace/OrcaWorkspaceAdapter.cpp`, function `restore_project_archive`. | `reset` clears the project's file name, so it must be set again with the public `Plater::set_project_filename`. The project gets a new backup folder and the old one is deleted, so JusPrin's conversation state has to be carried across. |
| `Plater::load_project(<folder>/.3mf, <original project path>)`, OrcaSlicer's own crash-restore entry | Loads from a folder laid out like the backup folder and keeps the original project's name | The loader takes the path, drops its last five characters, and adopts the result as the live backup folder (`bbs_3mf.cpp` line 1405). OrcaSlicer later deletes that folder. **Never point it at a file inside the checkpoint store.** Copy the checkpoint into a scratch folder first. It also does not unpack attached files from the archive, and it asks the save question before loading. |

## How to run it

**Worktree.** Create your own. Never reuse an existing one under `.claude/worktrees/`, even if it looks idle; its owner may resume.

```bash
git -C /Users/kenneth/Projects/JusPrin worktree add -b spike/autosave-checkpoint-storage /Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-checkpoint-storage jusprin-newui
```

**Configure.** Use the cmake in `/Applications/CMake.app/Contents/bin`, not Homebrew's. A failed configure poisons the cache; delete the build folder before retrying. The two `llvm-ar` flags are required on a fresh build folder.

```bash
/Applications/CMake.app/Contents/bin/cmake -S <worktree> -B <worktree>/build/arm64 -G "Ninja Multi-Config" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_PREFIX_PATH="/Users/kenneth/Projects/JusPrin/deps/build/arm64/OrcaSlicer_dep/usr/local;/Users/kenneth/Projects/OrcaSlicer/deps/build/arm64/OrcaSlicer_dep/usr/local" -DSLIC3R_GUI=ON -DSLIC3R_STATIC=ON -DSLIC3R_PCH=ON -DBUILD_TESTS=ON -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib
```

**Build.** The target is `workspace_adapter_integration_harness`. Disk sizes do not depend on the build type, so the usual `RelWithDebInfo` is fine for everything except question 6; that build type has optimization turned off in this repository and overstates times. Always pass the build folder as an absolute path: the shell's working directory resets between commands, and a relative path silently builds the main checkout instead.

```bash
/Applications/CMake.app/Contents/bin/cmake --build <worktree>/build/arm64 --config RelWithDebInfo --target workspace_adapter_integration_harness
```

A fresh build is several hundred compile steps. A build started in the background is killed when the tool's timeout fires, and the log then ends with "interrupted by user". Start long builds detached with `nohup`, write to a log, and wait for the log to show the exit code. Do not end your turn while a build or a run is still pending; nothing resumes you.

**Where to put the probe.** `tests/workspace/real_adapter_harness.cpp` starts the real application with an isolated settings folder and runs its checks in one pass. Add a mode flag next to `--manual-stock` in `main` (line 869), and run your probe after the OpenGL check at line 390, because exporting before the canvas has initialized OpenGL crashes. The harness creates an `OrcaWorkspaceAdapter` at line 416; drive the edits through it.

**Harness facts.**

- It sets both the settings folder and the temporary folder to a fresh folder under the system temp directory, so the backup folder is inside it, under `orcaslicer_model/`.
- Backup is on by default with a 10 second interval (`src/libslic3r/AppConfig.cpp` lines 532 to 537). The timer needs the event loop to run; a probe that never returns to the event loop will never see a backup write.
- On macOS this harness must exit through its existing path. Closing its window instead crashes after the results are printed.
- `Plater::export_3mf` refuses any path that does not end in `.3mf`.

## Rules

- No change to an OrcaSlicer-owned file should be needed. OrcaSlicer-owned means anything outside `src/slic3r/GUI/JusPrin/` and `tests/`. If you believe one is needed, stop and explain why in the findings.
- Probe code stays on your spike branch. Do not merge it and do not push to `jusprin-newui`.
- Do not start, drive, or stop any other running JusPrin or OrcaSlicer instance.
- Keep generated models and checkpoint stores in your scratch folder and delete them when done. The author's machine had about 100 GB free.
- Say which statements you measured and which you read from code.

## Deliverable

One file, left untracked: `/Users/kenneth/Projects/JusPrin/agent-docs/jusprin/autosave-checkpoint-storage-findings.md`. It contains:

1. A direct answer to Kenneth's question, in two or three sentences.
2. Corrections to "How the backup works", if any.
3. The answer to question 2, with the byte comparisons that support it.
4. A table: project size, session, scheme, bytes per checkpoint, total bytes.
5. The restore round trip result: what was compared, and whether it matched.
6. A recommended scheme, retention rule, and size cap, with the worked examples.
7. What could not be measured and why.
8. The spike branch name and the exact command that reruns the measurement.

## Out of scope

- Building the checkpoint feature or its user interface.
- How long a full save blocks the app; the other handoff covers it.
- Storing history inside the project file. Decision 2 rules it out.
- Windows and Linux.

## A related defect, so it does not surprise you

On 2026-09-29 the handoff author reproduced this with the command-line export: when the disk is nearly full, the export reports success and replaces the existing good file with a damaged one. With 40 KB free, the result was a valid archive that was missing its mesh. Two things follow. Keep plenty of free space while measuring. And when you check a restored project, check that every mesh is present; an archive that opens is not proof.

## Appendix: model generator

Requires Python 3 with numpy. Usage: `python3 gen_stl.py out.stl <triangles> <seed>`. Use a different seed per object so the meshes differ.

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
