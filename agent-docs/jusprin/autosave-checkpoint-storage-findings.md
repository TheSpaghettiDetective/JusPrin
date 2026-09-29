# Findings: how much disk project checkpoints need, and whether OrcaSlicer's backup layout makes them cheap

**Written:** 2026-09-29. **Answers:** `agent-docs/jusprin/autosave-checkpoint-storage-handoff.md`.
**This file is a temporary working document.** Leave it untracked. Do not commit it and do not add it to any ignore file.

A checkpoint here is a point-in-time copy of the open project that a person can go back to. "Measured" means a probe in the real application produced the number. "Read from code" means it was not run.

## 1. Direct answer

**No, disk use stops being a problem if checkpoints use the backup layout.** That layout is a small archive of settings and placement per checkpoint, plus each distinct mesh stored once. A checkpoint after moving objects or changing settings costs 15 to 17 KB at every project size, against 1 to 35 MB for a full copy. Over ten such checkpoints on the Large project that is 35 MB in total instead of 389 MB. Disk use then grows only with edits that change a mesh. Painting counts as one, because OrcaSlicer stores paint inside the mesh file, so each paint stroke on a 2,000,000-triangle object adds another 31 MB. (Measured.)

**But OrcaSlicer's backup folder cannot be where the meshes come from.** Three measured problems:

- It writes each changed mesh at least 3 s later, one object at a time.
- It never wrote the mesh after a repair or after JusPrin's region paint.
- A full save deletes its mesh files.

The recommended scheme keeps the backup layout, but JusPrin writes the mesh files itself, on a background thread, from a copy of the model. The copy takes under 0.5 ms on the main thread. A project rebuilt that way restored exactly (section 5).

## 2. Corrections to "How the backup works"

| Handoff statement | What was found | How |
|---|---|---|
| `.3mf` is a small archive with no meshes and no thumbnails | Correct. 15 to 16 KB for all three projects. | Measured |
| A mesh file is written "a few seconds after that object's mesh changes" | True only for some edits, and only one object at a time. Details follow the table. | Measured |
| Moving, rotating or scaling a whole object writes nothing to `3D/Objects/` | Correct, and it also holds for moving an object to another plate and for settings changes. Across 30 checkpoints (3 projects × 10), the probe waited 9 s before each one and saw no mesh file written. | Measured |
| A full save deletes `.3mf` and everything in `3D/Objects/` | Correct: the folder went from 12 mesh files to 0. Hard links to those files made before the save stayed intact (10 of 10). | Measured |
| `origin.txt` is written by the project loader | Also rewritten by a full save, which points it at the saved file. OrcaSlicer's own 10-second backup write is not marked silent, so by the code it would also point `origin.txt` at the backup's own `.3mf`. That second part was not observed. | Measured; the timer part read from code |
| (not in the handoff) | Deleting or renaming an object leaves its old mesh file in the folder until the next full save or close. The deletion task removes `mesh_<id>.xml`, a name that is never written, and `delete_object_mesh` is a no-op. The round trip saw 12 mesh files for 10 objects. | Read from code; the 12 files measured |
| (not in the handoff) | Rewriting an unchanged mesh gives a file with different bytes and a new inode. The mesh inside is identical; only the zip timestamp differs. | Measured |

**When a mesh file is written, and when it is not.**

| Edit | Mesh file in place after |
|---|---|
| Cut, import or merge | 3.1 to 3.5 s (Small and Medium), 7.0 s after cutting the 2,000,000-triangle object |
| Loading several objects | 29 s for Medium's 10 objects, 19 s for Large's 6 |
| JusPrin's `repair_object` | Never, within 20 s, on all three projects (3 of 3) |
| OrcaSlicer's own Fix model command (`ObjectList::fix_through_cgal`) | Never, within 30 s (1 of 1) |
| JusPrin's region paint (`apply_regions`) | Never, within 20 s, for every stroke on every project (18 of 18) |

