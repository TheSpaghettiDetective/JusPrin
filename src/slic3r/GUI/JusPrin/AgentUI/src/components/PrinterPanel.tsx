// The printer panel's cards: the pictures the thread draws, and the one card
// that asks for something -- the credential printer_connect needs, typed here
// so it never passes through the conversation -- and Undo on an added
// printer's receipt. Everything else the person does in this panel is a
// message.

import { memo, useState } from 'react';
import type {
  NetworkPrinterInfo,
  PrinterBlock,
  PrinterCardInfo,
  PrinterConnectionInfo,
  ToolActivityInfo,
} from '../bridge/protocol';
import { numberText } from '../printerWords';

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
export const PrinterBlockView = memo(function PrinterBlockView({
  block,
  onUndoAdd,
  onInstallPlugin,
}: {
  block: PrinterBlock;
  onUndoAdd?: (blockId: string) => void;
  onInstallPlugin?: () => void;
}) {
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

  // The receipt for printer_add: the same words every time, from what the
  // app saved, whatever the model's reply around it says. Undo takes the
  // printer away again, and the receipt stays to say so.
  if (block.kind === 'added') {
    const printer = block.printer!;
    return (
      <div className={block.removed ? 'printer-added printer-added-removed' : 'printer-added'} role="status">
        <svg className="printer-added-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden="true">
          <circle cx="12" cy="12" r="10" />
          {!block.removed && <path d="m7.5 12.5 3 3 6-6.5" />}
        </svg>
        <span className="printer-added-text">
          <span className="printer-added-caption">{block.removed ? 'Printer removed' : 'Printer added'}</span>
          <span className="printer-added-name">
            {printer.name}
            {printer.nozzle > 0 && <small> · {numberText(printer.nozzle)} mm nozzle</small>}
          </span>
        </span>
        {!block.removed && onUndoAdd && (
          <button type="button" className="printer-link-button printer-added-undo" onClick={() => onUndoAdd(block.id)}>
            Undo
          </button>
        )}
      </div>
    );
  }

  // The app's own notice that nothing can reach a Bambu Lab printer without
  // Bambu's network plug-in: the same words every time, whatever the model
  // says around it. Install runs Orca's installer; once the plug-in is there
  // the notice says so and the button goes.
  if (block.kind === 'plugin')
    return (
      <div className={block.installed ? 'printer-plugin printer-plugin-installed' : 'printer-plugin'} role="status">
        <svg className="printer-plugin-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden="true">
          <circle cx="12" cy="12" r="10" />
          {block.installed ? <path d="m7.5 12.5 3 3 6-6.5" /> : <path d="M12 7v6M12 16.5v.5" />}
        </svg>
        <span className="printer-plugin-text">
          <span className="printer-plugin-caption">
            {block.installed ? 'Network plug-in installed' : 'Bambu network plug-in needed'}
          </span>
          <span className="printer-plugin-body">
            {block.installed
              ? 'Your Bambu Lab printer can be connected now.'
              : 'Connecting a Bambu Lab printer needs Bambu’s network plug-in. Your printer already works for preparing prints without it.'}
          </span>
        </span>
        {!block.installed && onInstallPlugin && (
          <button type="button" className="primary printer-plugin-install" onClick={() => onInstallPlugin()}>
            Install plug-in
          </button>
        )}
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
// was typed; the model hears only how the connection went. After Connect the
// card follows the attempt itself, as the app reports it in `connection`.
export function PrinterCredentialCard({
  activity,
  connection,
  onDecision,
  onCancelConnection,
}: {
  activity: ToolActivityInfo;
  connection?: PrinterConnectionInfo;
  onDecision: (actionId: string, decision: 'approve' | 'reject', input?: { credential: string }) => void;
  onCancelConnection: (actionId: string) => void;
}) {
  const [credential, setCredential] = useState('');
  const bambu = activity.arguments.provider === 'bambu';
  if (activity.state !== 'pending') {
    // Approved and about to start, or started: both are the wait.
    const state =
      activity.state === 'rejected' ? 'cancelled' : connection?.state ?? (activity.state === 'failed' ? 'failed' : 'connecting');
    const target = connection?.target;
    return (
      <div className="tool-card printer-credential" data-testid={`tool-${activity.actionId}`}>
        <div className="tool-title">{activity.title}</div>
        {state === 'connecting' && (
          <>
            <div className="tool-state">Connecting…</div>
            <div className="tool-actions">
              <progress aria-label="Connecting" />
              {connection && (
                <button type="button" onClick={() => onCancelConnection(activity.actionId)}>
                  Cancel
                </button>
              )}
            </div>
          </>
        )}
        {state === 'verified' && <div className="tool-state done">Connected</div>}
        {state === 'failed' && <div className="tool-error">{target ? `Couldn't reach ${target}` : "Couldn't connect"}</div>}
        {state === 'cancelled' && <div className="tool-state">Cancelled</div>}
      </div>
    );
  }
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
