---
name: jusprin-release
description: Use when asked to make, prepare or start a new JusPrin release (a new version on GitHub), from choosing the commit range to the after-publish steps. Covers the version, release notes, server readiness, translations, the error-handling audit, tests, the guide check, moving main, the release workflow, testing the draft and what follows publishing. Not for JusPrin 1 (jusprin-v1) or OrcaSlicer's own releases.
---

# Making a JusPrin release

The release workflow, `.github/workflows/jusprin_release.yml`, builds the
release files from one commit and uploads them to a draft release on GitHub.
It refuses the commit without a new version, release notes, a guide check and
the Sentry secrets, and fails a platform whose debug files or page source
maps did not reach Sentry (see "Releasing" in `AGENTS.md`). It does not run the test suites, check the
server, check the translations or audit error handling. This skill covers
those too, in the order they have to happen.

## Kenneth decides these

Ask Kenneth, in plain words, and wait for his yes before each of these. A yes
covers that one action, not the next one.

- The version number, when he has not named it.
- The release notes' wording. Show him the file.
- Deploying the server.
- Which error-handling findings must be fixed before the release.
- Taking in commits that landed on `jusprin-dev` after the range was fixed.
- The Sentry values and read-only token for checking the draft.
- Spending API money on live AI checks. Say the cost.
- Pushing to `jusprin-dev`, and moving `main` forward.
- Starting the release workflow.
- Downloading the draft's files to this computer.
- Posting anything on GitHub, such as replies to issues.

**Never publish the draft.** Kenneth publishes it by hand on the Releases page.
Never force-push `main`.

## When something has to be fixed

A fix for anything a step finds is its own commit on `jusprin-dev`, never
part of the release commit. Make it in a worktree of its own, on a branch
from `origin/jusprin-dev`, not in your release worktree. Pushing it needs
Kenneth's yes. Once it is on `origin/jusprin-dev`:

- **Before section 8 landed the release commit:** commit your release edits
  on your branch, rebase onto `origin/jusprin-dev`, and write down that
  commit as the new end of the range. Then redo step 1.4 and sections 2 to 7
  for the whole range. A fix can need a line in the notes, a translation, or
  a change to a test or the guide.
- **After it landed (a problem found in section 9 or 10):** the version is
  not released yet, so keep it. The new end of the range is the fix's commit
  on `origin/jusprin-dev`. Redo step 1.4 and sections 2 to 7 on a branch
  from it, land what changed as in section 8, move `main` to the new release
  commit, and start the workflow again (section 9). A new run for the same
  version replaces the draft's files and text.

If a step fails and you cannot tell why, stop and tell Kenneth with the
output. Do not skip the step.

## Where each platform is tested

Every release is tested on all three platforms, twice: the test suites in
section 5, and the draft's own files in section 10. A platform that was not
tested blocks the release; it is not a line in the report.

- **macOS:** this computer.
- **Windows and Linux:** two test VMs in Google Cloud. How to reach each one,
  start it, copy code to it, build and run the app there is in
  `<main checkout>/.claude/vm-harness/windows/README.md` and
  `<main checkout>/.claude/vm-harness/linux/README.md`. That folder lives
  only on this computer, not in the repository. Use the Google Cloud account
  your instructions name for `gcloud`. If a VM is stopped, start it, and
  stop it again when you are done with it.

If a VM cannot be reached or that folder is missing, stop and tell Kenneth.

## 1. Set up

1. Check that the main checkout (the folder that holds `jusprin-dev`, not a
   worktree) has nothing that would be left out of the release:

   ```bash
   git -C <main checkout> fetch origin
   git -C <main checkout> status --porcelain -uno
   git -C <main checkout> log --oneline origin/jusprin-dev..jusprin-dev
   ```

   Both must print nothing. A changed file or a commit that is not on GitHub
   is work someone may expect in the release. Stop, list each one for
   Kenneth, and wait until he has pushed it or said to leave it out. Do not
   push, commit or discard it yourself. Untracked files do not block: they
   are working docs Kenneth keeps there on purpose.
