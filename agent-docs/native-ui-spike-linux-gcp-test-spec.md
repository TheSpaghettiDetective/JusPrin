# Spike 1 Linux GCP validation specification

**Status:** Executed on 2026-08-12 for Ubuntu 24.04, X11, and Mesa llvmpipe;
retained as the reproducible procedure for follow-up Linux coverage.

**Assessment method:** Classify observations using
[`native-ui-risk-and-verification.md`](native-ui-risk-and-verification.md).
Environment limitations and localized backend defects are not architecture
failures unless evidence shows that they require a broad boundary change.

## Objective

Validate Spike 1 from branch `codex/native-ui-spike-1` on an ephemeral Ubuntu
24.04 Compute Engine VM in the `elegoo-backend` GCP project. The run must exercise
the real OrcaSlicer OpenGL canvas beside the native wxWidgets pane and the
WebKitGTK Agent pane. It must add evidence to
`agent-docs/native-ui-spike-results.md`; it must not add product features or silently
fix failures discovered during validation.

This VM run can demonstrate Linux/WebKitGTK under its actual display stack. A
VNC/X11 session using Mesa software rendering does not demonstrate Wayland or a
physical GPU, so label those configurations untested unless they are exercised
separately.

## Access preflight

As of 2026-08-12, the locally configured identity is
`ai-agent@tsdtechnology.iam.gserviceaccount.com`. Read-only checks against
`elegoo-backend` must succeed before creating anything. Use a project-authorized
identity and confirm the exact project ID.

Do not change project IAM, billing, organization policies, networks, firewall
rules, or service accounts to work around failed access checks. If an authorized
identity is unavailable, stop and report the required permission to the user.

## Safety and cost requirements

- Use a unique VM name beginning with `orca-spike1-linux-` and label it
  `purpose=orca-spike1-validation`.
- Inspect existing instances, networks, available zones, project quotas, and
  applicable organization policies before selecting a zone.
- Prefer an Ubuntu 24.04 LTS x86-64 image, an `e2-standard-8` VM (8 vCPU,
  32 GiB RAM), and a 150 GiB balanced persistent boot disk. If unavailable,
  choose the smallest substitute that still provides at least 16 GiB RAM and
  enough disk for OrcaSlicer's dependency and application builds, and record
  the substitution.
- Set a maximum run duration of eight hours with deletion on termination when
  project policy supports it. Ensure the boot disk is configured for automatic
  deletion with the VM.
- Do not create a static IP. Prefer IAP with no external IP when existing
  project configuration permits it. Otherwise use an ephemeral IP and the
  existing SSH path.
- Do not expose VNC or noVNC ports through a firewall rule. Bind them to
  localhost on the VM and reach them through an SSH or IAP tunnel.
- Do not attach a service account or broad API scopes unless the build requires
  them. Cloning and building this public repository should not require GCP API
  access from inside the VM.
- Preserve evidence locally before cleanup. Delete the VM at the end even if
  validation fails, then verify that its boot disk and ephemeral address did
  not remain. Never delete pre-existing project resources.

## Preflight

1. Verify the active GCP identity and read access to `elegoo-backend`.
2. Confirm Compute Engine is already enabled. Do not enable APIs without user
   approval if that would change project configuration.
3. List existing labeled resources to avoid name collisions and accidental
   cleanup of another run.
4. Confirm VM creation, SSH or IAP access, and instance deletion permissions.
5. Resolve and record the exact Ubuntu 24.04 image family and chosen zone rather
   than assuming either exists.
6. Check that `tsd/codex/native-ui-spike-1` resolves to the pushed Spike 1
   commit and record that commit SHA.

## VM and desktop setup

Create the VM with the constraints above. Install a minimal X11 desktop and the
packages needed for a private remote GUI session, including:

- XFCE or an equivalent lightweight X11 session
- TigerVNC or an equivalent localhost-bound VNC server
- noVNC and websockify, served only on VM localhost
- Mesa utilities so the renderer can be recorded with `glxinfo -B`
- the WebKitGTK 4.1 runtime and helpers required by the packaged OrcaSlicer
  executable

Use SSH port forwarding for noVNC, for example by forwarding local port 6080 to
VM localhost port 6080. Do not add a public ingress rule for 5901 or 6080. Use a
VNC password without printing or saving it in the repository or result
document.

Record:

- Ubuntu version and kernel
- X11 or Wayland session type
- Mesa/OpenGL renderer and version
- GTK, wxWidgets, WebKitGTK, and JavaScriptCore versions
- VM machine type, CPU count, memory, disk, zone, and whether rendering is
  software or GPU-backed

