import { useEffect, useRef, useState } from 'react';
import type { PrinterCamera, PrinterInfo, PrinterStatus, SpoolInfo } from '../bridge/protocol';
import { printerHistoryLine } from '../history';
import { HistoryGlyph, MonitorGlyph, PrinterGlyph } from './Glyphs';
import { PrinterActions, PrinterMenu } from './PrinterMenu';
import { STAND_IN_PRINTER } from './Onboarding';

// Every card is the same list of fields, top to bottom, and shows each one
// only when the host sent it, so a card grows and shrinks with what its
// printer reports. Only the name and the status are always there, but on a
// printer with no connection ever set up, where the card says what
// connecting is for instead. Nothing stands in for a field the printer did
// not report.

const STATUS_WORD: Record<PrinterStatus, string> = {
  not_connected: 'Not connected',
  connecting: 'Connecting…',
  connected: 'Connected',
  ready: 'Ready',
  printing: 'Printing',
  paused: 'Paused',
  attention: 'Needs attention',
};

// "2h 14m left", "14m left".
export function timeLeft(seconds: number): string {
  const minutes = Math.max(1, Math.floor(seconds / 60));
  return minutes >= 60 ? `${Math.floor(minutes / 60)}h ${minutes % 60}m left` : `${minutes}m left`;
}

// What follows the status word: how far a job is, or how the last one ended.
export function statusDetail(printer: PrinterInfo): string | undefined {
  const percent = printer.progressPercent;
  if (printer.status === 'printing') {
    const parts: string[] = [];
    if (percent !== undefined) parts.push(`${Math.round(percent)}%`);
    if (printer.secondsLeft) parts.push(timeLeft(printer.secondsLeft));
    return parts.length > 0 ? parts.join(' · ') : undefined;
  }
  if (printer.status === 'paused' || printer.status === 'attention')
    return percent !== undefined ? `at ${Math.round(percent)}%` : undefined;
  if (printer.status === 'ready' && printer.lastPrint) return `last print ${printer.lastPrint}`;
  return undefined;
}

