// Composer input rules from the handoff: Enter sends, Shift+Enter inserts a
// newline, and Enter during IME composition accepts the composition without
// sending. Normal selection, clipboard, and focus behavior are left to the
// browser engine. The in-progress draft is reported (debounced) so the host
// can keep it in the local recovery store; reconnecting restores it via
// initialText.
//
// Attachments enter through a file picker, drag-and-drop, or pasting an image;
// staged attachments render as removable chips, and a message may be sent with
// attachments and no text. The host owns decoding and storage — the composer
// only hands it files.

import { ChangeEvent, ClipboardEvent, DragEvent, KeyboardEvent, useEffect, useRef, useState } from 'react';
import { AttachmentInfo, AttachmentSource } from '../bridge/protocol';
import { AttachmentChip } from './AttachmentChip';

interface Props {
  disabled: boolean;
  disabledReason?: string;
  streaming: boolean;
  initialText?: string;
  attachments: AttachmentInfo[]; // staged attachments only
  onSend: (text: string) => void;
  onStop: () => void;
  onAttachFiles: (files: File[], source: AttachmentSource) => void;
  onRemoveAttachment: (id: string) => void;
  onDraftChange?: (text: string) => void;
  draftDebounceMs?: number;
}

export function Composer({
  disabled,
  disabledReason,
  streaming,
  initialText,
  attachments,
  onSend,
  onStop,
  onAttachFiles,
  onRemoveAttachment,
  onDraftChange,
  draftDebounceMs = 300,
}: Props) {
  const [text, setText] = useState(initialText ?? '');
  const [dragging, setDragging] = useState(false);
  const draftTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);

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

  const hasSendable = attachments.some((a) => a.state === 'staged');
  const canSend = (!!text.trim() || hasSendable) && !disabled && !streaming;

  const send = () => {
    if (!canSend) return;
    if (draftTimer.current !== null) clearTimeout(draftTimer.current);
    onSend(text.trim());
    setText('');
  };

  const handleKeyDown = (event: KeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key !== 'Enter' || event.shiftKey) return;
    // 229 is the legacy IME-processing keyCode some engines still report.
    if (event.nativeEvent.isComposing || event.keyCode === 229) return;
    event.preventDefault();
    send();
  };

  const handleFiles = (files: FileList | null, source: AttachmentSource) => {
    if (!files || files.length === 0) return;
    onAttachFiles(Array.from(files), source);
  };

  const handlePickerChange = (event: ChangeEvent<HTMLInputElement>) => {
    handleFiles(event.target.files, 'picker');
    event.target.value = ''; // allow re-picking the same file
  };

  const handlePaste = (event: ClipboardEvent<HTMLTextAreaElement>) => {
    const files = Array.from(event.clipboardData.files ?? []);
    if (files.length > 0) {
      event.preventDefault();
      onAttachFiles(files, 'clipboard');
    }
  };

  const handleDrop = (event: DragEvent<HTMLDivElement>) => {
    event.preventDefault();
    setDragging(false);
    if (disabled) return;
    handleFiles(event.dataTransfer.files, 'drop');
  };

  return (
    <div
      className={`composer${dragging ? ' dragging' : ''}`}
      onDragOver={(event) => {
        event.preventDefault();
        if (!disabled) setDragging(true);
      }}
      onDragLeave={() => setDragging(false)}
      onDrop={handleDrop}
    >
      {attachments.length > 0 && (
        <div className="composer-attachments" aria-label="Staged attachments">
          {attachments.map((attachment) => (
            <AttachmentChip key={attachment.id} attachment={attachment} onRemove={onRemoveAttachment} />
          ))}
        </div>
      )}
      <div className="composer-row">
        <button
          type="button"
          className="attach-button"
          aria-label="Attach a file"
          disabled={disabled}
          onClick={() => fileInput.current?.click()}
        >
          +
        </button>
        <input
          ref={fileInput}
          type="file"
          multiple
          className="attach-input"
          aria-hidden="true"
          tabIndex={-1}
          style={{ display: 'none' }}
          onChange={handlePickerChange}
        />
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
          onPaste={handlePaste}
          rows={2}
        />
        {streaming ? (
          <button onClick={onStop} aria-label="Stop generating">
            Stop
          </button>
        ) : (
          <button className="primary" onClick={send} disabled={!canSend} aria-label="Send message">
            Send
          </button>
        )}
      </div>
    </div>
  );
}
