// The setup card and the setup page in every state, drawn by the production
// components with the production stylesheet and tokens.
//
// Every run asserts that each state can be produced from real inputs. With
// PREVIEW_OUT=/absolute/path/setup-card.html it also writes a review page:
// each state at the dock's 320 DIP minimum, where text has the least room,
// and at the reviewed 429, in both token modes.
//
//   PREVIEW_OUT=/tmp/setup-card.html npx vitest run SetupCard.preview

import { describe, expect, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { writeFileSync } from 'node:fs';
import { escapeHtml, productionCss, tokenVariables } from '../test/visual';
import { renderVisualCase, setupVisualCases } from './SetupCard.visual-cases';

const DOCK_WIDTHS = [320, 429] as const;

describe('setup card and setup page review', () => {
  it('names every state once', () => {
    expect(new Set(setupVisualCases.map((item) => item.id)).size).toBe(setupVisualCases.length);
  });

  it.each(setupVisualCases)('renders $name from its state inputs', (entry) => {
    const html = renderToStaticMarkup(renderVisualCase(entry));
    expect(html).toContain(entry.kind === 'page' ? 'data-testid="current-setup-page"' : 'data-testid="current-setup"');
    expect(html).not.toContain('undefined');
    expect(html).not.toContain('NaN');
  });

  it('writes a review page using the production components and stylesheet', () => {
    const out = process.env.PREVIEW_OUT;
    if (!out) return;
    const { light, dark } = tokenVariables();
    // A card sits in the band the panel pins it in; a page is the panel's
    // body under its own header.
    const figure = (mode: string, dock: number) => setupVisualCases.map((entry) => {
      const html = renderToStaticMarkup(renderVisualCase(entry));
      return `<figure data-case="${entry.id}" data-mode="${mode}" data-dock="${dock}">
<figcaption>${escapeHtml(entry.id)} · ${escapeHtml(entry.name)}${entry.frame ? ` · Figma ${entry.frame}` : ''}</figcaption>
<div class="dock ${mode}" style="width:${dock}px">${entry.kind === 'card' ? `<div class="pinned-setup">${html}</div>` : html}</div></figure>`;
    }).join('\n');
    writeFileSync(out, `<!doctype html><html lang="en"><meta charset="utf-8">
<title>Setup card and setup page</title>
<style>
:root { ${light} }
${productionCss()}
.dark { ${dark} }
html, body { height: auto; }
body { margin:0; padding:16px; background:var(--surface-subtle); color:var(--text-primary); font:var(--font-body); }
h1 { font:var(--font-section); margin:0 0 12px; }
h2 { font:var(--font-label-bold); margin:24px 0 8px; color:var(--text-secondary); }
.grid { display:flex; flex-wrap:wrap; gap:16px; align-items:flex-start; }
figure { margin:0; }
figcaption { font:var(--font-label); color:var(--text-secondary); margin-bottom:8px; }
.dock { display:flex; flex-direction:column; background:var(--surface-canvas); color:var(--text-primary); }
</style>
<h1>Setup card and setup page</h1>
${(['light', 'dark'] as const).flatMap((mode) => DOCK_WIDTHS.map((dock) =>
  `<h2>${mode} · ${dock} DIP dock</h2><div class="grid">${figure(mode, dock)}</div>`)).join('\n')}
</html>`);
  });
});
