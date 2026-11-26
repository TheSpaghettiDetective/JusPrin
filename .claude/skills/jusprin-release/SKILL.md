---
name: jusprin-release
description: Use when asked to make, prepare or start a new JusPrin release (a new version on GitHub), from choosing the commit range to the after-publish steps. Covers the version, release notes, server readiness, translations, the error-handling audit, tests, the guide check, moving main, the release workflow, testing the draft and what follows publishing. Not for JusPrin 1 (jusprin-v1) or OrcaSlicer's own releases.
---

# Making a JusPrin release

`.github/workflows/jusprin_release.yml` builds one commit into a draft
release. It refuses the commit without a new version, release notes, a guide
check and the Sentry secrets (see "Releasing" in `AGENTS.md`). It does not
run tests, check the server, check translations or audit error handling.
This skill covers all of it, in order.

## Kenneth decides these

Ask, and wait for his yes before each one. A yes covers that action only.

- The version, when he has not named it, and the release notes' wording.
- Deploying the server.
- Which error-handling findings must be fixed before the release.
- Taking in commits that landed on `jusprin-dev` after the range was fixed.
- Spending API money (say the cost).
- Every push, and moving `main`.
- Starting the release workflow.
- Downloading the draft's files.
- Posting anything on GitHub.

Never publish the draft: Kenneth does, by hand. Never force-push `main`.

## When something has to be fixed

A fix is its own commit on `jusprin-dev`, made in a worktree of its own from
`origin/jusprin-dev`, never in the release commit. Once it is pushed:

- **Before section 8 landed the release commit:** rebase your branch onto
  `origin/jusprin-dev`, record that as the new end of the range, and redo
  step 1.4 and sections 2 to 7.
- **After it landed:** keep the version. Redo step 1.4 and sections 2 to 7
  up to the fix, land what changed (section 8), move `main` and start the
  workflow again. A new run for the same version replaces the draft.

## Where each platform is tested

Every release is tested on all three platforms, twice: the test suites
(section 5) and the draft's own files (section 10). An untested platform
blocks the release.

- **macOS:** this computer.
- **Windows and Linux:** the two Google Cloud test VMs. How to reach, start,
  build on and run the app on each is in
  `<main checkout>/.claude/vm-harness/windows/README.md` and `…/linux/README.md`.
  That folder is local to this computer, never in the repository. Start a
  stopped VM, and stop it again when done. If a VM or that folder is
  missing, stop and tell Kenneth.

## 1. Set up

1. The main checkout (the folder holding `jusprin-dev`) must have no changed
   tracked files and no commits missing from `origin/jusprin-dev`. If it has
   any, list them for Kenneth and wait; never push, commit or discard them
   yourself. Untracked files are his working docs and do not block.
2. The range is the newest `jusprin/v*` tag to the current
   `origin/jusprin-dev`. Record that commit: every later step checks this
   range, and section 8 checks whether `jusprin-dev` moved.
3. Settle the version, then make a worktree at that commit under
   `<repo>/.claude/worktrees/`, on branch `claude/release-<version>` (add a
   suffix if taken). Never name a branch `jusprin-release/...`: pushing one
   starts an unsigned release build.
4. Read every commit in the range for what a person would notice and the
   GitHub issues it fixes.

## 2. Version and release notes

1. `kVersion` in `src/slic3r/GUI/JusPrin/Updates/Version.hpp`.
2. `src/slic3r/GUI/JusPrin/Packaging/release-notes/<version>.md`, shaped like
   the previous one, for a person who prints. It becomes the draft's text on
   every workflow run, and an older app shows it under "Release notes" in
   Preferences › Updates. Check every on-screen label you quote against the
   source.
3. Anything new the app sends from the person's computer goes in the notes,
   with what it holds.

## 3. Server readiness

Every address the release calls must already answer on
`https://jusprinv2.obico.io`. Many are joined from pieces, so list them from
the code that builds them and write out each full path:

```bash
git grep -n -E '"/(ent|o|accounts|jusprin)/|"/models"|ask_server\("' -- src/slic3r/GUI/JusPrin ':!*test*'
```

For example `ask_server("code/confirm", …)` is
`/ent/jusprin/v2/auth/code/confirm`, and the model list is the gateway's
address with its last part replaced by `/models`. A bare GET with no sign-in
or real data is enough: any 4xx answer except 404 means the address is there.

First GET a made-up address such as `/ent/jusprin/v2/no-such-address`: it
must answer 404. In maintenance mode the server answers 200 for every path,
and the check cannot tell anything apart; stop and tell Kenneth.

A missing address means the server change is not deployed. Find it in
tsd-enterprise (`web_ent/jusprin/v2/urls.py` on `master`) and tell Kenneth,
along with any other server behavior the range relies on. Deploying is his
call.

