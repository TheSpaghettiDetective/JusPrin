import { ReactNode } from 'react';
import { BuildInfo, ExportedCopyInfo, PhysicalPrintInfo, SliceStatisticsInfo } from '../bridge/protocol';
import { chatTimestamp } from './ChatNavigation';

export type ManufacturingHistoryEntry =
  | { kind: 'build'; seq: number; afterMessageId: string; record: BuildInfo }
  | { kind: 'copy'; seq: number; afterMessageId: string; record: ExportedCopyInfo }
  | { kind: 'print'; seq: number; afterMessageId: string; record: PhysicalPrintInfo };

function decimal(value: number, digits: number): string {
  return new Intl.NumberFormat(undefined, { maximumFractionDigits: digits }).format(value);
}

// The resting line reads the print time the way a person says it ("2h 36m");
// the exact minute count stays in the statistics list behind the disclosure.
function coarseDuration(seconds: number): string {
  const minutes = Math.round(seconds / 60);
  const hours = Math.floor(minutes / 60);
  return hours > 0 ? `${hours}h ${minutes % 60}m` : `${minutes}m`;
}

// The leading eight hex digits in the two groups the timeline reads them as.
// The <code> keeps the whole digest in its title, as the expanded rows do.
function shortHash(hash: string): string {
  return `${hash.slice(0, 4)} ${hash.slice(4, 8)}`;
}

function summaryStats(statistics: SliceStatisticsInfo): string {
  return `${coarseDuration(statistics.printTimeSeconds)} · ${decimal(statistics.materialGrams, 1)} g · ${statistics.layerCount} layers`;
}

function HashValue({ label, value }: { label: string; value: string }) {
  return (
    <div className="history-hash">
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
    <dl className="history-stats" aria-label="Slice statistics">
      <div><dt>Time</dt><dd>{duration} min</dd></div>
      <div><dt>Filament</dt><dd>{filament} mm</dd></div>
      <div><dt>Material</dt><dd>{grams} g · {cost}</dd></div>
      <div><dt>Layers</dt><dd>{statistics.layerCount}</dd></div>
    </dl>
  );
}

function Facts({ plate, printer, material }: { plate: string; printer: string; material: string }) {
  return (
    <dl className="history-facts">
      <div><dt>Plate</dt><dd>{plate || 'Unnamed plate'}</dd></div>
      <div><dt>Printer</dt><dd>{printer || 'Not recorded'}</dd></div>
      <div><dt>Material</dt><dd>{material || 'Not recorded'}</dd></div>
    </dl>
  );
}

// The card rests as this summary. Everything the record carries stays mounted
// in the sibling detail, which the disclosure reveals on demand rather than
// re-rendering: product-definition.md §2.7 puts the full change history under
// "what should appear when expanded".
function HistorySummary({ kind, id, status, tone, time, children }: {
  kind: string;
  id: string;
  status: string;
  tone: string;
  time: string;
  children?: ReactNode;
}) {
  return (
    <summary className="history-summary">
      <span className="history-summary-head">
        <span className="history-title"><span className="history-kind">{kind}</span><strong>{id}</strong></span>
        <span className={`history-status ${tone}`}>{status}</span>
        {time && <span className="history-time">{time}</span>}
        <span className="history-disclosure" aria-hidden="true" />
      </span>
      {children}
    </summary>
  );
}

export function ManufacturingHistoryCard({ entry }: { entry: ManufacturingHistoryEntry }) {
  if (entry.kind === 'build') {
    const build = entry.record;
    return (
      <article className="history-card build-card" aria-label={`Build ${build.id}`}>
        <details className="history-details">
          <HistorySummary
            kind="Build"
            id={build.id}
            status={build.stale ? 'Stale' : 'Current inputs'}
            tone={build.stale ? 'warning' : 'success'}
            time={chatTimestamp(build.createdAt)}
          >
            <span className="history-line">{summaryStats(build.statistics)}</span>
            {build.outputHash && <span className="history-line"><code title={build.outputHash}>{shortHash(build.outputHash)}</code></span>}
            {build.stale && <span className="history-line history-note">The project changed after this build — slice again to match the plate.</span>}
          </HistorySummary>
          <div className="history-detail">
            <Facts plate={build.plateName} printer={build.printer} material={build.material} />
            <Statistics statistics={build.statistics} />
            <dl className="history-hashes">
              <HashValue label="Input SHA-256" value={build.manufacturingInputHash} />
              <HashValue label="G-code SHA-256" value={build.outputHash} />
            </dl>
            <p className="history-provenance">{build.slicerVersion} · {build.configurationProvenance}</p>
            {build.warnings.length > 0 && <ul className="history-warnings">{build.warnings.map((warning, index) => <li key={index}>{warning}</li>)}</ul>}
          </div>
        </details>
      </article>
    );
  }

  if (entry.kind === 'copy') {
    const copy = entry.record;
    const status = copy.modified ? 'Checksum differs' : copy.verified ? 'Checksum verified' : 'Not checked';
    return (
      <article className="history-card copy-card" aria-label={`Exported copy ${copy.id}`}>
        <details className="history-details">
          <HistorySummary
            kind="Exported copy"
            id={copy.id}
            status={status}
            tone={copy.modified ? 'warning' : copy.verified ? 'success' : ''}
            time={chatTimestamp(copy.createdAt)}
          >
            {copy.destination && <span className="history-line long-content">{copy.destination}</span>}
          </HistorySummary>
          <div className="history-detail">
            <dl className="history-facts">
              <div><dt>Build</dt><dd>{copy.buildId}</dd></div>
              <div><dt>Destination</dt><dd className="long-content">{copy.destination}</dd></div>
            </dl>
            <dl className="history-hashes"><HashValue label="Expected SHA-256" value={copy.expectedOutputHash} /></dl>
          </div>
        </details>
      </article>
    );
  }

  const print = entry.record;
  const started = chatTimestamp(print.startedAt);
  const ended = chatTimestamp(print.endedAt);
  const runWindow = started && ended ? `${started} → ${ended}` : started || ended;
  return (
    <article className="history-card print-card" aria-label={`Physical print ${print.id}`}>
      <details className="history-details">
        <HistorySummary
          kind="Physical print"
          id={print.id}
          status={print.outcome}
          tone={print.outcome === 'completed' ? 'success' : 'warning'}
          time={runWindow}
        >
          {print.printer && <span className="history-line">{print.printer}</span>}
          {print.failure && <span className="history-line history-note">{print.failure}</span>}
        </HistorySummary>
        <div className="history-detail">
          <Facts plate={print.plateName} printer={print.printer} material={print.material} />
          <dl className="history-facts">
            <div><dt>Started</dt><dd>{print.startedAt}</dd></div>
            <div><dt>Ended</dt><dd>{print.endedAt}</dd></div>
            {print.failure && <div><dt>Failure</dt><dd>{print.failure}</dd></div>}
          </dl>
          <Statistics statistics={print.statistics} />
          <dl className="history-hashes">
            <HashValue label="Input SHA-256" value={print.manufacturingInputHash} />
            <HashValue label="Build SHA-256" value={print.outputHash} />
            <HashValue label="Printed G-code SHA-256" value={print.gcodeHash} />
          </dl>
        </div>
      </details>
    </article>
  );
}
