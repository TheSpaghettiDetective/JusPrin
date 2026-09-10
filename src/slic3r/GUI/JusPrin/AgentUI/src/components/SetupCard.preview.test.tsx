// Not an assertion suite: this renders the real card in every state of the
// fallback ladder and writes a page a human (or a screenshot) can look at.
// Green unit tests say the right strings are present; they say nothing about
// whether the thing is legible at the width it actually gets.
//
// Run with PREVIEW_OUT=/some/path/setup-card.html npx vitest run SetupCard.preview

import { describe, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { SetupCard } from './SetupCard';
import { applyStaticTokens } from '../tokens';
import { EstimateStatus, PresetDeltaInfo, SliceEstimateInfo, WorkspaceContext } from '../bridge/protocol';
import tokens from '../../../../../../../resources/jusprin/ui/design-tokens.json';

function context(o: {
  setupIntent?: string;
  preset?: string;
  estimate?: SliceEstimateInfo | null;
  deltas?: PresetDeltaInfo[];
  status?: EstimateStatus;
  invalidatedBy?: string;
} = {}): WorkspaceContext {
  return {
    sessionId: '1',
    revision: 1,
    projectName: 'Bracket',
    projectDirty: false,
    printer: { preset: 'MyKlipper 0.2 nozzle', filament: 'Generic PLA', process: o.preset ?? '0.20 mm Standard' },
    plates: [{ id: '1', name: 'Plate 1', active: true, sliced: o.estimate != null, estimate: o.estimate ?? null, estimateStatus: o.status ?? 'current', invalidatedBy: o.invalidatedBy ?? '', objects: [] }],
    selection: { status: 'none', objectIds: [] },
    history: { canUndo: false, canRedo: false },
    presetDeltas: o.deltas ?? [],
    currency: 'USD',
    setupIntent: o.setupIntent ?? '',
  };
}

const sliced: SliceEstimateInfo = { printTimeSeconds: 13800, materialGrams: 47, materialCost: null };
const delta = (key: string, label: string, from: string, to: string, origin: 'agent' | 'user' = 'agent') =>
  ({ key, label, preset: from, value: to, origin });
const five = [
  delta('wall_loops', 'Wall loops', '2', '4'),
  delta('sparse_infill_density', 'Sparse infill density', '15%', '45%'),
  delta('enable_support', 'Enable support', '0', '1'),
  delta('top_shell_layers', 'Top shell layers', '4', '6'),
  delta('layer_height', 'Layer height', '0.2', '0.16'),
];

const cases: { name: string; note: string; context: WorkspaceContext; expanded?: boolean }[] = [
  { name: '1b resting', note: 'kicker + title + facts', context: context({ setupIntent: "Strong — it'll bear weight", estimate: sliced, deltas: five }) },
  { name: '1c expanded', note: 'deltas as a layer over the thread', expanded: true, context: context({ setupIntent: "Strong — it'll bear weight", estimate: sliced, deltas: five }) },
  { name: '2e hand-edited', note: 'yours counted apart from the total', context: context({ setupIntent: "Strong — it'll bear weight", estimate: sliced, deltas: [...five, delta('brim_width', 'Brim width', '0', '5', 'user'), delta('spiral_mode', 'Spiral vase', '0', '1', 'user')] }) },
  { name: '2e opened', note: 'which two were yours', expanded: true, context: context({ setupIntent: "Strong — it'll bear weight", estimate: sliced, deltas: [...five, delta('brim_width', 'Brim width', '0', '5', 'user'), delta('spiral_mode', 'Spiral vase', '0', '1', 'user')] }) },
  { name: '3a title at ceiling', note: '40 characters, must not wrap', context: context({ setupIntent: 'Strong, smooth top, no marks on the face', estimate: sliced, deltas: five }) },
  { name: '3a overlong title', note: 'past the ceiling: clamps, never wraps', context: context({ setupIntent: 'Strong enough to bear real weight without any sagging on the long overhang', estimate: sliced, deltas: five }) },
  { name: '3b no title', note: 'preset takes the identity slot', context: context({ estimate: sliced, deltas: five }) },
  { name: '3c priced', note: 'money joins as a clause', context: context({ setupIntent: 'Cheap and quick', estimate: { ...sliced, materialCost: 1.12 }, deltas: five }) },
  { name: '3d never sliced', note: 'no cost row, says so instead', context: context({ setupIntent: "Strong — it'll bear weight", deltas: five }) },
  { name: '3e many objects', note: 'count, not concatenated intents', context: context({ setupIntent: '3 objects, different settings', estimate: sliced, deltas: [...five, ...five, delta('brim_width', 'Brim width', '0', '5')] }) },
  { name: '3f everything missing', note: 'a label, not a card', context: context() },
];

describe('setup card preview', () => {
  it('writes a page showing every state at dock width', () => {
    const out = process.env.PREVIEW_OUT;
    if (!out) return;

    const css = readFileSync(resolve(__dirname, '../styles.css'), 'utf8');
    // The type, radius and padding variables, from the same code the page runs
    // at startup, so the preview shows the real fonts rather than a fallback.
    applyStaticTokens();
    const staticVars = document.documentElement.style.cssText;
    const semantic = (tokens as { semantic: Record<string, Record<string, Record<string, string>>> }).semantic;
    const varsFor = (mode: string) =>
      Object.entries(semantic[mode])
        .flatMap(([group, values]) => Object.entries(values).map(([name, value]) => `--${group}-${name}: ${value};`))
        .join('\n  ');

    const panels = (mode: string) => cases
      .map((entry) => `<figure>
  <figcaption><b>${entry.name}</b> — ${entry.note}</figcaption>
  <div class="dock ${mode}"><div class="app"><div class="chat-content">
    <div class="pinned-setup">${renderToStaticMarkup(<SetupCard context={entry.context} expanded={entry.expanded ?? false} onToggle={() => {}} />)}</div>
    <div class="thread${entry.expanded ? ' thread-dimmed' : ''}">the conversation lives here, and every pixel the card takes comes out of it</div>
  </div></div></div>
</figure>`)
      .join('\n');

    writeFileSync(out, `<!doctype html><meta charset="utf-8"><title>Setup card states</title>
<style>
:root { ${varsFor('light')} ${staticVars} }
${css}
.dark { ${varsFor('dark')} }
body { background: #f2f2f2; font-family: system-ui, sans-serif; padding: 16px; margin: 0; }
.grid { display: flex; flex-wrap: wrap; gap: 16px; align-items: flex-start; }
figure { margin: 0; }
figcaption { font-size: 12px; margin-bottom: 6px; color: #333; max-width: 429px; }
/* The real dock, from the Figma frame: 429px. */
.dock { width: 429px; height: 240px; border: 1px solid #bbb; background: var(--surface-subtle); overflow: hidden; }
.thread { padding: 10px 12px; font-size: 13px; color: var(--text-secondary); }
.dock.dark { color: var(--text-primary); }
h2 { font: 600 14px system-ui, sans-serif; margin: 20px 0 10px; }
</style>
<h2>Light</h2>
<div class="grid">
${panels('light')}
</div>
<h2>Dark</h2>
<div class="grid">
${panels('dark')}
</div>
`);
  });
});
