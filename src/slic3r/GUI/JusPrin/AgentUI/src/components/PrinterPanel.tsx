// The printer panel's cards: the pictures the thread draws, and the one card
// that asks for something -- the credential printer_connect needs, typed here
// so it never passes through the conversation. Everything else the person
// does in this panel is a message.

import { memo, useState } from 'react';
import type { NetworkPrinterInfo, PrinterBlock, PrinterCardInfo, ToolActivityInfo } from '../bridge/protocol';

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

// One card in the thread, under the message it belongs to.
export const PrinterBlockView = memo(function PrinterBlockView({ block }: { block: PrinterBlock }) {
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

  if (block.kind === 'network')
    return (
      <div className="printer-found">
        <p className="printer-found-caption">FOUND ON YOUR NETWORK</p>
        {((block.printers ?? []) as NetworkPrinterInfo[]).map((printer) => (
          <div className="printer-row" key={printer.serial}>
            <span className={printer.online ? 'printer-dot printer-dot-online' : 'printer-dot'} aria-hidden="true" />
            <span className="printer-row-name">
              {printer.name}
              <small>{printer.serial}</small>
            </span>
          </div>
        ))}
      </div>
    );

  const printers = (block.printers ?? []) as PrinterCardInfo[];
  return (
    <div className={printers.length > 1 ? 'printer-cards printer-cards-compact' : 'printer-cards'}>
      {printers.map((printer) => (
        <div className="printer-card" key={printer.catalogId}>
          {printer.picture ? (
            <img className="printer-card-picture" src={printer.picture} alt="" />
          ) : (
            <span className="printer-card-picture printer-card-picture-empty" aria-hidden="true" />
          )}
          <span className="printer-card-name">
            {printer.name}
            <small>{printer.buildVolume}</small>
          </span>
        </div>
      ))}
    </div>
  );
});

// printer_connect's card. Connect approves the call and hands the app what
// was typed; the model hears only how the connection went.
export function PrinterCredentialCard({
  activity,
  onDecision,
}: {
  activity: ToolActivityInfo;
  onDecision: (actionId: string, decision: 'approve' | 'reject', input?: { credential: string }) => void;
}) {
  const [credential, setCredential] = useState('');
  const bambu = activity.arguments.provider === 'bambu';
  if (activity.state !== 'pending')
    return (
      <div className="tool-card printer-credential" data-testid={`tool-${activity.actionId}`}>
        <div className="tool-title">{activity.title}</div>
        <div className="tool-state">{activity.state === 'rejected' ? 'Cancelled' : 'Sent'}</div>
      </div>
    );
  return (
    <form
      className="tool-card printer-credential"
      data-testid={`tool-${activity.actionId}`}
      onSubmit={(event) => {
        event.preventDefault();
        onDecision(activity.actionId, 'approve', { credential });
      }}
    >
      <div className="tool-title">{activity.title}</div>
      <label className="printer-credential-field">
        <span>{bambu ? 'Access code' : 'API key (if the printer asks for one)'}</span>
        <input
          type="password"
          className="printer-credential-input"
          autoComplete="off"
          value={credential}
          onChange={(event) => setCredential(event.target.value)}
        />
      </label>
      <p className="printer-credential-note">Stays on this computer.</p>
      <div className="tool-actions">
        {/* Enabled while empty: a printer that already has its code needs none. */}
        <button type="submit" className="primary">
          Connect
        </button>
        <button type="button" onClick={() => onDecision(activity.actionId, 'reject')}>
          Cancel
        </button>
      </div>
    </form>
  );
}
