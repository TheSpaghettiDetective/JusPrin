# Spike: identifying a printer from a photo

Second half of the printer-identification spike. The first half,
`../printer-naming/`, tested words; this one tests photos, alone and together
with a few words. Same approach: the whole packaged catalogue is in the prompt
and the model does the identifying.

## Layout

| file | what it is |
| --- | --- |
| `corpus.jsonl` | 15 cases: 12 photos alone, 3 photos with words |
| `instructions.txt` | photo prompt v1: the words prompt v2 plus a section on reading photos |
| `instructions.v2.txt` | photo prompt v2: adds rules on printed sizes, shape-only guesses and clones |
| `schema.json` | answer shape; adds `evidence`, what the model says it saw |
| `run.sh` | runs the corpus; `-i` picks the prompt, `-t` tags the run |
| `score.py` | scores one or more runs |
| `out/<run>/` | per case: the photo as attached, the prompt, the answer, the log |

The photos are **not** in the repo. They live in `~/Downloads/printer-photos/`
with `ground-truth.json` (source page, author, licence, expected answer). They
are Wikimedia Commons photos under CC0, CC BY and CC BY-SA; the CC BY ones need
credit if they are ever published. `out/` holds copies, and `.gitignore` keeps
them out.

**The model can read an attachment's filename.** Codex sends each image with
its path as text, `<image name=[Image #1] path="...">`, using the path exactly
as passed on the command line; attached as `zebra-canary-7731.jpg` and asked,
the model quoted the name back. So each photo is copied under a random name
(`6d768f52812d.jpg`) and attached by that bare name from inside its folder: no
filename such as "Problemen met neptune 4 Max Elegoo.jpg", no run tag and no
case number reaches the model. `out/<run>/<case>.attached` records which random
name a case used. The chat in `../printer-naming/` does the same.

The photos' embedded metadata was checked with exiftool: none carries a title,
description or keyword naming the printer.

The first runs attached photos as `out/<run>/P09.jpg`; the spike was then
rerun with random names. Both sets are below.

## Running it

```bash
./run.sh -i instructions.v2.txt -t mine
./score.py gpt-5.6-terra-mine
```

To try a photo yourself, use the chat in `../printer-naming/`:

```
./run.sh
you> /photo ~/Downloads/printer-photos/08 - Voron 3D printer.jpg
you> the 300
```

## Results (2026-09-17, gpt-5.6-terra, three runs per prompt)

| | prompt v1 | prompt v2 |
| --- | --- | --- |
| pooled | 32/45 | 38/45 |
| model name or size printed on the machine (P01 P02 P04 P05 P06 P12) | 16/18 | 18/18 |
| shape only, no readable name (P03 P07 P09 P10 P11) | 4/15 | 8/15 |
| size can't be seen, must ask (P08) | 3/3 | 3/3 |
| photo plus words (W01–W03) | 9/9 | 9/9 |

v2's gain is almost all two cases: the bed-label rule fixed the A1 bed close-up
(P05, 1/3 → 3/3), and the shape-only rule made the old Anycubic ask instead of
guessing (P10, 0/3 → 3/3).

**Resin printers were refused every time** (P12, W03: 12/12 across both
prompts), including when the words said "my elegoo" and pointed at Elegoo's
filament printers.

**No invented catalogue ids** in 90 calls.

### Rerun with random names

Same corpus, same two prompts, three runs each, photos attached by random
name. Case P05 was also corrected: the catalogue gives the Bambu P2S the same
256x256x256 build volume as the A1, so showing both and asking is right, and
the earlier expectation that allowed only the A1 was wrong. All eight runs are
scored against the corrected corpus.

| | v1, old names | v1, random | v2, old names | v2, random |
| --- | --- | --- | --- | --- |
| model name or size printed on the machine | 16/18 | 16/18 | 18/18 | 18/18 |
| shape only, no readable name | 4/15 | 5/15 | 8/15 | 5/15 |
| size can't be seen, must ask | 3/3 | 3/3 | 3/3 | 3/3 |
| photo plus words | 9/9 | 9/9 | 9/9 | 9/9 |
| **total** | **32/45** | **33/45** | **38/45** | **35/45** |
| failures that were a confident wrong answer | 8 | 7 | 4 | 3 |
| failures that asked for the label (recoverable) | 5 | 5 | 3 | 7 |

Nothing moved where a name or size is printed, or where words came with the
photo: those rows are identical across all 180 case-runs. The random names
changed nothing there, which is what the old names carrying no hint predicts.

The shape-only row moves between runs (8/15, then 5/15 on the same prompt),
so three runs are not enough to measure v2 against v1 on that row. What does
hold across both v2 sets is the kind of failure: on v1 most failures were a
confident wrong answer; on v2 most are a wrong shortlist followed by a request
for the model name on the label, which the person can answer.

### Where it fails

- **P03 — a P1S shot from above, lid off.** 1 of 3 on v2. Two runs called it an
  A1, a completely different machine, despite a box frame the A1 does not have.
- **P07 — a home-built Prusa MINI clone.** 0 of 3 on both prompts. It reads the
  machine as a Prusa i3 MK2, which is not packaged, and calls it unsupported.
  The MINI profile is what fits.
- **P09 — a Neptune 4 Max.** 1 of 3 on v2. It sees ELEGOO and a large open
  frame but guesses the Neptune generation wrong (3 Max, 3-family, 2-family).

### It sometimes reports evidence that is not in the photo

Checked by eye against the photos:

- P03, v2 run 1: says the bed reads "BUILD VOLUME 256 x 256 x 256". Nothing on
  that bed is legible from that angle. The v2 rule that a printed size is the
  strongest evidence probably invited this.
- P03, v1 run 1: describes an "open-frame bed-slinger layout" and a "large gray
  vertical gantry". The photo shows an enclosed box frame.
- P07, v2 run 2: says there is an embossed "Prusa i3 MK2" marking. The base is
  embossed with a partial "PRUSA MINI".

Everywhere a label really is visible — "K1C" and "BUILD VOLUME 220x220x250" on
the K1C, "X1-Carbon" on the X1C, "Bambu Lab A1" behind the cat, "ELEGOO MARS" —
it read it correctly, every run.

So `evidence` cannot be shown to the person as fact, and a rule that rewards
finding a label needs a counterweight ("if you cannot read it, say so").

### What this suggests for the product

A photo is reliable when it shows a name or a size, and unreliable when the
model has to judge by shape. So when a photo has no readable name, the
assistant should show its best two or three and ask for **a photo of the
label** — the nameplate, the sticker on the back, the screen's About page —
rather than a typed answer. That turns the weak case into the strong one.

### What the numbers do not cover

- 12 photos, one model, one day. Three runs per prompt.
- Two ground truths are uncertain: P10 (uploader says only "Anycubic i3") and
  P11 (uploader says only "Creality").
- The photos are Commons photos, which skew tidier than what people send from
  a phone mid-problem.