2. Find the last release: `git tag --list 'jusprin/v*' --sort=-v:refname | head -1`.
   Write down the commit `origin/jusprin-dev` points to now
   (`git rev-parse origin/jusprin-dev`). The range is the last release's tag
   to that commit, and every later step checks that range. Section 8 checks
   whether `jusprin-dev` moved since.
3. Settle the version now: Kenneth's, or ask him when he has not named one.
   Make your own worktree at that commit, under `<repo>/.claude/worktrees/`,
   on a branch named `claude/release-<version>`. Never name it
   `jusprin-release/...`: pushing a branch with that name starts an unsigned
   release build. If that branch name is taken, add a suffix.
4. Read every commit in the range: `git log --format='%h %s%n%b' <range>`.
   Note what a person would notice, and the GitHub issues each change fixes.

## 2. Version and release notes

1. Set `kVersion` in `src/slic3r/GUI/JusPrin/Updates/Version.hpp`. It must be
   above every released JusPrin version.
2. Write `src/slic3r/GUI/JusPrin/Packaging/release-notes/<version>.md`. The
   draft's text is this file, and each run of the release workflow writes it
   again. Once the release is published, an older app that offers the update
   shows that text under "Release notes" in Preferences › Updates. Follow the
   shape of the previous release's file. Write for a person who prints, not for
   a developer. Check every on-screen label you quote against the source
   (`git grep`), and leave out anything you have not checked.
3. A change that sends anything new from the person's computer, such as a new
   message to our server, goes in the notes and says what it holds.

## 3. Server readiness

The app calls JusPrin's server at `https://jusprinv2.obico.io`. Every address
the release calls must already be deployed there.

Many addresses are joined from pieces, so no single search finds them all.
List them from the code that builds them:

```bash
git grep -n -E '"/(ent|o|accounts|jusprin)/|"/models"|ask_server\("' -- src/slic3r/GUI/JusPrin ':!*test*'
```

Read each hit and write out the full path. For example, `ask_server("code/confirm", …)`
in `Agent/CloudAccount.cpp` is `/ent/jusprin/v2/auth/code/confirm`, and the
model list is the gateway's address with its last part replaced by `/models`.
Leave off any query string. Check full paths only, never a bare prefix such
as `/ent/jusprin/v2/auth/`.

GET each one: `curl -s -o /dev/null -w '%{http_code}\n' https://jusprinv2.obico.io<path>`.
First GET a path that cannot exist, such as `/ent/jusprin/v2/no-such-address`.
It must answer 404. If it answers anything else, the check cannot tell
addresses apart: in maintenance mode the server answers 200 with the same
page for every path. Stop and tell Kenneth.

Then, for each address, an answer of 200, 3xx, 400, 401, 403, 405 or 426
means it is there. A 5xx means the server is failing: stop and tell Kenneth.
A 404 means it is not deployed. For a 404, find the server change in the tsd-enterprise
repository (`web_ent/jusprin/v2/urls.py` on `master`), tell Kenneth what is
missing, and wait until it is deployed. Also tell him about any other server
behavior a change in the range relies on, such as a new error code.

## 4. Error-handling audit

Run the `error-handling` skill on the range. When the release relies on
server changes, also audit the tsd-enterprise commits under `web_ent/jusprin/`
on `master` since the previous release tag's date (that repository has no
release tags).

Report the findings to Kenneth and wait. He says which ones must be fixed
before the release. Fixes follow "When something has to be fixed" above.

## 5. Tests

