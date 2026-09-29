# Findings: how long saving a project blocks the app

**Written:** 2026-09-29. **Answers:** `agent-docs/jusprin/autosave-save-cost-spike-handoff.md`.
**This file is a temporary working document.** Leave it untracked. Do not commit it and do not add it to any ignore file.

## Summary

Any save that writes meshes blocks the main thread (the thread that draws the window and handles input) for about 2.2 to 2.5 microseconds per triangle of the project's largest object. Almost all of that time is zip compression of the mesh text, running on one core per object. The everyday Benchy model blocks for 0.6 s, a 2,000,000-triangle object for 4.5 s, and a 5,000,000-triangle object for 11.2 s. The full save and the checkpoint cost the same. OrcaSlicer's backup write blocks for only 10 to 80 ms at every size, because it leaves meshes to a background thread. That thread writes each changed mesh at least 3 s after the edit, and after a change to many objects it needs about 3 s per object.

**Verdict: the design needs the two-tier approach.** A full save is too slow to be the only frequent write.

## 1. Machine, build type, commit

| | |
|---|---|
| Machine | Apple M2 Pro, 12 cores (8 performance, 4 efficiency), 32 GB, internal SSD, macOS 26.3 (25D125) |
| Build type | `Release` (`-O3 -DNDEBUG`). The harness builds and runs in `Release` on this machine. `RelWithDebInfo` was not measured. |
| Commit measured | `jusprin-newui` at `0dbc454843`, plus the probe in `279a14a75d` on the local branch `spike/autosave-save-cost` |
| Load during the runs | Load average 20 to 30 on 12 cores, mostly Spotlight indexing (31 `mdworker_shared` processes and `mds`). 8 GB of swap in use. See section 6 for what this did to the numbers. |

## 2. Results

Times are in milliseconds of main-thread blocking. "Cold" is the first run of that kind after the fixture was loaded. "Warm" is the median of the next five (one for Huge). "Worst" covers all runs of that kind.

- **Backup** is `export_3mf(<backup folder>/.3mf, SaveStrategy::Backup)`, the call OrcaSlicer's backup timer makes.
- **Checkpoint** is `export_3mf` with the flags that `OrcaWorkspaceAdapter::export_project_archive` passes. A single call through the adapter itself matched it within 4% for every fixture.
- **Full** is `export_3mf` with the flags that `Plater::save_project` passes. A separate row times `save_project()` itself.

| Fixture | Contents | Kind | Cold | Warm median | Worst | Bytes |
|---|---|---|---:|---:|---:|---:|
| Tiny | 2 cubes, 2 plates, 24 triangles | backup | 27 | 17 | 27 | 15,149 |
| | | checkpoint | 74 | 41 | 74 | 42,043 |
| | | full | 45 | 42 | 48 | 42,652 |
| | | `save_project()` | 42 | 44 | 46 | 42,652 |
| Typical | 3DBenchy, 225,154 triangles | backup | 28 | 14 | 28 | 15,046 |
| | | checkpoint | 639 | 608 | 639 | 2,585,996 |
| | | full | 611 | 611 | 614 | 2,586,578 |
| | | `save_project()` | 625 | 620 | 626 | 2,586,578 |
| Medium | 10 objects × 100,000 triangles, 2 plates | backup | 26 | 9 | 26 | 16,284 |
| | | checkpoint | 433 | 401 | 433 | 15,993,693 |
| | | full | 405 | 404 | 420 | 15,994,278 |
| | | `save_project()` | 428 | 420 | 428 | 15,994,278 |
| Large | 1 object × 1,998,000 triangles | backup | 13 | 10 | 16 | 15,088 |
| | | checkpoint | 4,595 | 4,546 | 4,595 | 31,467,264 |
| | | full | 4,537 | 4,559 | 4,611 | 31,467,851 |
| | | `save_project()` | 4,576 | 4,569 | 4,576 | 31,467,851 |
| Huge | 1 object × 4,995,960 triangles | backup | 24 | 16 | 24 | 15,117 |
| | | checkpoint | 11,217 | 11,140 | 11,217 | 78,272,024 |
| | | full | 11,207 | 11,226 | 11,226 | 78,272,608 |
| | | `save_project()` | 11,258 | 11,214 | 11,266 | 78,272,608 |
| Many | 200 objects × 1,984 triangles, 10 plates | backup | 1,148 | 78 | 1,148 | 38,694 |
| | | checkpoint | see note | 790 (clean runs) | 44,642 | 7,500,698 |
| | | full | 779 | 772 | 779 | 7,501,284 |
| | | `save_project()` | 775 | 770 | 775 | 7,501,284 |

**Reproduction.** A second run of Large in a fresh process gave a warm median of 4,539 ms for the checkpoint and 4,577 ms for the full save. Two further runs of Typical gave 606 and 612 ms.

**Note on Many.** In the first run, all six full saves took 770 to 779 ms, but only two of the six checkpoints were that fast (784 and 796 ms); the others took 2.2 to 12.3 s. In a second run, every Many save took 4.4 to 47 s. When slow, both the PNG step and the parallel mesh step slowed together. Many is the only fixture whose save spreads across many cores, and the Spotlight load was high throughout. My inference, not verified, is that this is contention from the machine's load and not the exporter. I report the clean runs as the exporter's cost.

**How the cost scales.** Mesh writing costs 2.53 µs per triangle for Benchy, 2.24 for Large and 2.22 for Huge, so it is linear in triangle count. Objects are written in parallel, so with fewer objects than cores the blocking time follows the largest object, not the total. Medium's million triangles in ten objects (400 ms) block for less than Benchy's 225,000 in one (610 ms).

**Comparison with the earlier 170 to 180 ms.** Tiny now takes 41 to 45 ms warm and 74 ms cold for a checkpoint. Thumbnails are still the largest part of a Tiny save: about 10 ms of rendering and 22 ms of PNG encoding, out of 41. The earlier notes did not record their build type. My guess is an unoptimized `RelWithDebInfo` build, but I did not check.

## 3. Time per step

These come from `Plater::export_3mf`'s progress callback (`Export3mfProgressFn`), as warm medians in ms. The step names say what the time was spent on:

- **pre**: everything before the first callback. For checkpoints and full saves, this includes rendering the four thumbnails per plate. The backup write renders none.
- **PNG**: the exporter's `EXPORT_STAGE_ADD_THUMBNAILS` stage. It PNG-encodes the already-rendered 512×512 images with miniz.
- **meshes**: `EXPORT_STAGE_ADD_MODELS`, up to the next callback. It writes every object's mesh as compressed XML, one object per task.
- **settings**: the config stages (`EXPORT_STAGE_ADD_PRINT_CONFIG` through `EXPORT_STAGE_ADD_SLICE_INFO`).
- **close+rename**: after `EXPORT_STAGE_FINISH`. It closes the zip, renames the `.tmp` file over the target, and releases the thumbnails.

The remaining stages each took under 1 ms.