## 4. Error-handling audit

Run the `error-handling` skill on the range. When the release relies on
server changes, also audit tsd-enterprise's `web_ent/jusprin/` commits on
`master` since the previous tag's date (that repository has no release tags).
Report the findings and wait for Kenneth to say which must be fixed.

## 5. Tests

Build in your worktree in Release, following "Building on a shared machine"
in `AGENTS.md`. Use CMake.app's cmake (Homebrew's fails to find Eigen3), node
on `PATH`, and absolute paths.

The app builds against the main checkout's dependency folder, which may
predate a dependency the range added or changed. Bring it up to date first:

```bash
<worktree>/scripts/deps_current.sh <main checkout>/deps/build/arm64
```

It compares the `deps/` recipe the folder was last built from with the
worktree's, and when they differ it rebuilds the libraries that changed. If
it says the main checkout's recipe differs from the worktree's, the main
checkout is not at the end of the range; stop and tell Kenneth.

```bash
cmake -S <worktree> -B <worktree>/build/arm64 -G "Ninja Multi-Config" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_PREFIX_PATH=<main checkout>/deps/build/arm64/OrcaSlicer_dep/usr/local \
  -DSLIC3R_GUI=ON -DSLIC3R_STATIC=ON -DSLIC3R_PCH=ON -DBUILD_TESTS=ON \
  -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib
cmake --build <worktree>/build/arm64 --config Release --target tests/all jusprin_web jusprin_catalogs -- -j4
ctest --test-dir <worktree>/build/arm64/tests -C Release --output-on-failure -LE RequiresApp
```

`CMAKE_PREFIX_PATH` takes one path only. `tests/all` builds every test
program, including `shell_integration_harness` for the guide check.
`agent_live_tests` is built but never registered with ctest, since each run
is billed. Also run `npm test` in `src/slic3r/GUI/JusPrin/AgentUI` and
`src/slic3r/GUI/JusPrin/HomeUI`.

- **Windows and Linux,** on the VMs, from a checkout of the end of the range:
  `build_win.bat -s --run-tests`, and `./build_linux.sh -st` then
  `ctest --test-dir build/tests -C Release --output-on-failure`, keeping the
  `RequiresApp` tests, which run only on Linux.
- **Prompt suites,** when the range changes text the model reads: run
  `tests/project_prompt` (and `tests/printer_prompt` when printer chats
  changed) on the free `claude-cli` backend. Its README also asks for a small
  paid run on the app's default model before a release; propose it with its
  cost.

To tell whether the range caused a failure, run the same test on a build of
the last release's tag. A failure there too is older: report it and let
Kenneth decide. One the range caused is a bug to fix before the release.

## 6. Translations

After the audit and tests, since a fix can add a sentence.

1. Run `src/slic3r/GUI/JusPrin/Localization/update_catalogs.py` (needs GNU
   gettext and `npm ci` in `AgentUI`). Changed catalogs go in the release
   commit.
2. Translate new entries in JusPrin's own catalogs, `Localization/native/`
   and `Localization/pages/`, as `Localization/README.md` says. Never touch
   upstream's `localization/i18n/` or run `run_gettext`; from `AGENTS.md` and
   the `orca-translations` skill take only the editing rules and glossary.
3. `npx vitest run src/catalogs.test.ts` in `AgentUI` must pass.
4. Rebuild `jusprin_web` and `jusprin_catalogs` for the guide check.
5. Tell Kenneth how many entries are marked `# Needs human review`.

No tool finds a sentence nobody marked for translation; look for one while
reading the range.

## 7. Guide check

Follow "Before a release" in `docs/guide/README.md`, on the step 5 build.

1. Tell Kenneth first: it drives the app on this Mac's screen, and nobody
   should use the computer meanwhile.
2. Add new screens or menu items from the range to the guide before the
   check: it cannot find them, and a page changed after it passes is
   unchecked.
3. Run `<worktree>/scripts/check_guide.py --ask-ai`. The script checks the
   tree it sits in, so the main checkout's copy checks the wrong code. It
   writes `docs/guide/checked-version.txt` when it passes.

## 8. Commit and land

1. The release commit holds `Version.hpp`, the notes, the catalogs, and what
   the guide check changed: `docs/guide/` (pages, `pictures/`,
   `checked-version.txt`) and `AgentUI/src/skills/guide-*/`. Nothing else.
2. If `origin/jusprin-dev` moved past the recorded end of the range, list the
   new commits for Kenneth. With his yes, rebase and redo step 1.4 and
   sections 2 to 7. If he says no, the release waits: your branch cannot
   reach `jusprin-dev` without them.
3. With his yes, push to `jusprin-dev` without a merge commit, then
   `git -C <main checkout> pull --ff-only`. If that pull is refused, stop and
   tell Kenneth without changing the main checkout.
