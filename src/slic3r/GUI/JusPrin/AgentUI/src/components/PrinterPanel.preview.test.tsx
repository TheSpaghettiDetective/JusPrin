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
import { PrinterChipRow, PrinterPinnedCard } from './PrinterPanel';
import { applyStaticTokens } from '../tokens';
import { Message } from '../state/store';
import type { AttachmentInfo, PrinterBlock, PrinterSessionPayload } from '../bridge/protocol';
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
    chipHint: '',
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

const card = (vendor: string, model: string, subline: string, action: 'add' | 'choose' = 'add'): PrinterBlock => ({
  id: `card-${vendor}-${model}`,
  seq: 3,
  afterMessageId: 'm2',
  kind: 'printers',
  live: true,
  printers: [{ catalogId: `${vendor}/${model}`, deviceId: '', vendor, model, subline, picture: '', action }],
});

const candidates: PrinterBlock = {
  id: 'candidates',
  seq: 3,
  afterMessageId: 'm2',
  kind: 'printers',
  live: true,
  printers: [
    { catalogId: 'v2', deviceId: '', vendor: 'Creality', model: 'Ender-3 V2', subline: '', picture: '', action: 'choose' },
    { catalogId: 's1', deviceId: '', vendor: 'Creality', model: 'Ender-3 S1', subline: '', picture: '', action: 'choose' },
  ],
};

const photo: AttachmentInfo = {
  id: 'a-1',
  name: 'IMG_2041.jpg',
  kind: 'image',
  state: 'staged',
  sizeBytes: 402_110,
} as AttachmentInfo;

function panel(state: PrinterSessionPayload, messages: Message[], attachments: AttachmentInfo[] = []) {
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
          toolActivities={[]}
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
        <PrinterChipRow chips={state.chips} hint={state.chipHint} disabled={false} onSay={noop} />
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
    note: 'the card, the assumptions line, Add this printer on the card itself',
    body: panel(
      session({
        facts: {
          printer: settled('Bambu Lab A1 mini'),
          nozzle: assumed('0.4 mm'),
          plate: assumed('Textured PEI'),
          filament: assumed('PLA'),
        },
        blocks: [card('Bambu Lab', 'A1 mini', '180 × 180 × 180 mm')],
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
        chips: [{ id: 'neither', label: 'Neither · not in the list', style: 'plain', say: 'Neither, it is not in the list' }],
        chipHint: 'or a photo of the front',
        placeholder: 'or say more: "it has a knob"',
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
        blocks: [card('Bambu Lab', 'A1 mini', 'read from the printer just now')],
        placeholder: 'access code, optional',
      }),
      [
        message('m2', 'note', '"Use this" · 01P00A3B'),
        message('m3', 'assistant',
          'Nothing to assume: it told me its nozzle, plate and spools. To keep it connected, enter its access code (Settings › Network on the printer).'),
      ],
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
        blocks: [card('Bambu Lab', 'A1 Combo', 'the A1 with the AMS lite beside it')],
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
    name: 'G · not on the list',
    note: 'the panel draws the way out itself, never only the agent\'s wording',
    body: panel(
      session({
        blocks: [
          {
            id: 'unsupported',
            seq: 3,
            afterMessageId: 'm2',
            kind: 'unsupported',
            reason: 'not_listed',
          },
        ],
        placeholder: 'e.g. "it\'s a voron 2.4, 350"',
      }),
      [
        message('m2', 'user', 'i built it myself, a voron with a btt octopus board'),
        message('m3', 'assistant', "I can't match that to a printer I ship a profile for."),
      ],
    ),
  },
  {
    name: 'H · not a filament printer',
    note: 'one sentence, no exit block: there is nothing to set up',
    body: panel(
      session({ placeholder: 'e.g. "I also have an ender 3"' }),
      [
        message('m2', 'user', 'my elegoo mars'),
        message('m3', 'assistant', "JusPrin slices for filament printers; resin printers like the Mars aren't something it can set up."),
      ],
    ),
  },
  {
    name: 'Change · a nozzle swapped',
    note: 'the changed fact is the only red thing in the card',
    body: panel(
      session({
        mode: 'change',
        caption: 'PRINTER',
        facts: {
          printer: settled('Bambu Lab A1 Combo'),
          nozzle: { value: '0.6 mm', provenance: 'changed' },
          plate: assumed('Textured PEI'),
          filament: settled('AMS · 4 slots'),
        },
        chips: [
          { id: 's1', label: 'Use 0.3 mm layers', style: 'suggested', say: 'Use 0.3 mm layers' },
          { id: 's2', label: 'Keep 0.2 mm', style: 'plain', say: 'Keep 0.2 mm' },
        ],
        chipHint: 'or just type',
        placeholder: 'e.g. "I swapped the plate" or "is it connected?"',
      }),
      [
        message('m1', 'assistant',
          'This is the A1 Combo. Tell me what changed on it, or ask anything about it: nozzle, plate, spools, connection. A photo of the part works too.'),
        message('m2', 'user', 'i put a 0.6 nozzle on it'),
        message('m3', 'assistant', 'Changed to 0.6 mm. With 0.6 you can also print thicker layers.'),
      ],
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