## Checkout and build

1. Clone `https://github.com/TheSpaghettiDetective/OrcaSlicer.git` and check out
   `codex/native-ui-spike-1` at the recorded remote SHA.
2. Confirm `resources/web/native-ui-spike/dist/index.html` is present.
3. Follow the repository's Linux build path rather than inventing a separate
   CMake configuration:
   - run `./build_linux.sh -u` to install the supported system prerequisites;
   - run `./build_linux.sh -dsi` to build dependencies, OrcaSlicer, and the
     release-like AppImage.
4. If an appropriate current Node.js runtime is available, also run `npm ci`,
   `npm run typecheck`, and `npm run build` in
   `resources/web/native-ui-spike`. Do not make Node setup a blocker for the
   Linux composition test because the committed single-file bundle is the
   packaged runtime asset.
5. Run `git diff --check` and the configured OrcaSlicer test suite. If no tests
   are configured, record that fact rather than reporting a pass.
6. Confirm the packaged artifact contains the Agent HTML resource and resolves
   WebKitGTK 4.1 from the intended host runtime.

If compilation or packaging fails, retain the complete command, relevant log,
and first actionable error. Distinguish failures introduced by Spike 1 from
environment or pre-existing branch failures. Do not patch a failure as part of
this validation task.

## Launch

Use a fresh isolated data directory and enable the development shell with
`ORCA_NATIVE_UI_SPIKE=1`. Launch the packaged or release-like OrcaSlicer build
from inside the remote X11 desktop. Do not reuse the operator's normal Orca
profile.

An empty project should seed two plates and two real mesh objects. Verify that
the center view is the production `GLCanvas3D` and that the Agent pane reports
the C++ project state.

## Required manual checks

Exercise every item below and record Pass, Fail, Partial, or Untested with a
short observation:

1. Select each object in the native left pane and confirm real viewport
   selection.
2. Select each object directly in the viewport and confirm the left pane follows
   it. This was not conclusively demonstrated on macOS and is a priority.
3. Continuously orbit the model while collapsing and restoring the left pane.
4. Submit a long mock Agent response and, while it streams, orbit the model,
   resize/maximize the application, and collapse/restore the pane.
5. Move focus among the canvas, object list, and chat input. Test global
   shortcuts from inside and outside the WebView, text selection, copy/paste,
   and an IME if one is available.
6. Switch Prepare to Preview and back while preserving the Agent pane and
   transcript.
7. Trigger Slice from the spike button and confirm it uses the existing slicing
   path and production G-code Preview.
8. Exercise normal and high-DPI scaling if the remote display stack supports
   both.
9. Test light and dark appearance.
10. Close and relaunch OrcaSlicer to exercise Agent WebView startup again. If a
    safe reload path is available, exercise it as well.

Watch specifically for black or stale GL regions, WebView gaps, flicker,
incorrect pane geometry, lost keyboard events, scroll-anchor jumps, crashes,
and WebKit helper startup failures.

## Resource observations

Capture one idle sample and one streaming sample. Record OrcaSlicer RSS and CPU
as well as identifiable WebKit network, GPU, and WebContent helper processes.
Treat these as diagnostic snapshots, not performance benchmarks.

## Evidence and result update

Collect repository-local screenshots for at least:

- resting Prepare shell
- collapsed and expanded shell
- active streamed response
- Preview after slicing
- any failure or rendering artifact

Append the Linux build, environment, observations, resource samples,
screenshots, and limitations to `agent-docs/native-ui-spike-results.md`. Do not erase
or rewrite the macOS evidence. State explicitly whether the result covers X11,
Wayland, software rendering, and/or GPU rendering.

The final conclusion must say one of:

- the proposed native/WebView boundary is demonstrated on this Linux setup;
- it is demonstrated with localized defects listed;
- it is not demonstrated, with the blocking behavior and evidence listed.

If source changes appear necessary, report the reproduced problem and propose a
separate fix task. Do not implement the fix in this validation run.

## Cleanup and handoff

1. Copy the updated results document, screenshots, build/test logs, environment
   inventory, and a patch or branch reference off the VM.
2. Confirm no credentials, SSH keys, VNC passwords, tokens, or private profiles
   are present in those artifacts.
3. Delete only the VM created for this run.
4. Verify its boot disk and ephemeral address are gone and that no new firewall
   rule or static IP exists.
5. Report the pushed commit tested, VM configuration, test matrix, evidence
   paths, cleanup verification, and any remaining Windows/Linux coverage gap.

