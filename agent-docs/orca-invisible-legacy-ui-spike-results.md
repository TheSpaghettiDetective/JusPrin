# Invisible legacy Orca UI spike results

## 1. Revision and build configuration

**Final result: PASS.**

The tested source was base revision `8a03153489a2d5454d229978cbef7ae8a810ed57` plus the uncommitted spike changes listed in section 9. No implementation commit was created.

- Host: macOS 26.2 (25C56), arm64
- Generator: Ninja Multi-Config
- Tested configuration: `RelWithDebInfo`
- CMake 3.29.0
- Apple clang 21.0.0 (`arm64-apple-darwin25.2.0`)
- `CMAKE_OSX_ARCHITECTURES=arm64`
- `CMAKE_OSX_DEPLOYMENT_TARGET=15.0`
- SDK: `MacOSX26.5.sdk`
- `BUILD_TESTING=ON`, `BUILD_TESTS=ON`
- Dependency prefixes:
  - `/Users/kenneth/Projects/JusPrin/deps/build/arm64/OrcaSlicer_dep/usr/local`
  - `/Users/kenneth/Projects/OrcaSlicer/deps/build/arm64/OrcaSlicer_dep/usr/local`

Affected application target build:

```text
cmake --build build/arm64 --config RelWithDebInfo --target OrcaSlicer -- -j4
[4/4] Linking CXX executable src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
exit 0
```

## 2. Lifecycle configuration tested

The run used `JUSPRIN_INVISIBLE_LEGACY_UI_SPIKE=1` and the deterministic project `/Users/kenneth/Downloads/stls/Projekt+-+standard+-+2+plates.3mf`.

Orca constructed and kept its normal `MainFrame`, `Plater`, sidebar, object list, canvas, model, plates, and undo stack. The legacy top-level remained mapped and `IsShown()==true`. A separate, unparented, borderless `wxFrame` named `JusPrin Empty Shell Spike` covered the legacy frame's exact screen rectangle with one opaque solid panel. The shell tracked legacy move, size, maximize, iconize, show, activate, and destroy events; a 100 ms timer checked coverage and z-order. The probe was a child top-level of the shell, positioned 24 logical pixels inside its bounds and raised above it.

The shell is deliberately not in the legacy frame's wx ownership hierarchy. That keeps it alive when the probe is destroyed. It still observes the legacy frame explicitly. Escape or Command-W closes the development shell, hides it, raises the already-existing legacy frame, and then destroys only the shell.

## 3. Verdict

**PASS after two small lifecycle/z-order adjustments.** Every required probe command completed against the real Orca model while the legacy frame stayed fully occluded. Callback state, history semantics, selection, names, presence, and duplicate ID were stable. No command surfaced a legacy window or dialog, stole focus permanently, hung, or crashed. Destroying the probe left the application and opaque shell alive.

The adjustments made during the spike were:

1. Native macOS choice dismissal could leave the shell as key window, so the probe now restores itself on the next event-loop turn.
2. Parenting the shell to the legacy frame caused probe teardown to cascade into legacy-frame teardown on macOS. Making the shell an independently owned top-level fixed this; the final run logged `probe_destroyed shell_remains_shown=1` and kept the app alive.

## 4. Complete ordered probe workflow log

The authoritative artifact is [probe.log](orca-invisible-legacy-ui-spike-evidence/probe.log). Its complete final-run contents are reproduced below.

