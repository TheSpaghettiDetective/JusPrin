// Guards the stylesheet's contract with resources/jusprin/ui/design-tokens.json:
// colors, radii, and type metrics resolve through the custom properties that
// tokens.ts emits, and spacing sits on the spacing scale. The file is read
// from disk on purpose: importing the CSS under Vitest yields an empty string,
// so a test written that way passes while asserting nothing.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { staticVariableNames } from './tokens';

const SPACING_SCALE = [0, 4, 8, 12, 16, 20, 24, 32, 40, 48];

const source = readFileSync(join(__dirname, 'styles.css'), 'utf8');

// Blank out comments but keep every newline so reported line numbers match
// the file.
const css = source.replace(/\/\*[\s\S]*?\*\//g, (comment) => comment.replace(/[^\n]/g, ' '));
const lines = css.split('\n');

// Line numbers (1-based) covered by an @font-face block, where font metrics
// are the declaration of the face itself rather than a use of it.
function fontFaceLines(): Set<number> {
  const covered = new Set<number>();
  let depth = 0;
  let inFontFace = false;
  lines.forEach((line, index) => {
    if (!inFontFace && /@font-face\b/.test(line)) inFontFace = true;
    if (inFontFace) covered.add(index + 1);
    for (const ch of line) {
      if (ch === '{') depth += 1;
      if (ch === '}') {
        depth -= 1;
        if (inFontFace && depth === 0) inFontFace = false;
      }
    }
  });
  return covered;
}

const exemptLines = fontFaceLines();

function offending(pattern: RegExp, extra?: (line: string) => boolean): string[] {
  const hits: string[] = [];
  lines.forEach((line, index) => {
    const number = index + 1;
    if (exemptLines.has(number)) return;
    if (pattern.test(line) && (!extra || extra(line))) hits.push(`${number}: ${line.trim()}`);
  });
  return hits;
}

describe('styles.css stays on the design tokens', () => {
  it('was actually read', () => {
    expect(source.trim().length).toBeGreaterThan(0);
  });

  it('carries no literal color', () => {
    expect(offending(/#[0-9a-fA-F]{3,8}\b|\b(rgba?|hsla?)\(/)).toEqual([]);
  });

  it('carries no pixel border-radius', () => {
    expect(offending(/border-radius\s*:[^;]*\d+px/)).toEqual([]);
  });

  // The patterns match after a `{` or `;` as well as at the start of a line,
  // so a single-line rule cannot slip past them.
  it('sets type only through the role variables', () => {
    expect(offending(/(?:^|[{;])\s*(font-size|line-height|font-weight)\s*:/)).toEqual([]);
    // A `font` shorthand with a literal size or line height would bypass the
    // check above.
    expect(offending(/(?:^|[{;])\s*font\s*:[^;]*\d+px/)).toEqual([]);
  });

  it('asks only for variables that tokens.ts emits', () => {
    const emitted = new Set(staticVariableNames());
    expect(emitted).toContain('--font-body-bold');
    expect(emitted).toContain('--project-card-min-width');
    expect(emitted).toContain('--printer-card-column-width');
    const requested = [
      ...css.matchAll(
        /var\((--(?:font|button|project-card|printer-card|status-dot|swatch|glyph|elevation)-[\w-]+)\)/g,
      ),
    ].map((m) => m[1]);
    expect(requested.length).toBeGreaterThan(0);
    expect(requested.filter((name) => !emitted.has(name))).toEqual([]);
  });

  it('keeps padding, margin, and gap on the spacing scale', () => {
    const offScale = offending(/^\s*(padding|margin|gap|row-gap|column-gap)(-[a-z]+)?\s*:/, (line) => {
      const value = line.replace(/^[^:]*:/, '');
      const pixels = [...value.matchAll(/(-?\d+(?:\.\d+)?)px/g)].map((m) => Math.abs(Number(m[1])));
      return pixels.some((px) => !SPACING_SCALE.includes(px));
    });
    expect(offScale).toEqual([]);
  });

  // Section 2 of the handoff: as many columns as fit, each card between its
  // own bounds. A fixed column count grows cards to fill a wide monitor, which
  // is the mistake this rule exists to prevent.
  it('sizes the gallery by the card bounds, not by a column count', () => {
    const rule = css.match(/\.project-grid\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).toMatch(/auto-fill/);
    expect(rule![0]).toMatch(/minmax\(\s*var\(--project-card-min-width\)\s*,\s*1fr\s*\)/);
    // The ceiling belongs on the card, not on the track: auto-fill counts
    // tracks by their maximum, so a 320px track cap fits two columns at 1280
    // where three belong.
    const card = css.match(/\.project-card\s*\{[^}]*\}/);
    expect(card).not.toBeNull();
    expect(card![0]).toMatch(/max-width:\s*var\(--project-card-max-width\)/);
  });

  // A 4:3 frame, never cropped: both a cropped model and an empty band above
  // and below appeared in review.
  it('keeps the thumbnail at the token aspect ratio and uncropped', () => {
    const frame = css.match(/\.project-thumbnail\s*\{[^}]*\}/);
    expect(frame).not.toBeNull();
    expect(frame![0]).toMatch(/aspect-ratio:\s*var\(--project-card-thumbnail-aspect\)/);
    const image = css.match(/\.project-thumbnail img\s*\{[^}]*\}/);
    expect(image).not.toBeNull();
    expect(image![0]).toMatch(/object-fit:\s*contain/);
    // An in-flow image contributes its intrinsic height as the frame's minimum
    // content height, which beats the ratio: every card holding a real .3mf
    // thumbnail came out square in the app while cards without one, the only
    // kind this suite can render, stayed 4:3. Out of flow, the ratio governs.
    expect(image![0]).toMatch(/position:\s*absolute/);
    expect(frame![0]).toMatch(/position:\s*relative/);
  });

  // A long name truncates rather than reflowing the grid.
  it('truncates the project title on one line', () => {
    const rule = css.match(/\.project-title\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).toMatch(/white-space:\s*nowrap/);
    expect(rule![0]).toMatch(/text-overflow:\s*ellipsis/);
  });

  // The frames run the thumbnail straight into the footer. A rule separating
  // them cuts the card in two, which is what it looked like before anyone
  // compared it to the design.
  it('does not rule a line between the thumbnail and the footer', () => {
    const rule = css.match(/\.project-footer\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).not.toMatch(/border/);
  });

  // A platform scrollbar beside the rail's divider reads as a second, heavier
  // line; the design has one divider there.
  it('draws its own thin scrollbar in the gallery, invisible at rest', () => {
    expect(css).toMatch(/\.gallery[^{]*\{[^}]*scrollbar-width:\s*thin/);
    expect(css).toMatch(/\.gallery::-webkit-scrollbar-track[^{]*\{[^}]*background:\s*transparent/);
    expect(css).toMatch(/\.gallery::-webkit-scrollbar-thumb[^{]*\{[^}]*background:\s*transparent/);
    expect(css).toMatch(/\.gallery:hover::-webkit-scrollbar-thumb[^{]*\{[^}]*background:\s*var\(--border-subtle\)/);
  });

  // The tint is the state. An ordinary card sits on surface.raised and only a
  // printing one is tinted; giving every card the tint leaves the printing
  // card differing by its border alone, which is not what the frames show in
  // either mode.
  it('tints only the printing card', () => {
    const card = css.match(/\.project-card\s*\{[^}]*\}/);
    expect(card).not.toBeNull();
    expect(card![0]).toMatch(/background:\s*var\(--surface-raised\)/);
    const printing = css.match(/\.project-card\.printing\s*\{[^}]*\}/);
    expect(printing).not.toBeNull();
    expect(printing![0]).toMatch(/background:\s*var\(--surface-subtle\)/);
    expect(printing![0]).toMatch(/border-color:\s*var\(--status-success\)/);
  });

  // One surface per card: a thumbnail frame with a ground of its own turns an
  // image-less card into a light box stacked on a tinted strip.
  it('gives the thumbnail frame no ground of its own', () => {
    const rule = css.match(/\.project-thumbnail\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).not.toMatch(/background/);
  });

  // Both cards rest just above the surface, which is the Subtle tier. The
  // design's audit replaced thirteen ad-hoc shadows with three tiers, so a
  // literal here is the thing that must not come back.
  it('lifts both cards with the subtle elevation token', () => {
    for (const selector of [/\.project-card\s*\{[^}]*\}/, /\.printer-card\s*\{[^}]*\}/]) {
      const rule = css.match(selector);
      expect(rule).not.toBeNull();
      expect(rule![0]).toMatch(/box-shadow:\s*var\(--elevation-subtle\)/);
    }
    // A shadow written out by hand would carry its own colour and escape the
    // token file; the only box-shadow allowed to name a colour is none.
    const literal = offending(/box-shadow\s*:(?![^;]*var\()/);
    expect(literal).toEqual([]);
  });

  // Without the border the black spool vanishes on the dark card and the white
  // one on the light card.
  it('borders every filament swatch', () => {
    const rule = css.match(/\.spool-swatch\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).toMatch(/border:\s*1px solid var\(--border-subtle\)/);
  });

  // The printer rail is a fixed width; the gallery takes what is left.
  it('fixes the printer column to its token width', () => {
    const rule = css.match(/\.printer-column\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).toMatch(/flex:\s*0 0 var\(--printer-card-column-width\)/);
  });
});
