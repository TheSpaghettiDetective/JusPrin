# CLAUDE.md

JusPrin — a long-lived fork of OrcaSlicer (open-source C++17 3D slicer, wxWidgets GUI, CMake build system).

## Upstream vs. fork attribution

This checkout holds two codebases: upstream OrcaSlicer (the `upstream` remote, SoftFever/OrcaSlicer) and the JusPrin fork's changes on top of it. Fork changes include everything under `src/slic3r/GUI/JusPrin/` and the seams the fork added inside OrcaSlicer-owned files.

- Say "OrcaSlicer does X" only for behavior upstream ships. Behavior added by a fork commit is "JusPrin does X", even when it lives in an OrcaSlicer-owned file.
- Check before attributing; the file path is not enough. Run `git blame` on the line, then `git branch -r --contains <sha> 'upstream/*'`. No upstream branch containing the commit means the fork owns it. Run `git fetch upstream` first if the remote may be stale.
- If you have not checked, say it is unverified whether the behavior is upstream or fork.

## Branches

- `jusprin-dev` is the development branch. Start each change on a new branch from it and land the change there. It began at `975c703f08`, the merge of OrcaSlicer 2.5.0, and contains the earlier development branches `jusprin-newui` and `ui-walkthrough`; start nothing new on those.
- `main` is the stable branch and GitHub's default branch: the repository's front page, its issue forms, and the workflows GitHub runs on a schedule come from it.
  - Never force-push `main`, so that people who cloned the repository never have to force-pull. Only in an extreme case with no other way out, and with Kenneth's yes, may an admin lift the GitHub protection that blocks force pushes and deletion.
  - For now `main` is also the release branch: it moves forward from `jusprin-dev` when a version is released, and releases are built from it. This may change if a separate release branch is added later.
- `jusprin-v1` holds JusPrin 1, for fixes to that version only.
- `jusprin-v2-poc` holds the experiments described below.
- Take each OrcaSlicer release into `jusprin-dev` as `agent-docs/jusprin/fork-stewardship.md` rule 10 describes.

## Error handling discipline

- Do not catch an error merely because it can be caught. Catch only when the code can recover, translate the error at an abstraction boundary, or add context that is essential for diagnosis. Logging alone is not recovery.
- Represent expected states with normal control flow, not exceptions.
- Never turn an invariant violation or an "impossible" state into success or a silent no-op. Fix the invalid transition at the layer that owns the decision and let unexpected failures propagate to Sentry.
- Avoid duplicating defensive checks across layers. Keep lower-level operations strict, with their preconditions enforced by the caller that owns the workflow.
- If concurrency makes an invalid state reachable, fix the ordering or synchronization instead of suppressing the resulting error.
- Suppress an error only when it is genuinely expected and safely recoverable; document the reason and cover it with a test. Never broadly swallow unexpected errors.
- Prefer removing the root cause over reducing the visibility of its symptoms. Tests should encode valid transitions and assert that invalid ones remain visible.

## Build Commands

Build the Release configuration unless asked otherwise.

```bash
# macOS
cmake --build build/arm64 --config Release --target all --

# Linux
cmake --build build --config Release --target all --

# Windows
cmake --build . --config Release --target ALL_BUILD -- -m
```

Node/npm is required to build the Agent and Home pages, or configure with `-DJUSPRIN_BUILD_WEB=OFF` and supply the bundles.

### Building on a shared machine

Several agent sessions build on the same Mac at once. Without a limit, ninja runs one compile job per core plus two, so every build you start can take the whole machine. In 2026-10 one session stacked four builds in one build directory: 56 clang processes on 12 cores. It kept restarting its build to change targets and never limited the job count.