- JusPrin's repair and OrcaSlicer's Fix model replace the mesh with `set_mesh` and never call `Slic3r::save_object_mesh`.
- JusPrin's region paint (`OrcaWorkspaceAdapter::change_regions`) does not call it either. OrcaSlicer's own paint tools do (`GLGizmoFdmSupports.cpp:184`, read from code).
- After each missed edit, the probe called `save_object_mesh` itself, as the paint tools do, and the file then arrived 3.1 to 7.7 s later.

The paint gap is JusPrin's own and is worth fixing whatever happens to checkpoints: today a crash after the agent paints loses that paint from OrcaSlicer's crash backup. The repair gap is inherited from OrcaSlicer.

## 3. Is an unchanged object's mesh file byte-for-byte identical from one write to the next?

**Not as a file, but the mesh text inside it is, except for one number.** (Measured on the Medium project.)

**In the backup folder:**

| Change | Result |
|---|---|
| OrcaSlicer rewrote an unchanged object | Different file bytes and a new inode. The mesh text inside is identical, with the same CRC; only the zip entry's timestamp moved. |
| Renamed an object | A new file under the new name. The mesh text inside is identical; only the zip entry's name changed. |
| Imported a new object, then deleted another | The other 10 files were untouched. |

**In full project archives:**

| Pair compared | Archive bytes | Mesh entries of unchanged objects |
|---|---|---|
| Two saves with nothing changed | Differ (timestamps) | 10 of 10 identical |
| Before and after renaming an object | Differ | 9 of 9 others identical |
| Before and after importing an object (added at the end) | Differ | 10 of 10 identical |
| Before and after deleting the first object | Differ | **0 of 10 identical** |

Deleting the first object renumbers every later object. The only difference is one line near the top of each mesh entry, for example `<object id="7" …>` becoming `<object id="5" …>`. The rest of each entry was identical.

**Backup file against full archive, same object:** 0 of 10 equal. Again the only difference is that line: the backup writes `id="65539"` (the backup id with a part number) where the full save writes `id="5"`. The two mesh files differ by exactly the 4 characters of the id.

**Consequence.** A store must key each mesh by its text with the `<object id="…">` value blanked, not by file bytes, not by inode, and not by entry name. With that key, one stored copy serves a backup write, a full save, a renumbered full save and a renamed object.

## 4. Disk use per scheme

The fixtures were generated bumpy spheres. The sessions ran as the handoff describes, through `IWorkspace`, with one checkpoint when the project opened (`c00`) plus the ones the handoff lists.

"Later" is the bytes each later checkpoint adds to a store that keeps each distinct piece once. Each stored checkpoint is also charged 64 bytes per referenced mesh for its list of contents.

- **Full copy:** a whole project archive per checkpoint.
- **Split afterwards:** full archives taken apart, each entry stored once by the hash of its content.
- **Split, normalized:** the same, with mesh entries keyed as section 3 recommends.
- **Backup layout:** OrcaSlicer's small archive plus each mesh file once, keyed by the mesh inside. This is also the disk profile of the recommended scheme.

