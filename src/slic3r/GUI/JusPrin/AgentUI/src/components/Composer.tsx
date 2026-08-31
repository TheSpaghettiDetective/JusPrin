// Composer input rules from the handoff: Enter sends, Shift+Enter inserts a
// newline, and Enter during IME composition accepts the composition without
// sending. Normal selection, clipboard, and focus behavior are left to the
// browser engine. The in-progress draft is reported (debounced) so the host
// can keep it in the local recovery store; reconnecting restores it via
// initialText.

import { KeyboardEvent, useEffect, useRef, useState } from 'react';

interface Props {
  disabled: boolean;
  disabledReason?: string;
  streaming: boolean;
  initialText?: string;
  onSend: (text: string) => void;
  onStop: () => void;
  onDraftChange?: (text: string) => void;
  draftDebounceMs?: number;
}

export function Composer({
  disabled,
  disabledReason,
  streaming,
  initialText,
  onSend,
  onStop,
  onDraftChange,
  draftDebounceMs = 300,
}: Props) {
  const [text, setText] = useState(initialText ?? '');
  const draftTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  // A recovered draft arrives after the first connect; apply it only while
  // the composer is untouched so it never clobbers active typing.
  const touched = useRef(false);
  useEffect(() => {
    if (!touched.current && initialText) setText(initialText);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [initialText]);

  useEffect(() => {
    return () => {
      if (draftTimer.current !== null) clearTimeout(draftTimer.current);
    };
  }, []);

  const reportDraft = (value: string) => {
    if (!onDraftChange) return;
    if (draftTimer.current !== null) clearTimeout(draftTimer.current);
    draftTimer.current = setTimeout(() => onDraftChange(value), draftDebounceMs);
  };

  const send = () => {
    const trimmed = text.trim();
    if (!trimmed || disabled || streaming) return;
    if (draftTimer.current !== null) clearTimeout(draftTimer.current);
    onSend(trimmed);
    setText('');
  };

  const handleKeyDown = (event: KeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key !== 'Enter' || event.shiftKey) return;
    // 229 is the legacy IME-processing keyCode some engines still report.
    if (event.nativeEvent.isComposing || event.keyCode === 229) return;
    event.preventDefault();
    send();
  };

  return (
    <div className="composer">
      <textarea
        aria-label="Message the Agent"
        placeholder={disabled ? disabledReason ?? 'The Agent is not available' : 'Ask about your print…'}
        value={text}
        disabled={disabled}
        onChange={(event) => {
          touched.current = true;
          setText(event.target.value);
          reportDraft(event.target.value);
        }}
        onKeyDown={handleKeyDown}
        rows={2}
      />
      {streaming ? (
        <button onClick={onStop} aria-label="Stop generating">
          Stop
        </button>
      ) : (
        <button className="primary" onClick={send} disabled={disabled || !text.trim()} aria-label="Send message">
          Send
        </button>
      )}
    </div>
  );
}