- Before starting a build, check `uptime` and `pgrep -fl ninja`. If the load is already above the core count, wait or build less.
- Limit jobs: `cmake --build … -- -j4`.
- Decide the full target list before starting. Never restart a running build to add or change targets: let it finish, then build the rest.
- Run one build per build directory. Two ninja processes in one directory compete and can leave its records inconsistent.
- Stopping a build means stopping ninja. `pkill -f "cmake --build …"` kills only the cmake wrapper, and ninja keeps compiling. Start long builds in their own process group (`perl -e 'setpgrp(0,0); exec @ARGV' cmake --build …`) and stop them with `kill -- -<pgid>`. Then confirm with `pgrep -fl ninja` before building again.
- When a build is slow, count your own compile processes before blaming the load on other sessions.
- Reuse an existing build where the task allows. Ask before configuring a fresh full-app build the task does not require.

## Testing

Catch2 framework. Tests in `tests/`; see [tests/AGENTS.md](tests/AGENTS.md) for where a new test belongs and the conventions to follow.

```bash
cd build && ctest -C Release --output-on-failure    # all tests
ctest --test-dir ./tests/libslic3r -C Release       # individual suite
ctest --test-dir ./tests/fff_print -C Release
```

Printer UI without hardware: `src/slic3r/GUI/JusPrin/Testing/README.md` (Bambu in-process fake); Moonraker and OctoPrint: `tests/printer_hosts/README.md` (Docker). Isolated `--datadir`; do not drive another JusPrin instance.

## Documentation

- Docs live in `docs/`; the high-level design of a subsystem goes in `docs/HLSD/<subsystem>.md`.
- Describe the design as it stands — what the subsystem does, why it exists, and the constraints that shape it. Not the route that got there: no phases, task lists, status markers, or "before/after this PR" framing.
- Planning and investigation output (brainstorms, superpowers design and plan docs) stays in `docs/superpowers/`, which is gitignored. Never commit it.
- Write a doc only when the design is not evident from the code, and when a change invalidates an existing one, update it in the same PR.

## Code Style

- C++17, selective C++20. PascalCase classes, snake_case functions/variables
- `#pragma once` for headers. Smart pointers and RAII preferred
- Include what you use: include the header for every symbol a file uses, and keep headers compilable on their own. Never rely on the precompiled header or a transitive include. The `clang-tidy` CI job enforces this on changed lines; run the same check locally with `scripts/run_clang_tidy.sh` (`scripts\run_clang_tidy.ps1` on Windows), which sets up everything it needs
- Parallelization via TBB — be mindful of shared state
- Wording: a file or object is "opened", "imported", "loaded" or "added", never "arrived". "Arrive" is fine for messages, responses, events and lists. `AgentUI/src/wording.test.ts` guards the text the model reads.
- Wording the model reads calls the product "the app", never Orca or OrcaSlicer: the model speaks as the app, and a second name reads as a second program. `AgentUI/src/wording.test.ts` guards this too. Log lines may name Orca. A throw may only if it never reaches the model: tool results forward save and open errors as they are, and the guard does not see those strings.
- Always use `SetSizerAndFit(sizer)` instead of `SetSizer(sizer)` on top level window. Unless `SetSizer` must be called before the full layout is built, call `sizer->SetSizeHints(window)` afterwards in this case.

## Key Entry Points

- App startup: `src/OrcaSlicer.cpp`
- Slicing pipeline: `src/libslic3r/Print.cpp`
- All print/printer/material settings: `src/libslic3r/PrintConfig.cpp`
- GUI: `src/slic3r/GUI/`
- Core algorithms: `src/libslic3r/` (GCode/, Fill/, Support/, Geometry/, Format/, Arachne/)
- Printer profiles: `resources/profiles/[manufacturer].json`

## JusPrin Production UI

