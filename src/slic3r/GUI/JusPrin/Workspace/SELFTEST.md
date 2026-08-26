# Workspace adapter self test

`OrcaWorkspaceAdapter` cannot be unit tested: every method needs a live `Plater`,
which needs the whole application including an OpenGL canvas. `workspace_contract_tests`
therefore only ever exercises `FakeWorkspace`, and the real adapter has no automated
coverage.

This self test closes that gap by making the application test itself. It launches
normally, loads a fixture, drives `IWorkspace` through a scripted scenario, checks
the results, prints a transcript, and terminates the process with exit code 0 when
every check passed or 1 when any check failed.

There is no GUI automation involved — no clicking, no synthetic input, no screenshots.
From the outside it behaves like any other test command.

## Running it

```bash
JUSPRIN_WORKSPACE_SELFTEST=1 build/arm64/src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/OrcaSlicer; echo "exit=$?"
```

Or use the wrapper, which also prints the exit code:

```bash
src/slic3r/GUI/JusPrin/Workspace/run_selftest.sh
```

### Environment variables

| Variable | Meaning |
| --- | --- |
| `JUSPRIN_WORKSPACE_SELFTEST=1` | Runs the self test and exits. Required. |
| `JUSPRIN_WORKSPACE_SELFTEST_LOG` | Also write the transcript to this file. The transcript always goes to stderr. |
| `JUSPRIN_WORKSPACE_SELFTEST_FIXTURE` | Load this model instead of the committed fixture. |

When `JUSPRIN_WORKSPACE_SELFTEST` is unset, nothing changes: the application starts
exactly as it did before.

## The scenario

One scenario, deliberately. It is the smallest sequence that touches the projection,
a command, the legacy UI, and the undo stack:

1. Load `resources/jusprin/selftest/selftest_cube.stl`, a 20 mm cube committed to
   the repository so the test does not depend on anything outside version control.
2. Rename the object through `IWorkspace::rename_object`.
3. Check a fresh `snapshot()` reports the new name.
4. Check the legacy `GUI_ObjectList` sidebar tree also displays the new name.
5. Undo through `IWorkspace::undo`. The 3D editor tab is brought up first,
   because `Plater::can_undo()` is false while the plater panel is hidden, and
   the undo is issued in the same event-loop turn that observes it — waiting a
   turn lets visibility flip again before `undo()` re-checks it.
6. Check a fresh `snapshot()` reports the original name again.
7. Check the command result matches reality: reporting success means the
   workspace changed, reporting failure means it did not. This one is about the
   contract rather than the scenario — a command that claims success while
   changing nothing is a defect whatever the underlying undo managed to do.

## Reading the transcript

Lines follow the same shape as the `WorkspaceProbe` log so the two can be compared:

```
SELFTEST seq=0007 CHECK snapshot_shows_new_name PASS expected=JusPrin SelfTest Renamed actual=JusPrin SelfTest Renamed
SELFTEST seq=0011 SUMMARY checks=6 passed=6 failed=0
SELFTEST seq=0012 RESULT pass
```

`SNAPSHOT` lines carry the full projection, `EVENT` lines record change notifications
delivered through `WorkspaceChangeHub`, and `CHECK` lines carry the assertions.

## Running under AddressSanitizer

A memory bug in the notification hub was previously found only under ASan, so the
self test is worth running that way periodically. `SLIC3R_ASAN` already exists as a
CMake option; it adds `-fsanitize=address -fno-omit-frame-pointer` and the matching
linker flags. Configure a separate build directory so the normal one keeps its
non-instrumented objects:

```bash
cmake -S . -B build/arm64-asan -G "Ninja Multi-Config" -DSLIC3R_ASAN=ON -DCMAKE_OSX_ARCHITECTURES=arm64 -DSLIC3R_STATIC=ON -DSLIC3R_GUI=ON -DDEP_BUILD_DIR="$PWD/deps/build/arm64"
```

Then build and run the self test against that binary:

```bash
cmake --build build/arm64-asan --config RelWithDebInfo --target OrcaSlicer
```

```bash
JUSPRIN_WORKSPACE_SELFTEST=1 build/arm64-asan/src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
```

The self test leaves through `std::_Exit` so the reported status is the test result
rather than whatever application teardown produces. That does not hide sanitizer
findings: ASan reports a use-after-free or overflow at the moment of the bad access
and aborts with its own status long before the test would exit. LeakSanitizer, which
runs at exit, is not part of this — it is off by default on macOS and the application
has plenty of unrelated leaks that would drown the signal.
