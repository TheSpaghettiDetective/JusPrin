// The printer form: "Browse the full printer list" in the printer panel's
// header opens it over the conversation, for a person who would rather pick
// their printer than describe it. Three steps, as the canvas's Add a printer
// boards draw them -- which printer, which plate, which filament to start
// with -- and the plate step only where OrcaSlicer offers a plate choice.
// The filament step lists the person's own presets, the profile's
// suggestions and every filament that fits; under the list, the ways out:
// filaments made for other printers (copied for this one when picked) and
// creating a preset in OrcaSlicer's own window.
//
// Everything it offers comes from the app (PrinterConversation::form_json):
// the catalogue, the Bambu Lab printers found on the network, and for the
// chosen model and nozzle the plates and compatible filaments OrcaSlicer
// would offer. Nothing is saved until the last step; closing it changes
// nothing.

import { useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import type { PrinterFormFilament, PrinterFormInfo, PrinterFormModel, PrinterFormOptions } from '../bridge/protocol';
import { ActionMenu, Dialog } from './ChatNavigation';

export interface PrinterFormChoice {
  catalogId: string;
  nozzle: number;
  plate?: number;
  filament?: string;
  deviceId?: string;
  // Add the printer, then open OrcaSlicer's Create filament window.
  create?: boolean;
}

type Step = 'printer' | 'plate' | 'filament';

// Search results drawn at once; the rest wait for a narrower search.
const RESULT_LIMIT = 50;

const byName = (a: string, b: string) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });

// 0.4 where the model ships it, as the chat assumes, otherwise its smallest.
function startingNozzle(model: PrinterFormModel): number {
  return model.nozzles.includes(0.4) ? 0.4 : model.nozzles[0];
}

function StepMarker({ step, steps }: { step: number; steps: number }) {
  return <div className="printer-form-step">
    <span>Step {step} of {steps}</span>
    <span className="printer-form-dots" aria-hidden="true">
      {Array.from({ length: steps }, (_, index) =>
        <span key={index} className={index < step ? 'printer-form-dot done' : 'printer-form-dot'} />)}
    </span>
  </div>;
}

