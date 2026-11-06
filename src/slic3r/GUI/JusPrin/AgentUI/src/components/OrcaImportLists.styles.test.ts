// Guards OrcaImportLists.css the way styles.test.ts guards styles.css, and
// one thing more: the Home page loads it too, so every variable it asks for
// must be one both pages emit. Read from disk: importing CSS under Vitest
// yields an empty string.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { semanticVariableNames } from '@shared/tokens';
import { staticVariableNames as agentVariableNames } from '../tokens';
import { staticVariableNames as homeVariableNames } from '../../../HomeUI/src/tokens';

const SPACING_SCALE = [0, 4, 8, 12, 16, 20, 24, 32, 40, 48];

const source = readFileSync(join(__dirname, 'OrcaImportLists.css'), 'utf8');
const css = source.replace(/\/\*[\s\S]*?\*\//g, (comment) => comment.replace(/[^\n]/g, ' '));
const lines = css.split('\n');

function offending(pattern: RegExp, extra?: (line: string) => boolean): string[] {
  return lines.flatMap((line, index) => (pattern.test(line) && (!extra || extra(line)) ? [`${index + 1}: ${line.trim()}`] : []));
}

describe('OrcaImportLists.css stays on the design tokens of both pages', () => {
  it('was actually read', () => {
    expect(source.trim().length).toBeGreaterThan(0);
  });

  it('carries no literal color, pixel radius or type metric', () => {
    expect(offending(/#[0-9a-fA-F]{3,8}\b|\b(rgba?|hsla?)\(/)).toEqual([]);
    expect(offending(/border-radius\s*:[^;]*\d+px/)).toEqual([]);
    expect(offending(/(?:^|[{;])\s*(font-size|line-height|font-weight)\s*:/)).toEqual([]);
    expect(offending(/(?:^|[{;])\s*font\s*:[^;]*\d+px/)).toEqual([]);
  });

  it('keeps padding, margin, and gap on the spacing scale', () => {
    const offScale = offending(/^\s*(padding|margin|gap|row-gap|column-gap)(-[a-z]+)?\s*:/, (line) => {
      const pixels = [...line.replace(/^[^:]*:/, '').matchAll(/(-?\d+(?:\.\d+)?)px/g)].map((m) => Math.abs(Number(m[1])));
      return pixels.some((px) => !SPACING_SCALE.includes(px));
    });
    expect(offScale).toEqual([]);
  });

  it('asks only for variables both the Agent page and Home emit', () => {
    const semantic = semanticVariableNames();
    const agent = new Set([...agentVariableNames(), ...semantic]);
    const home = new Set([...homeVariableNames(), ...semantic]);
    const requested = [...css.matchAll(/var\((--[\w-]+)\)/g)].map((m) => m[1]);
    expect(requested.length).toBeGreaterThan(0);
    expect(requested.filter((name) => !agent.has(name))).toEqual([]);
    expect(requested.filter((name) => !home.has(name))).toEqual([]);
  });
});