- Start with `agent-docs/jusprin/README.md` for the canonical product definition, production architecture, verification method, Orca integration guidance, and design system.
- Before editing any file outside `src/slic3r/GUI/JusPrin/`, read `agent-docs/jusprin/fork-stewardship.md`. Every line changed in an OrcaSlicer-owned file is a line this fork re-resolves at every rebase, so those changes must be small, product-neutral, additive where possible, and accompanied by the rebase evidence that document requires.
- Experimental plans, probes, screenshots, logs, and temporary UI code remain on `jusprin-v2-poc`. Use `agent-docs/jusprin/poc-reference.md` for exact commit and path pointers; do not cherry-pick the POC wholesale.
- Curated brand and semantic UI assets live under `resources/jusprin/`.
- Before writing or changing any UI code, native or Agent page, follow "Writing UI code" in `agent-docs/jusprin/design-system.md`: every color, radius, font, and control size comes from the token file through `ShellTheme`, the shell recipes, or the CSS variables. A value with no token gets a token and a guard first, never a literal.
- Before designing a screen or writing any on-screen text, follow `agent-docs/jusprin/design-guidelines.md`: whole-screen boards with real strings, the wording rule, and a cold-reader check on screenshots before and after building.
- Temporary working docs (handoffs, findings, plans written for another agent) stay untracked: never commit them, and never add them to `.gitignore` or `.git/info/exclude`. Untracked files in `git status` are how Kenneth tracks work that isn't finished.

## Critical Constraints

- **Backward compatibility required** for .3mf project files and printer profiles
- **Cross-platform** — all changes must work on Windows, macOS, and Linux
- Profile/format changes need version migration handling
- Dependencies built separately in `deps/build/`, then linked to main app

## Pull requests

Before opening a pull request or posting on GitHub, read [CONTRIBUTING.md](CONTRIBUTING.md), including its "If you are an AI agent" section.

## Code review focus areas

