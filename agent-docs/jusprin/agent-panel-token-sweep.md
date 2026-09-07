# Handoff: bring the Agent panel back onto the design system

**Status:** Not started. Scoped from a measurement pass on 2026-09-07, during the
printer/spool chip work. The chip and the header are already consistent; the
Agent panel is not.

## The problem

`src/slic3r/GUI/JusPrin/AgentUI/src/styles.css` resolves **colour** through the
semantic tokens correctly — that part is fine and should be left alone. What
drifted is **size and radius**: the panel uses values that exist nowhere in
`resources/jusprin/ui/design-tokens.json`, so the same kind of element is drawn
differently in the panel than in the native header beside it.

This is pre-existing. It was not introduced by the chip work, and it was left out
of that change deliberately rather than swept silently.

## What the system allows

From `resources/jusprin/ui/design-tokens.json` (authoritative) and
`agent-docs/jusprin/design-system.md`:

- **Radius:** `standard` 6, `compact` 8, `window` 12. Nothing else.
  (Standard was 4 until 2026-09-07; it is now 6, matching
  `Chip/Printer+Spool` in Figma. Menu rows are a documented exception at 0:
  they are full-width inside a rounded popover.)
- **Type roles:** page title 24/30 bold, section 18/24 bold, body and control
  14/20 regular, label 12/16 bold, dense metadata 10/14 regular. Nothing else.
- **Spacing scale:** 4, 8, 12, 16, 20, 24, 32, 40, 48.

## What to fix

Measured with `grep -oE "border-radius: *[0-9]+px" styles.css | sort | uniq -c`
and the same for `font-size`.

### Radii off the scale

| Value | Count | Selectors |
|---|---|---|
| `6px` | 5 | `button.icon`, `.attachment-chip`, `.setup-path`, `.setup-diff-after`, `.setup-error` |
| `10px` | 1 | `.setup-card` |
| `999px` | 3 | `.history-status`, `.agent-badge`, `.setup-progress` |

The `6px` ones are now **correct by accident** — 6 became the standard radius on
2026-09-07. Verify each is meant to be a standard control, then leave it.

`10px` on `.setup-card` is a card, not a control: it should be `12`
(`radius/window`, used for large surfaces) or `8`. Pick one and say which.

The three `999px` pills are a real shape the token file has no name for. Either
add a `radius/pill` token and use it, or convert them to `8`. Do not leave an
unnamed magic number.

### Type sizes off the scale

| Value | Count | Notes |
|---|---|---|
| `13px` | 10 | The biggest offender. Sits between Label (12) and Body (14) — decide which each one is. |
| `11px` | 4 | Between Dense Metadata (10) and Label (12). |
| `16px` | 3 | `.markdown-content h2`, `.attachment-remove`. Section is 18. |
| `20px` | 1 | `.attach-button`. This is an icon glyph, not text — consider an SVG instead. |

Line heights travel with these; the roles pair 12/16, 14/20, 10/14, 18/24.
Changing a size without its line height will look worse, not better.

## How to do it safely

1. **Measure before and after.** The panel has an existing visual test suite:
   `cd src/slic3r/GUI/JusPrin/AgentUI && npm test` (65 tests). They assert
   behaviour, not pixels, so they will not catch a layout regression — they only
   prove you did not break the panel.
2. **Look at it.** Build the bundle (`npm run build`, which writes
   `resources/jusprin/agent/index.html`) and run the app. Density changes are
   the risk: 13 → 14 makes every one of those ten places taller.
3. **Do it in two commits**: radii first (small, low risk), then type (larger,
   changes rhythm). Do not mix them.
4. **Do not touch colour.** Every `var(--…)` is already correct.

## Related

- `agent-docs/jusprin/design-system.md` — the roles and scales.
- `resources/jusprin/ui/design-tokens.json` — the authoritative values.
- `tests/brand/test_brand_tokens.cpp` — guards colour contrast only; it does not
  check radius or type, so it will not help here and does not need updating.
- The native side (`Shell/HeaderControls.cpp`) is already consistent and is a
  reasonable reference for what "on the system" looks like.
