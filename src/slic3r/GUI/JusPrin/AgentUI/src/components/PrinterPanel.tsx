// The printer panel's own parts: the card pinned above the thread, the cards
// the agent draws inside it, and the row of things to tap.
//
// Everything here renders host state. A tap either sends a typed printer
// action, which C++ carries out, or sends the chip's own words as the
// person's message -- never both, and never a change made on this side.

import { ChangeEvent, memo, useRef, useState } from 'react';
import type {
  AttachmentSource,
  NetworkPrinterInfo,
  PrinterBlock,
  PrinterBrowse,
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
  onAction: (action: 'network_pick' | 'candidate_pick' | 'add' | 'reject' | 'browse' | 'manual_setup', id: string) => void;
}

// Brand on its own small line, model bold beneath it: never one joined
// string, which is how "Bambulab Bambu Lab A1 mini" happened.
function CardName({ printer }: { printer: PrinterCardInfo }) {
  return (
    <span className="printer-card-name">
      <span className="printer-card-vendor">{printer.vendor}</span>
      <b>{printer.model}</b>
      {printer.subline && <small>{printer.subline}</small>}
    </span>
  );
}

function CardPicture({ printer }: { printer: PrinterCardInfo }) {
  return printer.picture ? (
    <img className="printer-card-picture" src={printer.picture} alt="" />
  ) : (
    <span className="printer-card-picture printer-card-picture-empty" aria-hidden="true" />
  );
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

  // Not a printer this app ships a profile for: the panel itself draws the
  // way out, so it is never lost in how the agent happened to phrase it.
  if (block.kind === 'unsupported')
    return (
      <div className="printer-exit">
        <button type="button" className="printer-exit-row" onClick={() => onAction('browse', '')}>
          <span>Browse the full list</span>
          <span aria-hidden="true">›</span>
        </button>
        <button type="button" className="printer-exit-row" onClick={() => onAction('manual_setup', '')}>
          <span>
            Set it up myself
            <small>for a printer you built, or one that isn't listed</small>
          </span>
          <span aria-hidden="true">›</span>
        </button>
      </div>
    );

  const printers = (block.printers ?? []) as PrinterCardInfo[];

  // Superseded by a newer answer, or the person's own "Not this one": no
  // button, no picture, one quiet line each. Only one live card at a time.
  if (block.live === false)
    return (
      <div className="printer-cards-collapsed">
        {printers.map((printer) => (
          <div className="printer-card-collapsed" key={printer.catalogId}>
            {/* The model name already carries the vendor's ("Bambu Lab X1
                Carbon"), so this stays one clean line instead of repeating
                it from the vendor field too. */}
            Not this one · {printer.model}
          </div>
        ))}
      </div>
    );

  // One printer, confidently: the card owns "Add this printer", bottom
  // right, with "Not this one" a quiet line beneath -- never a docked Add
  // at the panel's foot. Two or three: each carries its own "This one" and
  // nothing here is red, because nothing is settled yet.
  const single = printers.length === 1 && printers[0].action === 'add';

  if (single) {
    const printer = printers[0];
    return (
      <div className="printer-cards-wrap">
        <div className="printer-card printer-card-propose">
          <div className="printer-card-top">
            <CardPicture printer={printer} />
            <CardName printer={printer} />
          </div>
          <button type="button" className="printer-card-add" onClick={() => onAction('add', printer.catalogId)}>
            Add this printer
          </button>
        </div>
        <button type="button" className="printer-reject-line" onClick={() => onAction('reject', printer.catalogId)}>
          Not this one
        </button>
      </div>
    );
  }

  // Two or three, still a question: each keeps its own row and button, and
  // nothing here is red, because nothing is settled yet.
  return (
    <div className="printer-cards printer-cards-compact">
      {printers.map((printer) => (
        <div className="printer-card" key={printer.catalogId}>
          <CardPicture printer={printer} />
          <CardName printer={printer} />
          <button type="button" className="printer-quiet-button" onClick={() => onAction('candidate_pick', printer.catalogId)}>
            This one
          </button>
        </div>
      ))}
    </div>
  );
});