- Changes must not cause regressions in existing functionality, defaults, profiles, or project compatibility.
- Features gated by options must not affect existing behavior when those options are disabled.
- Changes should follow the existing code style and architecture. Architectural changes should be justified in code comments and the PR description.
- Add helper functions or utilities only when existing code cannot reasonably be reused. Avoid duplication.
- Changes to OrcaSlicer-owned files must be additive where possible. Rewriting an upstream function, or reimplementing upstream behavior beside it, needs explicit justification in the PR description.
- When an upstream line is deleted or relocated, say where it went and what a future rebase must carry into the new home.
- Keep code concise and clear. Manually simplify AI generated bloated codes before review.
- Include targeted tests or documented verification for behavior changes, especially in slicing logic, profiles, formats, and GUI defaults.
- For profile changes (`resources/profiles/<Vendor>/**`), check that `version` in the sibling `resources/profiles/<Vendor>.json` was bumped.
- For new or changed UI strings, check they follow [Writing translatable strings in the source](#writing-translatable-strings-in-the-source): no concatenated fragments, plural forms for counts, contexts for ambiguous words.
- For translation changes (`localization/i18n/**/*.po`), check that recurring terms match the [Localization glossary](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/developer_reference/localization_glossary.md) for that language.

## Localization & translations

Catalogs live in `localization/i18n/<lang>/OrcaSlicer_<lang>.po`; the template is `OrcaSlicer.pot`.
See the [Localization guide](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/developer_reference/localization_guide.md) for the human-facing version of these principles.

### Writing translatable strings in the source

Translators see one msgid at a time and cannot change the code around it, so each message must be complete in a single msgid. Use the wrapper that fits instead of assembling text from pieces:

- **One msgid per sentence, with placeholders.** Never build a message by concatenating translated fragments (`_L("A") + name + _L(" is B")`) or by splicing a translated word such as `_L("left")` into another sentence: the translator can't reorder or inflect the parts. Write `format(_L("%1% is too close to others"), name)`.
- **A count needs a plural form.** Don't write `file(s)` or leave a verb unchanged after a number. Use `_L_PLURAL("%1% object", "%1% objects", n)`. libslic3r has no plural lookup, so word the message so it doesn't depend on a count, or build it in the GUI.
- **An ambiguous word needs a context.** When a short string means different things in different places (verb or noun, a mode name, a CAD term), give each sense its own context with `_L_CONTEXT`/`_u8L_CONTEXT`/`L_CONTEXT`. Keep the most common sense without a context so its existing translations stay valid.
- **Mark every user-visible string** (`_L`/`_u8L`, or `L` where it is translated later) and list new source files in `localization/i18n/list.txt`: files that aren't listed are never extracted.
- Add a `// TRN` comment above a new string whose meaning or placeholders aren't obvious.
- Never choose behavior by comparing translated text; compare ids or flags.

See `.claude/skills/orca-translations/references/source-audit.md` for the full checklist.

### Terminology

- Use the [Localization glossary](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/developer_reference/localization_glossary.md) as the source of truth for recurring terms, so the same English term is always rendered the same way within a language, and terms that must stay in English (brand/product names, acronyms, materials, file formats, G-code tokens, macros/variables/identifiers) are not translated.
- If a term's established translation changes, update both the affected `.po` files and the glossary (`developer_reference/glossary/<lang>.tsv` in the wiki, then regenerate) so they stay in sync.
- Translate the *meaning*, not the words. Check what the string actually controls before translating it — English reuses one word for different things. `Flow ratio` (multiplier), `Flow Rate` (throughput) and `Flow Dynamics` (pressure compensation) are three different terms; `extruder` may mean the toolhead, the feeder motor, or the nozzle depending on the string.
- Reuse one template per recurring message shape (`Failed to connect to …`, `Are you sure you want to …?`), even where the English wording varies.

### Editing rules

- Only edit `msgstr` — **never** change `msgid`, and never "fix" wrong English in the translation alone. Report the source string instead.
- Preserve exactly: placeholders (`%s`, `%d`, `%1%`, `%zu`, `%%`), every `\n` (count *and* position, including leading/trailing), leading/trailing spaces, HTML tags, `℃`, and the file's encoding and line endings.
- **Never reorder positional arguments** in a `c-format` string. If the msgid is `%d` then `%s`, that order must hold — swapping them breaks at runtime.
- `msgctxt` separates homonyms — always read it. `Back`/`Camera View` is the rear view of the 3D navigator, while `Back`/`Navigation` is the go-back button; `Top` exists in the *Alignment*, *Layers* and *Camera View* senses.
- When a string needs disambiguating, add context in the source (`_L_CONTEXT`/`_u8L_CONTEXT`), don't work around it in the translation.
- A literal `%` inside a string xgettext flagged `possible-c-format` will fail `msgfmt`. Fix it with a `// xgettext:no-c-format, no-boost-format` comment above the string in the source — do not mangle the translation or use `%%` in text that is never passed through printf.
- Plural entries: read `nplurals` from the catalog's `Plural-Forms` header (it is **not** always 2 — ja/ko/zh/th/vi use 1, ru/cs/pl/lt use 3, uk uses 4). Each form must be genuinely inflected for its quantity; repeating one sentence across all forms is a bug in Slavic/Baltic languages, though it is correct for Turkish and Hungarian.
- An entry whose `msgstr` equals its `msgid` is untranslated even though it is not empty; a plural entry with any empty form is likewise incomplete.
- Mark machine-produced translations with an `# AI Translated` translator comment. Don't add it to a human translation you didn't actually rewrite.
- When you can't be sure of a machine translation's meaning or UI wording, also add `# Needs human review: <what to check>`. Never use `fuzzy` for this — fuzzy entries are hidden from users.
- Don't reflow or re-wrap unrelated entries — keep the diff limited to the strings you changed.

### Verifying

- `scripts/run_gettext.bat --full` (Windows) regenerates the template, merges every catalog and compiles the `.mo` files. It must exit 0.
- Or check a single catalog with `msgfmt --check-format --check-header -o <out>.mo localization/i18n/<lang>/OrcaSlicer_<lang>.po`. Without `--check-header`, msgfmt skips the placeholder check in plural forms.
- Fuzzy entries are not shown to users. If you correct one, clear its `fuzzy` flag, otherwise the fix never ships.
