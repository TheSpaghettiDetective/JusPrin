import type { ConnectionState, PrinterInfo, SpoolInfo } from '../bridge/protocol';
import { MonitorGlyph, PrinterGlyph } from './Glyphs';
import { PrinterActions, PrinterMenu } from './PrinterMenu';

// Every card has one shape: the header, the job while one runs, three fact
// rows in a fixed order, then the action. Each line rests on something the
// host verified, and a row with nothing to say is left out rather than filled
// with a placeholder.
//
// The dot means the connection and nothing else: green for Online and
// Connected, the warning colour for Offline, no dot for Not connected. A job
// is carried by the header's job text and the progress bar.
const DOT_LABEL: Record<ConnectionState, string | undefined> = {
  online: 'Online',
  connected: 'Connected',
  offline: 'Offline',
  none: undefined,
};

// The distinct materials, in the order the spools hold them.
function materials(spools: SpoolInfo[]): string {
  const names: string[] = [];
  for (const spool of spools) {
    if (spool.material && !names.includes(spool.material)) names.push(spool.material);
  }
  return names.join(', ');
}

export function PrinterCard({
  printer,
  otherNames,
  actions,
  onLaunchMonitor,
  highlighted,
}: {
  printer: PrinterInfo;
  otherNames: string[];
  actions: PrinterActions;
  onLaunchMonitor: (id: string) => void;
  // The printer a receipt just named: the card's own CSS animation draws
  // attention to it once and ends on its own -- no timer either side manages.
  highlighted?: boolean;
}) {
  const printing = printer.state === 'printing';
  const dotLabel = DOT_LABEL[printer.connectionState];
  // A LAN-only device offers none of the actions; a menu of disabled rows
  // would be a button that does nothing, so it has none.
  const hasActions = printer.canOpenSettings || printer.canRename || printer.canRemove;
  const loadedText = materials(printer.spools);
  const swatches = printer.spools.filter((spool) => spool.colour);
  // One label per kind of printer, not per state: a print host opens its own
  // page in a window of its own, a Bambu printer opens the monitor.
  const host = printer.connectionKind === 'host';
  return (
    <div className={highlighted ? 'printer-card printer-card-added' : 'printer-card'}>
      <div className="printer-head">
        <span className="printer-name-row">
          <PrinterGlyph />
          <span className="printer-name">{printer.name}</span>
        </span>
        <span className="printer-head-end">
          {dotLabel && (
            <span className={`status-dot ${printer.connectionState}`}>
              <span className="visually-hidden">{dotLabel}</span>
            </span>
          )}
          {hasActions && <PrinterMenu printer={printer} otherNames={otherNames} actions={actions} />}
        </span>
      </div>
      {/* The job heads the card under the name: the rail is too narrow to
          share the name's line with it without hiding the name. */}
      {printing && printer.statusText && <div className="printer-job">{printer.statusText}</div>}
      {printing && printer.progressPercent !== undefined && (
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
      <dl className="printer-facts">
        {printer.connectionText && (
          <div className="printer-fact">
            <dt>Connection</dt>
            <dd>{printer.connectionText}</dd>
          </div>
        )}
        {printer.modelText && (
          <div className="printer-fact">
            <dt>Model</dt>
            <dd>{printer.modelText}</dd>
          </div>
        )}
        {(loadedText || swatches.length > 0) && (
          <div className="printer-fact">
            <dt>Loaded</dt>
            <dd className="printer-spools">
              {loadedText && <span className="printer-material">{loadedText}</span>}
              {swatches.map((spool, index) => (
                // The border is what keeps a black spool visible on the dark
                // card and a white one on the light card.
                <span key={`${spool.colour}-${index}`} className="spool-swatch" style={{ background: spool.colour }} />
              ))}
            </dd>
          </div>
        )}
      </dl>
      {printer.canLaunchMonitor ? (
        <button type="button" className="button-secondary launch-monitor" onClick={() => onLaunchMonitor(printer.id)}>
          <MonitorGlyph />
          {host ? 'Open printer window' : 'Launch monitor'}
        </button>
      ) : (
        printer.connectionAction === 'reconnect' &&
        actions.onConnect && (
          <button type="button" className="button-secondary launch-monitor" onClick={() => actions.onConnect?.(printer.id)}>
            Reconnect
          </button>
        )
      )}
    </div>
  );
}
