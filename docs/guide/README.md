# JusPrin guide

How to use JusPrin, one page per part of the app. The same pages are in the app, under Help › JusPrin guide…, in the order of the list below.

- [Projects and files](projects-and-files.md)
- [Plates and objects](plates-and-objects.md)
- [Printers and filament](printers-and-filament.md)
- [Seeing and changing print settings](print-settings.md)
- [Slicing, checking and printing](slicing-and-printing.md)
- [AI chats, account and credits](ai-chats-and-account.md)
- [The app itself](the-app.md)

## Before a release

The guide is brought in line with the app before each release, on a computer with the app built.

1. Build the app's test harness and pages from the commit to release: the `shell_integration_harness` and `jusprin_web` targets.
2. Leave the computer alone and run `scripts/check_guide.py --ask-ai`. It takes about a minute a page, plus the questions.
3. Fix what it reports, then run it again until it passes:
   - **A label is on no screen.** The app's wording changed. Find the new wording in the app and correct the page.
   - **A run failed.** Open its log, which the command names. Either the screen no longer works as the page says, and the page needs correcting, or the harness expects something the app no longer does, and the harness needs correcting.
   - **A picture is blank.** The program that started the command may not record the screen. Grant it Screen Recording in the system's privacy settings and run again.
   - **AI answered a question wrongly.** Read its reply in the output. Usually the page is unclear or its first line does not say when to read it.
4. Look through what changed on screen since the last release (`git log <last release tag>..HEAD -- src/slic3r/GUI/JusPrin`). The command finds what a page gets wrong. It cannot find a new screen or menu item that no page mentions, so add those by hand.
5. Commit the pages together with their pictures, the skills the command rebuilt, and `checked-version.txt`. When every step passes for every page, the command writes the app's version to that file. A run with `--skip-app`, with `--area`, or without `--ask-ai` leaves it as it was. The release workflow refuses a version this file does not name. The file says the check passed for that version, not on which commit, so run it on the commit you mean to release.

## What the command does

1. **Walks the app and takes the pictures.** `tests/shell/guide_checks.json` names, for each page, the runs of the app's test harness that go through the screens the page describes, and the checks in each run the page depends on. `tests/shell/run_guide_checks.py` runs them in the app as built and copies the pictures a page uses into `pictures/<page>/`.
2. **Rebuilds the copy AI reads.** `src/slic3r/GUI/JusPrin/AgentUI/src/guide.ts` builds a skill from each page.
3. **Checks each page**, in `guide.test.ts` beside it: every label in bold is in the app's source, every section has a run in step 1, every picture is one step 1 took, and the page has questions for step 4. `components/Guide.test.tsx` checks that the app's guide window lists every page and shows each with all its pictures and without the parts written for AI.
4. **Asks AI the page's questions** (`--ask-ai`). `tests/project_prompt/guide_questions.json` holds them with what a right answer says. `tests/project_prompt/run_guide_tests.py` asks the model and has a judge read the replies.

## Writing a page

- Start it with `<!-- ai-description: … -->`: one line, at most 300 bytes, saying when AI should read it. The model reads every page's description together in one list, and the Claude command-line tool that `--ask-ai` runs through cuts a tool description longer than about 2,000 characters, so keep each one short: about 200 bytes. Write `...` rather than `…` in it, since that tool rewrites the one as the other.
- Put words that are only for AI in `<!-- ai: … -->`. They are hidden on this site.
- Use bold only for words that are on screen, exactly as they are on screen.
- A picture goes on a line of its own, as `![what it shows](pictures/<page>/<name>.png)`, and comes from a run named in `guide_checks.json`. AI's copy leaves it out.
- A picture is kept at twice the screen's size, and the guide window shows it at half, the size it has on screen. Mark a run's picture that the harness draws at the screen's own size with `"atScreenSize": true`, and the command doubles it.
- List a new page at the top of this file, or the guide window leaves it out.
- Give each `##` section a run in `guide_checks.json`.
- Add at least three questions for the page to `guide_questions.json`.
- AI's copy may hold at most 16 KiB. Split a page that grows past that.
