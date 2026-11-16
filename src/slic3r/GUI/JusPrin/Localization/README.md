# JusPrin's translations

JusPrin shows its interface in the languages OrcaSlicer ships a catalog for
(`languages.json`). OrcaSlicer's own windows translate through upstream's
catalogs in `localization/i18n/`, which the fork never edits. JusPrin's own
text has catalogs of its own here, so taking a new OrcaSlicer release never
conflicts in a translation file:

| Text | Marked with | Catalog | How it reaches the app |
| --- | --- | --- | --- |
| Native windows and text the native side sends to a page | `_L`, `_u8L`, `L`, `_L_CONTEXT`, `_L_PLURAL`, `translated_plural` | `native/<lang>.po`, template `native/JusPrin.pot` | `Brand/brand_catalogs.py` merges it into `resources/i18n/<lang>/<app key>.mo`, the catalog the app loads |
| The AI and Home pages | Lingui macros: `t`, `plural` from `@lingui/core/macro`; `Trans`, `Plural` from `@lingui/react/macro` | `pages/<lang>.po`, one for both pages | `@lingui/vite-plugin` compiles every catalog into each page; the page reads `?lang=` (`AgentUI/src/i18n.tsx`) |

Native text that OrcaSlicer's template already has is left out of
`native/JusPrin.pot`; upstream's translation is used.

## Updating the catalogs

After marking or changing text:

```bash
python3 src/slic3r/GUI/JusPrin/Localization/update_catalogs.py
```

It runs `xgettext` over the native sources and Lingui's extractor over the
pages, then merges the new entries into every language. Translate each new
entry in every `<lang>.po`, following the editing rules in the repository's
`AGENTS.md` (Localization & translations): keep every placeholder and line
break, give each plural form its own inflection, and mark machine
translations `# AI Translated`, adding `# Needs human review: <what>` where
the meaning or the wording is uncertain. `AgentUI/src/catalogs.test.ts` fails
while any entry is untranslated or a translation loses a placeholder.

The page catalogs use ICU placeholders: `{0}`, `{name}`, numbered tags like
`<0>…</0>` for elements inside a `Trans`, and
`{count, plural, one {# printer} other {# printers}}` for counts. Keep the
placeholder and tag names; translate the words, including the words inside a
plural's branches, and give the plural the categories the language uses
(`one`, `few`, `many`, `other`).

## Marking text

Mark every string a person reads: labels, buttons, headings, menu items,
tooltips, `aria-label`s, placeholders, empty states, confirmation and error
text. Do not change the English: marking must leave the source text exactly
as it was.

Leave these in English, unmarked:

- **Text the AI model reads**: prompts, skills, tool descriptions and tool
  results, the project and printer instructions, anything that goes into a
  request to a model. The model speaks the person's language on its own, and
  `wording.test.ts` guards that text. When the same string is shown to the
  person and also sent to the model, translate it only where it is shown.
- Identifiers and values the code compares, protocol fields, CSS classes,
  test ids, file names, URLs, setting keys, G-code.
- Log lines and the text of error reports (`report_unexpected`,
  `report_error`): they are read by developers.
- Names: JusPrin, OrcaSlicer, Bambu Lab, printer and filament model names.

### In the pages

```tsx
import { t, plural } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';

<button aria-label={t`Close`}>…</button>
<p><Trans>Takes effect when {app} restarts. <b>Your project is saved first.</b></Trans></p>
const label = plural(count, { one: '# printer', other: '# printers' });
const title = t({ message: 'Back', context: 'navigation' });   // a word with several meanings
const hint  = t({ message: 'Fit', comment: 'Verb: scale the model to the plate' });  // a note for translators
```

- One message per sentence. Never build a sentence from translated pieces,
  and never splice a translated word into another sentence: use a
  placeholder, or a `Trans` with the element inside it.
- A count needs `plural` / `Plural`, not "printer(s)".
- Put only plain variables in placeholders (`{name}`, not `{a.b.c}`): the
  translator sees the variable's name. Pull an expression into a well-named
  local first.
- The language is chosen before the first render and never changes while a
  page runs, so `t` works anywhere, including at a module's top level and in
  plain `.ts` files. `Trans` and `useLingui` from the macros need no
  provider (see `i18n.tsx`).
- Tests run in English, so a test that finds text by its English words keeps
  working.

### In the native code

- `_L("…")` gives a `wxString` for a window; `_u8L("…")` gives UTF-8 for
  JSON sent to a page.
- Translate when the text is used, never in a static initializer, which runs
  before the language is loaded. Mark such a table with `L("…")` and call
  `_L`/`_u8L` on the entry where it is shown.
- Use `format(_L("%1% is too close to others"), name)` rather than
  concatenation, and `_L_PLURAL` for counts; `// TRN` above a string tells
  the translator what it means.
- Every file that marks text is extracted: `update_catalogs.py` reads every
  `.cpp` and `.hpp` under `src/slic3r/GUI/JusPrin` except `Testing/`. Do not
  add JusPrin files to `localization/i18n/list.txt`.
