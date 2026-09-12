import type { PrinterInfo } from '../bridge/protocol';
import { MonitorGlyph, PrinterGlyph } from './Glyphs';

// A printing printer opens into its job; every other printer stays one row.
// Giving both states the same green dot made them indistinguishable, so only
// a printing printer's dot is colored.
export function PrinterCard({
  printer,
  onLaunchMonitor,
}: {
  printer: PrinterInfo;
  onLaunchMonitor: (id: string) => void;
}) {
  const printing = printer.state === 'printing';
  const name = (
    <span className="printer-name-row">
      <PrinterGlyph />
      <span className="printer-name">{printer.name}</span>
    </span>
  );
  if (!printing) {
    return (
      <div className="printer-card collapsed">
        {name}
        <span className="status-dot" aria-hidden="true" />
      </div>
    );
  }
  return (
    <div className="printer-card">
      <div className="printer-head">
        {name}
        <span className="status-dot printing" aria-hidden="true" />
      </div>
      {printer.statusText && <div className="printer-job">{printer.statusText}</div>}
      {printer.progressPercent !== undefined && (
        <div
          className="printer-progress"
          role="progressbar"
          aria-valuenow={Math.round(printer.progressPercent)}
          aria-valuemin={0}
          aria-valuemax={100}
        >
          <span style={{ width: `${Math.max(0, Math.min(100, printer.progressPercent))}%` }} />
        </div>
      )}
      {printer.connectionText && <div className="printer-detail">{printer.connectionText}</div>}
      {printer.nozzleText && <div className="printer-detail">{printer.nozzleText}</div>}
      {printer.spools.length > 0 && (
        <div className="printer-spools">
          {printer.materialLabel && <span className="printer-material">{printer.materialLabel}</span>}
          {printer.spools.map((spool, index) => (
            // The border is what keeps a black spool visible on the dark card
            // and a white one on the light card.
            <span
              key={`${spool.colour}-${index}`}
              className="spool-swatch"
              style={{ background: spool.colour }}
            />
          ))}
        </div>
      )}
      {printer.canLaunchMonitor && (
        <button type="button" className="button-secondary launch-monitor" onClick={() => onLaunchMonitor(printer.id)}>
          <MonitorGlyph />
          Launch monitor
        </button>
      )}
    </div>
  );
}
