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
import { Progress } from './Progress';
import { ToolStatus } from './ToolActivityCard';

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
        <span className="printer-notice-heading">
          <span className={`jp-icon jp-icon-${block.removed ? 'rotate-ccw' : 'check'}`} aria-hidden="true" />
          <span className="printer-added-caption">{block.removed ? 'Removed:' : 'Added:'} <span className="printer-added-name">{printer.name}</span></span>
        </span>
        {block.removed
          ? <span className="printer-notice-body">Removed from your printers.</span>
          : (printer.model || printer.nozzle > 0 || onUndoAdd) && (
            <span className="printer-added-detail">
              {(printer.model || printer.nozzle > 0) && <small>
                {[printer.model, printer.nozzle > 0 && `${numberText(printer.nozzle)} mm nozzle`].filter(Boolean).join(' · ')}
              </small>}
              {onUndoAdd && (
                <button type="button" className="panel-link-button printer-added-undo" onClick={() => onUndoAdd(block.id)}>
                  Undo
                </button>
              )}
            </span>
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
        <span className="printer-notice-heading">
          <span className={`jp-icon jp-icon-${block.installed ? 'check' : 'settings'}`} aria-hidden="true" />
          <span className="printer-plugin-caption">{block.installed ? 'Bambu plug-in installed' : 'Bambu plug-in needed'}</span>
        </span>
        {!block.installed && (
          <span className="printer-notice-body">You can prepare prints with this printer before installing the Bambu plug-in.</span>
        )}
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
        <p className="printer-found-caption">Found on your network</p>
        <div className="printer-found-rows">
          {((block.printers ?? []) as NetworkPrinterInfo[]).map((printer) => (
            <div className={printer.online ? 'printer-row' : 'printer-row printer-row-offline'} key={printer.serial}>
              <PrinterGlyph className="printer-row-icon" />
              <span className="printer-row-name">
                {printer.name}
                <small>{[printer.online ? 'Online' : 'Offline', ...(printer.online ? [printer.model, printer.serial] : [])].filter(Boolean).join(' · ')}</small>
              </span>
            </div>
          ))}
        </div>
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
          {/* Brand above model when the app sends the two apart. */}
          <span className="printer-card-name">
            {printer.brand && printer.model
              ? <><span className="printer-card-brand">{printer.brand}</span>{printer.model}</>
              : printer.name}
            <small>{printer.buildVolume}</small>
          </span>
        </div>
      ))}
    </div>
  );
});

// printer_connect's card. Connect hands the app what was typed; the model
// hears only how the connection went. After Connect the
// card follows the attempt itself, as the app reports it in `connection`.
export function PrinterCredentialCard({
  activity,
  connection,
  model,
  onInput,
  onCancelTool,
  onCancelConnection,
}: {
  activity: ToolActivityInfo;
  connection?: PrinterConnectionInfo;
  // What kind of printer the conversation is about, when the app knows.
  model?: string;
  onInput: (actionId: string, input: { credential: string }) => void;
  onCancelTool: (actionId: string) => void;
  onCancelConnection: (actionId: string) => void;
}) {
  const [credential, setCredential] = useState('');
  const bambu = activity.arguments.provider === 'bambu';
  if (activity.state !== 'input_required') {
    const state =
      activity.state === 'cancelled' ? 'cancelled' : connection?.state ?? (activity.state === 'failed' ? 'failed' : 'connecting');
    const target = connection?.target;
    // A figure only when the app has measured one, kept inside its range.
    const percent = typeof connection?.percent === 'number' && Number.isFinite(connection.percent)
      ? Math.round(Math.min(100, Math.max(0, connection.percent))) : undefined;
    return (
      <div className="tool-card printer-credential" data-testid={`tool-${activity.actionId}`}>
        <div className="tool-title">{activity.title}</div>
        {state === 'connecting' && (
          <>
            <ToolStatus state="running">{percent === undefined ? 'Connecting…' : `Connecting · ${percent}%`}</ToolStatus>
            <Progress label="Connecting" value={percent} max={percent === undefined ? undefined : 100} />
            {connection && <button type="button" className="panel-link-button printer-credential-cancel" onClick={() => onCancelConnection(activity.actionId)}>Cancel</button>}
          </>
        )}
        {state === 'verified' && <ToolStatus state="succeeded">{target ? `Connected to ${target}.` : 'Connected.'}</ToolStatus>}
        {state === 'failed' && <ToolStatus state="failed">{target ? `Couldn't reach ${target}` : "Couldn't connect"}</ToolStatus>}
        {state === 'cancelled' && <ToolStatus state="cancelled">Cancelled. No connection was made.</ToolStatus>}
      </div>
    );
  }
  return (
    <form
      className="tool-card printer-credential"
      data-testid={`tool-${activity.actionId}`}
      onSubmit={(event) => {
        event.preventDefault();
        onInput(activity.actionId, { credential });
      }}
    >
      <div className="printer-notice-heading">
        <PrinterGlyph className="printer-credential-glyph" />
        <span className="tool-title">{activity.title}</span>
      </div>
      <div className="printer-credential-state">{model ? `Pending · ${model}` : 'Pending'}</div>
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
        <button type="button" onClick={() => onCancelTool(activity.actionId)}>
          Cancel
        </button>
      </div>
    </form>
  );
}