export interface ChipRowProps {
  chips: PrinterChip[];
  hint: string;
  disabled: boolean;
  onSay: (text: string) => void;
  // "Photo of the label" opens the same picker "Photo" does, rather than
  // sending its label as a message.
  onAttachFiles: (files: File[], source: AttachmentSource) => void;
}

// Specific actions, never a bare Yes or No, with the composer always beneath.
export const PrinterChipRow = memo(function PrinterChipRow({ chips, hint, disabled, onSay, onAttachFiles }: ChipRowProps) {
  const fileInput = useRef<HTMLInputElement>(null);
  if (chips.length === 0) return null;

  const handlePickerChange = (event: ChangeEvent<HTMLInputElement>) => {
    const files = Array.from(event.target.files ?? []);
    event.target.value = '';
    if (files.length > 0) onAttachFiles(files, 'picker');
  };

  return (
    <div className="printer-chips">
      {chips.map((chip) => (
        <button
          key={chip.id}
          type="button"
          className={`printer-chip printer-chip-${chip.style}`}
          disabled={disabled}
          onClick={() => (chip.opensPhotoPicker ? fileInput.current?.click() : onSay(chip.say))}
        >
          {chip.label}
        </button>
      ))}
      {hint && <span className="printer-chip-hint">{hint}</span>}
      <input
        ref={fileInput}
        type="file"
        multiple
        className="attach-input"
        aria-hidden="true"
        tabIndex={-1}
        style={{ display: 'none' }}
        onChange={handlePickerChange}
      />
    </div>
  );
});

export interface PrinterBrowseProps {
  browse: PrinterBrowse;
  onBack: () => void;
  onOpenVendor: (vendorId: string) => void;
  onPick: (catalogId: string) => void;
  onManualSetup: () => void;
}

// Brands, then one vendor's models, inside the panel: replaces the thread
// while it is open, the way a layer over it never quite reads as "still in
// this conversation."
export const PrinterBrowseView = memo(function PrinterBrowseView({
  browse,
  onBack,
  onOpenVendor,
  onPick,
  onManualSetup,
}: PrinterBrowseProps) {
  const [filter, setFilter] = useState('');
  if (!browse) return null;

  if (browse.level === 'vendors') {
    const query = filter.trim().toLowerCase();
    const rows = query ? browse.vendors.filter((vendor) => vendor.name.toLowerCase().includes(query)) : browse.vendors;
    return (
      <div className="printer-browse">
        <button type="button" className="printer-browse-back" onClick={onBack}>
          ‹ Back to chat
        </button>
        <input
          type="text"
          className="printer-browse-filter"
          placeholder="Filter brands"
          value={filter}
          onChange={(event) => setFilter(event.target.value)}
        />
        <div className="printer-browse-rows">
          {rows.map((vendor) => (
            <button key={vendor.id} type="button" className="printer-browse-row" onClick={() => onOpenVendor(vendor.id)}>
              <span>{vendor.name}</span>
              <small>{vendor.count}</small>
            </button>
          ))}
        </div>
      </div>
    );
  }

  return (
    <div className="printer-browse">
      <button type="button" className="printer-browse-back" onClick={onBack}>
        ‹ Brands
      </button>
      <p className="printer-browse-caption">
        {browse.vendorName} · {browse.models.length}
      </p>
      <div className="printer-browse-rows">
        {browse.models.map((model) => (
          <button
            key={model.catalogId}
            type="button"
            className="printer-browse-row printer-browse-model"
            onClick={() => onPick(model.catalogId)}
          >
            {model.picture ? (
              <img className="printer-browse-picture" src={model.picture} alt="" />
            ) : (
              <span className="printer-browse-picture printer-browse-picture-empty" aria-hidden="true" />
            )}
            <span>
              <b>{model.model}</b>
              <small>{model.subline}</small>
            </span>
          </button>
        ))}
        <button type="button" className="printer-browse-row printer-browse-manual" onClick={onManualSetup}>
          <span>
            Set it up myself
            <small>my printer isn't listed</small>
          </span>
        </button>
      </div>
    </div>
  );
});
