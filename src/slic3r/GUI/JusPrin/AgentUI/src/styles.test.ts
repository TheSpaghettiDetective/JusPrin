// Guards the stylesheet's contract with resources/jusprin/ui/design-tokens.json:
// colors, radii, and type metrics resolve through the custom properties that
// tokens.ts emits, and spacing sits on the spacing scale. The file is read
// from disk on purpose: importing the CSS under Vitest yields an empty string,
// so a test written that way passes while asserting nothing.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';

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

  it('sets type only through the role variables', () => {
    expect(offending(/^\s*(font-size|line-height|font-weight)\s*:/)).toEqual([]);
    // A `font` shorthand with a literal size or line height would bypass the
    // check above.
    expect(offending(/^\s*font\s*:[^;]*\d+px/)).toEqual([]);
  });

  it('keeps padding, margin, and gap on the spacing scale', () => {
    const offScale = offending(/^\s*(padding|margin|gap|row-gap|column-gap)(-[a-z]+)?\s*:/, (line) => {
      const value = line.replace(/^[^:]*:/, '');
      const pixels = [...value.matchAll(/(-?\d+(?:\.\d+)?)px/g)].map((m) => Math.abs(Number(m[1])));
      return pixels.some((px) => !SPACING_SCALE.includes(px));
    });
    expect(offScale).toEqual([]);
  });
});
