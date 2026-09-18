# Spike: identifying a printer from what someone types

Prototype for replacing `printer_catalog_search` — the keyword filter in
`OrcaPrinterBackend::search_catalog` that requires every word of the query to
appear in a printer's name — with the whole catalogue in the model's prompt.

The question this spike answers: **if you hand the model all 383 packaged
printer models and let it do the interpreting, how often does it get the right
printer?**

Words only. Photos are the next spike.

## Layout

| file | what it is |
| --- | --- |
| `build_catalog.py` | reads `resources/profiles/` and dumps one entry per model |
| `catalog.json` | 383 models: id, vendor, model, build volume, nozzles, material |
| `catalog.txt` | the compact form that goes in the prompt (~3,700 tokens) |
| `instructions.txt` | prompt v1 |
| `instructions.v2.txt` | prompt v2 — adds the prefix-ambiguity rule |
| `corpus.jsonl` | 50 test cases with expected outcomes |
| `schema.json` | JSON shape the model must answer in |
| `run.sh` | chat with the assistant yourself, or `--batch` to run the corpus |
| `turn.py` | chat-mode helper: builds each turn's case, prints the answer, keeps the history |
| `score.py` | scores a run against the corpus |
| `compare.py` | shows where repeat runs disagree |
| `instructions.v2r2.txt`, `instructions.v2r3.txt` | byte-identical copies of v2; the run is named after the instruction file, so repeats need their own copy |
| `out/<run>/` | per-case prompt, raw answer and log |

## Trying it yourself

```bash
./run.sh
```

Type what printer you have, the way a person would. Each answer shows what the
assistant said, the cards it would draw (with build volume and catalog id), and
whether it asked a question or called the printer unsupported. The conversation
carries across turns, so you can correct it ("no wait, the S1") or answer its
question ("the small one").

- `/photo PATH [words]` attaches a photo (the path may contain spaces); the
  chat uses the photo-aware prompt from `../printer-photos/` by default
- `/reset` starts a new conversation
- `/raw` toggles the model's raw JSON answer
- `/quit` leaves
- `-m <model>` and `-i <instructions file>` pick another model or prompt
  (default `gpt-5.6-terra` and `instructions.v2.txt`)

Every turn's full prompt and answer are kept in the temp folder printed at
start, for when you want to see exactly what the model was given. A card marked
`INVENTED ID` means the model named a printer that is not in the catalogue.

## Running the corpus

```bash
./run.sh --batch -j 8
./score.py gpt-5.6-terra-v2
```

`--batch` skips cases that already have a result, so delete `out/<run>/` to
redo one. The run is named after the model and the instructions file.

If the profiles change, rebuild the catalogue first:

```bash
./build_catalog.py ../../../../resources/profiles > catalog.json && python3 -c "import json;d=json.load(open('catalog.json'));open('catalog.txt','w').write(chr(10).join(f\"{e['catalogId']} | {e['buildVolume'] or '?'}\" for e in d))"
```

## The corpus

50 cases in nine shapes:

- **name-in-sentence** (9) — the real model name inside ordinary speech
- **ambiguous-partial** (8) — "bambu a1", where several models fit
- **clue-only** (7) — "the ender with the touchscreen", no model name at all
- **size-only** (6) — a bed size and nothing else
- **not-in-catalogue** (6) — resin printers, laser cutters, models we don't ship
- **typo** (6) — "ender3v2", "neptun 4 pro", "BAMBU LAB A1 MINI"
- **correction** (4) — a second turn that revises the first
- **non-english** (2) — Spanish and Chinese, including the Chinese brand name
- **junk** (2) — gibberish, and a question that isn't an identification

Each case declares the ids that must appear, the ids that may appear, a card
cap, and whether a question is required. Scoring is automatic except for the
prose, which needs reading.

The scorer's hardest check is **invented ids**: any catalogId not in
`catalog.json` is a fail, because it means a card that cannot be installed.

## Results (2026-09-17, gpt-5.6-terra via the `codex` CLI)

**Prompt v1 — 47/50.** Three failures, all the same mistake: when the query
exactly equals one model name but is also the start of others, it showed that
one model alone. "bambu a1" got the A1 without the A1 mini; "bambu x1" got the
X1 without the X1 Carbon; "ender 3" got the bare Ender-3 out of twelve.

**Prompt v2 — 50/50 on the first run.** v2 adds one rule: a query that starts
more than one model name is not a clear match even when it exactly equals one
of them. That rule alone fixed all three.

**Repeats of v2 partly blocked.** The Codex account hit its usage limit after
129 calls, so runs 2 and 3 completed only 15 and 14 of the 50 cases.

**Pooled across the three v2 runs — 78 passes, 1 failure in 79 case-runs.**
The one failure is "bambu x1" in run 3, which reverted to the v1 behaviour and
showed the X1 alone.

Of the 14 cases that completed in all three v2 runs, 11 gave identical answers.
All three that varied were partial-name cases (`prusa mini`, `ender 3`,
`bambu x1`). Two of the three varied harmlessly — the same cards, labelled
`ask` in one run and `propose` in another. One varied into a wrong answer.

**No invented catalog ids in any run.** That was the failure mode most worth
measuring and it did not occur once in 129 calls.

**A corpus bug the model caught.** Case E07 expected "snapmaker artisan" to be
unsupported. The Snapmaker Artisan is in fact packaged. The model was right and
the corpus was wrong; E07 is now a positive case.

**These runs used a catalogue with gaps.** The first version of
`build_catalog.py` did not follow the `inherits` chain in machine presets, so
63 of the 383 models went into the prompt with build volume `?` — the Bambu
Lab A1 and the Snapmaker Artisan among them. The app itself resolves
inheritance (`PrinterCatalog.cpp`, `resolve_machine`). The dump now does too,
leaving 9 unknown. Every result above was recorded with the gappy catalogue, so
the size-based cases were tested with less information than the app has. They
should be re-run.

### What the numbers do not cover

- Only one model was tested, on one day.
- Repeats cover 14 of 50 cases, weighted toward the first two shapes.
- Automatic scoring judges which cards were shown and whether a question was
  asked. It does not judge the prose; that was read by hand once.
- The catalogue here keys on `<vendor folder>/<model name>`, while
  `OrcaPrinterBackend` keys on `<vendor id>/<model id>`. Same one-per-model
  granularity, different spelling.
- 383 models are dumped here; the app counts 384. One model has no machine
  preset naming it, so this dump drops it.
- 9 models still have no build volume after resolving inheritance (six are
  Creality `_CFS-C` duplicates). Not chased.