| Project | Session (checkpoints) | Scheme | First | Later: median | Later: largest | Total |
|---|---|---|---:|---:|---:|---:|
| Small (1 × 50,000) | Layout (11) | Full copy | 958 KB | 936 KB | 958 KB | 10.3 MB |
| | | Split afterwards | 953 KB | 140 KB | 156 KB | 2.35 MB |
| | | Split, normalized | 953 KB | 140 KB | 156 KB | 2.35 MB |
| | | **Backup layout** | 798 KB | **15.3 KB** | 15.3 KB | **951 KB** |
| | Mesh changes (6) | Full copy | 958 KB | 1.39 MB | 1.39 MB | 6.63 MB |
| | | Split afterwards | 953 KB | 788 KB | 1.20 MB | 4.80 MB |
| | | **Backup layout** | 798 KB | 802 KB | 1.21 MB | **4.46 MB** |
| | Painting (6) | Full copy | 958 KB | 961 KB | 964 KB | 5.76 MB |
| | | Split afterwards | 953 KB | 788 KB | 790 KB | 4.89 MB |
| | | **Backup layout** | 798 KB | 802 KB | 804 KB | **4.81 MB** |
| | Mixed (11) | Full copy | 958 KB | 1.02 MB | 1.78 MB | 12.1 MB |
| | | Split afterwards | 953 KB | 186 KB | 953 KB | 3.78 MB |
| | | **Backup layout** | 798 KB | 15.5 KB | 831 KB | **2.55 MB** |
| Medium (10 × 100,000, 2 plates) | Layout (11) | Full copy | 16.0 MB | 16.0 MB | 16.0 MB | 175.7 MB |
| | | Split afterwards | 16.0 MB | 322 KB | 347 KB | 19.0 MB |
| | | **Backup layout** | 15.7 MB | **17.1 KB** | 17.1 KB | **15.8 MB** |
| | Mesh changes (6) | Full copy | 16.0 MB | 15.3 MB | 16.0 MB | 92.2 MB |
| | | Split afterwards | 16.0 MB | 12.7 MB | 15.8 MB | 61.0 MB |
| | | Split, normalized | 16.0 MB | 789 KB | 3.14 MB | 22.6 MB |
| | | **Backup layout** | 15.7 MB | 804 KB | 3.15 MB | **22.1 MB** |
| | Painting (6) | Full copy | 16.0 MB | 16.0 MB | 16.0 MB | 96.0 MB |
| | | Split afterwards | 16.0 MB | 1.57 MB | 1.58 MB | 23.8 MB |
| | | **Backup layout** | 15.7 MB | 1.59 MB | 1.59 MB | **23.6 MB** |
| | Mixed (11) | Full copy | 16.0 MB | 16.0 MB | 16.8 MB | 177.4 MB |
| | | Split afterwards | 16.0 MB | 295 KB | 16.0 MB | 35.0 MB |
| | | Split, normalized | 16.0 MB | 295 KB | 1.90 MB | 20.9 MB |
| | | **Backup layout** | 15.7 MB | 17.3 KB | 1.62 MB | **18.2 MB** |
| Large (1 × 2,000,000 + 5 × 50,000, 2 plates) | Layout (11) | Full copy | 35.4 MB | 35.4 MB | 35.5 MB | 389.4 MB |
| | | Split afterwards | 35.4 MB | 178 KB | 261 KB | 37.3 MB |
| | | **Backup layout** | 35.2 MB | **16.4 KB** | 16.5 KB | **35.4 MB** |
| | Mesh changes (6) | Full copy | 35.4 MB | 35.5 MB | 35.5 MB | 212.3 MB |
| | | Split afterwards | 35.4 MB | 33.9 MB | 35.5 MB | 141.8 MB |
| | | Split, normalized | 35.4 MB | 863 KB | 31.5 MB | 70.3 MB |
| | | **Backup layout** | 35.2 MB | 803 KB | 31.5 MB | **69.9 MB** |
| | Painting (6) | Full copy | 35.4 MB | 35.4 MB | 35.4 MB | 212.4 MB |
| | | Split afterwards | 35.4 MB | 31.3 MB | 31.3 MB | 191.9 MB |
| | | **Backup layout** | 35.2 MB | **31.3 MB** | 31.3 MB | **191.8 MB** |
| | Mixed (11) | Full copy | 35.4 MB | 35.6 MB | 36.4 MB | 392.2 MB |
| | | Split afterwards | 35.4 MB | 166 KB | 35.5 MB | 72.9 MB |
| | | Split, normalized | 35.4 MB | 166 KB | 31.6 MB | 69.0 MB |
| | | **Backup layout** | 35.2 MB | 16.6 KB | 31.5 MB | **67.7 MB** |

Rows for "Split, normalized" are left out where they equal "Split afterwards".

**What the numbers say:**