```text
PROBE seq=0001 SNAPSHOT source=initial revision=0 plates=1 active=11 selection=none can_undo=0 can_redo=0 objects=[]
PROBE seq=0002 EVENT revision=1 reasons=Selection
PROBE seq=0003 SNAPSHOT source=callback revision=1 plates=2 active=83 selection=none can_undo=1 can_redo=1 objects=[102:Grundkörper pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0004 SNAPSHOT source=manual revision=1 plates=2 active=83 selection=none can_undo=1 can_redo=1 objects=[102:Grundkörper pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0005 COMMAND select success
PROBE seq=0006 EVENT revision=2 reasons=Selection
PROBE seq=0007 SNAPSHOT source=callback revision=2 plates=2 active=83 selection=102 can_undo=1 can_redo=0 objects=[102:Grundkörper pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0008 COMMAND rename success
PROBE seq=0009 EVENT revision=3 reasons=Contents
PROBE seq=0010 SNAPSHOT source=callback revision=3 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0011 COMMAND undo success
PROBE seq=0012 EVENT revision=4 reasons=Contents|History
PROBE seq=0013 SNAPSHOT source=callback revision=4 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0014 COMMAND redo success
PROBE seq=0015 EVENT revision=5 reasons=Contents|History
PROBE seq=0016 SNAPSHOT source=callback revision=5 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0017 COMMAND duplicate success object_id=234
PROBE seq=0018 SNAPSHOT source=command revision=5 plates=2 active=83 selection=234 can_undo=1 can_redo=0 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 234:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0019 EVENT revision=6 reasons=Selection|Contents
PROBE seq=0020 SNAPSHOT source=callback revision=6 plates=2 active=83 selection=234 can_undo=1 can_redo=0 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 234:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0021 COMMAND undo success
PROBE seq=0022 EVENT revision=7 reasons=Selection|Contents|History
PROBE seq=0023 SNAPSHOT source=callback revision=7 plates=2 active=83 selection=102 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0024 COMMAND redo success
PROBE seq=0025 EVENT revision=8 reasons=Selection|Contents|History
PROBE seq=0026 SNAPSHOT source=callback revision=8 plates=2 active=83 selection=234 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 234:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0027 COMMAND remove success
PROBE seq=0028 EVENT revision=9 reasons=Selection|Contents
PROBE seq=0029 SNAPSHOT source=callback revision=9 plates=2 active=83 selection=none can_undo=1 can_redo=1 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0030 COMMAND undo success
PROBE seq=0031 EVENT revision=10 reasons=Selection|Contents|History
PROBE seq=0032 SNAPSHOT source=callback revision=10 plates=2 active=83 selection=234 can_undo=1 can_redo=1 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 234:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0033 COMMAND select success
PROBE seq=0034 EVENT revision=11 reasons=Selection
PROBE seq=0035 SNAPSHOT source=callback revision=11 plates=2 active=83 selection=91 can_undo=1 can_redo=0 objects=[102:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 113:Deckel pos=(115.200,128.000,2.250) rot=(1.571,3.142,1.571); 234:Grundkörper renamed pos=(140.800,128.001,23.250) rot=(-1.571,-3.142,1.571); 91:Slider pos=(435.200,128.000,2.800) rot=(0.000,-3.142,1.571)]
PROBE seq=0036 PROBE consumer destroyed
```

## 5. Before/after command snapshots

| Step | Before | After authoritative callback/state |
|---|---|---|
| Refresh | rev 1, 2 plates, IDs 102/113/91, no selection | rev 1 unchanged; manual snapshot contains Grundkörper, Deckel, Slider |
| Select Grundkörper | rev 1, selection none | rev 2, exactly one `Selection` event, selection 102 |
| Rename | rev 2, `102:Grundkörper` | rev 3, `Contents`, `102:Grundkörper renamed` |
| Undo rename | rev 3, renamed | rev 4, `Contents|History`, original name restored |
| Redo rename | rev 4, original name | rev 5, `Contents|History`, renamed name restored |
| Duplicate | rev 5, IDs 102/113/91 | returned 234; rev 6 `Selection|Contents`, ID 234 present and selected |
| Undo duplicate | rev 6, ID 234 present | rev 7 `Selection|Contents|History`, ID 234 absent, selection 102 |
| Redo duplicate | rev 7, ID 234 absent | rev 8 `Selection|Contents|History`, the same ID 234 present and selected |
| Remove duplicate | rev 8, ID 234 present | rev 9 `Selection|Contents`, ID 234 absent, original ID 102 remains, selection none |
| Undo remove | rev 9, ID 234 absent | rev 10 `Selection|Contents|History`, the same ID 234 present and selected |
| Select Slider | rev 10, selection 234 | rev 11, exactly one `Selection` event, selection 91 (`Slider`) |
| Close probe | probe subscribed and visible | `PROBE seq=0036`; shell log seq 0014 reports `probe_destroyed shell_remains_shown=1` |

`can_undo` remained true after the project loaded. `can_redo` changed to false after new select/duplicate operations and returned to true after undo as recorded in the complete log.

## 6. Screenshot and focus/z-order evidence

Manual visual evidence, cropped to the tested shell rectangle to exclude unrelated desktop content:

