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
  // The agent is mid-thought. The card greys but does not empty: what it shows
  // is still exactly what Slice would produce this second, so the invariant
  // holds even while the answer is being written.
  working?: boolean;
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

// OrcaSlicer has no currency concept -- filament_cost's unit is literally
// "money/kg" -- so the host reads the ISO code from the machine's regional
// settings and the page formats it the way that viewer's locale writes money:
// symbol placement, grouping, and the right number of decimals (yen has none).
// Without a code there is nothing to denominate the number in, so it carries
// the word "cost" the way the slicer's own G-code legend does.
export function formatCost(cost: number, currency: string): string {
  if (currency) {
    try {
      return new Intl.NumberFormat(undefined, { style: 'currency', currency }).format(cost);
    } catch {
      // An unknown code is not worth losing the number over.
    }
  }
  return `cost ${cost.toFixed(2)}`;
}

interface CardModel {
  kicker: boolean;
  // 2c: the figure a running slice is about to replace, struck through, with
  // "re-slicing..." beside it on the same line. The number stays because the
  // reader is watching for the difference, and a blank kills the comparison.
  struckEstimate: string;
  inlineNote: string;
  // 2d: the estimate reads plainly -- it is the last honest figure, not a
  // discarded one -- and a plain-language row underneath says what took it.
  reason: string;
  outOfDate: boolean;
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

  const status = active?.estimateStatus ?? 'current';
  const changeWord = deltas.length === 1 ? 'change' : 'changes';
  const deltaLabel = deltas.length === 0 ? '' :
    status === 'recomputing' ? `${deltas.length} ${changeWord}`
                             : `${deltas.length} ${changeWord} from preset`;

  // A card earns its heading by having something to say. The preset name on
  // its own does not count: nothing was delegated and nothing has moved, so
  // what is left is a label and the caller renders it as one.
  const substantive = Boolean(title) || estimate !== null || deltaLabel !== '';

  // With no intent to restate, the preset name takes the identity slot -- and
  // it takes a whole row. Sharing the facts line was tried and measured in the
  // running app: preset names run to "0.08mm Extra Fine @MyKlipper", the dock
  // is ~310px, and whatever came last was clipped away. A row costs 16px; a
  // silently truncated cost is worse.
  const identity = !title && preset && substantive ? preset : '';

  const recomputing = estimate !== null && status === 'recomputing';
  const outOfDate = estimate !== null && status === 'stale';

  const facts: string[] = [];
  // Stale keeps its figure on the facts line, unstruck: it is the last number
  // the card could defend, not one already superseded by a better answer.
  if (estimate && !recomputing) {
    facts.push(formatPrintTime(estimate.printTimeSeconds));
    facts.push(formatGrams(estimate.materialGrams));
    // A priced job so small it rounds to nothing reads exactly like the
    // unpriced case, so it takes the same exit: no clause rather than 0.00.
    if (estimate.materialCost !== null && estimate.materialCost >= 0.005)
      facts.push(formatCost(estimate.materialCost, context.currency));
  }

  const struckEstimate = recomputing && estimate
    ? [formatPrintTime(estimate.printTimeSeconds), formatGrams(estimate.materialGrams)].join(' · ')
    : '';
  const inlineNote = recomputing ? 're-slicing…' : '';
  // Names the action when it is known and asks for the re-slice either way,
  // rather than attributing the loss to something the reader did not do.
  const reason = outOfDate ? (active?.invalidatedBy ?? '') : '';
  // "Not sliced yet" is a claim about the plate, not about whether we happen
  // to hold a number: a sliced plate that yielded no usable estimate must not
  // be described as unsliced. With no plate at all there is nothing to say.
  if (substantive && !estimate && active !== null && !active.sliced) facts.push('not sliced yet');

  return { kicker: substantive, title, identity, facts, deltas, deltaLabel, preset, material,
           struckEstimate, inlineNote, reason, outOfDate };
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

export function SetupCard({ context, expanded, onToggle, working }: SetupCardProps) {
  if (!context) return null;
  const model = cardModel(context);

  // Nothing was delegated and nothing has moved: what is left is a label, and
  // a label does not need a card, a border, or a heading over it.
  if (!model.kicker) {
    // Still the plan line even when it is only a label, so it names both the
    // preset and the material the plan is written against.
    const label = [model.preset, model.material].filter(Boolean).join(' · ');
    return label
      ? <div className={working ? 'current-setup bare working' : 'current-setup bare'} data-testid="current-setup">{label}</div>
      : null;
  }

  return (
    <section className={['current-setup', working ? 'working' : '', model.outOfDate ? 'out-of-date' : ''].filter(Boolean).join(' ')}
      data-testid="current-setup"
      aria-label="Current setup" aria-busy={working || undefined}>
      <p className="current-setup-eyebrow">
        <span>Current setup</span>
        {/* Out of date is a legitimate state, not an error: the card is not
            broken, it simply will not stand behind the number any more. */}
        {model.outOfDate && <span className="current-setup-status out-of-date">· out of date</span>}
        {/* The eyebrow's right half is where a state says its one word. */}
        {working && <span className="current-setup-status">working…</span>}
        {model.deltaLabel && (
          <button
            type="button"
            className="current-setup-chevron"
            aria-label={expanded ? 'Hide the changes' : 'Show the changes'}
            aria-expanded={expanded}
            data-testid="current-setup-chevron"
            onClick={onToggle}
          >
            {expanded ? '▲' : '▼'}
          </button>
        )}
      </p>
      {/* Clamped to one line: 40 characters is a contract with the agent,
          enforced in its prompt. All the card can do is keep a long one from
          breaking the layout -- the full sentence lives in the expansion. */}
      {model.title && <p className="current-setup-title" title={model.title}>{model.title}</p>}
      {model.identity && <p className="current-setup-identity" title={model.identity}>{model.identity}</p>}
      {/* The row is omitted, not left blank: a sliced plate with no usable
          estimate and an untouched preset has nothing to put on this line. */}
      {(model.facts.length > 0 || model.deltaLabel || model.struckEstimate) && <p className="current-setup-cost">
        <span className="clauses">
          {/* Struck through rather than removed: you are usually watching for
              the difference, and a blank kills the comparison. */}
          {model.struckEstimate && <span className="superseded">{model.struckEstimate}</span>}
          {model.struckEstimate && model.inlineNote && ' '}
          {model.inlineNote && <span className="estimate-note">{model.inlineNote}</span>}
          {model.struckEstimate && model.facts.length > 0 &&
            <span className="sep" aria-hidden="true"> · </span>}
          {model.facts.map((fact, index) => (
            <span key={index}>
              {index > 0 && <span className="sep" aria-hidden="true"> · </span>}
              {fact}
            </span>
          ))}
          {model.facts.length > 0 && model.deltaLabel &&
            <span className="sep" aria-hidden="true"> · </span>}
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
      </p>}
      {/* Plain language, then the action. "re-slice" is the thing to do, so it
          reads as a link rather than as more prose. */}
      {model.outOfDate && (
        <p className="current-setup-note">
          {model.reason && <span>{model.reason} — </span>}
          <span className="action">re-slice</span>
        </p>
      )}
      {expanded && model.deltaLabel && (
        // The same card, grown. The thread behind it dims rather than going
        // away, so the conversation is still legibly there while you read.
        <div data-testid="current-setup-expansion">
          <DeltaList deltas={model.deltas} />
          {/* What the deltas are measured against, so the list means
              something on its own. */}
          {model.preset && <p className="current-setup-base">on {model.preset}</p>}
        </div>
      )}
    </section>
  );
}