- **Layout and settings edits:** the backup layout costs about 16 KB per checkpoint. Split afterwards costs 140 to 350 KB, and 92% of that is the five thumbnails per plate, which are re-rendered on every save. (Medium, checkpoint 1: 164 KB of PNGs out of 178 KB.)
- **Mesh edits:** each costs one compressed mesh, about 15.7 bytes per triangle: 0.78 MB for 50,000 triangles, 1.56 MB for 100,000 and 31.3 MB for 2,000,000.
- **Painting:** costs the same as a mesh edit, because paint is stored per triangle inside the mesh file. Every stroke on the 2,000,000-triangle object added 31.3 MB in every scheme. No storage scheme avoids this without changing OrcaSlicer's file format.
- **Split afterwards needs the normalized key.** Without it, the Medium mesh session costs 61.0 MB instead of 22.6 MB and the Large one 141.8 MB instead of 70.3 MB, because deleting an object renumbers all the others.
- **Keying the backup layout by whole-file bytes** instead of the mesh inside cost 83.4 MB instead of 67.7 MB on Large mixed, because a rewritten file has new bytes.

## 5. Restore round trip

Run on the Medium project; every comparison was made by the probe (measured).

1. **Recorded state:** one support-paint stroke on an object, `wall_loops` set to 5, an object moved to plate 2, and another rotated. Then a checkpoint was taken and the state recorded as 662 lines: the object and plate count; each object's name, triangle count, part meshes, placement, rotation, scale, plate and support paint; and every setting in force.
2. **Destroyed:**
   - The project was saved once, to give it a file.
   - An object was cut, which changed a mesh.
   - An object was deleted.
   - `wall_loops` was set to 6.
   - `Plater::save_project()` ran, which emptied the backup folder's mesh files (12 to 0).
   - The destroyed state differed from the recorded one on 11 lines.
3. **Rebuilt and loaded:**
   - Each rebuild made a scratch folder laid out like the backup folder: the checkpoint's small archive, and one mesh file per object. The folder had no `origin.txt`, so no fallback to the saved project was possible.
   - Each load ran `Plater::reset()`, then `Plater::load_files({scratch/.3mf, project.3mf}, LoadModel | LoadConfig | Restore)`.
   - The meshes came from four sources:

| Mesh files taken from | Load time | Matched the recorded state |
|---|---:|---|
| Hard links made at checkpoint time to OrcaSlicer's backup files | 1.0 to 1.2 s | 662 of 662 lines |
| Entries of the full-copy archive (different object numbering) | 0.96 s | 662 of 662 lines |
| A background thread that wrote a copy of the model taken at checkpoint time | 0.97 s | 662 of 662 lines |
| Control: the full-copy archive loaded as an ordinary project | 0.98 to 1.0 s | 662 of 662 lines |

- **Mesh completeness:** every object's triangle count and every part's triangle count matched, and so did the painted part's paint data.
- **The loader adopts the scratch folder as the live backup folder.** Its backup path became `…/rt/scratch`, as the handoff warned. The rebuild must be a copy, never the store.
- **No dialogs** appeared on any load.
- **Load times** come from a machine under heavy load and are indicative only.

## 6. Recommendation

### Scheme: backup layout, with meshes written by JusPrin

At each checkpoint, on the main thread:

1. `Plater::export_3mf(<pending>/.3mf, SaveStrategy::Backup | SaveStrategy::Silence)`, the small archive. Median 23 to 24 ms, worst 42 ms, measured at all three sizes. `Silence` keeps it from rewriting `origin.txt` and the project name.
2. `Model copy(plater.model())`. At most 0.4 ms at every size, because the meshes are shared pointers and the copy duplicates only the object tree and the paint data. Copying the model does not queue backup writes: the copy is not marked for backup.

On a background thread:

3. Give the copy the backup ids of the original and a private backup path. The exporter creates a backup folder for any model without one. Then call `store_bbs_3mf` with `Silence | SplitModel | ShareMesh | SkipAuxiliary` and no settings. This worked once in the probe for all 10 Medium objects, and the result restored exactly.
4. Store each `3D/Objects/*.model` entry under the hash of its text with `<object id="…">` blanked, compressed, once.
5. Write a manifest: the small archive, plus for each object its entry name and mesh hash.

