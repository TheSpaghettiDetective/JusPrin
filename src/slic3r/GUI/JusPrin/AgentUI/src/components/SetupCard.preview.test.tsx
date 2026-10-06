// Run with PREVIEW_OUT=/absolute/path/setup-card.html npx vitest run SetupCard.preview.
// The page renders the production card and stylesheet for every approved
// frame of Figma sections 5 to 7, in both token modes and at the reviewed and
// the narrowest dock width, so each can be compared with its frame by eye.
// A second page shows the earlier-chat boundary in the real App.

import { describe, expect, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { act, render } from '@testing-library/react';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { App } from '../App';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from '../bridge/protocol';
import { renderVisualCase, setupVisualCases } from './SetupCard.visual-cases';
import { applyAppearance, applyStaticTokens } from '../tokens';

const escape = (value: string) => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;')
  .replaceAll('>', '&gt;').replaceAll('"', '&quot;');

const resources = resolve(__dirname, '../../../../../../../resources');

// The stylesheet as the WebView gets it: Vite inlines the icon masks and the
// fonts into the single-file bundle, so the review page inlines them too.
function productionCss(): string {
  return readFileSync(resolve(__dirname, '../styles.css'), 'utf8')
    .replace(/url\('@resources\/([^']+\.svg)'\)/g, (_, path) =>
      `url("data:image/svg+xml;base64,${readFileSync(resolve(resources, path)).toString('base64')}")`)
    .replace(/url\('(\.\.\/\.\.\/WebShared\/fonts\/[^']+\.woff)'\)/g, (_, path) =>
      `url("data:font/woff;base64,${readFileSync(resolve(__dirname, '..', path)).toString('base64')}")`);
}

