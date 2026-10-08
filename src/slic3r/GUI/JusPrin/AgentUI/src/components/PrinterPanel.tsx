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
  PrinterCredentialRequest,
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

// Local input for printer_connect. The completed tool call identifies the
// target, but the credential goes directly to the printer session.
export function PrinterCredentialForm({
  request,
  onConnect,
}: {
  request: PrinterCredentialRequest;
  onConnect: (actionId: string, credential: string) => void;
}) {
  const [credential, setCredential] = useState('');
  return (
    <form
      className="printer-credential"
      data-testid={`credential-${request.actionId}`}
      onSubmit={(event) => {
        event.preventDefault();
        onConnect(request.actionId, credential);
      }}
    >
      <div className="printer-notice-heading">
        <PrinterGlyph className="printer-credential-glyph" />
        <span className="tool-title">Connect to {request.target}</span>
      </div>
      <label className="printer-credential-field">
        <span>{request.provider === 'bambu' ? 'Access code' : 'API key (if the printer asks for one)'}</span>
        <input
          type="password"
          className="printer-credential-input"
          autoComplete="off"
          value={credential}
          onChange={(event) => setCredential(event.target.value)}
        />
      </label>
      <p className="printer-credential-note">Stays on this computer.</p>
      {/* Enabled while empty: a printer that already has its code needs none. */}
      <button type="submit" className="primary">Connect</button>
    </form>
  );
}
