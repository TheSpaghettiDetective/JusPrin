import { BuildInfo, ExportedCopyInfo, PhysicalPrintInfo, SliceStatisticsInfo } from '../bridge/protocol';

export type ManufacturingHistoryEntry =
  | { kind: 'build'; seq: number; afterMessageId: string; record: BuildInfo }
  | { kind: 'copy'; seq: number; afterMessageId: string; record: ExportedCopyInfo }
  | { kind: 'print'; seq: number; afterMessageId: string; record: PhysicalPrintInfo };

function HashValue({ label, value }: { label: string; value: string }) {
  return (
    <div className="history-hash">
      <dt>{label}</dt>
      <dd><code title={value}>{value}</code></dd>
    </div>
  );
}

function Statistics({ statistics }: { statistics: SliceStatisticsInfo }) {
  const duration = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 }).format(statistics.printTimeSeconds / 60);
  const filament = new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 }).format(statistics.filamentMm);
  const grams = new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 }).format(statistics.materialGrams);
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

function Facts({ plate, printer, material, revision }: { plate: string; printer: string; material: string; revision: string }) {
  return (
    <dl className="history-facts">
      <div><dt>Plate</dt><dd>{plate || 'Unnamed plate'}</dd></div>
      <div><dt>Printer</dt><dd>{printer || 'Not recorded'}</dd></div>
      <div><dt>Material</dt><dd>{material || 'Not recorded'}</dd></div>
      <div><dt>Revision</dt><dd>{revision || 'Not available'}</dd></div>
    </dl>
  );
}

export function ManufacturingHistoryCard({ entry }: { entry: ManufacturingHistoryEntry }) {
  if (entry.kind === 'build') {
    const build = entry.record;
    return (
      <article className="history-card build-card" aria-label={`Build ${build.id}`}>
        <header>
          <div><span className="history-kind">Build</span><strong>{build.id}</strong></div>
          <span className={`history-status ${build.stale ? 'warning' : 'success'}`}>{build.stale ? 'Stale' : 'Current inputs'}</span>
        </header>
        <Facts plate={build.plateName} printer={build.printer} material={build.material} revision={build.revisionId} />
        <Statistics statistics={build.statistics} />
        <dl className="history-hashes">
          <HashValue label="Input SHA-256" value={build.manufacturingInputHash} />
          <HashValue label="G-code SHA-256" value={build.outputHash} />
        </dl>
        <p className="history-provenance">{build.slicerVersion} · {build.configurationProvenance}</p>
        {build.warnings.length > 0 && <ul className="history-warnings">{build.warnings.map((warning, index) => <li key={index}>{warning}</li>)}</ul>}
      </article>
    );
  }

  if (entry.kind === 'copy') {
    const copy = entry.record;
    const status = copy.modified ? 'Checksum differs' : copy.verified ? 'Checksum verified' : 'Not checked';
    return (
      <article className="history-card copy-card" aria-label={`Exported copy ${copy.id}`}>
        <header>
          <div><span className="history-kind">Exported copy</span><strong>{copy.id}</strong></div>
          <span className={`history-status ${copy.modified ? 'warning' : copy.verified ? 'success' : ''}`}>{status}</span>
        </header>
        <dl className="history-facts">
          <div><dt>Build</dt><dd>{copy.buildId}</dd></div>
          <div><dt>Destination</dt><dd className="long-content">{copy.destination}</dd></div>
        </dl>
        <dl className="history-hashes"><HashValue label="Expected SHA-256" value={copy.expectedOutputHash} /></dl>
      </article>
    );
  }

  const print = entry.record;
  return (
    <article className="history-card print-card" aria-label={`Physical print ${print.id}`}>
      <header>
        <div><span className="history-kind">Physical print</span><strong>{print.id}</strong></div>
        <span className={`history-status ${print.outcome === 'completed' ? 'success' : 'warning'}`}>{print.outcome}</span>
      </header>
      {print.timelineRemoved && <p className="timeline-removed" role="status">Project timeline removed</p>}
      <Facts plate={print.plateName} printer={print.printer} material={print.material} revision={print.revisionId} />
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
    </article>
  );
}
