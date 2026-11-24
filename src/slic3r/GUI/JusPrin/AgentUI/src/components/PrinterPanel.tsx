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
  PrinterFormField,
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
}: {
  block: PrinterBlock;
  onUndoAdd?: (blockId: string) => void;
  onInstallPlugin?: () => void;
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

// Each box's label, and whether what is typed in it is hidden.
function formField(id: PrinterFormField): { label: string; secret: boolean } {
  switch (id) {
    case 'apiKey': return { label: t`API key`, secret: true };
    case 'accessCode': return { label: t`Access code`, secret: true };
    case 'serialNumber': return { label: t`Serial number`, secret: false };
    case 'userName': return { label: t`User name`, secret: false };
    case 'password': return { label: t`Password`, secret: true };
    // TRN A box where the person types which printer, of those a Repetier Server drives, to connect.
    case 'printerOnServer': return { label: t`Printer name on Repetier Server`, secret: false };
  }
}

// Local input for printer_bambu_connect, and for printer_host_connect when
// it asks for boxes or the host refused its test. The call waits for this
// form, so each way out ends it: Connect starts the attempt, and the person
// can also ask for help finding what it asks for, or close the form. What is
// typed goes directly to the printer session, never into the call.
export function PrinterCredentialForm({
  request,
  title,
  onConnect,
  onClose,
}: {
  request: PrinterCredentialRequest;
  // The waiting call's title, which names what it waits for.
  title: string;
  onConnect: (actionId: string, values: Partial<Record<PrinterFormField, string>>) => void;
  onClose: (actionId: string, help: boolean) => void;
}) {
  const [values, setValues] = useState<Partial<Record<PrinterFormField, string>>>({});
  const one = request.fields.length === 1 ? request.fields[0] : undefined;
  return (
    <form
      className="printer-credential"
      data-testid={`credential-${request.actionId}`}
      onSubmit={(event) => {
        event.preventDefault();
        onConnect(request.actionId, Object.fromEntries(request.fields.map((id) => [id, values[id] ?? ''])));
      }}
    >
      <div className="printer-notice-heading">
        <PrinterGlyph className="printer-credential-glyph" />
        <span className="tool-title">{title}</span>
      </div>
      {request.fields.map((id) => {
        const field = formField(id);
        return (
          <label key={id} className="printer-credential-field">
            <span>{field.label}</span>
            <input
              type={field.secret ? 'password' : 'text'}
              className="printer-credential-input"
              autoComplete="off"
              spellCheck={false}
              value={values[id] ?? ''}
              onChange={(event) => setValues({ ...values, [id]: event.target.value })}
            />
          </label>
        );
      })}
      <p className="printer-credential-note">
        {one
          ? t`The app saves this for this printer and sends it only to the printer. The AI doesn’t see it.`
          : t`The app saves these for this printer and sends them only to the printer. The AI doesn’t see them.`}
      </p>
      <span className="printer-credential-actions">
        {/* Enabled while empty: a printer that already has what it needs needs nothing typed. */}
        <button type="submit" className="primary"><Trans>Connect</Trans></button>
        <button type="button" className="link" onClick={() => onClose(request.actionId, true)}>
          {one === 'apiKey' ? t`Help me find the key` : one === 'accessCode' ? t`Help me find the code` : t`Help me find these`}
        </button>
        <button type="button" className="link" onClick={() => onClose(request.actionId, false)}>{t`Cancel`}</button>
      </span>
    </form>
  );
}