To restore, copy into a scratch folder the small archive and each mesh as `3D/Objects/<entry name>`, wrapped in a zip of its own. Then load it as section 5 does. Never point the loader at the store.

**Why not the other schemes:**

- **Full copy** repeats every mesh in every checkpoint. It blocks the main thread for the length of a full save: a median of 180 ms for Small, 417 ms for Medium and 4.6 s for Large, which agrees with the save-cost findings.
- **Split afterwards** has the same main-thread cost as a full copy, because the writing itself is the cost. It also stores thumbnails every time.
- **Linking OrcaSlicer's backup files** is cheap on the main thread (23 to 24 ms plus 1 to 4 ms of links), but is only correct once the folder is current. That is at least 3 s per changed object, one object at a time, and never, for repair and region paint. A full save also deletes the files a later checkpoint would need. A checkpoint taken just before an agent turn, which is the case that matters most, would sometimes capture a mesh that is out of date.

**What this scheme still has to prove:**

- **Writing only changed objects.** The probe wrote every object. Changed objects could be found by comparing each part's mesh pointer and its paint layers' timestamps with the previous checkpoint's copy. Without that, the background thread spends a full save's CPU time on every checkpoint: 4.5 s of one core for Large.
- **Background writing beyond one run.** The exporter is libslic3r code, and OrcaSlicer's own backup thread already uses it off the main thread. Still, one successful run is thin evidence.
- **Conversation state.** JusPrin's state in `Auxiliaries/` is not part of a checkpoint, and the restore loader does not unpack attached files.
- **Thumbnails.** Backup archives have none. A checkpoint picker that shows pictures would add about 80 to 180 KB per plate per checkpoint.

No change to an OrcaSlicer-owned file was needed. Every call the probe made is public.

### Retention rule and size cap (proposal)

A checkpoint that only moved objects or changed settings costs about 16 KB. So thinning checkpoints saves space only by releasing mesh versions that no kept checkpoint uses. Deleting a checkpoint frees only the meshes no other kept checkpoint refers to; a mark-and-sweep over the manifests finds them.

1. Keep every checkpoint from the last 48 hours.
2. After 48 hours, keep one per hour of editing for 7 days, then one per day for 30 days, then only the newest.
3. Always keep the newest checkpoint of a project, the one taken when it was opened, and any the person named.
4. **Cap per project:** the larger of 1 GB and 30 times the project's full-copy size. **Global cap:** 5 GB, or 5% of free disk space if that is smaller. Over a cap, delete the oldest checkpoints outside rule 3 first, even inside the 48 hours, but never the 10 newest.

**Worked examples: one week of use.**

The usage is assumed, not measured:

- 5 sessions of 2 hours, with 30 checkpoints each (before agent turns, and every 10 to 30 minutes of manual editing), so 150 checkpoints a week.
- 20% of them follow an edit that changes one object's mesh or paint.
- On Large, half of those edits touch the big object.

The per-piece costs are the measurements from section 4.

| Project | Backup layout, before thinning | Full copy, for comparison | With the rule |
|---|---:|---:|---|
| Small (0.8 MB of meshes) | 0.8 + 150 × 16 KB + 30 × 0.78 MB = **27 MB** | 150 × 0.96 MB = 144 MB | Under every cap. After 30 days of this use: about 110 MB before thinning, much less after. |
| Medium (15.7 MB of meshes) | 15.7 + 2.4 + 30 × 1.56 MB = **65 MB** | 150 × 16 MB = 2.4 GB | Under every cap. Thinning days 3 to 7 to hourly releases in-between mesh versions. |
| Large (35 MB of meshes) | 35 + 2.4 + 15 × 31.3 MB + 15 × 0.78 MB = **520 MB** | 150 × 35 MB = 5.3 GB | Under the 1.06 GB cap (30 × 35 MB). |
| Large, painting heavily: 50 checkpoints that each follow a stroke on the big object | 35 + 50 × 31.3 MB = **1.6 GB** | 5.3 GB or more | Over the cap within days. The cap deletes the oldest checkpoints and keeps about the 30 most recent versions of the big object. |

