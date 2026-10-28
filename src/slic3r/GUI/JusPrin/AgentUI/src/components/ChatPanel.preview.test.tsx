// Not an assertion suite: the sibling of SetupCard.preview.test.tsx, for the
// surfaces that carry a conversation. It renders the real MessageList,
// Composer and GcodeMarker at the dock's real width and writes a
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
import { applyAppearance, applyStaticTokens } from '../tokens';
import { Message } from '../state/store';
import { ChangeInfo, GcodeMarkerInfo, ToolActivityInfo } from '../bridge/protocol';
import { yesTurnActivities, yesTurnChanges, yesTurnMessages } from '../visual/yes-turn';

const message = (id: string, role: Message['role'], text: string): Message =>
  ({ id, role, state: 'complete', text, attempt: 1, lastSeq: 0 });

const turns: Message[] = [
  message('m1', 'user', 'This will hold a backpack. Make it strong, but keep the screw holes accurate.'),
  message('m2', 'assistant', 'I laid it on its side so the layers run through the hook, and thickened the walls around both screw holes.'),
  message('m3', 'user', 'Will it need supports?'),
  message('m4', 'assistant', "Only beneath the hook tip. They won't touch the wall-facing surface."),
];

const marker: GcodeMarkerInfo = {
  id: 'd-1', seq: 12, kind: 'send', at: '2026-08-30T13:06:00Z', conversationId: 'conv-1', afterId: 'm4',
  hash: 'a71f8c04'.repeat(8), plateIndex: 0, plateName: 'Plate 1', fileName: '', printerName: 'Bambu X1C',
};

const noop = () => {};

function thread(markers: GcodeMarkerInfo[], extra: {
  messages?: Message[]; activities?: ToolActivityInfo[]; changes?: ChangeInfo[]; streamingMessageId?: string;
} = {}) {
  return renderToStaticMarkup(
    <MessageList
      messages={extra.messages ?? turns}
      attachments={[]}
      streamingMessageId={extra.streamingMessageId ?? null}
      toolActivities={extra.activities ?? []}
      gcodeMarkers={markers}
      changes={extra.changes ?? []}
      restorePoints={[{ changeSeq: 42, versionId: 'v-42' }]}
      onRevert={noop}
      onRetry={noop}
      onToolCancel={noop}
      onSend={noop}
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
const yesTurn: Message[] = yesTurnMessages.map((message) => ({ ...message, lastSeq: 0 }));

const buildCases = (): { name: string; note: string; body: string; tall?: boolean }[] => [
  {
    name: 'a turn of seventeen tool calls',
    note: 'the 2026-10-20 "yes" turn: one line for the work, the reply, the turn’s changes as one block',
    body: thread([], { messages: yesTurn, activities: yesTurnActivities, changes: yesTurnChanges }),
    tall: true,
  },
  {
    name: 'the same turn still running',
    note: 'the line names the action in flight; nothing else is drawn for the work',
    body: thread([], {
      messages: yesTurn.slice(0, 5), streamingMessageId: 'm-6',
      activities: yesTurnActivities.slice(0, 3).map((activity, index) => index === 2 ? { ...activity, state: 'running' as const, progress: { current: 0, total: 0 } } : activity),
    }),
  },
  {
    name: 'conversation',
    note: 'user turn tinted and hugging; agent turn on canvas behind a 20px disc',
    body: thread([]),
  },
  {
    name: 'g-code marker',
    note: 'the one line where a G-code left the project',
    body: thread([marker]),
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
    applyAppearance('light');
    const lightVars = document.documentElement.style.cssText;
    applyAppearance('dark');
    const darkVars = document.documentElement.style.cssText;

    const panels = (mode: string) => cases
      .map((entry) => `<figure>
  <figcaption><b>${entry.name}</b> — ${entry.note}</figcaption>
  <div class="dock ${mode}${entry.tall ? ' tall' : ''}"><div class="app"><div class="chat-content">
    <header class="chat-header"><button class="chat-back chat-icon" aria-label="Back to chats"><span class="jp-icon jp-icon-chevron-left" aria-hidden="true"></span></button><h1>First print</h1><button class="chat-icon" aria-label="New chat"><span class="jp-icon jp-icon-plus" aria-hidden="true"></span></button><button class="chat-icon" aria-label="Chat actions"><span class="jp-icon jp-icon-ellipsis" aria-hidden="true"></span></button></header>
    ${entry.body}
    ${composer(false)}
  </div></div></div>
</figure>`)
      .join('\n');

    writeFileSync(out, `<!doctype html><meta charset="utf-8"><title>Chat panel</title>
<style>
:root { ${lightVars} }
${css}
.dark { ${darkVars} }
body { background: #f2f2f2; font-family: system-ui, sans-serif; padding: 16px; margin: 0; }
.grid { display: flex; flex-wrap: wrap; gap: 16px; align-items: flex-start; }
figure { margin: 0; }
figcaption { font-size: 12px; margin-bottom: 6px; color: #333; max-width: 429px; }
/* The real dock, from the Figma frame: 429px. */
.dock { width: 429px; height: 620px; border: 1px solid #bbb; background: var(--surface-subtle); overflow: hidden; }
.dock.dark { color: var(--text-primary); }
.dock.tall { height: auto; }
.dock.tall .message-list { overflow: visible; }
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
