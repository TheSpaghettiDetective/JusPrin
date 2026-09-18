// Not an assertion suite: the sibling of ChatPanel.preview.test.tsx, for the
// printer panel. It renders every state of the Add flow and the Change flow
// at the printers column's real width and writes a page to compare against
// the wireframes (turn 19b and 19b-add), in both appearances.
//
// Run with PREVIEW_OUT=/some/path/printer-panel.html npx vitest run PrinterPanel.preview

import { describe, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { Composer } from './Composer';
import { MessageList } from './MessageList';
import { PrinterAccessCode, PrinterChangeCard, PrinterChipRow, PrinterPinnedCard } from './PrinterPanel';
import { applyStaticTokens } from '../tokens';
import { Message } from '../state/store';
import type { AttachmentInfo, PrinterBlock, PrinterChip, PrinterSessionPayload, ToolActivityInfo } from '../bridge/protocol';
import tokens from '../../../../../../../resources/jusprin/ui/design-tokens.json';

const noop = () => {};

const message = (id: string, role: Message['role'], text: string, attachments?: string[]): Message =>
  ({ id, role, state: 'complete', text, attempt: 1, lastSeq: 0, attachments } as Message);

const empty = { value: '', provenance: 'settled' as const };
const assumed = (value: string) => ({ value, provenance: 'assumed' as const });
const settled = (value: string, swatch?: string) => ({ value, provenance: 'settled' as const, swatch });

function session(overrides: Partial<PrinterSessionPayload>): PrinterSessionPayload {
  return {
    mode: 'add',
    caption: 'NEW PRINTER',
    facts: { printer: empty, nozzle: empty, plate: empty, filament: empty },
    blocks: [],
    chips: [],
    placeholder: 'e.g. "bambu a1 mini" or "not sure, the small one"',
    ...overrides,
  };
}

const tip: PrinterBlock = { id: 'tip', seq: 1, afterMessageId: 'm1', kind: 'tip' };

const network: PrinterBlock = {
  id: 'net',
  seq: 2,
  afterMessageId: 'm1',
  kind: 'network',
  printers: [{ deviceId: '01P00A3B', name: 'Bambu Lab A1 mini', serial: '01P00A3B', online: true }],
};

const card = (name: string, subline: string, action: 'add' | 'choose' = 'add'): PrinterBlock => ({
  id: `card-${name}`,
  seq: 3,
  afterMessageId: 'm2',
  kind: 'printers',
  printers: [{ catalogId: name, deviceId: '', name, subline, picture: '', action }],
});

const candidates: PrinterBlock = {
  id: 'candidates',
  seq: 3,
  afterMessageId: 'm2',
  kind: 'printers',
  printers: [
    { catalogId: 'v2', deviceId: '', name: 'Ender-3 V2', subline: '', picture: '', action: 'choose' },
    { catalogId: 's1', deviceId: '', name: 'Ender-3 S1', subline: '', picture: '', action: 'choose' },
  ],
};

const addChips: PrinterChip[] = [
  { id: 'add', label: 'Add this printer', style: 'primary', action: 'add' },
  { id: 'reject', label: 'Not this one', style: 'plain', action: 'reject' },
];

const photo: AttachmentInfo = {
  id: 'a-1',
  name: 'IMG_2041.jpg',
  kind: 'image',
  state: 'staged',
  sizeBytes: 402_110,
} as AttachmentInfo;

function panel(
  state: PrinterSessionPayload,
  messages: Message[],
  attachments: AttachmentInfo[] = [],
  activities: ToolActivityInfo[] = [],
) {
  return renderToStaticMarkup(
    <div className="app app--printer">
      <div className="chat-content">
        <header className="chat-header printer-header">
          <button type="button" className="icon-button" aria-label="Back to printers">
            ‹
          </button>
          <h1>Printers</h1>
          <button type="button" className="printer-manual-link">
            Set it up myself
          </button>
        </header>
        <div className="pinned-setup">
          <PrinterPinnedCard session={state} />
        </div>
        <MessageList
          messages={messages}
          attachments={attachments}
          streamingMessageId={null}
          toolActivities={activities}
          renderActivity={(activity) => <PrinterChangeCard activity={activity} onDecision={noop} />}
          builds={[]}
          exportedCopies={[]}
          physicalPrints={[]}
          changes={[]}
          printerBlocks={state.blocks}
          onPrinterAction={noop}
          answeredState={false}
          onRetry={noop}
          onToolDecision={noop}
          onToolCancel={noop}
        />
        {state.accessCode && <PrinterAccessCode value="" onChange={noop} />}
        <PrinterChipRow chips={state.chips} disabled={false} onAdd={noop} onReject={noop} />
        <Composer
          disabled={false}
          placeholder={state.placeholder}
          photoButton
          streaming={false}
          attachments={attachments}
          onSend={noop}
          onStop={noop}
          onAttachFiles={noop}
          onRemoveAttachment={noop}
        />
      </div>
    </div>,
  );
}

function changeActivity(state: ToolActivityInfo['state']): ToolActivityInfo {
  return {
    actionId: 't-1',
    correlationId: 'm3',
    server: 'jusprin',
    tool: 'printer_change',
    title: 'Change nozzle',
    arguments: { nozzle: 0.6, confirm: { printer: 'Bambu Lab A1 mini', before: { nozzle: 0.4 } } },
    actionClass: 'mutation',
    requiresApproval: true,
    sessionId: '1',
    expectedRevision: 1,
    state,
    progress: { current: 0, total: 1 },
  };
}

const opener = message('m1', 'assistant',
  'What printer do you have? Say it any way: "bambu a1 mini", "the ender with the touchscreen", "not sure, the small one".');

const cases = () => [
  {
    name: 'A · opener',
    note: 'words, a photo, or a network find — all in the first message',
    body: panel(session({ blocks: [tip, network] }), [opener]),
  },
  {
    name: 'B · recognised from words',
    note: 'the card, the assumptions line, Add as the primary chip',
    body: panel(
      session({
        facts: {
          printer: settled('Bambu Lab A1 mini'),
          nozzle: assumed('0.4 mm'),
          plate: assumed('Textured PEI'),
          filament: assumed('PLA'),
        },
        blocks: [card('Bambu Lab A1 mini', '180 × 180 × 180 mm')],
        chips: addChips,
        placeholder: 'e.g. "I put a 0.6 nozzle on it"',
      }),
      [
        message('m2', 'user', 'the small bambu one'),
        message('m3', 'assistant',
          "This one? I'll assume a **0.4 mm nozzle**, the **textured PEI plate** it ships with, and **PLA**. If anything is different, say so here or in your first project."),
      ],
    ),
  },
  {
    name: 'C · ambiguous',
    note: 'what tells them apart, two cards, nothing settled',
    body: panel(
      session({
        blocks: [candidates],
      }),
      [
        message('m2', 'user', 'the ender with the touchscreen'),
        message('m3', 'assistant',
          'Two Ender 3s have a touchscreen. The V2 has a knob under the screen; the S1 has none and a direct-drive extruder on the head.'),
      ],
    ),
  },
  {
    name: 'D · after "Use this"',
    note: 'nothing assumed, the access code optional',
    body: panel(
      session({
        facts: {
          printer: settled('Bambu Lab A1 mini'),
          nozzle: settled('0.4 mm'),
          plate: settled('Textured PEI'),
          filament: settled('AMS lite · PLA Matte + 3', '#5f7d4f'),
        },
        blocks: [{ ...card('Bambu Lab A1 mini', '0.4 mm nozzle · AMS lite · PLA Matte + 3 · read from the printer just now'), afterMessageId: 'm2' }],
        chips: addChips,
        accessCode: true,
      }),
      [message('m2', 'note', 'The person chose the network printer 01P00A3B, a Bambu Lab A1 mini that reports a 0.4 mm nozzle.')],
    ),
  },
  {
    name: 'E · photo staged, not sent',
    note: 'the thumbnail waits inside the composer until it is sent',
    body: panel(session({ blocks: [tip] }), [opener], [photo]),
  },
  {
    name: 'F · recognised from a photo',
    note: 'the sent photo is the person’s turn; the card answers it',
    body: panel(
      session({
        facts: {
          printer: settled('Bambu Lab A1 Combo'),
          nozzle: assumed('0.4 mm'),
          plate: assumed('Textured PEI'),
          filament: settled('AMS lite · 4 slots'),
        },
        blocks: [card('Bambu Lab A1 Combo', 'the A1 with the AMS lite beside it')],
        chips: addChips,
        placeholder: 'e.g. "I put a 0.6 nozzle on it"',
      }),
      [
        message('m2', 'user', "it's this one", ['a-1']),
        message('m3', 'assistant',
          'From the photo: an A1 with the four-spool AMS lite next to it, so the Combo. I’ll assume a **0.4 mm nozzle** and the **textured PEI plate**; spools we’ll sort out in your first project.'),
      ],
      [{ ...photo, state: 'sent' } as AttachmentInfo],
    ),
  },
  {
    name: 'G · "Not this one"',
    note: 'the refused card folds to a line; the agent asks what next',
    body: panel(
      session({ blocks: [{ ...card('Bambu Lab A1 mini', '180 × 180 × 180 mm'), collapsed: true }] }),
      [
        message('m2', 'user', 'the small bambu one'),
        message('m3', 'assistant', "That's the Bambu Lab A1 mini. I'll assume the 0.4 mm nozzle, the Textured PEI plate and Bambu PLA Basic."),
        message('m4', 'note', 'The person said Bambu Lab A1 mini is not their printer.'),
        message('m5', 'assistant', 'Which one is it, then? The model name is on a sticker on the back.'),
      ],
    ),
  },
  {
    name: 'Change · confirming a nozzle',
    note: 'the change in words before anything is saved',
    body: panel(
      session({
        mode: 'change',
        caption: 'PRINTER',
        facts: {
          printer: settled('Bambu Lab A1 mini'),
          nozzle: settled('0.4 mm'),
          plate: assumed('Textured PEI Plate'),
          filament: settled('AMS lite · PLA Matte + 1', '#5f7d4f'),
        },
        placeholder: 'e.g. "I put a 0.6 nozzle on it" or "loaded black PETG"',
      }),
      [
        message('m1', 'assistant',
          'This is the Bambu Lab A1 mini. Tell me what changed on it, or ask anything about it: nozzle, plate, spools, connection. A photo of the part works too.'),
        message('m2', 'user', 'i put a 0.6 nozzle on it'),
        message('m3', 'assistant', ''),
      ],
      [],
      [changeActivity('pending')],
    ),
  },
  {
    name: 'Change · a nozzle swapped',
    note: 'the changed fact is the only red thing in the card; Undo is the app’s',
    body: panel(
      session({
        mode: 'change',
        caption: 'PRINTER',
        facts: {
          printer: settled('Bambu Lab A1 mini'),
          nozzle: { value: '0.6 mm', provenance: 'changed' },
          plate: assumed('Textured PEI Plate'),
          filament: settled('AMS lite · PLA Matte + 1', '#5f7d4f'),
        },
        blocks: [{ id: 'undo', seq: 1, afterMessageId: 'm3', kind: 'undo', text: 'Nozzle set to 0.6 mm' }],
        placeholder: 'e.g. "I put a 0.6 nozzle on it" or "loaded black PETG"',
      }),
      [
        message('m1', 'assistant',
          'This is the Bambu Lab A1 mini. Tell me what changed on it, or ask anything about it: nozzle, plate, spools, connection. A photo of the part works too.'),
        message('m2', 'user', 'i put a 0.6 nozzle on it'),
        message('m3', 'assistant', ''),
        message('m4', 'assistant', "Done — it's on the 0.6 mm nozzle now. You can print thicker layers with it."),
      ],
      [],
      [changeActivity('succeeded')],
    ),
  },
];

describe('printer panel preview', () => {
  it('writes a page showing every state at the printers column width', () => {
    const out = process.env.PREVIEW_OUT;
    if (!out) return;

    const css = readFileSync(resolve(__dirname, '../styles.css'), 'utf8');
    applyStaticTokens();
    const staticVars = document.documentElement.style.cssText;
    const semantic = (tokens as { semantic: Record<string, Record<string, Record<string, string>>> }).semantic;
    const varsFor = (mode: string) =>
      Object.entries(semantic[mode])
        .flatMap(([group, values]) => Object.entries(values).map(([name, value]) => `--${group}-${name}: ${value};`))
        .join('\n  ');

    const width = (tokens as { component: { printerCard: { columnWidth: number } } }).component.printerCard.columnWidth;
    const panels = (mode: string) =>
      cases()
        .map((entry) => `<figure>
  <figcaption><b>${entry.name}</b> — ${entry.note}</figcaption>
  <div class="column ${mode}">${entry.body}</div>
</figure>`)
        .join('\n');

    writeFileSync(out, `<!doctype html><meta charset="utf-8"><title>Printer panel</title>
<style>
:root { ${varsFor('light')} ${staticVars} }
${css}
.dark { ${varsFor('dark')} }
body { background: #f2f2f2; font-family: system-ui, sans-serif; padding: 16px; margin: 0; }
.grid { display: flex; flex-wrap: wrap; gap: 16px; align-items: flex-start; }
figure { margin: 0; }
figcaption { font-size: 12px; margin-bottom: 6px; color: #333; max-width: ${width}px; }
/* Home's printers column, which this panel takes over. */
.column { width: ${width}px; height: 620px; border: 1px solid #bbb; background: var(--surface-subtle); overflow: hidden; }
.column.dark { color: var(--text-primary); }
.column .app { height: 100%; }
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
