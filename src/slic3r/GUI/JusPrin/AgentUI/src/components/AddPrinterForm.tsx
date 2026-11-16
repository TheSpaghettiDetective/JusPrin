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

import { plural, t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import type { PrinterFormFilament, PrinterFormInfo, PrinterFormOptions } from '../bridge/protocol';
import { Dialog } from './ChatNavigation';
import { PrinterPicker } from './PrinterPicker';
import type { PrinterPick } from './PrinterPicker';

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

// The caution on a filament made for other printers, as one sentence per
// case so each language can inflect it: `printers` are the models its
// profile names (none it can say, one, two, or more), `modelName` the
// printer being added.
function madeForSentence(printers: string[], yours: boolean, modelName: string): string {
  const [first, second] = printers;
  const more = printers.length - 2;
  if (yours) {
    if (printers.length === 0) return t`Your preset, made for other printers. Picking it saves a copy for the ${modelName}.`;
    if (printers.length === 1) return t`Your preset, made for the ${first}. Picking it saves a copy for the ${modelName}.`;
    if (printers.length === 2) return t`Your preset, made for the ${first} and ${second}. Picking it saves a copy for the ${modelName}.`;
    return plural(more, {
      one: `Your preset, made for the ${first}, ${second} and # other printer. Picking it saves a copy for the ${modelName}.`,
      other: `Your preset, made for the ${first}, ${second} and # other printers. Picking it saves a copy for the ${modelName}.`,
    });
  }
  if (printers.length === 0) return t`Made for other printers. Picking it saves a copy for the ${modelName}.`;
  if (printers.length === 1) return t`Made for the ${first}. Picking it saves a copy for the ${modelName}.`;
  if (printers.length === 2) return t`Made for the ${first} and ${second}. Picking it saves a copy for the ${modelName}.`;
  return plural(more, {
    one: `Made for the ${first}, ${second} and # other printer. Picking it saves a copy for the ${modelName}.`,
    other: `Made for the ${first}, ${second} and # other printers. Picking it saves a copy for the ${modelName}.`,
  });
}

function StepMarker({ step, steps }: { step: number; steps: number }) {
  return <div className="printer-form-step">
    <span><Trans>Step {step} of {steps}</Trans></span>
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
  const [pick, setPick] = useState<PrinterPick>({});
  const { catalogId, nozzle, deviceId } = pick;
  const [step, setStep] = useState<Step>('printer');
  const [plate, setPlate] = useState<number>();
  const [query, setQuery] = useState('');
  const [showOthers, setShowOthers] = useState(false);
  // The Create a filament preset sheet, which says what happens before it does.
  const [creating, setCreating] = useState(false);
  // Finishing waits for the app: it either closes the form or says why not.
  const [finishing, setFinishing] = useState(false);
  useEffect(() => { setFinishing(false); }, [form.error]);

  const model = form.catalog.find((entry) => entry.id === catalogId);
  const modelName = model?.model ?? '';
  const back = t({ message: 'Back', context: 'navigation' });

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

  const finish = (filament?: string, create = false) => {
    if (!catalogId || nozzle === undefined || !options) return;
    setFinishing(true);
    onFinish({ catalogId, nozzle, ...(offersPlates ? { plate } : {}), ...(filament ? { filament } : {}),
      ...(deviceId ? { deviceId } : {}), ...(create ? { create: true } : {}) });
  };

  const loadingNote = !options || form.loading
    ? <p className="printer-form-note"><Trans>Reading this printer’s profiles…</Trans></p>
    : options.error ? <p className="printer-form-error" role="alert">{options.error}</p> : null;
  const errorNote = form.error ? <p className="printer-form-error" role="alert">{form.error}</p> : null;

  if (step === 'printer') {
    return <Dialog key="printer" title={t`Which printer?`} onClose={onClose} className="printer-form"
      eyebrow={<StepMarker step={1} steps={steps} />}>
      <PrinterPicker form={form} pick={pick} onPick={setPick} />
      {errorNote}
      <div className="chat-dialog-buttons">
        <button type="button" onClick={onClose}>{t`Cancel`}</button>
        <button type="button" className="primary" disabled={!model || nozzle === undefined} onClick={() => setStep('plate')}>{t`Next`}</button>
      </div>
    </Dialog>;
  }

  if (step === 'plate') {
    // The printer's build plate; All settings' "Plate" is a project plate.
    return <Dialog key="plate" title={t`Which plate is on it?`} onClose={onClose} className="printer-form"
      eyebrow={<StepMarker step={2} steps={steps} />}>
      {loadingNote ?? <div className="printer-form-choices" role="radiogroup" aria-label={t({ message: 'Plate', context: 'build plate' })}>
        {options!.plates.map((entry) => <button type="button" key={entry.type} role="radio" aria-checked={entry.type === plate}
          className="printer-form-choice" onClick={() => { setPlate(entry.type); setStep('filament'); }}>
          <span className="printer-form-choice-name">{entry.label}</span>
          {entry.type === options!.defaultPlate && <span className="printer-form-choice-detail"><Trans>Default for the {modelName}</Trans></span>}
        </button>)}
      </div>}
      <div className="chat-dialog-buttons">
        <button type="button" onClick={() => setStep('printer')}>{back}</button>
        <button type="button" onClick={onClose}>{t`Cancel`}</button>
      </div>
    </Dialog>;
  }

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
    const { madeFor, base } = entry;
    if (madeFor)
      return <span className="printer-form-option-detail cautious">
        <span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />
        <span>{madeForSentence(madeFor, own.includes(entry), modelName)}</span>
      </span>;
    if (own.includes(entry))
      return <span className="printer-form-option-detail">{base ? t`Your preset, based on ${base}` : t`Your preset`}</span>;
    if (isDefault) return <span className="printer-form-option-detail"><Trans>The {modelName} profile’s default</Trans></span>;
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
    <div className="section-caption section-caption--row">{title}{count !== undefined && <span className="printer-form-count"> · {count}</span>}</div>;
  const createRow = <button type="button" className="printer-form-option printer-form-create" disabled={finishing}
    onClick={() => setCreating(true)}>
    <span className="jp-icon jp-icon-plus" aria-hidden="true" />
    <span className="printer-form-option-name"><Trans>Create a filament preset…</Trans></span>
  </button>;
  // Its second line says what ticking it would add for what is typed.
  const toggleDetail = showOthers ? t`Picking one saves a copy for the ${modelName}.`
    : !wanted ? plural(others.length, {
      one: `# more. Picking one saves a copy for the ${modelName}.`,
      other: `# more. Picking one saves a copy for the ${modelName}.`,
    })
      : otherShown.length > 0 ? plural(otherShown.length, {
        one: `# of them match your search. Picking one saves a copy for the ${modelName}.`,
        other: `# of them match your search. Picking one saves a copy for the ${modelName}.`,
      })
        : plural(others.length, {
          one: 'None of the # match your search either.',
          other: 'None of the # match your search either.',
        });
  const othersToggle =
    <button type="button" role="checkbox" aria-checked={showOthers} className="printer-form-check"
      onClick={() => setShowOthers(!showOthers)}>
      <span className="printer-form-check-box" aria-hidden="true">
        {showOthers && <span className="jp-icon jp-icon-check" />}
      </span>
      <span className="printer-form-check-copy">
        <Trans>Show filaments made for other printers</Trans>
        <span className="printer-form-check-detail">{toggleDetail}</span>
      </span>
    </button>;

  if (creating) {
    const printer = model ? `${model.brand} ${model.model}` : '';
    const size = nozzle === undefined ? '' : String(nozzle);
    return <Dialog key="create" title={t`Create a filament preset`} onClose={onClose} className="printer-form"
      eyebrow={<StepMarker step={steps} steps={steps} />}>
      <p><Trans>JusPrin makes filament presets for printers that are already set up, so the {modelName} is added first. Then the
        Create filament window opens. Tick the {modelName} there, and JusPrin puts your new preset in slot 1.</Trans></p>
      <dl className="printer-form-facts">
        <div><dt><Trans>Printer</Trans></dt><dd>{t`${printer}, ${size} mm nozzle`}</dd></div>
        <div><dt><Trans>Slot 1</Trans></dt><dd>{defaultLabel !== undefined
          ? t`${defaultLabel}, until your preset is saved`
          : t`the printer profile’s default, until your preset is saved`}</dd></div>
        <div><dt><Trans>Then</Trans></dt><dd><Trans>The Create filament window opens</Trans></dd></div>
      </dl>
      {errorNote}
      <div className="chat-dialog-buttons">
        <button type="button" onClick={() => setCreating(false)}>{back}</button>
        <button type="button" className="primary" disabled={finishing} onClick={() => finish(undefined, true)}>
          <Trans>Add the {modelName} and continue</Trans></button>
      </div>
    </Dialog>;
  }

  let body: ReactNode;
  if (!wanted) {
    body = <>
      {ownShown.length > 0 && <>{section(t`Your filaments`)}{ownShown.map((entry) => row(entry, `own:${entry.name}`))}</>}
      {section(t`Suggested for the ${modelName}`)}
      {suggested.map((entry, index) => row(entry, `suggested:${entry.name}`, index === 0 && entry.name === options?.defaultFilament))}
      {section(t`Every filament for the ${modelName}`, filaments.length)}
      {filaments.map((entry) => row(entry, `fits:${entry.name}`))}
      {showOthers && <>{section(t`Made for other printers`, others.length)}{others.map((entry) => row(entry, `other:${entry.name}`))}</>}
    </>;
  } else {
    const found = ownShown.length + fitting.length;
    const search = query.trim();
    body = <>
      {ownShown.length > 0 && <>{section(t`Your filaments`)}{ownShown.map((entry) => row(entry, `own:${entry.name}`))}</>}
      {fitting.length > 0 && <>{section(t`For the ${modelName}`, fitting.length)}
        {fitting.slice(0, RESULT_LIMIT).map((entry) => row(entry, `fits:${entry.name}`))}</>}
      {fitting.length > RESULT_LIMIT && <p className="printer-form-note">
        {plural(fitting.length - RESULT_LIMIT, {
          one: '# more match. Type more to narrow the list.',
          other: '# more match. Type more to narrow the list.',
        })}</p>}
      {showOthers && otherShown.length > 0 && <>{section(t`Made for other printers`, otherShown.length)}
        {otherShown.slice(0, RESULT_LIMIT).map((entry) => row(entry, `other:${entry.name}`))}</>}
      {found === 0 && !(showOthers && otherShown.length > 0) && (otherShown.length > 0
        ? <div className="printer-form-message">
            <p className="printer-form-lead"><b><Trans>No filament for the {modelName} matches “{search}”.</Trans></b>{' '}
              {plural(otherShown.length, {
                1: 'One filament made for other printers does.',
                other: '# filaments made for other printers do.',
              })}</p>
            <button type="button" className="panel-link-button" onClick={() => setShowOthers(true)}>
              {plural(otherShown.length, { 1: 'Show it', other: 'Show them' })}</button>
          </div>
        : <div className="printer-form-message">
            <p className="printer-form-lead"><Trans><b>No filament matches “{search}”,</b> for the {modelName} or any other printer
              JusPrin has profiles for.</Trans></p>
            {defaultLabel && <button type="button" className="panel-link-button" disabled={finishing} onClick={() => finish()}>
              <Trans>Start with {defaultLabel} instead</Trans></button>}
          </div>)}
    </>;
  }

  return <Dialog key="filament" title={t`Which filament do you want to start with?`} onClose={onClose} className="printer-form"
    eyebrow={<StepMarker step={steps} steps={steps} />}>
    {loadingNote ?? <>
      <label className="field-search">
        <span className="jp-icon jp-icon-search" aria-hidden="true" />
        <input type="search" placeholder={t`Search brand or material`} aria-label={t`Search brand or material`} value={query}
          onChange={(event) => setQuery(event.target.value)} />
        {query && <button type="button" className="printer-form-clear" aria-label={t`Clear search`} onClick={() => setQuery('')}>
          <span className="jp-icon jp-icon-close" aria-hidden="true" /></button>}
      </label>
      <div className="printer-form-filaments" role="group" aria-label={t`Filaments`}>{body}</div>
      {/* Outside the list, which is long: the ways out stay in view. */}
      <div className="printer-form-end">
        <p className="printer-form-lead"><b><Trans>Not in the list?</Trans></b></p>
        {others.length > 0 && othersToggle}
        {createRow}
      </div>
      <button type="button" className="panel-link-button printer-form-skip" disabled={finishing} onClick={() => finish()}>
        {defaultLabel ? t`Skip — start with ${defaultLabel}` : t`Skip — choose a filament later`}
      </button>
    </>}
    {errorNote}
    <div className="chat-dialog-buttons">
      <button type="button" onClick={() => setStep(offersPlates ? 'plate' : 'printer')}>{back}</button>
      <button type="button" onClick={onClose}>{t`Cancel`}</button>
    </div>
  </Dialog>;
}