| Fixture, kind | pre | PNG | meshes | settings | close+rename | Total |
|---|---:|---:|---:|---:|---:|---:|
| Medium, checkpoint | 19.6 | 75.4 | 295.1 | 2.1 | 9.6 | 401 |
| Medium, full | 19.7 | 75.3 | 291.2 | 2.1 | 16.7 | 404 |
| Medium, backup | 1.4 | none | 0.2 | 2.8 | 4.5 | 9 |
| Large, checkpoint | 27.5 | 26.5 | 4,480.2 | 2.0 | 9.8 | 4,546 |
| Large, full | 27.5 | 26.3 | 4,485.3 | 2.0 | 21.2 | 4,559 |
| Large, backup | 1.3 | none | 0.2 | 2.8 | 5.8 | 10 |
| Many, full (for contrast) | 43.5 | 591.2 | 117.5 | 5.4 | 11.6 | 772 |

**Inside the mesh step.** This comes from a CPU profile of one Large checkpoint save: `sample`, 4 s, 3,034 main-thread samples.

| Where the main thread was | Share of samples |
|---|---:|
| Writing the mesh (`_add_mesh_to_object_stream`) | 99.7% |
| Zip compression and bookkeeping (`mz_zip_writer_add_staged_data`) | 81% |
| ... of which DEFLATE itself (`tdefl_compress`, level 6) | 72.5% |
| Turning coordinates into text (`printf` family for floats, Boost.Spirit Karma for integers) | 15% |
| Thumbnail rendering and PNG | under 1% |

On a Huge save, sampled during the first run, DEFLATE was about 79% of main-thread samples and number formatting about 13%.

**Thumbnails.** The "pre" step, which includes rendering them, takes 7 to 50 ms per save. PNG encoding costs about 11 ms per plate for Tiny, 37 for Medium and 60 for Many. It dominates only for projects with many plates and little geometry: 591 of Many's 772 ms.

## 4. Backup write: the window in which a crash loses an edit

The backup write does not block for long, but it does not include meshes. A background thread writes each object's mesh to `<backup folder>/3D/Objects/<name>_<id>.model` after the object changes. The table below measures the time from an edit to that file being in place. The edit was one of two triggers:

- renaming the object through `Plater::rename_object`, which queues the same background write as a mesh edit and names a new file;
- calling `Slic3r::save_object_mesh` directly, which is what OrcaSlicer's part move, rotate and scale, and its paint tools call.

| Fixture | Main-thread cost of the edit's backup call | File in place after (rename) | File in place after (direct call) |
|---|---:|---:|---:|
| Tiny | under 0.1 ms (rename: 2.2 ms) | 3.0 s | 3.0 s |
| Typical | under 0.1 ms (rename: 2.4 ms) | 3.6 s | 3.6 s |
| Medium (one 100,000-triangle object) | under 0.1 ms (rename: 2.7 ms) | 3.3 s | 3.3 s |
| Large | under 0.1 ms (rename: 3.1 ms) | 7.5 s | 7.5 s |
| Huge | under 0.1 ms (rename: 3.3 ms) | 14.1 s | 14.3 s |
| Many | 0.1 ms (rename: 44 to 70 ms, which is the object list, not the backup) | 3.0 to 5.8 s | 3.0 to 3.1 s |

**After a change to many objects, the window is much longer.** It took 29 s after loading Medium's 10 objects before all their mesh files existed, and 11.3 minutes (678 s, then 676 s in a second run) after loading Many's 200. That is about 3 s per object regardless of size.

The code explains this, although I read it rather than instrumented it. `_BBS_Backup_Manager::delay_task` in `src/libslic3r/Format/bbs_3mf.cpp` waits up to 3 s before writing each object's mesh, to debounce repeated edits. Nothing wakes that wait while other objects are still queued, so each queued object pays the full 3 s. Until an object's file exists, a crash loses that object's current mesh.

The design should expect this window after any operation that touches many objects: import, arrange, or an agent turn that changes many objects.

## 5. Verdict against the thresholds

| Rule | Measured | Result |
|---|---|---|
| Medium over 500 ms? | 401 ms checkpoint, 404 ms full (worst 433) | No. It falls in "acceptable only while the person is idle". |
| Large over 1 s? | 4.5 s for checkpoint and full | **Yes, by 4.5 times. The two-tier approach is needed.** |
| Typical (no rule given) | 610 ms | Over the 500 ms line on the everyday model, because one object uses one core |
| Backup write under 100 ms? | Warm 9 to 78 ms at every size. One cold outlier of 1,148 ms (Many, first run, spent before the first stage) did not recur in the second run (71 ms). | Yes. It is safe to run at any time. |

## 6. Recommendation

**Use two tiers.** Here are the numbers that decide it, and what they mean for the rest of the design.

1. **Only the backup write is cheap enough to be the frequent write.** It blocks for 9 to 78 ms warm across all six fixtures, well under 100 ms. Its mesh files are written off the main thread, and triggering one costs under 0.1 ms on the main thread.
2. **A full save a few seconds after each burst of edits would freeze the app noticeably for ordinary projects.** It takes 0.6 s for one Benchy, 4.5 s for a 2,000,000-triangle object and 11.2 s for 5,000,000.
3. **"Only when idle" is not enough for large objects.** A save that starts when the person pauses keeps the window frozen for 4.5 to 11 s, so they feel it as soon as they move the mouse again. For large projects the full save belongs on close and on explicit save, or the design needs a way to know the person has stepped away, not just paused.
4. **Checkpoints cost exactly as much as full saves.** The two agree within 2% at every size, because the only difference is the attached files and the fixtures had none. So "a checkpoint before each AI agent turn" freezes the app 0.6 s per turn for a Benchy and 4.5 s for a 2,000,000-triangle object. This affects the checkpoint policy as much as autosave.
5. **The backup tier's weak point is the mesh window, not blocking.** After a single edit, the window is 3 s plus the write time: 3 to 3.6 s for normal objects, 7.5 s for 2,000,000 triangles, 14 s for 5,000,000. After a change to many objects, it grows by about 3 s per object: 29 s for 10 objects, 11 minutes for 200. If autosave relies on this tier, the design must accept that window or change the queue's delay. Changing the delay means touching OrcaSlicer-owned code.

**If a later design wants frequent full saves anyway**, the profile says where the time goes:
- 72% is DEFLATE at level 6 on the mesh text;
- 15% is formatting numbers as text;
- the work runs on one core per object, on the main thread.

Directions to measure then, none of them tested here: a lower compression level for the mesh files; writing meshes off the main thread; reusing an unchanged object's already-compressed mesh file from the previous save. Each changes the exporter, which is OrcaSlicer-owned code and out of scope for this spike.

## 7. What could not be measured, or was measured differently

