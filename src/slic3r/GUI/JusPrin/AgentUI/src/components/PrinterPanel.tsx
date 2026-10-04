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

function PrinterGlyph({ className }: { className: string }) {
  return <span className={`${className} jp-icon jp-icon-printer`} aria-hidden="true" />;
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
        <span className={`printer-added-icon jp-icon jp-icon-${block.removed ? 'circle-minus' : 'circle-check'}`} aria-hidden="true" />
        <span className="printer-added-text">
          <span className="printer-added-caption">{block.removed ? 'Removed:' : 'Added:'} <span className="printer-added-name">{printer.name}</span></span>
          {!block.removed && printer.nozzle > 0 && <small>{numberText(printer.nozzle)} mm nozzle</small>}
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
        <span className={`printer-plugin-icon jp-icon jp-icon-${block.installed ? 'circle-check' : 'circle-alert'}`} aria-hidden="true" />
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
            <PrinterGlyph className="printer-row-icon" />
            <span className="printer-row-name">
              {printer.name}
              <small>{printer.serial}</small>
            </span>
            <span className="printer-row-status">
              <span className={printer.online ? 'printer-dot printer-dot-online' : 'printer-dot'} aria-hidden="true" />
              {printer.online ? 'Online' : 'Offline'}
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
            <span className="printer-card-picture printer-card-picture-empty" aria-hidden="true"><PrinterGlyph className="printer-card-glyph" /></span>
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
            <div className="tool-state"><span className="tool-status-icon running" aria-hidden="true" />Connecting…</div>
            <progress aria-label="Connecting" />
            {connection && <button type="button" className="tool-cancel" onClick={() => onCancelConnection(activity.actionId)}>Cancel</button>}
          </>
        )}
        {state === 'verified' && <div className="tool-state done"><span className="tool-status-icon done" aria-hidden="true" />Connected</div>}
        {state === 'failed' && <div className="tool-error"><span className="tool-status-icon failed" aria-hidden="true" />{target ? `Couldn't reach ${target}` : "Couldn't connect"}</div>}
        {state === 'cancelled' && <div className="tool-state"><span className="tool-status-icon cancelled" aria-hidden="true" />Cancelled</div>}
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
      <div className="tool-state"><span className="tool-status-icon pending" aria-hidden="true" />Pending</div>
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