Build in your worktree, in Release, following "Building on a shared machine"
in `AGENTS.md`: check the load first, use `-j4`, choose every target before
you start. A new worktree has no dependencies of its own, so configure it
against the main checkout's. Use CMake.app's cmake (Homebrew's fails to find
Eigen3), absolute paths (the shell's folder can change between commands), and
put node on `PATH` for the pages:

```bash
export PATH=/Applications/CMake.app/Contents/bin:<node bin folder>:$PATH
cmake -S <worktree> -B <worktree>/build/arm64 -G "Ninja Multi-Config" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_PREFIX_PATH=<main checkout>/deps/build/arm64/OrcaSlicer_dep/usr/local \
  -DSLIC3R_GUI=ON -DSLIC3R_STATIC=ON -DSLIC3R_PCH=ON -DBUILD_TESTS=ON \
  -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib
cmake --build <worktree>/build/arm64 --config Release \
  --target tests/all jusprin_web jusprin_catalogs -- -j4
ctest --test-dir <worktree>/build/arm64/tests -C Release --output-on-failure -LE RequiresApp
```

`tests/all` builds every test program: OrcaSlicer's suites, JusPrin's
(`agent_bridge_tests`, `jusprin_tests`, `jusprin_update_tests`,
`jusprin_profile_tests`, `shell_theme_tests`, `jusprin_fake_bambu_tests` and
the rest), `tool_call_check` and `shell_integration_harness`, which the guide
check uses. `ctest` runs every suite registered with it. `agent_live_tests`
is built but never registered, because each run costs API money. Also run
`npm test` in `src/slic3r/GUI/JusPrin/AgentUI` and in
`src/slic3r/GUI/JusPrin/HomeUI`.

- Windows and Linux: the release workflow neither builds nor runs the tests,
  so nothing else shows they even compile there. Build and run them on each:
  `build_win.bat -s --run-tests` on Windows, and on Linux `./build_linux.sh -st`
  then `ctest --test-dir build/tests -C Release --output-on-failure` (keep
  the `RequiresApp` tests there: they run only on Linux). Run them on the
  Windows and Linux VMs ("Where each platform is tested"), from a checkout
  of the end of the range.
- When the range changes text the model reads (prompts, skills, tool
  descriptions, tool results, how history is sent), run the live prompt suite
  (`tests/project_prompt/README.md`, and `tests/printer_prompt` when printer
  chats changed). Its `claude-cli` backend costs no API money. The README
  also asks for a small paid run on the app's default model before a release.
  Propose it to Kenneth with its cost, and run it only after his yes.

Report each failure with its output. To tell whether the range caused it,
run the same test on a build of the last release's tag. A failure there too
is older than this release: report it as such and let Kenneth decide. A
failure the range caused is a bug: fix it as "When something has to be
fixed" says, never skip the step.

## 6. Translations

These come after the audit and the tests, since a fix from either can add a
sentence.

1. Run `python3 src/slic3r/GUI/JusPrin/Localization/update_catalogs.py`. It
   needs GNU gettext and `npm ci` in `src/slic3r/GUI/JusPrin/AgentUI`. If it
   changes any catalog, those changes go into the release commit.
2. Translate every new entry in every language, as
   `src/slic3r/GUI/JusPrin/Localization/README.md` says. Those catalogs are
   JusPrin's own, in `Localization/native/` and `Localization/pages/`. Never
   edit upstream's `localization/i18n/` and never run `run_gettext`. From
   "Localization & translations" in `AGENTS.md` and the `orca-translations`
   skill, take only the editing rules and the glossary.
3. `npx vitest run src/catalogs.test.ts` in `AgentUI` must pass: it fails
   while an entry is untranslated or a translation lost a placeholder.
4. Rebuild `jusprin_web` and `jusprin_catalogs` so the guide check in step 7
   runs on the new catalogs.
5. Count the entries marked `# Needs human review` in
   `Localization/native/*.po` and `Localization/pages/*.po`. Tell Kenneth the
   count, so he can decide whether someone should read a language.

These steps cannot find a sentence nobody marked for translation. While
reading the range, look for text a person reads that is not marked.

## 7. Guide check

Follow "Before a release" in `docs/guide/README.md`, on the build from step 5
with the new version already in `Version.hpp`.

1. Tell Kenneth first: the command drives the app on this computer's screen
   for about a minute a page, so nobody should use the computer meanwhile.
2. Look through `git log <range> -- src/slic3r/GUI/JusPrin` for new screens or
   menu items no page mentions, and add them to the guide. Do this first: the
   check below cannot find them, and a page changed after it passes was not
   checked.
3. Run your worktree's copy, `<worktree>/scripts/check_guide.py --ask-ai`,
   with node on `PATH`. The script checks the tree it sits in, so the main
   checkout's copy would check the wrong code. Fix what it reports until it
   passes. It then writes the version to `docs/guide/checked-version.txt`. If
   you change any page afterwards, run it again.

## 8. Commit and land

1. Commit on your branch: `Version.hpp`, the release notes, the catalogs in
   `Localization/native/` and `Localization/pages/`, everything the guide
   check changed in `docs/guide/` (pages, `pictures/`, `checked-version.txt`)
   and the guide skills it rebuilt in `AgentUI/src/skills/guide-*/`. Make
   sure no temporary working docs, logs or screenshots outside
   `docs/guide/pictures/` are in it.
2. Run `git fetch origin` and compare `origin/jusprin-dev` with the latest
   end of the range you wrote down. If it moved, those new commits have had none of the
   checks above. List them for Kenneth and wait for his answer. With his yes,
   rebase onto `origin/jusprin-dev` and redo step 1.4 and sections 2 to 7 for
   the whole range. If he says no, stop: the release waits until he decides, because
   your branch cannot reach `jusprin-dev` without those commits.
3. With Kenneth's yes, land it on `jusprin-dev` without a merge commit:
   `git push origin HEAD:jusprin-dev`. Then bring the main checkout along:
   `git -C <main checkout> pull --ff-only`. If that pull is refused, someone
   committed in the main checkout meanwhile: stop and tell Kenneth, and do not
   change the main checkout. Write down your commit
   (`git rev-parse HEAD`). This is the release commit.
4. With Kenneth's yes, move `main` to that exact commit:
   `git push origin <release commit>:main`. This only succeeds as a fast
   forward. If it is refused, stop and tell Kenneth. Never force it.

## 9. Start the release workflow

Only the release publisher may run it: the repository variable
`JUSPRIN_RELEASE_PUBLISHER`, or `kennethjiang` when that is unset. The `gh`
token on this computer may not be allowed to read the variable (`gh variable
list` can answer 403). Check that `gh api user -q .login` prints
`kennethjiang`. If the run's first job fails with "Only @<name> may make a
JusPrin release", tell Kenneth.

The workflow refuses to start without five Sentry secrets:
`JUSPRIN_SENTRY_DSN` (the project the app sends reports to), and
`SENTRY_AUTH_TOKEN`, `SENTRY_URL`, `SENTRY_ORG` and `SENTRY_PROJECT` (which
upload the debug files and page source maps that let Sentry name the
functions in a stack, with file and line). The `gh` token on this computer
cannot list secrets (`gh secret list` answers 403), so do not try. If the
run's first job fails on a missing secret, tell Kenneth which one. Only he
can add it.

With his yes:

```bash
gh workflow run jusprin_release.yml --ref main -f sign_macos=true
gh run list --workflow jusprin_release.yml --limit 3 --json databaseId,headSha,createdAt,event
```

Run the second command about 15 seconds after the first: a new run takes a
few seconds to appear. Take the `workflow_dispatch` run
created after you started it, and check that its `headSha` is the release
commit. Keep Mac signing on:
Homebrew refuses a Mac app that is not signed and notarised. Wait for the run
in the background (`gh run watch <databaseId>`). If its "Upload to the draft
release" job waits for an approval (the `release` environment can require
one), tell Kenneth. A failed run is a failure to report with its log, not a
reason to tick a skip box.