1. **The machine was loaded.** Load average was 20 to 30 from Spotlight indexing, and 8 GB of swap was in use. The single-object fixtures stayed within 1 to 2% between runs and reproduced in separate processes, so I consider them sound. Many did not (section 2).
2. **The first Huge run was discarded.** It measured checkpoints of 182 to 289 s. The harness process then had a 3.1 GB footprint with only 620 MB resident, meaning macOS had compressed or swapped out the rest. Huge was rerun in a fresh process, where it held 1.9 GB and took a steady 11.2 s. The rerun's numbers are the ones reported.
3. **Huge has one warm run, not five**, to keep the run length reasonable. Its two runs of each kind agree within 1%.
4. **"Cold" is not a first save after app start.** It is the first run of that kind after loading the fixture, in the order backup, checkpoint, full. So the full save's cold run followed seven checkpoint writes. Every fixture loaded in the same process after a Tiny warm-up.
5. **The mesh-edit trigger was a rename or a direct `save_object_mesh` call**, not a geometry edit made through the user interface. Both go through OrcaSlicer's same background write.
6. **The backup write was called directly**, with the same arguments as OrcaSlicer's 10-second timer callback, not through the timer. The timer path adds only an `up_to_date` check first.
7. **`Plater::save_project()` was timed only after a first export had set the project's file path**, so it opened no file dialog. It adds 10 to 15 ms over the bare export, and it also clears the backup folder's mesh files, which is why the probe runs it last.
8. **No fixture had attached files.** A full save of a project with attachments also copies them; a checkpoint skips them.
9. **Only the internal SSD was measured.** Windows and Linux were out of scope.
10. **The profile covers one Large checkpoint save**, plus one Huge save sampled while that process was paging, and no other fixture.
11. **The Many fixture's cold backup write of 1,148 ms is unexplained.** It was spent before the first export stage and did not recur (71 ms cold in the second run).

## 8. Spike branch and how to rerun

- **Branch:** `spike/autosave-save-cost` (local only, not pushed), commit `279a14a75d`, based on `jusprin-newui` `0dbc454843`.
- **Worktree:** `/Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost`, with its Release build in `build/arm64`.
- **Probe:** a `--save-cost <dir>` mode in `tests/workspace/real_adapter_harness.cpp`. Every measurement prints one line starting with `SAVECOST`.
- **Scripts:** `tests/workspace/save_cost/gen_stl.py` is the handoff's generator with an added radius argument, so objects fit on their plates. `summarize.py` builds the tables above, and `run.sh` does everything.

Build, if the worktree's build folder is gone:

```bash
/Applications/CMake.app/Contents/bin/cmake -S /Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost -B /Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost/build/arm64 -G "Ninja Multi-Config" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_PREFIX_PATH="/Users/kenneth/Projects/JusPrin/deps/build/arm64/OrcaSlicer_dep/usr/local;/Users/kenneth/Projects/OrcaSlicer/deps/build/arm64/OrcaSlicer_dep/usr/local" -DSLIC3R_GUI=ON -DSLIC3R_STATIC=ON -DSLIC3R_PCH=ON -DBUILD_TESTS=ON -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib
```

```bash
/Applications/CMake.app/Contents/bin/cmake --build /Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost/build/arm64 --config Release --target jusprin_web jusprin_catalogs workspace_adapter_integration_harness
```

Rerun the measurement. This takes about 20 minutes, of which about 12 are the Many fixture waiting for 200 background mesh writes. The first argument is a scratch folder outside the repository; the script generates the fixtures there, about 400 MB with Huge.

```bash
/Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost/tests/workspace/save_cost/run.sh /tmp/save-cost tiny,typical,medium,large,many 5
```

```bash
/Users/kenneth/Projects/JusPrin/.claude/worktrees/autosave-save-cost/tests/workspace/save_cost/run.sh /tmp/save-cost huge 1
```

The raw log is `<scratch folder>/save-cost.log`. To profile a save, run `sample $(pgrep -x JusPrinWorkspaceHarness) 4 -file <out>` while a checkpoint of a large fixture is in progress. Delete the scratch folder afterwards.

## Appendix: raw measurement lines

These are the `SAVECOST` lines from the runs the tables use: the first run for Tiny through Large, the Huge rerun, and both Many runs. The discarded Huge lines from the first run are omitted.