Follow the repository commit policy for any results-document commit: review
`git status` and `git diff`, propose the commit message, and wait for explicit
user confirmation before committing. Use a separate results branch unless the
user explicitly directs otherwise.

## Lessons learned and issue reproduction

The 2026-08-12 run demonstrated the boundary on Ubuntu 24.04 under X11 with
Mesa llvmpipe. It did not reveal a blocking Spike 1 product defect. The issues
below were infrastructure, environment, or test-harness limitations. Preserve
that distinction in future result reports.

### SSH can disconnect during the dependency build

The first `./build_linux.sh -dsi` connection reset after dependency
compilation. The remote build process was no longer running, but the completed
dependency outputs were intact. Running the same supported command again
resumed incrementally and produced the AppImage without source changes.

To reproduce and recover:

1. Start the normal build over SSH and save its output:
   `./build_linux.sh -dsi 2>&1 | tee build-linux-dsi.log`.
2. If SSH disconnects, reconnect and check for an existing build process with
   `pgrep -af 'build_linux.sh|cmake --build|ninja|make'`.
3. If no build is active, rerun exactly `./build_linux.sh -dsi`; do not invent a
   different CMake configuration or clean the successful dependency outputs.
4. Record the transport interruption separately from the final build result.

Lesson: the supported Linux build is safely incremental for this failure mode.
Use a persistent remote session or a detached log for long runs, and never
classify an SSH transport reset as a compilation failure without checking the
remote process and build log.

### The default Node.js was too old for the locked web toolchain

Ubuntu supplied Node.js 18.19.1 and no npm, while the locked Vite 8 toolchain
requires Node.js 20.19+ or 22.12+. This did not block the composition test
because the committed single-file `dist/index.html` was present in both the
package tree and AppImage.

To reproduce:

1. Run `node --version` and `npm --version` on the VM.
2. Inspect the package engines with
   `cd resources/web/native-ui-spike && node -p "require('./package-lock.json').packages['node_modules/vite'].engines.node"`
   when npm/Node are sufficiently functional to do so.
3. Confirm `resources/web/native-ui-spike/dist/index.html` exists before
   deciding whether the optional web rebuild can be skipped.

Lesson: check the Node version before spending time on `npm ci`. Treat the
committed bundle as the runtime input for this validation, but require a current
Node environment whenever the web source itself must be rebuilt or changed.

### Software-rendered VNC needs an explicit WebKit launch path

The VNC desktop reported Mesa llvmpipe rather than a physical GPU. WebKitGTK
logged expected DRI3 warnings in that environment. Launching the AppImage with
extraction and the DMABUF renderer disabled produced a stable Agent pane:

```sh
export DISPLAY=:1
export XDG_SESSION_TYPE=x11
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
APPIMAGE_EXTRACT_AND_RUN=1 \
ORCA_NATIVE_UI_SPIKE=1 \
WEBKIT_DISABLE_DMABUF_RENDERER=1 \
./OrcaSlicer_Linux_V2.4.0-dev.AppImage \
  --datadir "$SPIKE_DATA_DIR"
```

Before launch, create `SPIKE_DATA_DIR` as a new empty directory outside any
normal OrcaSlicer profile, for example:

```sh
SPIKE_DATA_DIR=$(mktemp -d /tmp/orca-spike1-data.XXXXXX)
export SPIKE_DATA_DIR
```

Confirm the renderer with
`DISPLAY=:1 glxinfo -B`. The expected evidence is `llvmpipe`; do not describe
this run as GPU or Wayland coverage.

Lesson: `libEGL`/DRI3 warnings alone are not proof of a product defect under
software VNC. Correlate them with visible corruption, a WebKit helper failure,
or a crash. Keep `WEBKIT_DISABLE_DMABUF_RENDERER=1` specific to this test
environment rather than turning it into a product default.

### Fresh profiles introduce unrelated startup prompts

The isolated data directory correctly triggered the setup wizard, an SSL
certificate decision, and an available-version notice. These prompts obscured
the shell until they were completed or dismissed, but they were not caused by
the Spike pane.

To reproduce, launch with a new `--datadir`, complete the Generic Klipper setup
without installing proprietary plug-ins, dismiss the update notice, and then
verify that a new project seeds the Spike shell. On close, choose not to save
the synthetic project before relaunching.

Lesson: budget for first-run UI in an end-to-end test. Record it separately so
startup prompts are not mistaken for WebView load failures.

### Theme and DPI changes are most reliable across a relaunch

