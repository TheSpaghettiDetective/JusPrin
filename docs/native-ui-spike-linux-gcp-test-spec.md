# Spike 1 Linux GCP validation specification

## Objective

Validate Spike 1 from branch `codex/native-ui-spike-1` on an ephemeral Ubuntu
24.04 Compute Engine VM in the `elegoo-obico` GCP project. The run must exercise
the real OrcaSlicer OpenGL canvas beside the native wxWidgets pane and the
WebKitGTK Agent pane. It must add evidence to
`docs/native-ui-spike-results.md`; it must not add product features or silently
fix failures discovered during validation.

This VM run can demonstrate Linux/WebKitGTK under its actual display stack. A
VNC/X11 session using Mesa software rendering does not demonstrate Wayland or a
physical GPU, so label those configurations untested unless they are exercised
separately.

## Current access blocker

As of 2026-08-12, the locally configured identity is
`ai-agent@tsdtechnology.iam.gserviceaccount.com`. Read-only checks against
`elegoo-obico` return `PERMISSION_DENIED` or project-not-found. Before creating
anything, use a project-authorized identity and confirm the exact project ID.

Do not change project IAM, billing, organization policies, networks, firewall
rules, or service accounts to work around this blocker. If an authorized
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

1. Verify the active GCP identity and read access to `elegoo-obico`.
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
screenshots, and limitations to `docs/native-ui-spike-results.md`. Do not erase
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