```text
# First run: Tiny, Typical, Medium, Large
SAVECOST START dir=/private/tmp/claude/claude-502/-Users-kenneth-Projects-JusPrin/bbf2157e-559d-48c8-8701-c991ba9f02bb/scratchpad/fixtures warm=5 threads=12
SAVECOST LOAD fixture=tiny objects=2 plates=2 per_plate=1,1 triangles=24 load_ms=811.0 dialogs=0
SAVECOST LOAD fixture=tiny objects=2 plates=2 per_plate=1,1 triangles=24 load_ms=119.6 dialogs=0
SAVECOST MESHREADY fixture=tiny ok=1 after_ms=2981.6
SAVECOST RUN fixture=tiny kind=backup run=0 ms=27.1 bytes=15149 result=0 steps=pre:4.7,open:0.2,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:3.2,config_file:0.1,slice_info:0.1,auxiliaries:0.0,relations:0.1,finish:18.3
SAVECOST RUN fixture=tiny kind=backup run=1 ms=20.4 bytes=15149 result=0 steps=pre:3.2,open:0.2,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.7,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:14.0
SAVECOST RUN fixture=tiny kind=backup run=2 ms=20.3 bytes=15149 result=0 steps=pre:4.8,open:0.2,content_types:0.1,models:0.1,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:12.9
SAVECOST RUN fixture=tiny kind=backup run=3 ms=17.0 bytes=15149 result=0 steps=pre:2.2,open:0.1,content_types:0.1,models:0.1,custom_gcode:0.0,print_config:2.3,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:12.0
SAVECOST RUN fixture=tiny kind=backup run=4 ms=14.0 bytes=15149 result=0 steps=pre:1.5,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:9.9
SAVECOST RUN fixture=tiny kind=backup run=5 ms=12.8 bytes=15149 result=0 steps=pre:1.2,open:0.1,content_types:0.0,models:0.1,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:9.2
SAVECOST MESHLAG fixture=tiny trigger=rename ok=1 edit_ms=2.2 file_after_ms=3010.4
SAVECOST MESHLAG fixture=tiny trigger=save_object_mesh ok=1 edit_ms=0.0 file_after_ms=3033.3
SAVECOST RUN fixture=tiny kind=checkpoint run=0 ms=73.5 bytes=42043 result=0 steps=pre:20.0,open:0.3,content_types:0.1,thumbnails:35.4,models:0.5,custom_gcode:0.0,print_config:3.1,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.1,finish:13.9
SAVECOST RUN fixture=tiny kind=checkpoint run=1 ms=50.0 bytes=42043 result=0 steps=pre:14.6,open:0.1,content_types:0.1,thumbnails:24.0,models:0.3,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:8.4
SAVECOST RUN fixture=tiny kind=checkpoint run=2 ms=43.8 bytes=42043 result=0 steps=pre:10.8,open:0.1,content_types:0.1,thumbnails:22.5,models:0.3,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:7.9
SAVECOST RUN fixture=tiny kind=checkpoint run=3 ms=40.7 bytes=42043 result=0 steps=pre:9.6,open:0.1,content_types:0.0,thumbnails:21.7,models:0.3,custom_gcode:0.0,print_config:1.8,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:7.0
SAVECOST RUN fixture=tiny kind=checkpoint run=4 ms=40.9 bytes=42043 result=0 steps=pre:9.4,open:0.1,content_types:0.0,thumbnails:21.9,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:6.9
SAVECOST RUN fixture=tiny kind=checkpoint run=5 ms=41.0 bytes=42043 result=0 steps=pre:9.6,open:0.1,content_types:0.0,thumbnails:22.0,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:6.8
SAVECOST RUN fixture=tiny kind=checkpoint_adapter run=0 ms=41.7 bytes=42043 result=0
SAVECOST RUN fixture=tiny kind=full run=0 ms=44.9 bytes=42652 result=0 steps=pre:10.1,open:0.1,content_types:0.0,thumbnails:21.7,models:0.3,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:10.5
SAVECOST RUN fixture=tiny kind=full run=1 ms=41.8 bytes=42652 result=0 steps=pre:9.8,open:0.1,content_types:0.0,thumbnails:22.0,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:7.3
SAVECOST RUN fixture=tiny kind=full run=2 ms=47.7 bytes=42652 result=0 steps=pre:11.6,open:0.1,content_types:0.0,thumbnails:21.8,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:11.7
SAVECOST RUN fixture=tiny kind=full run=3 ms=42.5 bytes=42652 result=0 steps=pre:9.5,open:0.1,content_types:0.0,thumbnails:22.2,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.2
SAVECOST RUN fixture=tiny kind=full run=4 ms=40.7 bytes=42652 result=0 steps=pre:8.8,open:0.1,content_types:0.0,thumbnails:21.6,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:7.6
SAVECOST RUN fixture=tiny kind=full run=5 ms=41.0 bytes=42652 result=0 steps=pre:9.0,open:0.1,content_types:0.0,thumbnails:22.0,models:0.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:7.5
SAVECOST RUN fixture=tiny kind=save_project run=0 ms=42.0 bytes=42652 result=5103 dialogs=0
SAVECOST RUN fixture=tiny kind=save_project run=1 ms=45.8 bytes=42652 result=5103 dialogs=0
SAVECOST RUN fixture=tiny kind=save_project run=2 ms=41.7 bytes=42652 result=5103 dialogs=0
SAVECOST LOAD fixture=typical objects=1 plates=1 per_plate=1 triangles=225154 load_ms=317.6 dialogs=0
SAVECOST MESHREADY fixture=typical ok=1 after_ms=3460.6
SAVECOST RUN fixture=typical kind=backup run=0 ms=28.2 bytes=15046 result=0 steps=pre:5.0,open:0.4,content_types:0.2,models:0.4,custom_gcode:0.0,print_config:4.9,config_file:0.2,slice_info:0.1,auxiliaries:0.0,relations:0.1,finish:17.1
SAVECOST RUN fixture=typical kind=backup run=1 ms=24.5 bytes=15046 result=0 steps=pre:7.9,open:0.2,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:3.2,config_file:0.1,slice_info:0.1,auxiliaries:0.0,relations:0.0,finish:12.7
SAVECOST RUN fixture=typical kind=backup run=2 ms=13.8 bytes=15046 result=0 steps=pre:1.5,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.5,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:9.2
SAVECOST RUN fixture=typical kind=backup run=3 ms=12.2 bytes=15046 result=0 steps=pre:1.3,open:0.1,content_types:0.1,models:0.1,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:8.3
SAVECOST RUN fixture=typical kind=backup run=4 ms=14.5 bytes=15046 result=0 steps=pre:1.5,open:0.1,content_types:0.1,models:0.1,custom_gcode:0.0,print_config:2.1,config_file:0.0,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:10.6
SAVECOST RUN fixture=typical kind=backup run=5 ms=11.0 bytes=15046 result=0 steps=pre:1.1,open:0.1,content_types:0.0,models:0.1,custom_gcode:0.0,print_config:1.9,config_file:0.0,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:7.6
SAVECOST MESHLAG fixture=typical trigger=rename ok=1 edit_ms=2.4 file_after_ms=3623.1
SAVECOST MESHLAG fixture=typical trigger=save_object_mesh ok=1 edit_ms=0.0 file_after_ms=3610.7
SAVECOST RUN fixture=typical kind=checkpoint run=0 ms=639.1 bytes=2585996 result=0 steps=pre:14.0,open:0.1,content_types:0.1,thumbnails:31.2,models:582.5,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.7
SAVECOST RUN fixture=typical kind=checkpoint run=1 ms=612.2 bytes=2585996 result=0 steps=pre:9.1,open:0.1,content_types:0.0,thumbnails:18.1,models:572.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:10.4
SAVECOST RUN fixture=typical kind=checkpoint run=2 ms=606.4 bytes=2585996 result=0 steps=pre:7.3,open:0.1,content_types:0.0,thumbnails:17.9,models:570.0,custom_gcode:0.0,print_config:2.4,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.4
SAVECOST RUN fixture=typical kind=checkpoint run=3 ms=607.5 bytes=2585996 result=0 steps=pre:7.5,open:0.1,content_types:0.0,thumbnails:18.3,models:570.4,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.8
SAVECOST RUN fixture=typical kind=checkpoint run=4 ms=604.0 bytes=2585996 result=0 steps=pre:7.4,open:0.1,content_types:0.0,thumbnails:18.2,models:569.3,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:6.8
SAVECOST RUN fixture=typical kind=checkpoint run=5 ms=610.3 bytes=2585996 result=0 steps=pre:7.3,open:0.1,content_types:0.1,thumbnails:18.0,models:575.0,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:7.7
SAVECOST RUN fixture=typical kind=checkpoint_adapter run=0 ms=609.5 bytes=2585996 result=0
SAVECOST RUN fixture=typical kind=full run=0 ms=611.3 bytes=2586578 result=0 steps=pre:8.0,open:0.1,content_types:0.0,thumbnails:18.2,models:570.8,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:11.6
SAVECOST RUN fixture=typical kind=full run=1 ms=612.9 bytes=2586578 result=0 steps=pre:10.6,open:0.1,content_types:0.0,thumbnails:18.1,models:570.4,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:11.3
SAVECOST RUN fixture=typical kind=full run=2 ms=611.3 bytes=2586578 result=0 steps=pre:7.3,open:0.1,content_types:0.0,thumbnails:18.2,models:572.3,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:11.0
SAVECOST RUN fixture=typical kind=full run=3 ms=613.7 bytes=2586578 result=0 steps=pre:7.9,open:0.1,content_types:0.0,thumbnails:18.0,models:571.2,custom_gcode:0.0,print_config:2.1,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:14.1
SAVECOST RUN fixture=typical kind=full run=4 ms=607.6 bytes=2586578 result=0 steps=pre:7.2,open:0.1,content_types:0.0,thumbnails:18.0,models:569.6,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:10.4
SAVECOST RUN fixture=typical kind=full run=5 ms=611.1 bytes=2586578 result=0 steps=pre:6.9,open:0.1,content_types:0.0,thumbnails:18.2,models:569.7,custom_gcode:0.0,print_config:2.1,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:13.8
SAVECOST RUN fixture=typical kind=save_project run=0 ms=625.2 bytes=2586578 result=5103 dialogs=0
SAVECOST RUN fixture=typical kind=save_project run=1 ms=626.3 bytes=2586578 result=5103 dialogs=0
SAVECOST RUN fixture=typical kind=save_project run=2 ms=614.5 bytes=2586578 result=5103 dialogs=0
SAVECOST LOAD fixture=medium objects=10 plates=2 per_plate=5,5 triangles=999040 load_ms=1016.4 dialogs=0
SAVECOST MESHREADY fixture=medium ok=1 after_ms=29000.4
SAVECOST RUN fixture=medium kind=backup run=0 ms=26.1 bytes=16284 result=0 steps=pre:4.3,open:0.3,content_types:0.2,models:0.6,custom_gcode:0.0,print_config:5.9,config_file:0.6,slice_info:0.1,auxiliaries:0.0,relations:0.1,finish:13.9
SAVECOST RUN fixture=medium kind=backup run=1 ms=12.8 bytes=16284 result=0 steps=pre:1.8,open:0.2,content_types:0.1,models:0.4,custom_gcode:0.0,print_config:3.4,config_file:0.3,slice_info:0.1,auxiliaries:0.0,relations:0.0,finish:6.5
SAVECOST RUN fixture=medium kind=backup run=2 ms=12.0 bytes=16284 result=0 steps=pre:1.4,open:0.1,content_types:0.1,models:0.3,custom_gcode:0.0,print_config:2.7,config_file:0.3,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:7.0
SAVECOST RUN fixture=medium kind=backup run=3 ms=9.2 bytes=16284 result=0 steps=pre:1.3,open:0.1,content_types:0.1,models:0.3,custom_gcode:0.0,print_config:2.6,config_file:0.3,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:4.5
SAVECOST RUN fixture=medium kind=backup run=4 ms=9.2 bytes=16284 result=0 steps=pre:2.4,open:0.1,content_types:0.1,models:0.3,custom_gcode:0.0,print_config:2.5,config_file:0.2,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:3.5
SAVECOST RUN fixture=medium kind=backup run=5 ms=8.4 bytes=16284 result=0 steps=pre:1.1,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.2,config_file:0.2,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:4.5
SAVECOST MESHLAG fixture=medium trigger=rename ok=1 edit_ms=2.7 file_after_ms=3263.3
SAVECOST MESHLAG fixture=medium trigger=save_object_mesh ok=1 edit_ms=0.0 file_after_ms=3260.6
SAVECOST RUN fixture=medium kind=checkpoint run=0 ms=432.9 bytes=15993693 result=0 steps=pre:39.1,open:0.2,content_types:0.1,thumbnails:96.2,models:285.9,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.9
SAVECOST RUN fixture=medium kind=checkpoint run=1 ms=400.8 bytes=15993693 result=0 steps=pre:18.7,open:0.1,content_types:0.0,thumbnails:75.4,models:295.3,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:9.0
SAVECOST RUN fixture=medium kind=checkpoint run=2 ms=415.0 bytes=15993693 result=0 steps=pre:19.7,open:0.1,content_types:0.1,thumbnails:75.5,models:305.9,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:11.3
SAVECOST RUN fixture=medium kind=checkpoint run=3 ms=405.1 bytes=15993693 result=0 steps=pre:19.4,open:0.1,content_types:0.0,thumbnails:75.0,models:295.1,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:13.0
SAVECOST RUN fixture=medium kind=checkpoint run=4 ms=396.7 bytes=15993693 result=0 steps=pre:19.9,open:0.1,content_types:0.0,thumbnails:75.2,models:289.5,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:9.6
SAVECOST RUN fixture=medium kind=checkpoint run=5 ms=393.6 bytes=15993693 result=0 steps=pre:19.6,open:0.1,content_types:0.0,thumbnails:76.7,models:286.2,custom_gcode:0.0,print_config:2.1,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.5
SAVECOST RUN fixture=medium kind=checkpoint_adapter run=0 ms=400.4 bytes=15993693 result=0
SAVECOST RUN fixture=medium kind=full run=0 ms=404.9 bytes=15994278 result=0 steps=pre:18.3,open:0.1,content_types:0.0,thumbnails:75.6,models:291.5,custom_gcode:0.0,print_config:1.9,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:17.0
SAVECOST RUN fixture=medium kind=full run=1 ms=411.4 bytes=15994278 result=0 steps=pre:19.7,open:0.1,content_types:0.0,thumbnails:75.3,models:299.3,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:14.7
SAVECOST RUN fixture=medium kind=full run=2 ms=403.5 bytes=15994278 result=0 steps=pre:20.3,open:0.1,content_types:0.0,thumbnails:75.3,models:288.3,custom_gcode:0.0,print_config:2.1,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:17.0
SAVECOST RUN fixture=medium kind=full run=3 ms=398.8 bytes=15994278 result=0 steps=pre:19.6,open:0.1,content_types:0.0,thumbnails:74.9,models:285.1,custom_gcode:0.0,print_config:1.9,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:16.7
SAVECOST RUN fixture=medium kind=full run=4 ms=420.0 bytes=15994278 result=0 steps=pre:21.5,open:0.1,content_types:0.0,thumbnails:75.5,models:303.0,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:17.4
SAVECOST RUN fixture=medium kind=full run=5 ms=404.4 bytes=15994278 result=0 steps=pre:18.6,open:0.1,content_types:0.0,thumbnails:75.8,models:291.2,custom_gcode:0.0,print_config:2.0,config_file:0.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:16.2
SAVECOST RUN fixture=medium kind=save_project run=0 ms=428.2 bytes=15994278 result=5103 dialogs=0
SAVECOST RUN fixture=medium kind=save_project run=1 ms=424.6 bytes=15994278 result=5103 dialogs=0
SAVECOST RUN fixture=medium kind=save_project run=2 ms=416.1 bytes=15994278 result=5103 dialogs=0
SAVECOST LOAD fixture=large objects=1 plates=1 per_plate=1 triangles=1998000 load_ms=3232.4 dialogs=0
SAVECOST MESHREADY fixture=large ok=1 after_ms=6315.1
SAVECOST RUN fixture=large kind=backup run=0 ms=13.0 bytes=15088 result=0 steps=pre:5.0,open:0.2,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.9,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:4.6
SAVECOST RUN fixture=large kind=backup run=1 ms=11.7 bytes=15088 result=0 steps=pre:2.0,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:3.3,config_file:0.1,slice_info:0.1,auxiliaries:0.0,relations:0.0,finish:5.8
SAVECOST RUN fixture=large kind=backup run=2 ms=15.9 bytes=15088 result=0 steps=pre:2.1,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.8,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:10.4
SAVECOST RUN fixture=large kind=backup run=3 ms=8.5 bytes=15088 result=0 steps=pre:1.3,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.6,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:4.1
SAVECOST RUN fixture=large kind=backup run=4 ms=9.2 bytes=15088 result=0 steps=pre:1.1,open:0.1,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:2.4,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:5.2
SAVECOST RUN fixture=large kind=backup run=5 ms=10.5 bytes=15088 result=0 steps=pre:1.1,open:0.1,content_types:0.1,models:0.1,custom_gcode:0.0,print_config:2.2,config_file:0.1,slice_info:0.0,auxiliaries:0.0,relations:0.0,finish:6.9
SAVECOST MESHLAG fixture=large trigger=rename ok=1 edit_ms=3.1 file_after_ms=7531.4
SAVECOST MESHLAG fixture=large trigger=save_object_mesh ok=1 edit_ms=0.0 file_after_ms=7515.6
SAVECOST RUN fixture=large kind=checkpoint run=0 ms=4595.2 bytes=31467264 result=0 steps=pre:43.5,open:0.1,content_types:0.0,thumbnails:27.6,models:4514.6,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:7.1
SAVECOST RUN fixture=large kind=checkpoint run=1 ms=4525.6 bytes=31467264 result=0 steps=pre:28.0,open:0.1,content_types:0.0,thumbnails:27.0,models:4461.7,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:6.6
SAVECOST RUN fixture=large kind=checkpoint run=2 ms=4582.7 bytes=31467264 result=0 steps=pre:28.0,open:0.1,content_types:0.0,thumbnails:26.4,models:4516.0,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:9.8
SAVECOST RUN fixture=large kind=checkpoint run=3 ms=4552.7 bytes=31467264 result=0 steps=pre:25.2,open:0.1,content_types:0.0,thumbnails:26.5,models:4490.2,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.5
SAVECOST RUN fixture=large kind=checkpoint run=4 ms=4546.1 bytes=31467264 result=0 steps=pre:27.5,open:0.1,content_types:0.0,thumbnails:26.0,models:4480.2,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:10.1
SAVECOST RUN fixture=large kind=checkpoint run=5 ms=4529.8 bytes=31467264 result=0 steps=pre:23.9,open:0.1,content_types:0.0,thumbnails:27.5,models:4466.3,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:9.8
SAVECOST RUN fixture=large kind=checkpoint_adapter run=0 ms=4524.3 bytes=31467264 result=0
SAVECOST RUN fixture=large kind=full run=0 ms=4537.1 bytes=31467851 result=0 steps=pre:24.5,open:0.1,content_types:0.0,thumbnails:26.2,models:4459.5,custom_gcode:0.0,print_config:2.4,config_file:0.1,slice_info:0.0,auxiliaries:1.3,relations:0.0,finish:22.8
SAVECOST RUN fixture=large kind=full run=1 ms=4559.4 bytes=31467851 result=0 steps=pre:27.5,open:0.1,content_types:0.0,thumbnails:26.1,models:4485.3,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:18.1
SAVECOST RUN fixture=large kind=full run=2 ms=4559.2 bytes=31467851 result=0 steps=pre:24.0,open:0.1,content_types:0.0,thumbnails:26.6,models:4483.7,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:22.5
SAVECOST RUN fixture=large kind=full run=3 ms=4611.2 bytes=31467851 result=0 steps=pre:25.9,open:0.1,content_types:0.0,thumbnails:26.6,models:4535.1,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:21.2
SAVECOST RUN fixture=large kind=full run=4 ms=4547.9 bytes=31467851 result=0 steps=pre:27.6,open:0.1,content_types:0.0,thumbnails:26.0,models:4469.7,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:22.2
SAVECOST RUN fixture=large kind=full run=5 ms=4569.1 bytes=31467851 result=0 steps=pre:28.3,open:0.1,content_types:0.0,thumbnails:26.3,models:4494.9,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:17.1
SAVECOST RUN fixture=large kind=save_project run=0 ms=4575.8 bytes=31467851 result=5103 dialogs=0
SAVECOST RUN fixture=large kind=save_project run=1 ms=4564.3 bytes=31467851 result=5103 dialogs=0
SAVECOST RUN fixture=large kind=save_project run=2 ms=4573.3 bytes=31467851 result=5103 dialogs=0

# Huge, rerun in a fresh process
SAVECOST START dir=/private/tmp/claude/claude-502/-Users-kenneth-Projects-JusPrin/bbf2157e-559d-48c8-8701-c991ba9f02bb/scratchpad/fixtures warm=1 threads=12
SAVECOST LOAD fixture=huge objects=1 plates=1 per_plate=1 triangles=4995960 load_ms=18949.3 dialogs=0
SAVECOST MESHREADY fixture=huge ok=1 after_ms=10955.6
SAVECOST RUN fixture=huge kind=backup run=0 ms=23.5 bytes=15117 result=0 steps=pre:5.2,open:0.4,content_types:0.2,models:0.4,custom_gcode:0.0,print_config:5.3,config_file:0.2,slice_info:0.1,auxiliaries:0.0,relations:0.1,finish:11.7
SAVECOST RUN fixture=huge kind=backup run=1 ms=16.1 bytes=15117 result=0 steps=pre:2.4,open:0.2,content_types:0.1,models:0.2,custom_gcode:0.0,print_config:3.5,config_file:0.1,slice_info:0.1,auxiliaries:0.0,relations:0.0,finish:9.5
SAVECOST MESHLAG fixture=huge trigger=rename ok=1 edit_ms=3.3 file_after_ms=14072.7
SAVECOST MESHLAG fixture=huge trigger=save_object_mesh ok=1 edit_ms=0.0 file_after_ms=14315.7
SAVECOST RUN fixture=huge kind=checkpoint run=0 ms=11217.3 bytes=78272024 result=0 steps=pre:71.4,open:0.2,content_types:0.1,thumbnails:39.7,models:11089.7,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:14.0
SAVECOST RUN fixture=huge kind=checkpoint run=1 ms=11139.8 bytes=78272024 result=0 steps=pre:47.6,open:0.1,content_types:0.1,thumbnails:28.2,models:11051.4,custom_gcode:0.0,print_config:1.9,config_file:0.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:10.3
SAVECOST RUN fixture=huge kind=checkpoint_adapter run=0 ms=11280.0 bytes=78272024 result=0
SAVECOST RUN fixture=huge kind=full run=0 ms=11206.9 bytes=78272608 result=0 steps=pre:45.7,open:0.1,content_types:0.0,thumbnails:26.3,models:11102.8,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:4.2,relations:0.0,finish:25.7
SAVECOST RUN fixture=huge kind=full run=1 ms=11225.8 bytes=78272608 result=0 steps=pre:47.3,open:0.1,content_types:0.1,thumbnails:26.9,models:11128.6,custom_gcode:0.0,print_config:2.0,config_file:0.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:20.5
SAVECOST RUN fixture=huge kind=save_project run=0 ms=11258.5 bytes=78272608 result=5103 dialogs=0
SAVECOST RUN fixture=huge kind=save_project run=1 ms=11161.1 bytes=78272608 result=5103 dialogs=0
SAVECOST RUN fixture=huge kind=save_project run=2 ms=11266.3 bytes=78272608 result=5103 dialogs=0

# Many, first run
SAVECOST START dir=/private/tmp/claude/claude-502/-Users-kenneth-Projects-JusPrin/bbf2157e-559d-48c8-8701-c991ba9f02bb/scratchpad/fixtures warm=5 threads=12
SAVECOST LOAD fixture=many objects=200 plates=10 per_plate=20,20,20,20,20,20,20,20,20,20 triangles=396800 load_ms=1218.0 dialogs=0
SAVECOST MESHREADY fixture=many ok=1 after_ms=678368.7
SAVECOST RUN fixture=many kind=backup run=0 ms=1147.8 bytes=38694 result=0 steps=pre:1000.0,open:3.3,content_types:0.7,models:26.1,custom_gcode:0.1,print_config:19.7,config_file:26.6,slice_info:3.6,auxiliaries:0.0,relations:2.9,finish:64.6
SAVECOST RUN fixture=many kind=backup run=1 ms=73.6 bytes=38694 result=0 steps=pre:4.4,open:0.8,content_types:0.5,models:9.5,custom_gcode:0.0,print_config:9.0,config_file:16.6,slice_info:0.3,auxiliaries:0.0,relations:0.2,finish:32.3
SAVECOST RUN fixture=many kind=backup run=2 ms=177.3 bytes=38694 result=0 steps=pre:4.1,open:0.3,content_types:0.2,models:12.1,custom_gcode:0.1,print_config:7.4,config_file:11.9,slice_info:0.6,auxiliaries:0.0,relations:0.2,finish:140.3
SAVECOST RUN fixture=many kind=backup run=3 ms=77.8 bytes=38694 result=0 steps=pre:11.4,open:0.4,content_types:0.2,models:6.7,custom_gcode:0.2,print_config:12.6,config_file:25.2,slice_info:0.5,auxiliaries:0.0,relations:0.2,finish:20.4
SAVECOST RUN fixture=many kind=backup run=4 ms=74.6 bytes=38694 result=0 steps=pre:7.7,open:0.6,content_types:0.3,models:13.8,custom_gcode:0.0,print_config:8.2,config_file:13.1,slice_info:0.3,auxiliaries:0.0,relations:0.3,finish:30.1
SAVECOST RUN fixture=many kind=backup run=5 ms=99.1 bytes=38694 result=0 steps=pre:9.9,open:0.6,content_types:0.3,models:13.3,custom_gcode:0.0,print_config:11.7,config_file:29.4,slice_info:0.8,auxiliaries:0.0,relations:1.9,finish:31.1
SAVECOST MESHLAG fixture=many trigger=rename ok=1 edit_ms=69.9 file_after_ms=5836.9
SAVECOST MESHLAG fixture=many trigger=save_object_mesh ok=1 edit_ms=0.1 file_after_ms=3074.6
SAVECOST RUN fixture=many kind=checkpoint run=0 ms=12311.5 bytes=7500698 result=0 steps=pre:348.6,open:0.5,content_types:0.2,thumbnails:11825.7,models:126.7,custom_gcode:0.0,print_config:2.0,config_file:3.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:4.4
SAVECOST RUN fixture=many kind=checkpoint run=1 ms=2232.7 bytes=7500698 result=0 steps=pre:55.9,open:0.1,content_types:0.0,thumbnails:603.2,models:1210.3,custom_gcode:0.1,print_config:13.5,config_file:22.1,slice_info:0.2,auxiliaries:0.4,relations:0.1,finish:326.8
SAVECOST RUN fixture=many kind=checkpoint run=2 ms=6746.5 bytes=7500698 result=0 steps=pre:490.0,open:0.6,content_types:0.6,thumbnails:2648.7,models:3597.1,custom_gcode:0.0,print_config:2.1,config_file:3.2,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:3.8
SAVECOST RUN fixture=many kind=checkpoint run=3 ms=784.4 bytes=7500698 result=0 steps=pre:58.2,open:0.1,content_types:0.0,thumbnails:599.6,models:116.6,custom_gcode:0.0,print_config:2.0,config_file:3.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:4.6
SAVECOST RUN fixture=many kind=checkpoint run=4 ms=6947.1 bytes=7500698 result=0 steps=pre:42.6,open:0.1,content_types:0.0,thumbnails:2046.7,models:4847.8,custom_gcode:0.0,print_config:2.1,config_file:3.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:4.4
SAVECOST RUN fixture=many kind=checkpoint run=5 ms=795.9 bytes=7500698 result=0 steps=pre:53.9,open:0.1,content_types:0.0,thumbnails:589.8,models:138.7,custom_gcode:0.0,print_config:2.0,config_file:3.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:8.0
SAVECOST RUN fixture=many kind=checkpoint_adapter run=0 ms=759.5 bytes=7500698 result=0
SAVECOST RUN fixture=many kind=full run=0 ms=778.6 bytes=7501284 result=0 steps=pre:42.8,open:0.1,content_types:0.0,thumbnails:595.6,models:120.3,custom_gcode:0.0,print_config:2.0,config_file:3.1,slice_info:0.0,auxiliaries:0.6,relations:0.0,finish:14.0
SAVECOST RUN fixture=many kind=full run=1 ms=765.3 bytes=7501284 result=0 steps=pre:43.5,open:0.1,content_types:0.0,thumbnails:586.2,models:116.7,custom_gcode:0.0,print_config:2.0,config_file:3.1,slice_info:0.0,auxiliaries:0.1,relations:0.0,finish:13.5
SAVECOST RUN fixture=many kind=full run=2 ms=773.0 bytes=7501284 result=0 steps=pre:42.7,open:0.1,content_types:0.0,thumbnails:599.9,models:117.8,custom_gcode:0.0,print_config:2.3,config_file:3.3,slice_info:0.1,auxiliaries:0.2,relations:0.0,finish:6.5
SAVECOST RUN fixture=many kind=full run=3 ms=771.9 bytes=7501284 result=0 steps=pre:43.6,open:0.1,content_types:0.0,thumbnails:591.2,models:119.9,custom_gcode:0.0,print_config:2.1,config_file:3.2,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:11.6
SAVECOST RUN fixture=many kind=full run=4 ms=775.2 bytes=7501284 result=0 steps=pre:45.8,open:0.1,content_types:0.0,thumbnails:598.5,models:112.4,custom_gcode:0.0,print_config:2.2,config_file:3.1,slice_info:0.0,auxiliaries:0.2,relations:0.0,finish:12.8
SAVECOST RUN fixture=many kind=full run=5 ms=764.7 bytes=7501284 result=0 steps=pre:43.1,open:0.1,content_types:0.0,thumbnails:587.8,models:117.5,custom_gcode:0.0,print_config:2.2,config_file:3.4,slice_info:0.0,auxiliaries:0.2,relations:0.1,finish:10.2
SAVECOST RUN fixture=many kind=save_project run=0 ms=774.6 bytes=7501284 result=5103 dialogs=0
SAVECOST RUN fixture=many kind=save_project run=1 ms=765.0 bytes=7501284 result=5103 dialogs=0
SAVECOST RUN fixture=many kind=save_project run=2 ms=774.9 bytes=7501284 result=5103 dialogs=0

# Many, second run (disturbed)
SAVECOST START dir=/private/tmp/claude/claude-502/-Users-kenneth-Projects-JusPrin/bbf2157e-559d-48c8-8701-c991ba9f02bb/scratchpad/fixtures warm=5 threads=12
SAVECOST LOAD fixture=many objects=200 plates=10 per_plate=20,20,20,20,20,20,20,20,20,20 triangles=396800 load_ms=1190.7 dialogs=0
SAVECOST MESHREADY fixture=many ok=1 after_ms=676090.3
SAVECOST RUN fixture=many kind=backup run=0 ms=71.1 bytes=38719 result=0 steps=pre:9.2,open:0.5,content_types:0.3,models:7.8,custom_gcode:0.0,print_config:9.8,config_file:18.4,slice_info:0.2,auxiliaries:0.0,relations:0.1,finish:24.7
SAVECOST RUN fixture=many kind=backup run=1 ms=56.1 bytes=38719 result=0 steps=pre:8.6,open:0.4,content_types:0.2,models:8.4,custom_gcode:0.0,print_config:7.6,config_file:11.8,slice_info:0.3,auxiliaries:0.1,relations:0.2,finish:18.5
SAVECOST RUN fixture=many kind=backup run=2 ms=46.5 bytes=38719 result=0 steps=pre:4.2,open:0.4,content_types:0.2,models:5.8,custom_gcode:0.0,print_config:6.8,config_file:11.9,slice_info:0.2,auxiliaries:0.0,relations:0.1,finish:17.0
SAVECOST RUN fixture=many kind=backup run=3 ms=63.7 bytes=38719 result=0 steps=pre:9.4,open:0.4,content_types:0.3,models:13.6,custom_gcode:0.1,print_config:9.1,config_file:9.1,slice_info:0.4,auxiliaries:0.0,relations:0.1,finish:21.2
SAVECOST RUN fixture=many kind=backup run=4 ms=50.6 bytes=38719 result=0 steps=pre:6.7,open:0.4,content_types:0.2,models:6.4,custom_gcode:0.1,print_config:6.8,config_file:8.2,slice_info:0.1,auxiliaries:0.0,relations:0.1,finish:21.6
SAVECOST RUN fixture=many kind=backup run=5 ms=60.3 bytes=38719 result=0 steps=pre:7.7,open:0.6,content_types:0.5,models:12.1,custom_gcode:0.0,print_config:5.6,config_file:14.0,slice_info:0.2,auxiliaries:0.0,relations:0.1,finish:19.5
SAVECOST MESHLAG fixture=many trigger=rename ok=1 edit_ms=44.4 file_after_ms=3030.6
SAVECOST MESHLAG fixture=many trigger=save_object_mesh ok=1 edit_ms=0.1 file_after_ms=3025.8
SAVECOST RUN fixture=many kind=checkpoint run=0 ms=7376.0 bytes=7500724 result=0 steps=pre:299.8,open:0.6,content_types:0.3,thumbnails:2114.1,models:4866.3,custom_gcode:0.1,print_config:15.4,config_file:17.0,slice_info:0.3,auxiliaries:0.9,relations:0.1,finish:61.1
SAVECOST RUN fixture=many kind=checkpoint run=1 ms=4764.6 bytes=7500724 result=0 steps=pre:177.9,open:0.4,content_types:0.1,thumbnails:2175.7,models:2299.5,custom_gcode:0.0,print_config:35.5,config_file:19.8,slice_info:0.2,auxiliaries:0.6,relations:0.2,finish:54.7
SAVECOST RUN fixture=many kind=checkpoint run=2 ms=4413.7 bytes=7500724 result=0 steps=pre:237.0,open:1.0,content_types:0.7,thumbnails:1915.7,models:2224.4,custom_gcode:0.0,print_config:6.1,config_file:12.6,slice_info:0.2,auxiliaries:0.8,relations:0.1,finish:14.9
SAVECOST RUN fixture=many kind=checkpoint run=3 ms=44642.2 bytes=7500724 result=0 steps=pre:359.5,open:0.9,content_types:0.8,thumbnails:1889.6,models:42353.7,custom_gcode:0.1,print_config:9.7,config_file:12.2,slice_info:0.2,auxiliaries:0.7,relations:0.2,finish:14.6
SAVECOST RUN fixture=many kind=checkpoint run=4 ms=26088.3 bytes=7500724 result=0 steps=pre:1877.1,open:0.5,content_types:1.6,thumbnails:9937.3,models:14162.9,custom_gcode:0.1,print_config:11.4,config_file:14.3,slice_info:0.2,auxiliaries:2.3,relations:0.2,finish:80.5
SAVECOST RUN fixture=many kind=checkpoint run=5 ms=5561.3 bytes=7500724 result=0 steps=pre:229.0,open:1.5,content_types:0.4,thumbnails:1957.6,models:3301.1,custom_gcode:0.0,print_config:10.1,config_file:18.1,slice_info:0.4,auxiliaries:0.6,relations:0.2,finish:42.2
SAVECOST RUN fixture=many kind=checkpoint_adapter run=0 ms=4634.3 bytes=7500724 result=0
SAVECOST RUN fixture=many kind=full run=0 ms=4895.9 bytes=7501310 result=0 steps=pre:121.7,open:0.5,content_types:0.2,thumbnails:2387.1,models:2254.6,custom_gcode:0.1,print_config:15.1,config_file:17.6,slice_info:0.2,auxiliaries:1.5,relations:0.1,finish:97.1
SAVECOST RUN fixture=many kind=full run=1 ms=5057.9 bytes=7501310 result=0 steps=pre:154.7,open:0.6,content_types:0.2,thumbnails:2606.3,models:2213.1,custom_gcode:0.0,print_config:8.6,config_file:18.8,slice_info:0.2,auxiliaries:1.8,relations:0.3,finish:53.2
SAVECOST RUN fixture=many kind=full run=2 ms=4547.7 bytes=7501310 result=0 steps=pre:166.0,open:0.6,content_types:0.2,thumbnails:1718.5,models:2601.5,custom_gcode:0.1,print_config:6.7,config_file:10.9,slice_info:0.2,auxiliaries:0.8,relations:0.1,finish:42.2
SAVECOST RUN fixture=many kind=full run=3 ms=5055.1 bytes=7501310 result=0 steps=pre:179.4,open:0.6,content_types:0.2,thumbnails:1971.6,models:2834.8,custom_gcode:0.1,print_config:12.4,config_file:12.9,slice_info:0.3,auxiliaries:1.2,relations:0.2,finish:41.5
SAVECOST RUN fixture=many kind=full run=4 ms=6092.0 bytes=7501310 result=0 steps=pre:151.8,open:0.6,content_types:0.2,thumbnails:2100.0,models:3750.8,custom_gcode:0.1,print_config:12.0,config_file:21.1,slice_info:0.2,auxiliaries:0.8,relations:0.1,finish:54.3
SAVECOST RUN fixture=many kind=full run=5 ms=46885.8 bytes=7501310 result=0 steps=pre:224.9,open:0.6,content_types:0.1,thumbnails:2397.2,models:44198.3,custom_gcode:0.2,print_config:8.6,config_file:11.8,slice_info:0.3,auxiliaries:1.7,relations:0.1,finish:41.9
SAVECOST RUN fixture=many kind=save_project run=0 ms=10191.1 bytes=7501310 result=5103 dialogs=0
SAVECOST RUN fixture=many kind=save_project run=1 ms=16830.2 bytes=7501310 result=5103 dialogs=0
SAVECOST RUN fixture=many kind=save_project run=2 ms=793.3 bytes=7501310 result=5103 dialogs=0
```