**The painting case is the one real disk risk.** It is also where the checkpoint policy matters more than storage. The handoff's painting session takes a checkpoint after every stroke. With checkpoints only before agent turns and every 10 to 30 minutes, a burst of strokes becomes one stored mesh version, not one per stroke.

## 7. What could not be measured, or was measured differently

1. **Timings are indicative.** The machine had a load average of 18 to 32 on 12 cores, mostly Spotlight indexing, with 8.1 of 9.2 GB of swap in use.
   - The first full run gave medians that agree with the save-cost findings: full-copy checkpoints of 180 ms, 417 ms and 4.6 s; small archives of 23 to 24 ms.
   - Later runs had checkpoints of the same size take up to 141 s while the whole machine was paging. The probe's own process used 0.7 to 1.4 GB and 4% CPU at the time. Those runs were stopped and their timings are not used.
   - The background mesh write took 159 s in the only run that measured it, under that paging, so its real duration is not known. The save-cost spike's rate of about 2.3 µs per triangle, with objects written in parallel, suggests about 0.3 s of elapsed time (2.3 s of CPU in total) for Medium and 4.5 s for Large.
2. **Question 6** was answered with the timings above and the save-cost findings, not with a separate measurement.
3. **Painting used JusPrin's region paint**, not OrcaSlicer's paint tool, which can't be driven without mouse input. The disk cost is the same, since both change the same paint data. The "never backed up" result applies only to region paint; OrcaSlicer's tool calls the backup hook in code.
4. **OrcaSlicer's Fix model:** the probe's call to select the object failed. The command still repaired the target, whose triangle count went from 49,572 to 49,714, and the backup file was still not rewritten.
5. **Two of the 30 edits in each mixed session changed nothing.** Settings are kept in the application's presets, so values set in the layout session carried over. This slightly understates settings churn and does not affect mesh storage. The round trip used explicit values instead.
6. **The disk tables come from the first full run.** Later partial runs overwrote the Medium and Large layout output folders, so rerun everything with the command below before comparing.
7. **Not tested:**
   - writing only changed objects;
   - detecting changed objects;
   - checkpoints of projects with attached files;
   - restoring conversation state;
   - Windows and Linux.

## 8. Spike branch and how to rerun

- **Branch:** `spike/autosave-checkpoint-storage` (local only, not pushed), commit `1723a73dd1`. It is based on the save-cost spike's commit `279a14a75d`, which is based on `jusprin-newui` at `0dbc454843`.
- **Worktree:** `/Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-checkpoint-storage`, with a Release build in `build/arm64`.
- **Probe:** a `--ckpt <dir>` mode in `tests/workspace/real_adapter_harness.cpp`, with `--ckpt-projects=` and `--ckpt-sessions=`. The sessions are `layout`, `mesh`, `paint`, `mixed`, `identity`, `roundtrip` and `orcarepair`. Every event prints one line starting with `CKPT`.
- **Scripts in `tests/workspace/ckpt_storage/`:**
  - `make_fixtures.sh` generates the models with the save-cost spike's `gen_stl.py`, plus an open mesh for the repair edit.
  - `analyze.py` prints the section 4 table, and the section 3 comparisons with `--q2`.
  - `run.sh` does everything.

Build, if the build folder is gone (configure as in the handoff, with the worktree path):

```bash
/Applications/CMake.app/Contents/bin/cmake --build /Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-checkpoint-storage/build/arm64 --config Release --target jusprin_web jusprin_catalogs workspace_adapter_integration_harness
```

Rerun. The first full run took 25 minutes on this loaded machine and used about 2 GB in the scratch folder. Delete the folder afterwards.

```bash
/Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-checkpoint-storage/tests/workspace/ckpt_storage/run.sh /tmp/ckpt-storage
```

The raw log is `<scratch folder>/ckpt.log`.