- [Opaque shell with probe above it](orca-invisible-legacy-ui-spike-evidence/coverage-shell-and-probe.png)
- [Workflow complete, Slider selected](orca-invisible-legacy-ui-spike-evidence/workflow-complete-cropped.png)
- [Opaque shell still alive after probe close](orca-invisible-legacy-ui-spike-evidence/after-probe-close-cropped.png)

The [shell log](orca-invisible-legacy-ui-spike-evidence/shell.log) records identical legacy/shell bounds (`x=522 y=1473 width=1083 height=767`) and `coverage_match=1`. It records probe bounds inside that rectangle, a z-order restoration after legacy activation, probe destruction with the shell still shown, and recovery closure:

```text
SHELL seq=0006 ... legacy_shown=1 ... shell_shown=1 ... coverage_match=1 ...
SHELL seq=0008 probe_shown ... bounds={x=546 y=1497 width=760 height=480}
SHELL seq=0013 z_order_restored_after_legacy_activation ... coverage_match=1 ...
SHELL seq=0014 probe_destroyed shell_remains_shown=1
SHELL seq=0015 close_shortcut
SHELL seq=0016 close_requested
SHELL seq=0018 legacy_revealed_after_shell_close legacy_shown=1 legacy_active=1 shell_shown=0 ...
SHELL seq=0019 destroyed
```

## 7. Surfaced legacy windows or dialogs

No command-time legacy window or dialog surfaced. The top-level monitor recorded one pre-workflow Orca startup window, `Loading...`, with `modal=0`; it disappeared before the required workflow began. No modal top-level was recorded during the workflow. The legacy frame became visible only after the probe had been destroyed and the tester intentionally closed the shell with Command-W to exercise the recovery path.

## 8. Automated and manual evidence

Automated focused contract tests:

```text
build/arm64/tests/workspace/RelWithDebInfo/workspace_contract_tests.app/Contents/MacOS/workspace_contract_tests --reporter console
All tests passed (41 assertions in 7 test cases)
exit 0
```

Additional checks:

- RelWithDebInfo `OrcaSlicer` application target built successfully after the final source change.
- `git diff --check` passed.
- Manual full workflow used only the separate probe; no clicks or keystrokes were sent to the covered legacy UI.
- Accessibility inspection after probe close reported `JusPrin Empty Shell Spike`, not the legacy frame.
- Accessibility inspection after intentional shell Command-W reported the existing `*Projekt+-+standard+-+2+plates` legacy frame.
- The application was then quit cleanly and test mutations were discarded; no recovery prompt was left behind.

## 9. Production files changed and rebase risk

Changed or added production files:

- `src/slic3r/GUI/JusPrin/InvisibleLegacyUiSpike.cpp` (new)
- `src/slic3r/GUI/JusPrin/InvisibleLegacyUiSpike.hpp` (new)
- `src/slic3r/GUI/JusPrin/Workspace/WorkspaceProbe.cpp`
- `src/slic3r/GUI/JusPrin/Workspace/WorkspaceProbe.hpp`
- `src/slic3r/GUI/Plater.cpp`
- `src/slic3r/CMakeLists.txt`

The runtime path is environment-gated and development-only. No model, plate, selection, geometry, profile, project-format, or undo algorithm changed. Rebase risk is low in the new JusPrin files, low in the probe, and moderate around the `Plater::priv` development-spike initialization and the GUI source list if those areas change upstream.

## 10. Limitations

- This validates concealment, not decoupling. The complete legacy GUI remains constructed, mapped, painting, processing events, and consuming resources.
- The run used one macOS arm64 machine, one display arrangement, one deterministic project, and the current wxWidgets behavior. Windows and Linux z-order/lifecycle behavior were not manually exercised.
- Exact static coverage and one ordinary legacy activation/z-order recovery were observed. Move, resize, maximize, restoration, and display-reconfiguration paths are implemented but were not separately driven because the spike forbids synthesized interaction with the covered legacy window.
- A non-modal Orca `Loading...` top-level exists briefly during startup before the test-ready state. No command-time dialog surfaced.
- The probe is a development tool. Its fixed 760x480 size and 24-pixel inset are not product shell layout behavior.

Within those limits, keeping the legacy Orca UI mapped but fully occluded is operationally viable for the tested workspace command surface.
