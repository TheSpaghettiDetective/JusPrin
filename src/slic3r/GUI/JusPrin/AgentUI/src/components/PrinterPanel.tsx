// The printer panel's cards: the pictures the thread draws, and the one card
// that asks for something -- the access code or API key a connection needs, typed here
// so it never passes through the conversation -- and Undo on an added
// printer's receipt. Everything else the person does in this panel is a
// message.

import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import { memo, useState } from 'react';
import type {
  NetworkPrinterInfo,
  PrinterBlock,
  PrinterCardInfo,
  PrinterCredentialRequest,
} from '../bridge/protocol';
import { decimalText } from '../i18n';

function PrinterGlyph({ className }: { className: string }) {
  return <span className={`${className} jp-icon jp-icon-printer`} aria-hidden="true" />;
}

// One card in the thread, under the message it belongs to.
export const PrinterBlockView = memo(function PrinterBlockView({
  block,
  onUndoAdd,
  onInstallPlugin,
  onAnswerReport,
}: {
  block: PrinterBlock;
  onUndoAdd?: (blockId: string) => void;
  onInstallPlugin?: () => void;
  onAnswerReport?: (blockId: string, send: boolean) => void;
}) {
  if (block.kind === 'tip')
    return (
      <div className="printer-tip">
        <span>
          <Trans>
            <b>Tip:</b> a photo is the fastest way. Use <b>+</b> below, or drop a picture of the printer, its nameplate or the box
            into this panel.
          </Trans>
        </span>
      </div>
    );

  // The receipt for printer_add: the same words every time, from what the
  // app saved, whatever the model's reply around it says. Undo takes the
  // printer away again, and the receipt stays to say so.
  if (block.kind === 'added') {
    const printer = block.printer!;
    const name = printer.name;
    const size = decimalText(printer.nozzle, 1, 2);
    return (
      <div className={block.removed ? 'printer-added printer-added-removed' : 'printer-added'} role="status">
        <span className="printer-notice-heading">
          <span className={`jp-icon jp-icon-${block.removed ? 'rotate-ccw' : 'check'}`} aria-hidden="true" />
          <span className="printer-added-caption">{block.removed
            ? <Trans>Removed: <span className="printer-added-name">{name}</span></Trans>
            : <Trans>Added: <span className="printer-added-name">{name}</span></Trans>}</span>
        </span>
        {block.removed
          ? <span className="printer-notice-body"><Trans>Removed from your printers.</Trans></span>
          : (printer.model || printer.nozzle > 0 || onUndoAdd) && (
            <span className="printer-added-detail">
              {(printer.model || printer.nozzle > 0) && <small>
                {[printer.model, printer.nozzle > 0 && t`${size} mm nozzle`].filter(Boolean).join(' · ')}
              </small>}
              {onUndoAdd && (
                <button type="button" className="panel-link-button printer-added-undo" onClick={() => onUndoAdd(block.id)}>
                  <Trans>Undo</Trans>
                </button>
              )}
            </span>
          )}
      </div>
    );
  }

  // The app's own question once connecting has failed twice. What it would
  // send is there to read, and nothing goes unless the person says so. There
  // is no server yet, so a yes keeps the report on this computer and says so.
  if (block.kind === 'report') {
    const path = block.path ?? '';
    return (
      <div className="printer-plugin" role="status">
        <span className="printer-notice-heading">
          <span className="jp-icon jp-icon-circle-alert" aria-hidden="true" />
          <span className="printer-plugin-caption"><Trans>Send a report to the JusPrin developers?</Trans></span>
        </span>
        <span className="printer-notice-body">
          {!block.answer && t`Connecting this printer has failed twice, and a report helps us fix why. It holds what the app saw and this chat, with anything that looks like an access code or API key hidden. Nothing is sent unless you choose to.`}
          {block.answer === 'declined' && t`No report was sent.`}
          {block.answer === 'failed' && t`The report could not be saved.`}
          {block.answer === 'kept' && t`Sending isn’t built yet, so nothing has left this computer. The report is saved at ${path}.`}
        </span>
        <details className="printer-report-details">
          <summary><Trans>What the report holds</Trans></summary>
          <pre className="printer-report-text">{block.report}</pre>
        </details>
        {!block.answer && onAnswerReport && (
          <span className="printer-report-actions">
            <button type="button" className="primary printer-plugin-install" onClick={() => onAnswerReport(block.id, true)}>{t`Send report`}</button>
            <button type="button" className="panel-link-button" onClick={() => onAnswerReport(block.id, false)}>{t`Not now`}</button>
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
          <span className="printer-plugin-caption">{block.installed ? t`Bambu plug-in installed` : t`Bambu plug-in needed`}</span>
        </span>
        {!block.installed && (
          <span className="printer-notice-body"><Trans>You can prepare prints with this printer before installing the Bambu plug-in.</Trans></span>
        )}
        {!block.installed && onInstallPlugin && (
          <button type="button" className="primary printer-plugin-install" onClick={() => onInstallPlugin()}>
            <Trans>Install plug-in</Trans>
          </button>
        )}
      </div>
    );

  if (block.kind === 'network')
    return (
      <div className="printer-found">
        <p className="printer-found-caption"><Trans>Found on your network</Trans></p>
        <div className="printer-found-rows">
          {((block.printers ?? []) as NetworkPrinterInfo[]).map((printer) => (
            <div className={printer.online ? 'printer-row' : 'printer-row printer-row-offline'} key={printer.serial}>
              <PrinterGlyph className="printer-row-icon" />
              <span className="printer-row-name">
                {printer.name}
                <small>{[printer.online ? t`Online` : t`Offline`, ...(printer.online ? [printer.model, printer.serial] : [])].filter(Boolean).join(' · ')}</small>
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

// Local input for printer_bambu_connect, and for printer_host_connect when
// the host refused its test. The call waits for this form, so each way out
// ends it: Connect starts the attempt, and the person can also say they have
// no credential, or close the form. The credential goes directly to the
// printer session, never into the call.
export function PrinterCredentialForm({
  request,
  title,
  onConnect,
  onClose,
}: {
  request: PrinterCredentialRequest;
  // The waiting call's title, which names what it waits for.
  title: string;
  onConnect: (actionId: string, credential: string) => void;
  onClose: (actionId: string, missing: boolean) => void;
}) {
  const [credential, setCredential] = useState('');
  const bambu = request.provider === 'bambu';
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
        <span className="tool-title">{title}</span>
      </div>
      <label className="printer-credential-field">
        <span>{bambu ? t`Access code` : t`API key`}</span>
        <input
          type="password"
          className="printer-credential-input"
          autoComplete="off"
          value={credential}
          onChange={(event) => setCredential(event.target.value)}
        />
      </label>
      <p className="printer-credential-note"><Trans>Stays on this computer.</Trans></p>
      <span className="printer-credential-actions">
        {/* Enabled while empty: a printer that already has its code needs none. */}
        <button type="submit" className="primary"><Trans>Connect</Trans></button>
        <button type="button" className="link" onClick={() => onClose(request.actionId, true)}>
          {bambu ? t`Help me find the code` : t`Help me find the key`}
        </button>
        <button type="button" className="link" onClick={() => onClose(request.actionId, false)}>{t`Cancel`}</button>
      </span>
    </form>
  );
}
