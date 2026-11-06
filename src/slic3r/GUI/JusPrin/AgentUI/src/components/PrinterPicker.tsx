// Which printer: the Brand and Model menus, the nozzle, and the Bambu Lab
// printers found on the network. The add-printer form's first step, and
// the first launch's printer screen on Home, which draws it in a shadow root
// of its own (HomeUI PrinterPickerHost). The caller keeps the choice.

import { useMemo } from 'react';
import type { ReactNode } from 'react';
import type { PrinterFormInfo, PrinterFormModel } from '../bridge/protocol';
import { ActionMenu } from './ChatNavigation';

export interface PrinterPick {
  vendorId?: string;
  catalogId?: string;
  nozzle?: number;
  // The printer found on the network the model was picked from.
  deviceId?: string;
}

const byName = (a: string, b: string) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });

// 0.4 where the model ships it, as the chat assumes, otherwise its smallest.
export function startingNozzle(model: PrinterFormModel): number {
  return model.nozzles.includes(0.4) ? 0.4 : model.nozzles[0];
}

export function PrinterPicker({ form, pick, onPick, underMenus, nozzleUnit = false, nozzleNote }: {
  form: Pick<PrinterFormInfo, 'catalog' | 'nearby'>;
  pick: PrinterPick;
  onPick: (pick: PrinterPick) => void;
  // Drawn between the menus and the nozzle.
  underMenus?: ReactNode;
  // Each nozzle size reads "0.4 mm" rather than "0.4".
  nozzleUnit?: boolean;
  nozzleNote?: ReactNode;
}) {
  const { vendorId, catalogId, nozzle, deviceId } = pick;
  // One brand per vendor file: a vendor whose model names spell the brand
  // two ways is still one brand.
  const brands = useMemo(() => {
    const seen = new Map<string, string>();
    for (const model of form.catalog) if (!seen.has(model.vendorId)) seen.set(model.vendorId, model.brand);
    return [...seen].map(([id, label]) => ({ id, label })).sort((a, b) => byName(a.label, b.label));
  }, [form.catalog]);
  const models = useMemo(() => form.catalog.filter((model) => model.vendorId === vendorId)
    .sort((a, b) => byName(a.model, b.model)), [form.catalog, vendorId]);
  const model = form.catalog.find((entry) => entry.id === catalogId);

  const chooseModel = (entry: PrinterFormModel, device?: string) =>
    onPick({ vendorId: entry.vendorId, catalogId: entry.id, nozzle: startingNozzle(entry), deviceId: device });

  return <>
    <div className="printer-form-fields">
      <div className="printer-form-field">
        <span className="printer-form-label">Brand</span>
        <ActionMenu label="Brand" appearance="select" text={brands.find((brand) => brand.id === vendorId)?.label ?? 'Choose a brand'}
          search={{ label: 'Search brands', noMatch: (text) => `No brand matches “${text}”` }}
          items={brands.map((brand) => ({ key: brand.id, label: brand.label, current: brand.id === vendorId,
            onSelect: () => { if (brand.id !== vendorId) onPick({ vendorId: brand.id }); } }))} />
      </div>
      <div className="printer-form-field">
        <span className="printer-form-label">Model</span>
        <ActionMenu label="Model" appearance="select" disabled={!vendorId} text={model?.model ?? 'Choose a model'}
          search={{ label: 'Search models', noMatch: (text) => `No model matches “${text}”` }}
          items={models.map((entry) => ({ key: entry.id, label: entry.model, current: entry.id === catalogId,
            onSelect: () => { if (entry.id !== catalogId) chooseModel(entry); } }))} />
      </div>
      {underMenus}
      {model && <div className="printer-form-field">
        <span className="printer-form-label">Nozzle</span>
        {model.nozzles.length > 1
          ? <div className="jp-segmented printer-form-nozzles" role="radiogroup" aria-label="Nozzle">
              {model.nozzles.map((size) => <button type="button" key={size} role="radio" aria-checked={size === nozzle}
                className={size === nozzle ? 'jp-segment selected' : 'jp-segment'} onClick={() => onPick({ ...pick, nozzle: size })}>
                {nozzleUnit ? `${size} mm` : size}</button>)}
            </div>
          : <p className="printer-form-value">{model.nozzles[0]} mm, the only size it comes with</p>}
        {nozzleNote}
      </div>}
    </div>
    {form.nearby.length > 0 && <>
      <hr className="printer-form-rule" />
      <div className="printer-form-found">
        <span className="printer-form-heading">Found on your network</span>
        {form.nearby.map((found) => {
          const entry = form.catalog.find((item) => item.id === found.catalogId);
          return <button type="button" key={found.deviceId} className="printer-form-row" aria-pressed={found.deviceId === deviceId}
            onClick={() => { if (entry) chooseModel(entry, found.deviceId); }}>
            <span className="printer-form-row-name">{entry ? `${entry.brand} ${entry.model}` : found.name} · {found.deviceId}</span>
            <span className="printer-form-online" aria-label="On the network" />
          </button>;
        })}
        <p className="printer-form-note printer-form-center">You can connect it later.</p>
      </div>
    </>}
  </>;
}
