// The printer panel's choices, actions, and confirmation of a proposed change.
// Setup guidance lives in the conversation, without a pinned hardware summary.
//
// Everything here renders host state, in words made on this page
// (printerWords.ts). A tap sends a typed printer action or a tool decision,
// which C++ carries out -- never a change made on this side, and never words
// sent as the person's. A tap the model should know about carries its note,
// which the app records if the tap goes through.

import { memo } from 'react';
import type {
  NetworkPrinterInfo,
  PrinterBlock,
  PrinterCardInfo,
  PrinterChangeConfirm,
  PrinterSpoolInfo,
  ToolActivityInfo,
} from '../bridge/protocol';
import {
  ADD_LABEL,
  REJECT_LABEL,
  cardSubline,
  changeTitle,
  chosenNote,
  mm,
  networkNote,
  undoText,
  undoneNote,
} from '../printerWords';

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

export type PrinterBlockAction = 'network_pick' | 'candidate_pick' | 'undo';

// A tap on a card: the card's id, and the note the model is told if it goes
// through.
export interface PrinterTap {
  blockId?: string;
  note?: string;
}

export interface PrinterBlockProps {
  block: PrinterBlock;
  onAction: (action: PrinterBlockAction, id: string, tap: PrinterTap) => void;
}

// One card in the thread, under the message it belongs to.
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
            <button
              type="button"
              className="printer-quiet-button"
              onClick={() => onAction('network_pick', printer.deviceId, { note: networkNote(printer) })}
            >
              Use this
            </button>
          </div>
        ))}
      </div>
    );
  }

  if (block.kind === 'undo')
    return (
      <div className="printer-undo">
        <span>{undoText(block)}</span>
        <span aria-hidden="true"> · </span>
        <button type="button" className="printer-link-button" onClick={() => onAction('undo', block.id, { note: undoneNote(block) })}>
          Undo
        </button>
      </div>
    );

  const printers = (block.printers ?? []) as PrinterCardInfo[];
  // A card a newer answer replaced, or the person refused, stays in the
  // thread as a line, so the conversation still reads in order.
  if (block.collapsed)
    return <div className="printer-cards-collapsed">{printers.map((printer) => printer.name).join(' · ')}</div>;

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
            {cardSubline(printer) && <small>{cardSubline(printer)}</small>}
          </span>
          {compact && (
            <button
              type="button"
              className="printer-quiet-button"
              onClick={() => onAction('candidate_pick', printer.catalogId, { blockId: block.id, note: chosenNote(printer) })}
            >
              This one
            </button>
          )}
        </div>
      ))}
    </div>
  );
});

// The code a Bambu printer shows under Settings > Network. It goes with Add
// to the app, which connects the printer; it is never part of the chat.
export function PrinterAccessCode({ value, onChange }: { value: string; onChange: (value: string) => void }) {
  return (
    <input
      className="printer-access-code"
      type="text"
      autoComplete="off"
      spellCheck={false}
      maxLength={32}
      aria-label="Access code"
      placeholder="access code, optional"
      value={value}
      onChange={(event) => onChange(event.target.value)}
    />
  );
}

export interface ChipRowProps {
  canAdd: boolean;
  disabled: boolean;
  onAdd: () => void;
  onReject: () => void;
}

// What the person can do with the printer on the card. Both act natively;
// adding leads, because the tap is the approval.
export const PrinterChipRow = memo(function PrinterChipRow({ canAdd, disabled, onAdd, onReject }: ChipRowProps) {
  if (!canAdd) return null;
  return (
    <div className="printer-chips">
      <button type="button" className="printer-chip printer-chip-primary" disabled={disabled} onClick={onAdd}>
        {ADD_LABEL}
      </button>
      <button type="button" className="printer-chip printer-chip-plain" disabled={disabled} onClick={onReject}>
        {REJECT_LABEL}
      </button>
    </div>
  );
});

function SpoolList({ spools }: { spools: PrinterSpoolInfo[] }) {
  if (spools.length === 0) return <span className="printer-change-none">none</span>;
  return (
    <ul className="printer-change-spools">
      {spools.map((spool, index) => (
        <li key={`${spool.name}-${index}`}>
          {spool.colour && <span className="printer-swatch" style={{ background: spool.colour }} aria-hidden="true" />}
          {spool.name}
          {spool.material && spool.material !== spool.name && <small> {spool.material}</small>}
        </li>
      ))}
    </ul>
  );
}

// A change the model worked out from what the person said, stated in words
// before anything is saved: the card is where a misreading is caught.
export function PrinterChangeCard({
  activity,
  onDecision,
}: {
  activity: ToolActivityInfo;
  onDecision: (actionId: string, decision: 'approve' | 'reject') => void;
}) {
  const args = activity.arguments as { nozzle?: number; spools?: PrinterSpoolInfo[]; confirm?: PrinterChangeConfirm };
  const confirm = args.confirm;
  const printer = confirm?.printer ?? 'this printer';
  const nozzle = args.nozzle !== undefined && confirm?.before.nozzle !== undefined;
  const spools = args.spools !== undefined && confirm?.before.spools !== undefined;
  const keep = nozzle && !spools ? `Keep ${mm(confirm!.before.nozzle!)}` : 'Keep as it is';
  const set = nozzle && !spools ? `Set ${mm(args.nozzle!)}` : spools && !nozzle ? 'Set these spools' : 'Set both';

  return (
    <div className={`printer-change state-${activity.state}`} data-testid={`tool-${activity.actionId}`}>
      <div className="printer-change-title">{changeTitle(nozzle, spools)}</div>
      {nozzle && (
        <>
          <div className="printer-change-line">
            {mm(confirm!.before.nozzle!)} → <b>{mm(args.nozzle!)}</b> on {printer}
          </div>
          <div className="printer-change-note">Every project that uses this printer slices for {mm(args.nozzle!)}.</div>
        </>
      )}
      {spools && (
        <div className="printer-change-line printer-change-spool-diff">
          <div>
            <small>Now on {printer}</small>
            <SpoolList spools={confirm!.before.spools!} />
          </div>
          <span aria-hidden="true">→</span>
          <div>
            <small>After</small>
            <SpoolList spools={args.spools!} />
          </div>
        </div>
      )}
      {activity.state === 'pending' && (
        <div className="printer-change-actions">
          <button type="button" className="printer-chip printer-chip-plain" onClick={() => onDecision(activity.actionId, 'reject')}>
            {keep}
          </button>
          <button
            type="button"
            className="printer-chip printer-chip-primary"
            onClick={() => onDecision(activity.actionId, 'approve')}
          >
            {set}
          </button>
        </div>
      )}
      {activity.state === 'rejected' && <div className="printer-change-state">Kept as it was</div>}
      {(activity.state === 'approved' || activity.state === 'running') && <div className="printer-change-state">Saving…</div>}
      {activity.state === 'failed' && <div className="tool-error">{activity.error?.message ?? 'The change was not saved.'}</div>}
    </div>
  );
}
