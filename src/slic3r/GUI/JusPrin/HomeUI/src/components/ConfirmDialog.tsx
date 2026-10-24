import { useEffect, useRef } from 'react';
import type { ConfirmPayload } from '../bridge/protocol';

// The host's question, as the design system's Confirmation dialog: the
// printer dialogs' kind, with Cancel and the named action. Focus starts on
// Cancel and stays inside; Escape is the same as Cancel.
export function ConfirmDialog({
  question,
  onAnswer,
}: {
  question: ConfirmPayload;
  onAnswer: (confirmed: boolean) => void;
}) {
  const panel = useRef<HTMLDivElement>(null);

  useEffect(() => {
    panel.current?.querySelector<HTMLButtonElement>('button')?.focus();
  }, [question.requestId]);

  return (
    <div className="printer-dialog-shade">
      <div
        ref={panel}
        className="printer-dialog confirmation"
        role="dialog"
        aria-modal="true"
        aria-labelledby="confirm-dialog-title"
        onKeyDown={(event) => {
          if (event.key === 'Escape') {
            event.stopPropagation();
            onAnswer(false);
          }
          if (event.key === 'Tab') {
            const controls = Array.from(panel.current!.querySelectorAll<HTMLElement>('button'));
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
        }}
      >
        <h2 id="confirm-dialog-title">{question.title}</h2>
        <p>{question.body}</p>
        <div className="printer-dialog-buttons">
          <button type="button" className="button-secondary" onClick={() => onAnswer(false)}>
            {question.cancel}
          </button>
          <button type="button" className="button-primary" onClick={() => onAnswer(true)}>
            {question.confirm}
          </button>
        </div>
      </div>
    </div>
  );
}
