// The setup card: what the agent heard, what it will cost, and how far the
// project has drifted from its preset. It sits above the thread and every
// pixel it takes comes out of the conversation, so it rests at three lines and
// grows only for something you have not seen.
//
// Each row has its own presence rule, and every fallback ends in omitting the
// row -- never a placeholder, a zero, or a guess. Missing data therefore makes
// the card shorter, never broken. When every optional row is gone the kicker
// goes too: a heading over one line is furniture, and what is left is a label
// rather than a card.

import { PresetDeltaInfo, WorkspaceContext } from '../bridge/protocol';

export interface SetupCardProps {
  context: WorkspaceContext | null;
  expanded: boolean;
  onToggle: () => void;
}

// "~3h 50" over an hour, "~50 min" under it. Seconds are never shown: the
// number is an estimate and false precision reads as a promise.
export function formatPrintTime(seconds: number): string {
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `~${minutes} min`;
  return `~${Math.floor(minutes / 60)}h ${String(minutes % 60).padStart(2, '0')}`;
}

export function formatGrams(grams: number): string {
  return `${grams < 10 ? Math.round(grams * 10) / 10 : Math.round(grams)} g`;
}

// OrcaSlicer has no currency concept at all: filament_cost's unit is literally
// "money/kg" and the slicer's own G-code legend prints "Cost: 1.12" with no
// symbol. So the number carries a word instead, exactly as upstream does -- a
// bare "1.12" next to "47 g" would read as one more measurement.
export function formatCost(cost: number): string {
  return `cost ${cost.toFixed(2)}`;
}

interface CardModel {
  kicker: boolean;
  title: string;
  identity: string;
  facts: string[];
  deltas: PresetDeltaInfo[];
  deltaLabel: string;
  preset: string;
  material: string;
}

// Everything the card decides about itself, derived once so the resting card
// and the expansion cannot disagree about what is present.
export function cardModel(context: WorkspaceContext): CardModel {
  const active = context.plates.find((plate) => plate.active) ?? null;
  const estimate = active?.estimate ?? null;
  const deltas = context.presetDeltas;
  const title = context.setupIntent.trim();
  // The deltas are measured against the process preset, so that is the name
  // the card shows -- never the printer, which already lives in the top bar.
  // A printer with no process preset has no process settings to drift from
  // either, so there is nothing here to name and the card says nothing.
  const preset = context.printer.process.trim();
  // The material the plan is written against, so a spool swap shows up here as
  // well as on the chip. The "@printer" qualifier is dropped as the chip drops
  // it: it disambiguates presets in a settings list and says nothing here.
  const material = context.printer.filament.split('@')[0].trim();

  const deltaLabel = deltas.length === 0 ? '' :
    `${deltas.length} ${deltas.length === 1 ? 'change' : 'changes'} from preset`;

  // A card earns its heading by having something to say. The preset name on
  // its own does not count: nothing was delegated and nothing has moved, so
  // what is left is a label and the caller renders it as one.
  const substantive = Boolean(title) || estimate !== null || deltaLabel !== '';

  // With no intent to restate, the preset name takes the identity slot. It
  // goes on the identity ROW when the facts line already has a change count to
  // carry, because at dock width the two together push the useful clauses off
  // the end; with nothing else to say it rides inline and the card stays two
  // lines, as the design has it.
  const identity = !title && preset && deltaLabel !== '' ? preset : '';


  const facts: string[] = [];
  if (substantive && !title && !identity && preset) facts.push(preset);
  if (substantive && material) facts.push(material);
  if (estimate) {
    facts.push(formatPrintTime(estimate.printTimeSeconds));
    facts.push(formatGrams(estimate.materialGrams));
    // A priced job so small it rounds to nothing reads exactly like the
    // unpriced case, so it takes the same exit: no clause rather than 0.00.
    if (estimate.materialCost !== null && estimate.materialCost >= 0.005)
      facts.push(formatCost(estimate.materialCost));
  }
  // The estimate exists only after a slice. Saying so in a row the card
  // already has is honest and costs no height; growing a row for it would not.
  if (substantive && !estimate) facts.push('not sliced yet');

  return { kicker: substantive, title, identity, facts, deltas, deltaLabel, preset, material };
}

function DeltaList({ deltas }: { deltas: PresetDeltaInfo[] }) {
  return (
    <dl className="current-setup-deltas">
      {deltas.map((delta) => (
        <div key={delta.key}>
          <dt title={delta.key}>{delta.label || delta.key}</dt>
          <dd>
            <span className="was">{delta.preset}</span>
            <span aria-hidden="true"> → </span>
            <span className="now">{delta.value}</span>
          </dd>
        </div>
      ))}
    </dl>
  );
}

export function SetupCard({ context, expanded, onToggle }: SetupCardProps) {
  if (!context) return null;
  const model = cardModel(context);

  // Nothing was delegated and nothing has moved: what is left is a label, and
  // a label does not need a card, a border, or a heading over it.
  if (!model.kicker) {
    // Still the plan line even when it is only a label, so it names both the
    // preset and the material the plan is written against.
    const label = [model.preset, model.material].filter(Boolean).join(' · ');
    return label
      ? <div className="current-setup bare" data-testid="current-setup">{label}</div>
      : null;
  }

  return (
    <section className="current-setup" data-testid="current-setup" aria-label="Current setup">
      <p className="current-setup-kicker">
        <span>Current setup</span>
        {model.deltaLabel && (
          <button
            type="button"
            className="current-setup-chevron"
            aria-label={expanded ? 'Hide the changes' : 'Show the changes'}
            aria-expanded={expanded}
            data-testid="current-setup-chevron"
            onClick={onToggle}
          >
            {expanded ? '▴' : '▾'}
          </button>
        )}
      </p>
      {/* Clamped to one line: 40 characters is a contract with the agent,
          enforced in its prompt. All the card can do is keep a long one from
          breaking the layout -- the full sentence lives in the expansion. */}
      {model.title && <p className="current-setup-title" title={model.title}>{model.title}</p>}
      {model.identity && <p className="current-setup-identity" title={model.identity}>{model.identity}</p>}
      <p className="current-setup-facts">
        <span className="clauses">
          {model.facts.map((fact, index) => (
            <span key={index}>
              {index > 0 && <span className="sep" aria-hidden="true"> · </span>}
              {fact}
            </span>
          ))}
          {model.facts.length > 0 && model.deltaLabel && <span className="sep" aria-hidden="true"> · </span>}
        </span>
        {/* The count is the handle: a label exists exactly when there are
            deltas to open, so this never offers an empty list. */}
        {model.deltaLabel && (
          <button
            type="button"
            className="current-setup-handle"
            aria-expanded={expanded}
            data-testid="current-setup-handle"
            onClick={onToggle}
          >
            {model.deltaLabel}
          </button>
        )}
      </p>
      {expanded && model.deltaLabel && (
        // A temporary layer over the thread rather than a taller card: the
        // conversation is only covered while you are reading, and comes back
        // on the next tap or the next keystroke.
        <div className="current-setup-expansion" data-testid="current-setup-expansion">
          {model.title && <p className="current-setup-title-full">{model.title}</p>}
          <DeltaList deltas={model.deltas} />
          {/* What the deltas are measured against, so the list means
              something on its own. */}
          {model.preset && <p className="current-setup-base">on {model.preset}</p>}
        </div>
      )}
    </section>
  );
}