export function AddPrinterForm({ form, onAsk, onFinish, onClose }: {
  form: PrinterFormInfo;
  onAsk: (catalogId: string, nozzle: number) => void;
  onFinish: (choice: PrinterFormChoice) => void;
  onClose: () => void;
}) {
  const [vendorId, setVendorId] = useState<string>();
  const [catalogId, setCatalogId] = useState<string>();
  const [nozzle, setNozzle] = useState<number>();
  const [deviceId, setDeviceId] = useState<string>();
  const [step, setStep] = useState<Step>('printer');
  const [plate, setPlate] = useState<number>();
  const [query, setQuery] = useState('');
  const [showOthers, setShowOthers] = useState(false);
  // The Create a filament preset sheet, which says what happens before it does.
  const [creating, setCreating] = useState(false);
  // Finishing waits for the app: it either closes the form or says why not.
  const [finishing, setFinishing] = useState(false);
  useEffect(() => { setFinishing(false); }, [form.error]);

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

  useEffect(() => { if (catalogId && nozzle !== undefined) onAsk(catalogId, nozzle); }, [catalogId, nozzle]);

  // The options the app sent for exactly this choice, and none for another.
  const options: PrinterFormOptions | undefined =
    form.options && form.options.catalogId === catalogId && form.options.nozzle === nozzle ? form.options : undefined;
  const ready = options !== undefined && !form.loading;
  const offersPlates = ready && options.plates.length > 0;
  const steps = ready && !offersPlates ? 2 : 3;

  // Moving on before the options arrived lands on the plate step, which
  // gives way to the filament step once it is clear there is no plate to ask.
  useEffect(() => {
    if (step === 'plate' && ready && !offersPlates) setStep('filament');
  }, [step, ready, offersPlates]);
  useEffect(() => {
    if (!options) return;
    setPlate(options.plates.some((entry) => entry.type === options.defaultPlate) ? options.defaultPlate : options.plates[0]?.type);
  }, [options?.catalogId, options?.nozzle]);

  const chooseModel = (entry: PrinterFormModel, device?: string) => {
    setVendorId(entry.vendorId);
    setCatalogId(entry.id);
    setNozzle(startingNozzle(entry));
    setDeviceId(device);
  };

  const finish = (filament?: string, create = false) => {
    if (!catalogId || nozzle === undefined || !options) return;
    setFinishing(true);
    onFinish({ catalogId, nozzle, ...(offersPlates ? { plate } : {}), ...(filament ? { filament } : {}),
      ...(deviceId ? { deviceId } : {}), ...(create ? { create: true } : {}) });
  };

  const loadingNote = !options || form.loading
    ? <p className="printer-form-note">Reading this printer’s profiles…</p>
    : options.error ? <p className="printer-form-error" role="alert">{options.error}</p> : null;
  const errorNote = form.error ? <p className="printer-form-error" role="alert">{form.error}</p> : null;

  if (step === 'printer') {
    return <Dialog key="printer" title="Which printer?" onClose={onClose} className="printer-form"
      eyebrow={<StepMarker step={1} steps={steps} />}>
      <div className="printer-form-fields">
        <div className="printer-form-field">
          <span className="printer-form-label">Brand</span>
          <ActionMenu label="Brand" appearance="select" text={brands.find((brand) => brand.id === vendorId)?.label ?? 'Choose a brand'}
            search={{ label: 'Search brands', noMatch: (text) => `No brand matches “${text}”` }}
            items={brands.map((brand) => ({ key: brand.id, label: brand.label, current: brand.id === vendorId,
              onSelect: () => {
                if (brand.id === vendorId) return;
                setVendorId(brand.id);
                setCatalogId(undefined);
                setNozzle(undefined);
                setDeviceId(undefined);
              } }))} />
        </div>
        <div className="printer-form-field">
          <span className="printer-form-label">Model</span>
          <ActionMenu label="Model" appearance="select" disabled={!vendorId} text={model?.model ?? 'Choose a model'}
            search={{ label: 'Search models', noMatch: (text) => `No model matches “${text}”` }}
            items={models.map((entry) => ({ key: entry.id, label: entry.model, current: entry.id === catalogId,
              onSelect: () => { if (entry.id !== catalogId) chooseModel(entry); } }))} />
        </div>
        {model && <div className="printer-form-field">
          <span className="printer-form-label">Nozzle</span>
          {model.nozzles.length > 1
            ? <div className="jp-segmented printer-form-nozzles" role="radiogroup" aria-label="Nozzle">
                {model.nozzles.map((size) => <button type="button" key={size} role="radio" aria-checked={size === nozzle}
                  className={size === nozzle ? 'jp-segment selected' : 'jp-segment'} onClick={() => setNozzle(size)}>
                  {size}</button>)}
              </div>
            : <p className="printer-form-value">{model.nozzles[0]} mm, the only size it comes with</p>}
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
      {errorNote}
      <div className="chat-dialog-buttons">
        <button type="button" onClick={onClose}>Cancel</button>
        <button type="button" className="primary" disabled={!model || nozzle === undefined} onClick={() => setStep('plate')}>Next</button>
      </div>
    </Dialog>;
  }

  if (step === 'plate') {
    return <Dialog key="plate" title="Which plate is on it?" onClose={onClose} className="printer-form"
      eyebrow={<StepMarker step={2} steps={steps} />}>
      {loadingNote ?? <div className="printer-form-choices" role="radiogroup" aria-label="Plate">
        {options!.plates.map((entry) => <button type="button" key={entry.type} role="radio" aria-checked={entry.type === plate}
          className="printer-form-choice" onClick={() => { setPlate(entry.type); setStep('filament'); }}>
          <span className="printer-form-choice-name">{entry.label}</span>
          {entry.type === options!.defaultPlate && <span className="printer-form-choice-detail">Default for the {model?.model}</span>}
        </button>)}
      </div>}
      <div className="chat-dialog-buttons">
        <button type="button" onClick={() => setStep('printer')}>Back</button>
        <button type="button" onClick={onClose}>Cancel</button>
      </div>
    </Dialog>;
  }

  const modelName = model?.model ?? '';
  const filaments = options?.filaments ?? [];
  const others = options?.others ?? [];
  const own = options?.own ?? [];
  const wanted = query.trim().toLowerCase();
  const matches = (entry: PrinterFormFilament) => !wanted ||
    [entry.label, entry.vendor, entry.material, entry.base ?? ''].some((text) => text.toLowerCase().includes(wanted));
  const ownShown = own.filter(matches);
  const fitting = filaments.filter(matches);
  const otherShown = others.filter(matches);
  const suggested = (options?.suggested.length ? options.suggested : options?.defaultFilament ? [options.defaultFilament] : [])
    .map((name) => filaments.find((entry) => entry.name === name))
    .filter((entry): entry is PrinterFormFilament => entry !== undefined);
  const defaultLabel = filaments.find((entry) => entry.name === options?.defaultFilament)?.label;

  const detail = (entry: PrinterFormFilament, isDefault = false) => {
    if (entry.madeFor)
      return <span className="printer-form-option-detail cautious">
        <span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />
        <span>{own.includes(entry) ? 'Your preset, made' : 'Made'} for {entry.madeFor}. Picking it saves a
          copy for the {modelName}.</span>
      </span>;
    if (own.includes(entry))
      return <span className="printer-form-option-detail">{entry.base ? `Your preset, based on ${entry.base}` : 'Your preset'}</span>;
    if (isDefault) return <span className="printer-form-option-detail">The {modelName} profile’s default</span>;
    return null;
  };
  const row = (entry: PrinterFormFilament, key: string, isDefault = false) =>
    <button type="button" key={key} className="printer-form-option" disabled={finishing} onClick={() => finish(entry.name)}>
      <span className="printer-form-option-copy">
        <span className="printer-form-option-name">{entry.label}</span>
        {detail(entry, isDefault)}
      </span>
      <span className="printer-form-option-kind">{entry.material}</span>
    </button>;
  const section = (title: string, count?: number) =>
    <div className="printer-form-section">{title}{count !== undefined && <span className="printer-form-count"> · {count}</span>}</div>;
  const createRow = <button type="button" className="printer-form-option printer-form-create" disabled={finishing}
    onClick={() => setCreating(true)}>
    <span className="jp-icon jp-icon-plus" aria-hidden="true" />
    <span className="printer-form-option-name">Create a filament preset…</span>
  </button>;
  // Its second line says what ticking it would add for what is typed.
  const toggleDetail = showOthers ? `Picking one saves a copy for the ${modelName}.`
    : !wanted ? `${others.length} more. Picking one saves a copy for the ${modelName}.`
      : otherShown.length > 0 ? `${otherShown.length} of them match your search. Picking one saves a copy for the ${modelName}.`
        : `None of the ${others.length} match your search either.`;
  const othersToggle =
    <button type="button" role="checkbox" aria-checked={showOthers} className="printer-form-check"
      onClick={() => setShowOthers(!showOthers)}>
      <span className="printer-form-check-box" aria-hidden="true">
        {showOthers && <span className="jp-icon jp-icon-check" />}
      </span>
      <span className="printer-form-check-copy">
        Show filaments made for other printers
        <span className="printer-form-check-detail">{toggleDetail}</span>
      </span>
    </button>;

  if (creating) {
    return <Dialog key="create" title="Create a filament preset" onClose={onClose} className="printer-form"
      eyebrow={<StepMarker step={steps} steps={steps} />}>
      <p>JusPrin makes filament presets for printers that are already set up, so the {modelName} is added first. Then the
        Create filament window opens. Tick the {modelName} there, and JusPrin puts your new preset in slot 1.</p>
      <dl className="printer-form-facts">
        <div><dt>Printer</dt><dd>{model ? `${model.brand} ${model.model}` : ''}, {nozzle} mm nozzle</dd></div>
        <div><dt>Slot 1</dt><dd>{defaultLabel ?? 'the printer profile’s default'}, until your preset is saved</dd></div>
        <div><dt>Then</dt><dd>The Create filament window opens</dd></div>
      </dl>
      {errorNote}
      <div className="chat-dialog-buttons">
        <button type="button" onClick={() => setCreating(false)}>Back</button>
        <button type="button" className="primary" disabled={finishing} onClick={() => finish(undefined, true)}>
          Add the {modelName} and continue</button>
      </div>
    </Dialog>;
  }

  let body: ReactNode;
  if (!wanted) {
    body = <>
      {ownShown.length > 0 && <>{section('Your filaments')}{ownShown.map((entry) => row(entry, `own:${entry.name}`))}</>}
      {section(`Suggested for the ${modelName}`)}
      {suggested.map((entry, index) => row(entry, `suggested:${entry.name}`, index === 0 && entry.name === options?.defaultFilament))}
      {section(`Every filament for the ${modelName}`, filaments.length)}
      {filaments.map((entry) => row(entry, `fits:${entry.name}`))}
      {showOthers && <>{section('Made for other printers', others.length)}{others.map((entry) => row(entry, `other:${entry.name}`))}</>}
    </>;
  } else {
    const found = ownShown.length + fitting.length;
    body = <>
      {ownShown.length > 0 && <>{section('Your filaments')}{ownShown.map((entry) => row(entry, `own:${entry.name}`))}</>}
      {fitting.length > 0 && <>{section(`For the ${modelName}`, fitting.length)}
        {fitting.slice(0, RESULT_LIMIT).map((entry) => row(entry, `fits:${entry.name}`))}</>}
      {fitting.length > RESULT_LIMIT && <p className="printer-form-note">
        {fitting.length - RESULT_LIMIT} more match. Type more to narrow the list.</p>}
      {showOthers && otherShown.length > 0 && <>{section('Made for other printers', otherShown.length)}
        {otherShown.slice(0, RESULT_LIMIT).map((entry) => row(entry, `other:${entry.name}`))}</>}
      {found === 0 && !(showOthers && otherShown.length > 0) && (otherShown.length > 0
        ? <div className="printer-form-message">
            <p className="printer-form-lead"><b>No filament for the {modelName} matches “{query.trim()}”.</b>{' '}
              {otherShown.length === 1 ? 'One filament made for other printers does.' :
                `${otherShown.length} filaments made for other printers do.`}</p>
            <button type="button" className="panel-link-button" onClick={() => setShowOthers(true)}>
              Show {otherShown.length === 1 ? 'it' : 'them'}</button>
          </div>
        : <div className="printer-form-message">
            <p className="printer-form-lead"><b>No filament matches “{query.trim()}”,</b> for the {modelName} or any other printer
              JusPrin has profiles for.</p>
            {defaultLabel && <button type="button" className="panel-link-button" disabled={finishing} onClick={() => finish()}>
              Start with {defaultLabel} instead</button>}
          </div>)}
    </>;
  }

  return <Dialog key="filament" title="Which filament do you want to start with?" onClose={onClose} className="printer-form"
    eyebrow={<StepMarker step={steps} steps={steps} />}>
    {loadingNote ?? <>
      <label className="field-search">
        <span className="jp-icon jp-icon-search" aria-hidden="true" />
        <input type="search" placeholder="Search brand or material" aria-label="Search brand or material" value={query}
          onChange={(event) => setQuery(event.target.value)} />
        {query && <button type="button" className="printer-form-clear" aria-label="Clear search" onClick={() => setQuery('')}>
          <span className="jp-icon jp-icon-close" aria-hidden="true" /></button>}
      </label>
      <div className="printer-form-filaments" role="group" aria-label="Filaments">{body}</div>
      {/* Outside the list, which is long: the ways out stay in view. */}
      <div className="printer-form-end">
        <p className="printer-form-lead"><b>Not in the list?</b></p>
        {others.length > 0 && othersToggle}
        {createRow}
      </div>
      <button type="button" className="panel-link-button printer-form-skip" disabled={finishing} onClick={() => finish()}>
        {defaultLabel ? `Skip — start with ${defaultLabel}` : 'Skip — choose a filament later'}
      </button>
    </>}
    {errorNote}
    <div className="chat-dialog-buttons">
      <button type="button" onClick={() => setStep(offersPlates ? 'plate' : 'printer')}>Back</button>
      <button type="button" onClick={onClose}>Cancel</button>
    </div>
  </Dialog>;
}
