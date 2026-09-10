// Not an assertion suite: the sibling of SetupCard.preview.test.tsx, for the
// surfaces that carry a conversation. It renders the real MessageList,
// Composer and ManufacturingHistoryCard at the dock's real width and writes a
// page a human (or a screenshot) can look at.
//
// It exists because the token guards in styles.test.ts prove the stylesheet
// went through the design tokens, not that the result is legible -- and the
// running app is not always reachable (a disconnected RDP session on the
// Windows verification box kills both screen capture and input).
//
// Run with PREVIEW_OUT=/some/path/chat-panel.html npx vitest run ChatPanel.preview

import { describe, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { MessageList } from './MessageList';
import { Composer } from './Composer';
import { applyStaticTokens } from '../tokens';
import { Message } from '../state/store';
import { BuildInfo, ExportedCopyInfo, PhysicalPrintInfo } from '../bridge/protocol';
import tokens from '../../../../../../../resources/jusprin/ui/design-tokens.json';

const message = (id: string, role: Message['role'], text: string): Message =>
  ({ id, role, state: 'complete', text, attempt: 1, lastSeq: 0 });

const turns: Message[] = [
  message('m1', 'user', 'This will hold a backpack. Make it strong, but keep the screw holes accurate.'),
  message('m2', 'assistant', 'I laid it on its side so the layers run through the hook, and thickened the walls around both screw holes.'),
  message('m3', 'user', 'Will it need supports?'),
  message('m4', 'assistant', "Only beneath the hook tip. They won't touch the wall-facing surface."),
];

const hash = 'a71f8c04'.repeat(8);
const statistics = { printTimeSeconds: 9360, filamentMm: 1842.5, materialGrams: 68, materialCost: 1.12, layerCount: 181 };

const build = (id: string, stale: boolean): BuildInfo => ({
  id, seq: 10, createdAt: '2026-08-30T13:04:00Z', projectId: 'project-1', revisionId: 'r-2',
  conversationId: 'conv-1', afterMessageId: 'm4', plateIndex: 0, plateName: 'Plate 1',
  printer: 'Bambu X1C 0.4', material: 'Generic PLA', manufacturingInputHash: hash, outputHash: hash,
  slicerVersion: 'JusPrin deterministic Phase 6', configurationProvenance: '0.20mm Standard @BBL X1C, 5 changes',
  statistics, warnings: stale ? ['A deterministic warning'] : [], stale,
});

const copy: ExportedCopyInfo = {
  id: 'e-1', seq: 11, createdAt: '2026-08-30T14:24:00Z', buildId: 'b-1', conversationId: 'conv-1',
  afterMessageId: 'm4', destination: '/Users/maker/Desktop/Prints/bracket-v3.gcode',
  expectedOutputHash: hash, observedOutputHash: hash, verified: true, modified: false,
};

const print: PhysicalPrintInfo = {
  id: 'p-1', seq: 12, startedAt: '2026-08-30T13:09:00Z', endedAt: '2026-08-30T14:13:00Z',
  outcome: 'failed', failure: 'Layer shift reported near layer 62.', buildId: 'b-1', projectId: 'project-1',
  revisionId: 'r-2', conversationId: 'conv-1', afterMessageId: 'm4', plateIndex: 0, plateName: 'Plate 1',
  printer: 'Bambu X1C 0.4', material: 'Generic PLA', manufacturingInputHash: hash, outputHash: hash,
  gcodeHash: hash, statistics, timelineRemoved: false,
};

const noop = () => {};

function thread(builds: BuildInfo[], copies: ExportedCopyInfo[], prints: PhysicalPrintInfo[]) {
  return renderToStaticMarkup(
    <MessageList
      messages={turns}
      attachments={[]}
      streamingMessageId={null}
      toolActivities={[]}
      revisions={[]}
      builds={builds}
      exportedCopies={copies}
      physicalPrints={prints}
      onRetry={noop}
      onToolDecision={noop}
      onToolCancel={noop}
      onRevert={noop}
    />,
  );
}

const composer = (streaming: boolean) =>
  renderToStaticMarkup(
    <Composer
      disabled={false}
      streaming={streaming}
      attachments={[]}
      onSend={noop}
      onStop={noop}
      onAttachFiles={noop}
      onRemoveAttachment={noop}
    />,
  );

// Built lazily: rendering the thread on the server logs a useLayoutEffect
// warning, and there is no reason to pay it on the runs that are not writing
// a preview -- which is every run but the one a human asked for.
const buildCases = (): { name: string; note: string; body: string }[] => [
  {
    name: 'conversation',
    note: 'user turn tinted and hugging; agent turn on canvas behind a 20px disc',
    body: thread([], [], []),
  },
  {
    name: 'with a build',
    note: 'the build rests as a summary; its provenance stays behind the disclosure',
    body: thread([build('b-1', false)], [], []),
  },
  {
    name: 'stale build, copy and failed print',
    note: 'every manufacturing record at resting altitude',
    body: thread([build('b-1', true)], [copy], [print]),
  },
];

describe('chat panel preview', () => {
  it('writes a page showing the thread and composer at dock width', () => {
    const out = process.env.PREVIEW_OUT;
    if (!out) return;

    const cases = buildCases();
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
    <header class="chat-header"><button class="chat-back chat-icon" aria-label="Back to chats"><svg viewBox="0 0 24 24"><path d="m15 5-8 7 8 7"/></svg></button><h1>First print</h1><button class="chat-icon" aria-label="New chat"><svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg></button><button class="chat-icon" aria-label="Chat actions"><svg viewBox="0 0 24 24"><circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/></svg></button></header>
    ${entry.body}
    ${composer(false)}
  </div></div></div>
</figure>`)
      .join('\n');

    writeFileSync(out, `<!doctype html><meta charset="utf-8"><title>Chat panel</title>
<style>
:root { ${varsFor('light')} ${staticVars} }
${css}
.dark { ${varsFor('dark')} }
body { background: #f2f2f2; font-family: system-ui, sans-serif; padding: 16px; margin: 0; }
.grid { display: flex; flex-wrap: wrap; gap: 16px; align-items: flex-start; }
figure { margin: 0; }
figcaption { font-size: 12px; margin-bottom: 6px; color: #333; max-width: 429px; }
/* The real dock, from the Figma frame: 429px. */
.dock { width: 429px; height: 620px; border: 1px solid #bbb; background: var(--surface-subtle); overflow: hidden; }
.dock.dark { color: var(--text-primary); }
.dock .app { height: 100%; }
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