// "Last connected just now", "… 20 min ago", "… 3h ago", "… 2 days ago".
export function lastConnected(iso: string, now: Date = new Date()): string | undefined {
  const at = Date.parse(iso);
  if (Number.isNaN(at)) return undefined;
  const minutes = Math.floor((now.getTime() - at) / 60000);
  if (minutes < 1) return 'Last connected just now';
  if (minutes < 60) return `Last connected ${minutes} min ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `Last connected ${hours}h ago`;
  const days = Math.floor(hours / 24);
  return `Last connected ${days} ${days === 1 ? 'day' : 'days'} ago`;
}

// The distinct materials, in the order the spools hold them.
function materials(spools: SpoolInfo[]): string {
  const names: string[] = [];
  for (const spool of spools) {
    if (spool.material && !names.includes(spool.material)) names.push(spool.material);
  }
  return names.join(', ');
}

// In place of the printer the first launch set up when the person skipped
// choosing one: files slice for it, but it is not theirs, so it has no
// status, connection or menu. Home lists it only until they add their own.
export function NoPrinterCard({ onAdd }: { onAdd: () => void }) {
  return (
    <div className="printer-card no-printer-card">
      <span className="printer-name-row">
        <PrinterGlyph />
        <span className="printer-name">No printer yet</span>
      </span>
      <p className="no-printer-text">{STAND_IN_PRINTER}</p>
      <button type="button" className="button-primary button-primary-small" onClick={onAdd}>Add your printer</button>
    </div>
  );
}

// How often a camera with only a picture is asked for the next one.
const SNAPSHOT_EVERY_MS = 2000;
// How often the camera is asked, out of sight, whether it still answers.
export const CAMERA_CHECK_EVERY_MS = 5000;

// A URL asked again rather than served from the cache.
function fresh(url: string, tick: number): string {
  return `${url}${url.includes('?') ? '&' : '?'}t=${tick}`;
}

// The printer's camera, on the card for as long as it answers: an MJPEG
// stream as it is, a camera with only a picture asked again every couple of
// seconds. A stream that stops raises no error in the app's web view -- the
// image keeps its last frame -- so the camera is also asked for one picture
// every few seconds, out of sight. One that does not answer is taken off the
// card rather than left as a frozen or broken image, and comes back, its
// stream reconnected, once it answers again.
function Camera({ camera, name, status }: { camera: PrinterCamera; name: string; status: PrinterStatus }) {
  const [answering, setAnsweringState] = useState(true);
  const answeringNow = useRef(true);
  // Bumped when a camera answers again, so the stream connects anew.
  const [connection, setConnection] = useState(0);
  const setAnswering = (answered: boolean) => {
    if (answered && !answeringNow.current) setConnection((value) => value + 1);
    answeringNow.current = answered;
    setAnsweringState(answered);
  };
  const [tick, setTick] = useState(0);
  const stream = camera.streamUrl;
  const snapshot = camera.snapshotUrl;
  useEffect(() => {
    answeringNow.current = true;
    setAnsweringState(true);
  }, [stream, snapshot]);
  useEffect(() => {
    if (stream || !snapshot || !answering) return undefined;
    const timer = window.setInterval(() => setTick((value) => value + 1), SNAPSHOT_EVERY_MS);
    return () => window.clearInterval(timer);
  }, [stream, snapshot, answering]);
  useEffect(() => {
    const check = snapshot ?? stream;
    if (!check) return undefined;
    let asked = 0;
    let pending: HTMLImageElement | undefined;
    const close = (probe: HTMLImageElement) => {
      probe.onload = null;
      probe.onerror = null;
      // Closes a stream asked for as the check.
      probe.src = '';
    };
    const ask = () => {
      // A camera still silent since the last check -- a Pi that lost power
      // leaves the connection hanging -- is not answering.
      if (pending) {
        close(pending);
        setAnswering(false);
      }
      const probe = new Image();
      pending = probe;
      const settle = (answered: boolean) => {
        close(probe);
        pending = undefined;
        setAnswering(answered);
      };
      probe.onload = () => settle(true);
      probe.onerror = () => settle(false);
      probe.src = fresh(check, asked++);
    };
    const timer = window.setInterval(ask, CAMERA_CHECK_EVERY_MS);
    return () => {
      window.clearInterval(timer);
      if (pending) close(pending);
    };
  }, [stream, snapshot]);
  if (!answering) return null;
  const source = stream ?? (snapshot ? fresh(snapshot, tick) : undefined);
  if (!source) return null;
  return (
    <div className="printer-camera">
      <img key={connection} src={source} alt={`Camera view of ${name}`} onError={() => setAnswering(false)} />
      <span className="printer-camera-live">
        <span className={`status-dot ${status}`} aria-hidden="true" />
        LIVE
      </span>
    </div>
  );
}

export function PrinterCard({
  printer,
  otherNames,
  actions,
  onLaunchMonitor,
  onHistory,
  highlighted,
  now,
}: {
  printer: PrinterInfo;
  otherNames: string[];
  actions: PrinterActions;
  // Launch monitor, or a print host's Open printer window.
  onLaunchMonitor: (id: string) => void;
  // The Print history filtered to this printer (board history-05).
  onHistory: (id: string) => void;
  // The printer a receipt just named: the card's own CSS animation draws
  // attention to it once and ends on its own -- no timer either side manages.
  highlighted?: boolean;
  now?: Date;
}) {
  // A LAN-only device offers none of the actions; a menu of disabled rows
  // would be a button that does nothing, so it has none.
  const hasActions = printer.canOpenSettings || printer.canRename || printer.canRemove;
  const detail = statusDetail(printer);
  const jobRuns = printer.status === 'printing' || printer.status === 'paused' || printer.status === 'attention';
  const since =
    printer.status === 'not_connected' && printer.lastConnectedAt ? lastConnected(printer.lastConnectedAt, now) : undefined;
  const facts = [printer.connectionLabel, printer.address, printer.modelText].filter(Boolean).join(' · ');
  const loadedText = materials(printer.spools);
  const swatches = printer.spools.filter((spool) => spool.colour);
  const quiet = printer.status === 'not_connected' || printer.status === 'connecting';
  return (
    <div className={highlighted ? 'printer-card printer-card-added' : 'printer-card'}>
      {printer.camera && <Camera camera={printer.camera} name={printer.name} status={printer.status} />}
      <div className="printer-head">
        <span className="printer-name-row">
          <PrinterGlyph />
          <span className="printer-name">{printer.name}</span>
        </span>
        {hasActions && <PrinterMenu printer={printer} otherNames={otherNames} actions={actions} />}
      </div>
      {!printer.noConnection && (
        <div className="printer-status">
          {!quiet && <span className={`status-dot ${printer.status}`} aria-hidden="true" />}
          <span className="printer-status-text">
            <span className={quiet ? 'printer-status-word quiet' : 'printer-status-word'}>{STATUS_WORD[printer.status]}</span>
            {detail && <span className="printer-status-detail"> · {detail}</span>}
          </span>
        </div>
      )}
      {jobRuns && printer.job && <div className="printer-job">{printer.job}</div>}
      {jobRuns && printer.progressPercent !== undefined && (
        <div
          className={`printer-progress ${printer.status}`}
          role="progressbar"
          aria-label={printer.job ? `${printer.job}, ${Math.round(printer.progressPercent)} percent` : undefined}
          aria-valuenow={Math.round(printer.progressPercent)}
          aria-valuemin={0}
          aria-valuemax={100}
        >
          <span style={{ width: `${Math.max(0, Math.min(100, printer.progressPercent))}%` }} />
        </div>
      )}
      {printer.message && <p className="printer-message">{printer.message}</p>}
      {since && <div className="printer-since">{since}</div>}
      {facts && <div className="printer-facts">{facts}</div>}
      {(loadedText || swatches.length > 0) && (
        <div className="printer-spools">
          {loadedText && <span className="printer-material">{loadedText}</span>}
          {swatches.map((spool, index) => (
            // The border is what keeps a black spool visible on the dark
            // card and a white one on the light card.
            <span key={`${spool.colour}-${index}`} className="spool-swatch" style={{ background: spool.colour }} />
          ))}
        </div>
      )}
      {printer.history && (
        <button type="button" className="project-history printer-history" onClick={() => onHistory(printer.id)}>
          <HistoryGlyph />
          <span className="project-history-text">{printerHistoryLine(printer.history)}</span>
        </button>
      )}
      {(printer.action === 'monitor' || printer.action === 'printer_window') && (
        <button type="button" className="button-secondary launch-monitor" onClick={() => onLaunchMonitor(printer.id)}>
          <MonitorGlyph />
          {printer.action === 'printer_window' ? 'Open printer window' : 'Launch monitor'}
        </button>
      )}
      {(printer.action === 'reconnect' || printer.action === 'connect') && actions.onConnect && (
        <div className={printer.noConnection ? 'printer-connect-row with-why' : 'printer-connect-row'}>
          <button type="button" className="button-secondary launch-monitor" onClick={() => actions.onConnect?.(printer.id)}>
            {printer.action === 'reconnect' ? 'Reconnect' : 'Connect…'}
          </button>
          {printer.noConnection && <span className="printer-connect-why">Connect it to send prints straight from JusPrin</span>}
        </div>
      )}
    </div>
  );
}