describe('approved setup card visual matrix', () => {
  it('has one state input for every card frame in sections 5, 6, and 7', () => {
    // Section 5's fourth frame is the project-updates boundary, which the App
    // owns; the second review page renders it.
    expect(setupVisualCases.filter((item) => item.section === '5')).toHaveLength(3);
    expect(setupVisualCases.filter((item) => item.section === '6')).toHaveLength(8);
    expect(setupVisualCases.filter((item) => item.section === '7')).toHaveLength(8);
    expect(new Set(setupVisualCases.map((item) => item.id)).size).toBe(setupVisualCases.length);
    expect(new Set(setupVisualCases.map((item) => item.frame)).size).toBe(setupVisualCases.length);
  });

  it.each(setupVisualCases)('renders approved case $id from its state inputs', (entry) => {
    const html = renderToStaticMarkup(renderVisualCase(entry));
    expect(html).toContain('data-testid="current-setup"');
    expect(html).not.toContain('undefined');
    expect(html).not.toContain('NaN');
  });

  it('writes a review page using the actual component and production CSS', () => {
    const out = process.env.PREVIEW_OUT;
    if (!out) return;

    // The variables exactly as the page writes them at startup and on an
    // appearance change.
    applyStaticTokens();
    applyAppearance('dark');
    const dark = document.documentElement.style.cssText;
    applyAppearance('light');
    const light = document.documentElement.style.cssText;
    const css = productionCss();
    const page = (title: string, body: string, extra = '') => `<!doctype html><html lang="en"><meta charset="utf-8">
<title>${title}</title>
<style>
:root { ${light} }
${css}
.dark { ${dark} }
html, body { height: auto; }
body { margin:0; padding:16px; background:var(--surface-canvas); color:var(--text-primary); font:var(--font-body); }
nav { display:flex; flex-wrap:wrap; gap:12px; margin-bottom:20px; font:var(--font-label); }
nav a { color:var(--action-primary); }
h1 { font:var(--font-section); margin:0 0 12px; }
h2 { font:var(--font-body-bold); margin:24px 0 12px; }
.grid { display:flex; flex-wrap:wrap; gap:16px; align-items:flex-start; }
figure { margin:0; }
figcaption { font:var(--font-label); color:var(--text-secondary); margin-bottom:8px; }
.dock { box-sizing:border-box; min-width:0; background:var(--surface-canvas); color:var(--text-primary); }
.dock.dark, .stage.dark { outline:8px solid var(--surface-canvas); }
${extra}
</style>
<body>${body}</body></html>`;

    const modes = ['light', 'dark'] as const;
    const widths = [405, 320] as const;
    const sections = ['5', '6', '7'] as const;
    // The card alone, at its own width: the pinned band adds 12px of dock
    // around it in the app and nothing to the card itself.
    const panels = (section: string, mode: string, width: number) =>
      setupVisualCases.filter((item) => item.section === section).map((item) =>
        `<figure data-case="${item.id}" data-mode="${mode}" data-width="${width}">
          <figcaption>${escape(item.name)} · Figma ${item.frame}</figcaption>
          <div class="dock ${mode}" style="width:${width}px">${renderToStaticMarkup(renderVisualCase(item))}</div>
        </figure>`).join('\n');
    const groups = modes.flatMap((mode) => widths.flatMap((width) => sections.map((section) =>
      `<section id="s${section}-${mode}-${width}"><h2>Section ${section} · ${mode} · ${width} DIP</h2>
        <div class="grid">${panels(section, mode, width)}</div></section>`))).join('\n');
    const nav = modes.flatMap((mode) => widths.flatMap((width) => sections.map((section) =>
      `<a href="#s${section}-${mode}-${width}">${section} · ${mode} · ${width}</a>`))).join('');
    writeFileSync(out, page('Setup card acceptance frames', `<h1>Setup card acceptance frames</h1><nav>${nav}</nav>${groups}`));

    const summary = setupVisualCases.find((item) => item.id === '5-earlier')!.context;
    const boundary = (status: 'changed' | 'unchanged' | 'unavailable') => {
      const state: StatePayload = {
        agent: { status: 'ready' }, appearance: 'light',
        conversations: [
          { id: 'earlier', title: 'Strong bracket', createdAt: '2026-10-03T10:42:00' },
          { id: 'active', title: 'Current project', createdAt: '2026-10-05T10:42:00' },
        ],
        activeConversationId: 'active', viewedConversationId: 'earlier', docRevision: 23,
        chatResume: status === 'unavailable' ? { status }
          : { status, savedAt: '2026-10-03T10:42:00', versionId: 'saved-1', summary },
        conversation: [
          { id: 'm-1', role: 'user', text: 'Make this a quick fit check, under an hour.', createdAt: '2026-10-03T10:40:00', status: 'complete' },
          { id: 'm-2', role: 'assistant', text: 'Done: coarse layers, two walls, 5% infill.', createdAt: '2026-10-03T10:41:00', status: 'complete' },
        ], streamingMessageId: null, toolActivities: [], builds: [], exportedCopies: [],
        physicalPrints: [], draft: '', context: setupVisualCases.find((item) => item.id === '6-working')!.context,
      } as unknown as StatePayload;
      const { container, unmount } = render(<App getTransport={() => ({ post: () => {} })} />);
      const deliver = (type: string, payload: unknown) => {
        const envelope: Envelope = { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: `visual-${type}`, type, payload };
        act(() => window.__jusprinBridge!.deliver(envelope));
      };
      deliver('hello_ack', { version: PROTOCOL_VERSION, agent: state.agent, appearance: state.appearance });
      deliver('state', state);
      const html = container.querySelector('.app')!.outerHTML;
      unmount();
      return html;
    };
    const stages = (['changed', 'unchanged', 'unavailable'] as const).map((status) => {
      const html = boundary(status);
      expect(html).toContain('project-updates');
      return `<h2>Checkpoint ${status}</h2><div class="grid">${modes.flatMap((mode) => widths.map((width) =>
        `<figure data-status="${status}" data-mode="${mode}" data-width="${width}">
          <figcaption>${mode} · ${width} DIP</figcaption>
          <div class="stage ${mode}" style="--stage-width:${width + 24}px">${html}</div></figure>`)).join('')}</div>`;
    }).join('\n');
    writeFileSync(out.replace(/\.html$/, '-historical.html'), page('Earlier chat boundary',
      `<h1>Section 5 · project-update boundary (Figma 1291:1808)</h1>${stages}`,
      `.stage { box-sizing:border-box; width:var(--stage-width); height:640px; overflow:hidden;
  background:var(--surface-canvas); color:var(--text-primary); border:1px solid var(--border-subtle); }
.stage .app { height:100%; }`));
  });
});