## 10. Test the draft

The draft holds the Windows installer and portable zip, the Mac disk image,
the Linux AppImage and `SHA256SUMS.txt`. These files are what people will get,
so test them, not a local build. Ask Kenneth before downloading them.

On each platform: the Mac disk image on this computer, and the Windows
installer and the AppImage on their VMs. Install over the previous release
and start the app. Check
that Preferences shows the new version. Kenneth signs in, or you use a test
account he names: never type a password yourself. Have one chat (it spends
that account's credits), slice, and
check each fix in the notes on the platform where it mattered (for example a
locale bug on Windows, where it always shows).

Check that Sentry can read this release's stacks, on the draft's own files.
Run both scripts from your release worktree on the Mac, for the Mac disk
image and for the Windows portable zip unpacked there (its `JusPrin/` folder
holds `OrcaSlicer.dll` and `resources`). Run the AppImage's part on the Linux
VM, in a checkout of the release commit with node and npx:
`--appimage-extract` only works on Linux.
Both scripts read `SENTRY_URL`, `SENTRY_ORG`, `SENTRY_PROJECT` and
`SENTRY_AUTH_TOKEN` from the environment. GitHub secrets cannot be read back,
so ask Kenneth for the values and for a token that can only read the
project. Set them in the shell for these commands only, and never write
them into a file.

- Run `src/slic3r/GUI/JusPrin/Packaging/sentry_debug_files.sh verify <file>`
  on the Mac app's `Contents/MacOS/OrcaSlicer` (it checks both
  architectures), Windows' `OrcaSlicer.dll` and the AppImage's
  `bin/orca-slicer` (`--appimage-extract` puts it under `squashfs-root`).
  Each must print "Sentry holds debug information".
