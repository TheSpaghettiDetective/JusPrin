// Guards the stylesheet's contract with resources/jusprin/ui/design-tokens.json:
// colors, radii, and type metrics resolve through the custom properties that
// tokens.ts emits, and spacing sits on the spacing scale. The file is read
// from disk on purpose: importing the CSS under Vitest yields an empty string,
// so a test written that way passes while asserting nothing.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { semanticVariableNames } from '@shared/tokens';
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
    const emitted = new Set([...staticVariableNames(), ...semanticVariableNames()]);
    expect(emitted).toContain('--font-body-bold');
    expect(emitted).toContain('--button-secondary-padding');
    expect(emitted).toContain('--progress-height');
    expect(emitted).toContain('--action-primary-text');
    const requested = [
      ...css.matchAll(
        /var\((--(?:radius|font|button|progress|action|text|surface|border|status|menu-row|popover|segmented|tooltip|composer|focus|select|field-search|swatch)-[\w-]+)\)/g,
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

  // The dock narrows with the window, well below the 429px design frame, and
  // a header that wraps there silently costs the thread a row. The label on
  // the left must clip instead, so the state on the right is always whole.
  // Each design-system control is drawn from its own recipe, so a size
  // typed into one of them cannot drift from Figma unnoticed.
  it.each([
    ['.panel-link-button', ['--button-link-height', '--button-link-padding', '--font-label-bold']],
    // The Agent connection rows (canvas Agent/Provider Row, Agent/Tool Row).
    ['.agent-row-action', ['--button-secondary-padding', '--button-secondary-icon-gap', '--radius-small', '--font-body-small']],
    ['.agent-key-field', ['--provider-row-field-height', '--field-padding-x', '--radius-small', '--border-subtle']],
    ['.change-revert-tooltip', ['--tooltip-height', '--tooltip-padding-y', '--tooltip-padding-x', '--tooltip-radius',
      '--font-metadata']],
    ['.chat-menu', ['--popover-padding-y', '--popover-border-width', '--popover-radius']],
    ['.chat-menu-list', ['--popover-row-gap']],
    ['.field-search', ['--field-search-height', '--field-search-padding-x', '--field-search-icon-gap',
      '--field-search-radius', '--field-search-border-width', '--border-subtle', '--font-body']],
    ['.field-search .jp-icon', ['--field-search-icon-size', '--text-secondary']],
    ['.field-search:focus-within', ['--field-search-focus-border-width', '--border-strong']],
    ['.chat-actions--above .chat-menu--search', ['--popover-width']],
    ['.chat-menu button', ['--menu-row-height', '--menu-row-side-inset', '--menu-row-radius']],
    ['button.primary', ['--button-primary-padding', '--font-body-small-bold']],
    ['.composer-send', ['--button-primary-small-height', '--button-primary-small-padding']],
    ['.composer-send.composer-stop', ['--button-secondary-small-height', '--button-secondary-small-padding']],
    ['.composer-choices button.chat-icon', ['--button-icon-width', '--button-icon-height']],
    ['.composer-picker', ['--button-picker-height', '--button-picker-padding', '--button-picker-icon-gap', '--font-label']],
    ['.chat-actions--above .chat-menu', ['--popover-width']],
    ['.composer-box:has(textarea:focus)', ['--focus-width', '--border-focus']],
    // The All settings panel's controls: the design system's Select,
    // Segmented Control (for any surface, not only the setup screens) and
    // Swatch.
    ['.jp-select', ['--select-height', '--select-padding-x', '--select-radius', '--font-body', '--border-subtle']],
    ['.jp-select .jp-icon', ['--select-chevron-size', '--text-secondary']],
    ['.jp-segmented', ['--segmented-height', '--segmented-padding', '--segmented-border-width', '--segmented-radius']],
    ['.jp-segment', ['--segmented-segment-height', '--segmented-segment-padding-y', '--segmented-segment-padding-x',
      '--font-label']],
    ['.all-settings-swatch', ['--swatch-size', '--radius-small', '--border-subtle']],
  ])('draws %s from its recipe', (selector, variables) => {
    // Only rules that name the selector itself, alone or in a list, not a
    // descendant form of it such as `.dialog button.primary`.
    const escaped = selector.replace(/[.()[\]]/g, '\\$&');
    const rules = [...css.matchAll(new RegExp(`(?:^|[},])\\s*(?:[^{},]*,\\s*)*${escaped}\\s*[,{][^}]*\\}`, 'gm'))]
      .map((match) => match[0]).join('\n');
    expect(rules.length).toBeGreaterThan(0);
    for (const variable of variables) expect(rules).toContain(`var(${variable})`);
  });

  // A menu row's keyboard focus is the hovered row's fill, never the outside
  // ring, which would stick out past the popover's edge.
  it('draws a focused menu row as the hovered one', () => {
    const bodies = css.split('}').map((rule) => rule.split('{'))
      .filter(([selectors]) => selectors.split(',').some((name) => name.trim() === '.chat-menu button:focus-visible'))
      .map(([, body]) => body)
      .join(';');
    expect(bodies).toMatch(/outline:\s*none/);
    expect(bodies).toMatch(/background:\s*var\(--surface-raised-hover\)/);
  });

  // The composer's menus open above the box at its left edge, never at their
  // own button, where a wide menu would run past the pane's right edge.
  it('places the composer menus on the box, not on their buttons', () => {
    const rule = (selector: string) => css.split('}').map((block) => block.split('{'))
      .filter(([selectors]) => selectors.split(',').some((name) => name.trim() === selector))
      .map(([, body]) => body).join(';');
    expect(rule('.composer-box')).toMatch(/position:\s*relative/);
    expect(rule('.composer-choices .chat-actions')).toMatch(/position:\s*static/);
    const menu = rule('.chat-actions--above .chat-menu');
    expect(menu).toMatch(/left:\s*0/);
    expect(menu).toMatch(/bottom:\s*calc\(100% \+ 8px\)/);
    expect(menu).toMatch(/max-width:\s*100%/);
  });

  // A narrow pane drops "Thinking:" before it cuts the model's name, and
  // only the model picker gives up width; + and Send keep theirs. While the
  // composer measures, the prefix is shown, so the full width is what counts.
  it('shortens the thinking picker before the model picker', () => {
    const rule = (selector: string) => css.split('}').map((block) => block.split('{'))
      .filter(([selectors]) => selectors.split(',').some((name) => name.trim() === selector))
      .map(([, body]) => body).join(';');
    expect(rule('.composer-choices--narrow .composer-picker-prefix')).toMatch(/display:\s*none/);
    expect(rule('.composer-choices--measuring .composer-picker-prefix')).toMatch(/display:\s*inline/);
    expect(rule('.composer-choices .composer-model-menu')).toMatch(/flex:\s*0 1 auto/);
    expect(rule('.composer-choices .chat-actions')).toMatch(/flex:\s*0 0 auto/);
  });

  // The model menu's search field stays put while the rows scroll under it.
  it('scrolls a menu\'s rows, not its search field', () => {
    const rule = (selector: string) => css.split('}').map((block) => block.split('{'))
      .filter(([selectors]) => selectors.split(',').some((name) => name.trim() === selector))
      .map(([, body]) => body).join(';');
    expect(rule('.chat-menu-list')).toMatch(/overflow-y:\s*auto/);
    expect(rule('.chat-actions--above .chat-menu')).not.toMatch(/overflow-y/);
    expect(rule('.chat-menu-search')).toMatch(/flex:\s*0 0 auto/);
  });

  // The design's docked band (canvas "Current Setup Card", 2026-10-20): the
  // card runs edge to edge under the header with square corners and no
  // stripe; a divider below it always, elevation only while scrolled.
  it('docks the setup card as a full-width band', () => {
    const rule = (selector: string) => css.split('}').map((block) => block.split('{'))
      .filter(([selectors]) => selectors.split(',').some((name) => name.trim() === selector))
      .map(([, body]) => body).join(';');
    expect(rule('.pinned-setup')).not.toMatch(/padding/);
    expect(rule('.pinned-setup')).toMatch(/border-bottom:\s*1px solid var\(--border-subtle\)/);
    expect(rule('.pinned-setup')).not.toMatch(/elevation/);
    expect(rule('.pinned-setup--scrolled')).toMatch(/var\(--elevation-subtle\)/);
    expect(rule('.current-setup')).not.toMatch(/border-radius/);
    expect(rule('.current-setup--live')).not.toMatch(/box-shadow/);
  });

  it('keeps the setup card header on one line', () => {
    const rule = css.match(/\.current-setup-header\s*\{[^}]*\}/);
    expect(rule).not.toBeNull();
    expect(rule![0]).toMatch(/white-space:\s*nowrap/);
    const label = css.match(/\.current-setup-label\s*\{[^}]*\}/);
    expect(label).not.toBeNull();
    expect(label![0]).toMatch(/text-overflow:\s*ellipsis/);
  });
});
