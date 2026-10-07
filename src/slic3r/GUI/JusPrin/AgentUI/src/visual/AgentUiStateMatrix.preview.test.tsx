// The state matrices on the Figma page "Agent UI · States & Components",
// rendered from the production components and stylesheet.
//
// Every run asserts that each named state can be produced from real inputs.
// With PREVIEW_OUT=/absolute/path/agent-ui-states.html it also writes a review
// page: each state at the reviewed dock width and at the pane's 320 DIP
// minimum, in both token modes, to be compared with its Figma node by eye.
//
//   PREVIEW_OUT=/tmp/agent-ui-states.html npx vitest run AgentUiStateMatrix

import { describe, expect, it } from 'vitest';
import { writeFileSync } from 'node:fs';
import { escapeHtml, productionCss, tokenVariables, VisualCase } from '../test/visual';
import { approvalCases } from './approvals.visual-cases';
import { authoringCases } from './authoring.visual-cases';
import { historyCases } from './history.visual-cases';
import { printerCases } from './printer.visual-cases';
import { runtimeCases } from './runtime.visual-cases';

const cases: VisualCase[] = [...approvalCases, ...runtimeCases, ...authoringCases, ...historyCases, ...printerCases];

const DOCK_WIDTHS = [429, 320] as const;
// The thread insets its items 16 on each side.
const THREAD_INSET = 32;

describe('Agent UI state matrices', () => {
  it('names every state once', () => {
    expect(new Set(cases.map((item) => item.id)).size).toBe(cases.length);
  });

  it.each(cases)('renders $matrix · $name from its state inputs', async (entry) => {
    const html = await entry.build();
    for (const expected of entry.expects) expect(html).toContain(expected);
    expect(html).not.toContain('undefined');
    expect(html).not.toContain('NaN');
  });

  it('writes a review page using the production components and stylesheet', async () => {
    const out = process.env.PREVIEW_OUT;
    if (!out) return;

    const { light, dark } = tokenVariables();
    const built = new Map<string, string>();
    for (const entry of cases) built.set(entry.id, await entry.build());

    const matrices = [...new Set(cases.map((item) => item.matrix))];
    const slug = (name: string) => name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
    const figure = (entry: VisualCase, mode: string, dock: number) => {
      const width = entry.frame === 'card' ? dock - THREAD_INSET : dock;
      const inner = entry.frame === 'pane'
        ? `<div class="stage ${mode}" style="width:${width}px">${built.get(entry.id)}</div>`
        : `<div class="${entry.frame === 'block' ? 'block' : 'specimen'} ${mode} ${entry.within ?? ''}" style="width:${width}px">${built.get(entry.id)}</div>`;
      return `<figure data-case="${entry.id}" data-mode="${mode}" data-dock="${dock}">
  <figcaption>${escapeHtml(entry.name)}</figcaption>${inner}</figure>`;
    };
    const sections = matrices.map((matrix) => {
      const members = cases.filter((item) => item.matrix === matrix);
      const groups = (['light', 'dark'] as const).flatMap((mode) => DOCK_WIDTHS.map((dock) =>
        `<h3 id="${slug(matrix)}-${mode}-${dock}">${mode} · ${dock} DIP dock</h3>
<div class="grid">${members.map((entry) => figure(entry, mode, dock)).join('\n')}</div>`)).join('\n');
      return `<section id="${slug(matrix)}"><h2>${escapeHtml(matrix)} · Figma ${members[0].node}</h2>${groups}</section>`;
    }).join('\n');
    const nav = matrices.map((matrix) => `<a href="#${slug(matrix)}">${escapeHtml(matrix)}</a>`).join('');

    writeFileSync(out, `<!doctype html><html lang="en"><meta charset="utf-8">
<title>Agent UI state matrices</title>
<style>
:root { ${light} }
${productionCss()}
.dark { ${dark} }
html, body { height: auto; }
body { margin:0; padding:16px; background:var(--surface-subtle); color:var(--text-primary); font:var(--font-body); }
nav { display:flex; flex-wrap:wrap; gap:12px; margin-bottom:20px; font:var(--font-label); }
nav a { color:var(--action-primary); }
h1 { font:var(--font-section); margin:0 0 12px; }
h2 { font:var(--font-body-bold); margin:32px 0 4px; }
h3 { font:var(--font-label-bold); margin:16px 0 8px; color:var(--text-secondary); }
.grid { display:flex; flex-wrap:wrap; gap:16px; align-items:flex-start; }
figure { margin:0; }
[hidden] { display:none !important; }
figcaption { font:var(--font-label); color:var(--text-secondary); margin-bottom:8px; max-width:429px; }
/* A thread item sits on the thread's canvas, in the thread's column. */
.specimen { box-sizing:content-box; display:flex; flex-direction:column; gap:8px; padding:16px;
  background:var(--surface-canvas); color:var(--text-primary); }
/* A block spans the dock, as the composer does. */
.block { background:var(--surface-canvas); color:var(--text-primary); }
/* The thread's own column, without its scroll box. */
.specimen.thread .message-list { flex:none; padding:0; overflow:visible; }
.stage { box-sizing:content-box; position:relative; height:640px; overflow:hidden; border:1px solid var(--border-subtle);
  background:var(--surface-subtle); color:var(--text-primary); }
.stage > .app, .stage > div > .app { height:100%; }
[data-visual-focus] { outline:2px solid var(--border-focus); outline-offset:2px; }
</style>
<body><h1>Agent UI state matrices</h1><nav>${nav}</nav>${sections}
<script>
// ?show=<case>,<case>&mode=light&dock=429 narrows the page to those states,
// for comparing one of them with its Figma node.
const query = new URLSearchParams(location.search);
if (query.get('show')) {
  const ids = query.get('show').split(','), mode = query.get('mode') || 'light', dock = query.get('dock') || '429';
  document.querySelectorAll('figure').forEach((figure) => {
    figure.hidden = !(ids.includes(figure.dataset.case) && figure.dataset.mode === mode && figure.dataset.dock === dock);
  });
  document.querySelectorAll('body > h1, body > nav, section > h2, section > h3').forEach((element) => { element.hidden = true; });
  if (query.get('height')) document.querySelectorAll('.stage').forEach((stage) => { stage.style.height = query.get('height') + 'px'; });
}
</script>
</body></html>`);
  });
});
