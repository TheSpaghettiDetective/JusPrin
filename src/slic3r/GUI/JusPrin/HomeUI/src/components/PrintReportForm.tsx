import { useEffect, useRef, useState } from 'react';
import type { ReactNode } from 'react';
import type { HistoryEntry, PrintReportOutcome, PrintReportPhoto, PrintReportResult } from '../bridge/protocol';
import { fileSize, REPORT_CREDITS, REPORT_MIN_TEXT, reportSubject } from '../history';

const OUTCOMES: { value: PrintReportOutcome; label: string }[] = [
  { value: 'worked', label: 'It worked' },
  { value: 'partly', label: 'Partly' },
  { value: 'failed', label: 'It didn’t work' },
];

// The print report form (board 8b): a photo, a few sentences and the person's
// own answer about how it came out, which is the report's outcome and not a
// print state. Print history has no message box, so once the report is sent the
// entry's own row says so (board 8c).
export function PrintReportForm({
  entry,
  photo,
  result,
  onChoosePhoto,
  onRemovePhoto,
  onSend,
  onClose,
  onGetNewVersion,
}: {
  entry: HistoryEntry;
  photo?: PrintReportPhoto;
  result?: PrintReportResult;
  onChoosePhoto: () => void;
  onRemovePhoto: () => void;
  onSend: (form: { photoId: string; text: string; outcome: PrintReportOutcome }) => void;
  onClose: () => void;
  // When the server no longer serves this release, in Send report's place:
  // sending again would be refused the same way.
  onGetNewVersion: () => void;
}) {
  const [text, setText] = useState('');
  const [outcome, setOutcome] = useState<PrintReportOutcome>();
  const [sending, setSending] = useState(false);
  const panel = useRef<HTMLDivElement>(null);

  useEffect(() => {
    panel.current?.querySelector<HTMLButtonElement>('button')?.focus();
  }, []);

  const closesNow = result && result.state !== 'failed';
  useEffect(() => {
    if (result) setSending(false);
    if (closesNow) onClose();
  }, [result, closesNow, onClose]);

  const length = text.trim().length;
  const canSend = Boolean(photo && outcome) && length >= REPORT_MIN_TEXT && !sending;

  const shell = (children: ReactNode) => (
    <div className="printer-dialog-shade">
      <div ref={panel} className="printer-dialog print-report" role="dialog" aria-modal="true" aria-labelledby="print-report-title"
        onKeyDown={(event) => {
          if (event.key === 'Escape' && !sending) {
            event.stopPropagation();
            onClose();
          }
          if (event.key === 'Tab') {
            const controls = Array.from(panel.current!.querySelectorAll<HTMLElement>('button:enabled, textarea:enabled'));
            const first = controls[0];
            const last = controls[controls.length - 1];
            if (event.shiftKey && document.activeElement === first) {
              event.preventDefault();
              last.focus();
            }
            if (!event.shiftKey && document.activeElement === last) {
              event.preventDefault();
              first.focus();
            }
          }
        }}>
        <div className="print-report-head">
          <h2 id="print-report-title">Tell us how it printed</h2>
          <span>{reportSubject(entry)}</span>
        </div>
        {children}
      </div>
    </div>
  );

  return shell(
    <>
      <p>To send this and get {REPORT_CREDITS} credits, add a photo of the printed object, write what happened, and pick how it came out.</p>
      <div className="print-report-field">
        <span className="print-report-label">Photo of the printed object</span>
        {photo ? (
          <div className="print-report-photo">
            {photo.thumbnail ? <img src={photo.thumbnail} alt="" /> : <span className="print-report-photo-empty" />}
            <span className="print-report-photo-name">{photo.name}</span>
            <span className="print-report-photo-size">{fileSize(photo.sizeBytes)}</span>
            <button type="button" className="history-link" disabled={sending} onClick={onRemovePhoto}>Remove</button>
          </div>
        ) : (
          <div>
            <button type="button" className="button-secondary button-secondary-small" onClick={onChoosePhoto}>Choose a photo…</button>
          </div>
        )}
      </div>
      <label className="print-report-field">
        <span className="print-report-label">What happened</span>
        <textarea rows={5} value={text} disabled={sending} onChange={(event) => setText(event.target.value)} />
        {length < REPORT_MIN_TEXT && <span className="print-report-hint">Write at least {REPORT_MIN_TEXT} characters ({length} so far).</span>}
      </label>
      <div className="print-report-field">
        <span className="print-report-label" id="print-report-outcome">How did it come out?</span>
        <div className="print-report-outcomes" role="radiogroup" aria-labelledby="print-report-outcome">
          {OUTCOMES.map((option) => (
            <button key={option.value} type="button" role="radio" aria-checked={outcome === option.value} disabled={sending}
              className={outcome === option.value ? 'print-report-outcome selected' : 'print-report-outcome'}
              onClick={() => setOutcome(option.value)}>
              {option.label}
            </button>
          ))}
        </div>
      </div>
      {result?.state === 'failed' && !sending && <p role="alert">{result.updateMessage ?? 'The report couldn’t be sent. Check your connection and try again.'}</p>}
      <div className="printer-dialog-buttons">
        <button type="button" className="button-secondary" disabled={sending} onClick={onClose}>Not now</button>
        {result?.updateMessage && !sending ? (
          <button type="button" className="button-primary" onClick={onGetNewVersion}>Get the new version…</button>
        ) : (
          <button type="button" className="button-primary" disabled={!canSend}
            onClick={() => {
              setSending(true);
              onSend({ photoId: photo!.id, text: text.trim(), outcome: outcome! });
            }}>
            {sending ? 'Sending…' : 'Send report'}
          </button>
        )}
      </div>
    </>,
  );
}