4. With his yes, move `main` to that exact release commit, as a fast
   forward only.

## 9. Start the release workflow

The publisher is the repository variable `JUSPRIN_RELEASE_PUBLISHER`, or
`kennethjiang` when unset; `gh api user -q .login` must print it. This
computer's `gh` token cannot read repository variables or secrets (403), so
the workflow's first job is what reports a wrong publisher or a missing
Sentry secret (`JUSPRIN_SENTRY_DSN`, `SENTRY_AUTH_TOKEN`, `SENTRY_URL`,
`SENTRY_ORG`, `SENTRY_PROJECT`). Only Kenneth can add a secret.

With his yes:

```bash
gh workflow run jusprin_release.yml --ref main -f sign_macos=true
```

Keep Mac signing on: Homebrew refuses an unsigned Mac app. Check that the new
`workflow_dispatch` run is on the release commit. If its "Upload to the draft
release" job waits for an approval (the `release` environment can require
one), tell Kenneth. A failed run is reported with its log, never worked
around with a skip box.

The Mac jobs build on GitHub's Macs. If GitHub has no Mac machine for the
repo, they end "cancelled" 15 minutes after they were queued, with no runner
and no steps. GitHub can also cancel a Mac job that is already building, for
lack of capacity: its log then ends with only "The operation was canceled".
Either way, tell Kenneth. With his yes, start the run again on Kenneth's own
Mac (the self-hosted runner, user `jusprin-ci`):

```bash
gh workflow run jusprin_release.yml --ref main -f sign_macos=true -f self_hosted_mac=true
```

## 10. Test the draft

Test the draft's own files: the Mac disk image on this computer, the Windows
installer and the AppImage on their VMs. Install over the previous release,
check that Preferences shows the new version, have one chat and slice, and
check each fix in the notes on the platform where it mattered. Kenneth signs
in, or you use a test account he names; never type a password. A chat spends
that account's credits.

Never print the app's settings file (the `.conf` file in its data folder), on
the VMs or here: it holds saved API keys and sign-ins, and whatever a command
prints stays in the session's log. Read or change the one setting you need,
such as the language, with a command that prints only that setting.

Sentry must be able to read this release's stacks. Take the auth token from
"Sentry Access" in Kenneth's global instructions (`~/.claude/CLAUDE.md`) as
`SENTRY_AUTH_TOKEN`, with `SENTRY_URL=https://sentry.obico.io`,
`SENTRY_ORG=sentry` and `SENTRY_PROJECT=jusprin-app`. Set them in the shell
only, never in a file, and use the token only to read. From the release
worktree, on the Mac for the disk image and the unpacked Windows
portable zip (its `JusPrin/` folder), and on the Linux VM for the AppImage
(`--appimage-extract` only works on Linux; that VM needs a checkout of the
release commit and node):

- `src/slic3r/GUI/JusPrin/Packaging/sentry_debug_files.sh verify <file>` on
  the Mac app's `Contents/MacOS/OrcaSlicer`, Windows' `OrcaSlicer.dll` and
  the AppImage's `squashfs-root/bin/orca-slicer` must print "Sentry holds
  debug information".
- `src/slic3r/GUI/JusPrin/Packaging/sentry_page_maps.sh verify <folder>` on
  the Mac app's `Contents/Resources`, the Windows zip's `JusPrin/resources`
  and `squashfs-root/resources` must say Sentry holds both pages' source
  maps.
- The app cannot cause an error report on purpose, and Help › Report a
  problem opens a GitHub issue, not a Sentry report. If an unexpected error
  happens while testing, send it and check in Sentry, under release
  `JusPrin <version> (OrcaSlicer <SoftFever_VERSION from version.inc>)`, that
  the app's frames show function, file and line. Otherwise say it was not
  done.

`winget_updater.yml`, inherited from OrcaSlicer, would submit the installer
to winget as `SoftFever.OrcaSlicer` on publish. `gh workflow list --all` must
show it `disabled_manually`; if not, tell Kenneth before he publishes.

Tell Kenneth what was tested on which platform. He publishes.

## 11. After publishing

- Start the previous release and check that Preferences › Updates offers the
  new one. The update check reads only published releases.
- With Kenneth's yes, reply to and close the issues the release fixes, and
  open a Homebrew cask pull request if JusPrin's cask is in Homebrew
  (`brew info --cask jusprin`).
- In Sentry, a new report's or crash's stack must name the app's functions
  with file and line, and a page error's frames the pages' sources
  (`AgentUI/src/...`, `HomeUI/src/...`). Bare addresses or
  `app:///home/index.html` positions mean a platform's debug files or a map
  are missing: run the verify scripts on that platform's released files.
- The next day, run the `jusprin-production-check` skill in tsd-enterprise.
- Remove your worktree and branch.