Changing the XFCE theme while OrcaSlicer was already open did not fully repaint
the existing shell. Fresh launches at 96 and 192 logical DPI worked, and a
fresh `Greybird-dark` launch produced matching dark native, GL, and Agent
surfaces.

To reproduce:

1. Set normal DPI with
   `xfconf-query -c xsettings -p /Xft/DPI -n -t int -s 96`, then launch and
   capture the shell.
2. Close OrcaSlicer, set the property to `192`, relaunch, and inspect native/
   WebView geometry at the same VNC resolution.
3. Restore DPI to `96`, then relaunch with `GTK_THEME=Greybird-dark` to test
   dark appearance.

Lesson: test theme and logical-DPI startup behavior with fresh processes. A
fixed 1280x720 VNC screen at 192 logical DPI is useful seam coverage, but it is
not evidence from a physical high-resolution monitor.

### Some input checks require a human or a richer remote-input harness

The single-pointer remote controller could orbit, collapse/restore, resize, and
stream in the same stress sequence, but it could not hold one uninterrupted
orbit drag while independently clicking another native control. No IME was
configured. `Ctrl+1` also produced no visible camera change from the tested
oblique view, so global shortcut routing remained inconclusive.

To reproduce the remaining manual checks:

1. With one hand, hold the normal viewport orbit gesture continuously.
2. While the Agent response is streaming, use a second input method to toggle
   the native list and resize/maximize the window without ending the orbit.
3. Focus the canvas and press a known global shortcut that produces an
   unmistakable visual change; repeat with focus in the chat input.
4. Install and configure an IBus engine, compose non-ASCII text in the chat
   input, commit it, and verify the exact submitted string.

Lesson: report these checks as Partial or Untested unless simultaneous input,
shortcut effect, and IME composition are directly observed. Sequential stress
steps are useful evidence, but they are not equivalent to physical simultaneity.

### Aggressive synthetic resize can emit non-fatal GTK assertions

Rapid `wmctrl` unmaximize/resize/maximize sequences produced isolated GTK
scrollbar and zero-width assertions. The shell recovered without a black GL
region, WebView gap, or crash.

To reproduce, identify the OrcaSlicer window with `wmctrl -lx`, then remove its
maximized state, resize it to approximately 1050x640, and maximize it again
while a mock response streams. Retain both the application log and screenshots.

Lesson: do not ignore these messages, but judge them with runtime evidence. A
non-fatal toolkit assertion without visible or functional impact is an
observation, not by itself a failed native/WebView boundary.

### Resource measurements include OrcaSlicer's existing WebViews

Several WebKit network and WebContent helpers were already owned by the main
process, and no separate GPU helper was identifiable under software rendering.
The Spike Agent helper could not be attributed reliably by process name alone.

To reproduce, sample the application and all child helpers with
`ps -eo pid,ppid,comm,%cpu,%mem,rss,vsz,etime,args` at idle and during a stream,
then relate helpers to the OrcaSlicer parent PID.

Lesson: report whole-application point samples and ranges. Do not claim that a
specific helper or its memory belongs exclusively to the Agent pane without
stronger process-level attribution.

### Keep remote-display credentials ephemeral

The private VNC session required a password, but the cleartext handoff file was
not part of the evidence and was deleted before the VM. A pattern audit of the
copied evidence found no credentials, SSH keys, tokens, private profiles, or VNC
passwords.

To reproduce the safe cleanup, remove any temporary cleartext password file,
scan only the preserved evidence for common credential/key patterns, copy the
evidence off the VM, delete the VM, and verify that its instance, auto-delete
boot disk, ephemeral address, matching firewall rules, and static addresses are
all absent.

Lesson: bind VNC/noVNC to localhost, tunnel them over SSH, never put the password
in the repository or result document, and perform the credential audit before
destroying the VM.

## References

- OrcaSlicer Linux build entry point: `build_linux.sh`
- OrcaSlicer build documentation:
  <https://github.com/OrcaSlicer/OrcaSlicer/wiki/how_to_build>
- Google Cloud Linux VM creation:
  <https://docs.cloud.google.com/compute/docs/create-linux-vm-instance>
- `gcloud compute instances create` reference:
  <https://docs.cloud.google.com/sdk/gcloud/reference/compute/instances/create>
- `gcloud compute ssh` and IAP tunneling reference:
  <https://docs.cloud.google.com/sdk/gcloud/reference/compute/ssh>
- Google Cloud VM stop/delete cost behavior:
  <https://docs.cloud.google.com/compute/docs/instances/stop-start-instance>
