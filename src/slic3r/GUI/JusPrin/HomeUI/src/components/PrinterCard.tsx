import type { PrinterInfo } from '../bridge/protocol';
import { MonitorGlyph, PrinterGlyph } from './Glyphs';
import { PrinterActions, PrinterMenu } from './PrinterMenu';

// A printing printer opens into its job; every other printer stays one row.
// Giving both states the same green dot made them indistinguishable, so only
// a printing printer's dot is colored. Either way the header row ends in the
// printer's actions menu.
export function PrinterCard({
  printer,
  otherNames,
  actions,
  onLaunchMonitor,
}: {
  printer: PrinterInfo;
  otherNames: string[];
  actions: PrinterActions;
  onLaunchMonitor: (id: string) => void;
}) {
  const printing = printer.state === 'printing';
  const name = (
    <span className="printer-name-row">
      <PrinterGlyph />
      <span className="printer-name">{printer.name}</span>
    </span>
  );
  // A LAN-only device offers none of the actions; a menu of disabled rows
  // would be a button that does nothing, so it has none.
  const hasActions = printer.canOpenSettings || printer.canRename || printer.canRemove;
  const end = (
    <span className="printer-head-end">
      <span className={printing ? 'status-dot printing' : 'status-dot'} aria-hidden="true" />
      {hasActions && <PrinterMenu printer={printer} otherNames={otherNames} actions={actions} />}
    </span>
  );
  // Highlighted for exactly the one screen after "Add this printer" (F16);
  // the card's own animation ends this on its own, with no timer to manage.
  const cardClass = `printer-card${printer.justAdded ? ' just-added' : ''}`;
  if (!printing) {
    return (
      <div className={`${cardClass} collapsed`}>
        {name}
        {end}
      </div>
    );
  }
  return (
    <div className={cardClass}>
      <div className="printer-head">
        {name}
        {end}
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
