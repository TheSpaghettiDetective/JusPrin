// Composer input rules from the handoff: Enter sends, Shift+Enter inserts a
// newline, and Enter during IME composition accepts the composition without
// sending. Normal selection, clipboard, and focus behavior are left to the
// browser engine.

import { KeyboardEvent, useState } from 'react';

interface Props {
  disabled: boolean;
  disabledReason?: string;
  streaming: boolean;
  onSend: (text: string) => void;
  onStop: () => void;
}

export function Composer({ disabled, disabledReason, streaming, onSend, onStop }: Props) {
  const [text, setText] = useState('');

  const send = () => {
    const trimmed = text.trim();
    if (!trimmed || disabled || streaming) return;
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
        onChange={(event) => setText(event.target.value)}
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
