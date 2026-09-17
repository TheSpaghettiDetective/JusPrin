// The printer panel's own parts: the card pinned above the thread, the cards
// the agent draws inside it, and the row of things to tap.
//
// Everything here renders host state. A tap either sends a typed printer
// action, which C++ carries out, or sends the chip's own words as the
// person's message -- never both, and never a change made on this side.

import { memo } from 'react';
import type {
  NetworkPrinterInfo,
  PrinterBlock,
  PrinterCardInfo,
  PrinterChip,
  PrinterFact,
  PrinterSessionPayload,
} from '../bridge/protocol';

const EM_DASH = '—';

function factClass(fact: PrinterFact): string {
  if (!fact.value) return 'printer-fact-value printer-fact-empty';
  return `printer-fact-value printer-fact-${fact.provenance}`;
}

function factSuffix(fact: PrinterFact): string {
  if (!fact.value) return '';
  if (fact.provenance === 'assumed') return ' · assumed';
  if (fact.provenance === 'changed') return ' · changed';
  return '';
}

function FactRow({ label, fact }: { label: string; fact: PrinterFact }) {
  return (
    <div className="printer-fact">
      <span className="printer-fact-label">{label}</span>
      <span className={factClass(fact)}>
        {fact.swatch && <span className="printer-swatch" style={{ background: fact.swatch }} aria-hidden="true" />}
        {fact.value || EM_DASH}
        {factSuffix(fact)}
      </span>
    </div>
  );
}

// The four facts a printer is, in the order the panel always states them.
export const PrinterPinnedCard = memo(function PrinterPinnedCard({ session }: { session: PrinterSessionPayload }) {
  return (
    <section className="printer-pinned" aria-label={session.caption}>
      <p className="printer-pinned-caption">{session.caption}</p>
      <FactRow label="Printer" fact={session.facts.printer} />
      <FactRow label="Nozzle" fact={session.facts.nozzle} />
      <FactRow label="Plate" fact={session.facts.plate} />
      <FactRow label="Filament" fact={session.facts.filament} />
    </section>
  );
});

// The camera the panel shows wherever a photo is offered. Inline, like the
// composer's send arrow, because the icon set carries no camera yet.
export function CameraGlyph({ className }: { className: string }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" aria-hidden="true">
      <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" />
      <circle cx="12" cy="13" r="4" />
    </svg>
  );
}

export interface PrinterBlockProps {
  block: PrinterBlock;
  onAction: (action: 'network_pick' | 'candidate_pick' | 'add', id: string) => void;
}

// One card the agent drew, in the thread, under the message that drew it.
export const PrinterBlockView = memo(function PrinterBlockView({ block, onAction }: PrinterBlockProps) {
  if (block.kind === 'tip')
    return (
      <div className="printer-tip">
        <CameraGlyph className="printer-tip-icon" />
        <span>
          <b>Tip:</b> a photo is the fastest way. Use <b>Photo</b> below, or drop a picture of the printer, its nameplate or the box
          into this panel.
        </span>
      </div>
    );

  if (block.kind === 'network') {
    const found = (block.printers ?? []) as NetworkPrinterInfo[];
    return (
      <div className="printer-found">
        <p className="printer-found-caption">FOUND ON YOUR NETWORK</p>
        {found.map((printer) => (
          <div className="printer-row" key={printer.deviceId}>
            <span className={printer.online ? 'printer-dot printer-dot-online' : 'printer-dot'} aria-hidden="true" />
            <span className="printer-row-name">
              {printer.name}
              <small>{printer.serial}</small>
            </span>
            <button type="button" className="printer-quiet-button" onClick={() => onAction('network_pick', printer.deviceId)}>
              Use this
            </button>
          </div>
        ))}
      </div>
    );
  }

  const printers = (block.printers ?? []) as PrinterCardInfo[];
  // One printer is an answer and gets the full card; two or three are a
  // question, and each carries the button that answers it.
  const compact = printers.length > 1;
  return (
    <div className={compact ? 'printer-cards printer-cards-compact' : 'printer-cards'}>
      {printers.map((printer) => (
        <div className="printer-card" key={printer.catalogId}>
          {printer.picture ? (
            <img className="printer-card-picture" src={printer.picture} alt="" />
          ) : (
            <span className="printer-card-picture printer-card-picture-empty" aria-hidden="true" />
          )}
          <span className="printer-card-name">
            {printer.name}
            {printer.subline && <small>{printer.subline}</small>}
          </span>
          {compact && (
            <button type="button" className="printer-quiet-button" onClick={() => onAction('candidate_pick', printer.catalogId)}>
              This one
            </button>
          )}
        </div>
      ))}
    </div>
  );
});

export interface ChipRowProps {
  chips: PrinterChip[];
  hint: string;
  disabled: boolean;
  onAdd: () => void;
  onSay: (text: string) => void;
}

// Specific actions, never a bare Yes or No, with the composer always beneath.
export const PrinterChipRow = memo(function PrinterChipRow({ chips, hint, disabled, onAdd, onSay }: ChipRowProps) {
  if (chips.length === 0) return null;
  return (
    <div className="printer-chips">
      {chips.map((chip) => (
        <button
          key={chip.id}
          type="button"
          className={`printer-chip printer-chip-${chip.style}`}
          disabled={disabled}
          onClick={() => (chip.action === 'add' ? onAdd() : onSay(chip.say ?? chip.label))}
        >
          {chip.label}
        </button>
      ))}
      {hint && <span className="printer-chip-hint">{hint}</span>}
    </div>
  );
});