- Run `src/slic3r/GUI/JusPrin/Packaging/sentry_page_maps.sh verify <folder>`
  from the release commit, with the folder that holds `jusprin/agent` and
  `jusprin/home` in each platform's app: the Mac app's `Contents/Resources`,
  the unpacked Windows zip's `JusPrin/resources`, the AppImage's `squashfs-root/resources`.
  Each must print "Sentry holds the agent page's source map" and the same
  for home.
- If an unexpected error happens while you test, send its report and open
  it in Sentry (the project `SENTRY_PROJECT` names, filtered by
  `release:"JusPrin <version> (OrcaSlicer <SoftFever_VERSION>)"`, the name
  `Updates::version_label()` gives, with `SoftFever_VERSION` from
  `version.inc`). The frames of the app's own code must show function names
  with file and line, not only addresses. The app has no way to cause one on
  purpose, and Help › Report a problem opens a GitHub issue, not a Sentry
  report. If none happens, say this check was not done; the two scripts
  above cover whether Sentry can read the stacks.

Before Kenneth publishes, check `gh workflow list --all | grep -i winget`.
`winget_updater.yml` is inherited from OrcaSlicer. It runs when a release is
published and submits the installer to winget as `SoftFever.OrcaSlicer`. It
must say `disabled_manually`. If it is enabled, tell Kenneth before he
publishes.

Tell Kenneth what was tested on which platform and what was not. He publishes.

## 11. After publishing

- Start the previous release on one computer and check that Preferences ›
  Updates offers the new version. The update check reads only published
  releases, so this is the first moment it can be checked.
- Reply to and close the GitHub issues the release fixes, with Kenneth's yes.
- Homebrew: if JusPrin's cask is in Homebrew/homebrew-cask
  (`brew info --cask jusprin` finds it), it needs a pull request there for
  the new version. Opening it is posting on GitHub, so it needs Kenneth's yes.
- Open the newest events in Sentry for the new release and check that a
  report's or crash's stack names the app's functions with file and line,
  and that a page error's frames name the page's source files
  (`HomeUI/src/...`, `AgentUI/src/...`). A stack of bare addresses or of
  `app:///home/index.html` positions means a platform's debug files or a
  page's map are missing: run `sentry_debug_files.sh verify` or
  `sentry_page_maps.sh verify` on that platform's released files.
- The next day, run the `jusprin-production-check` skill in the tsd-enterprise
  repository for new failures from the new version.
- Remove your worktree and branch.
