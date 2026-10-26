import { ReactNode } from 'react';
import { PhysicalPrintInfo, SliceStatisticsInfo } from '../bridge/protocol';
import { chatTimestamp } from './ChatNavigation';

export type ManufacturingHistoryEntry = { kind: 'print'; seq: number; afterMessageId: string; record: PhysicalPrintInfo };

function decimal(value: number, digits: number): string {
  return new Intl.NumberFormat(undefined, { maximumFractionDigits: digits }).format(value);
}

// The resting line reads the print time the way a person says it ("2h 36m");
// the exact minute count stays in the statistics list behind the disclosure.
function coarseDuration(seconds: number): string {
  const minutes = Math.round(seconds / 60);
  const hours = Math.floor(minutes / 60);
  return hours > 0 ? `${hours}h ${String(minutes % 60).padStart(2, '0')}m` : `${minutes}m`;
}

// The leading eight hex digits in the two groups the timeline reads them as.
// The <code> keeps the whole digest in its title, as the expanded rows do.
function shortHash(hash: string): string {
  return `${hash.slice(0, 4)} ${hash.slice(4, 8)}`;
}

function HashValue({ label, value }: { label: string; value: string }) {
  return (
    <div className="history-block history-hash">
      <dt>{label}</dt>
      <dd><code title={value}>{value}</code></dd>
    </div>
  );
}

function Statistics({ statistics }: { statistics: SliceStatisticsInfo }) {
  const duration = decimal(statistics.printTimeSeconds / 60, 0);
  const filament = decimal(statistics.filamentMm, 1);
  const grams = decimal(statistics.materialGrams, 1);
  const cost = new Intl.NumberFormat(undefined, { style: 'currency', currency: 'USD' }).format(statistics.materialCost);
  return (
    <dl className="history-rows" aria-label="Slice statistics">
      <div><dt>Time</dt><dd>{duration} min</dd></div>
      <div><dt>Filament</dt><dd>{filament} mm</dd></div>
      <div><dt>Material</dt><dd>{grams} g · {cost}</dd></div>
      <div><dt>Layers</dt><dd>{statistics.layerCount}</dd></div>
    </dl>
  );
}

function Facts({ plate, printer, material }: { plate: string; printer: string; material: string }) {
  return (
    <dl className="history-rows">
      <div><dt>Plate</dt><dd>{plate || 'Unnamed plate'}</dd></div>
      <div><dt>Printer</dt><dd>{printer || 'Not recorded'}</dd></div>
      <div><dt>Material</dt><dd>{material || 'Not recorded'}</dd></div>
    </dl>
  );
}

// A line of state on a record: its glyph and its words, so the state never
// rests on colour alone.
type Tone = 'success' | 'warning' | 'neutral';
const toneIcon: Record<Tone, string> = { success: 'check', warning: 'triangle-alert', neutral: 'circle-minus' };

function Status({ tone, children }: { tone: Tone; children: ReactNode }) {
  return (
    <span className={`history-status ${tone}`}>
      <span className={`jp-icon jp-icon-${toneIcon[tone]}`} aria-hidden="true" />
      <span>{children}</span>
    </span>
  );
}

// What kind of record this is, which one, and when; the chevron at the end
// reads the disclosure. The whole summary is the target.
function Heading({ kind, id, idTitle, time }: { kind: string; id?: string; idTitle?: string; time: string }) {
  return (
    <span className="history-heading">
      <span className="history-title">
        <span className="history-kind">{kind}</span>
        {id && <code title={idTitle}>{id}</code>}
      </span>
      {time && <span className="history-time">{time}</span>}
      <span className="history-disclosure" aria-hidden="true" />
    </span>
  );
}

// The card rests as its summary. Everything the record carries stays mounted
// in the sibling detail, which the disclosure reveals on demand rather than
// re-rendering: product-definition.md §2.7 puts the full change history under
// "what should appear when expanded".
export function ManufacturingHistoryCard({ entry, onDiscussFailure, discussDisabled = false }: {
  entry: ManufacturingHistoryEntry;
  onDiscussFailure?: (print: PhysicalPrintInfo) => void;
  discussDisabled?: boolean;
}) {
  const print = entry.record;
  const started = chatTimestamp(print.startedAt);
  const ended = chatTimestamp(print.endedAt);
  const runWindow = started && ended ? `${started} → ${ended}` : started || ended;
  const elapsedSeconds = (Date.parse(print.endedAt) - Date.parse(print.startedAt)) / 1000;
  const elapsed = Number.isFinite(elapsedSeconds) && elapsedSeconds >= 0 ? coarseDuration(elapsedSeconds) : '';
  const details = (
    <div className="history-detail">
      <Facts plate={print.plateName} printer={print.printer} material={print.material} />
      <dl className="history-rows">
        <div><dt>Started</dt><dd>{print.startedAt}</dd></div>
        <div><dt>Ended</dt><dd>{print.endedAt}</dd></div>
        {print.failure && <div><dt>Failure</dt><dd>{print.failure}</dd></div>}
      </dl>
      <Statistics statistics={print.statistics} />
      <dl className="history-rows">
        <HashValue label="Input SHA-256" value={print.manufacturingInputHash} />
        <HashValue label="Sliced G-code SHA-256" value={print.outputHash} />
        <HashValue label="Printed G-code SHA-256" value={print.gcodeHash} />
      </dl>
    </div>
  );
  if (print.outcome === 'failed') {
    return (
      <article className="history-card print-card print-failed" aria-label={`Failed physical print ${print.id}`}>
        <details className="history-details">
          <summary className="history-summary">
            <Heading kind="Print failed" time={runWindow} />
            <strong className="failure-title">{print.printer || 'Printer'} · stopped{print.stoppedPercent !== undefined ? ` at ${print.stoppedPercent}%${elapsed ? `, ${elapsed} in` : ''}` : elapsed ? ` after ${elapsed}` : ''}</strong>
            {print.failure && <span className="failure-reason">{print.failure}{print.gcodeHash && <> · G-code <code title={print.gcodeHash}>{shortHash(print.gcodeHash)}</code></>}</span>}
          </summary>
          {details}
        </details>
        {onDiscussFailure && <button type="button" className="history-action" disabled={discussDisabled}
          onClick={() => onDiscussFailure(print)}>Discuss this failure</button>}
      </article>
    );
  }
  return (
    <article className="history-card print-card" aria-label={`Physical print ${print.id}`}>
      <details className="history-details">
        <summary className="history-summary">
          <Heading kind="Physical print" id={print.id} time={runWindow} />
          <Status tone={print.outcome === 'completed' ? 'success' : 'warning'}>{print.outcome}</Status>
          {print.printer && <span className="history-line primary">{print.printer}</span>}
          {print.failure && <span className="history-line history-note">{print.failure}</span>}
        </summary>
        {details}
      </details>
    </article>
  );
}
