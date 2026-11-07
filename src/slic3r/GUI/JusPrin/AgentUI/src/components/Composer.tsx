// The Agent message box (the Composer proposal on the "JusPrin v2" canvas):
// one bordered box holding the staged attachments, the text field, and a
// toolbar with + on the left and Send on the right. The field starts one
// line high and grows a line at a time up to component.composer.maxLines,
// then scrolls inside the box; the toolbar stays.
//
// Input rules from the handoff: Enter sends, Shift+Enter inserts a newline,
// and Enter during IME composition accepts the composition without sending.
// Normal selection, clipboard, and focus behavior are left to the browser
// engine. The in-progress draft is reported (debounced) so the host can keep
// it with the project's saved state; reconnecting restores it via initialText.
//
// Attachments enter through the + menu's file picker, drag-and-drop, or
// pasting an image; staged attachments render as removable rows, and a
// message may be sent with attachments and no text. The host owns decoding
// and storage -- the composer only hands it files.
//
// With an account (the `credits` prop): the credits ring beside the pickers,
// and Send disabled with one line above the box when the next message cannot
// be paid (boards 2a, 3a, 3b, 3e).

import { ChangeEvent, ClipboardEvent, DragEvent, KeyboardEvent, ReactNode, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { AttachmentInfo, AttachmentPickKind, AttachmentSource, ChatModelChoice, CreditAccount, ModelCatalogPayload } from '../bridge/protocol';
import { lowNotice, sendBlock } from '../credits';
import { composerInputMaxHeight } from '../tokens';
import { AttachmentChip } from './AttachmentChip';
import { BalancePanel } from './BalancePanel';
import { ActionMenu } from './ChatNavigation';
import { findModel, ModelPickers } from './ModelPickers';

// What the + menu offers: the four kinds the design names. In the app each
// opens the operating system's file dialog through the host, which filters
// it to the extensions it files under that kind. `accept` is the browser
// input's copy of the same lists, used only where the page runs without
// that host (review pages, tests). Text and SVG files are accepted too, by
// dropping them.
export const ATTACHMENT_CHOICES: readonly { label: string; kind: AttachmentPickKind; accept: string }[] = [
  { label: 'Photo or image', kind: 'image', accept: '.png,.jpg,.jpeg,.gif,.webp' },
  { label: '3D model (STL, 3MF, STEP)', kind: 'model',
    accept: '.stl,.3mf,.step,.stp,.obj,.amf,.oltp,.drc,.ply,.usd,.usda,.usdc,.usdz,.abc' },
  { label: 'G-code', kind: 'gcode', accept: '.gcode,.g,.gco' },
  { label: 'Document (PDF)', kind: 'pdf', accept: '.pdf' },
];

export interface ComposerCredits {
  account: CreditAccount | null;
  // "About 25 credits left" is said once: it stands until the person sends a
  // message past it or closes it.
  lowNoticeDone: boolean;
  onLowNoticeDone: () => void;
  onGetMore: () => void;
  onSeeUsage: () => void;
}

interface Props {
  disabled: boolean;
  disabledReason?: string;
  streaming: boolean;
  initialText?: string;
  attachments: AttachmentInfo[]; // staged attachments only
  onSend: (text: string) => void;
  onStop: () => void;
  onAttachFiles: (files: File[], source: AttachmentSource) => void;
  // Opens the host's own file dialog for one kind; absent where the host
  // cannot, and the browser's file input is used instead.
  onPickFiles?: (kind: AttachmentPickKind) => void;
  onRemoveAttachment: (id: string) => void;
  onDraftChange?: (text: string) => void;
  draftDebounceMs?: number;
  // Fired on the keystroke itself, not on the debounced draft save. Anything
  // covering the thread uses this to get out of the way immediately.
  onTyping?: () => void;
  // What the empty field says until the person's first message, in place of
  // the project chat's invitation: the printer panel's likely answer to its
  // opening question, or the filament chat's own invitation.
  placeholder?: string;
  // The person has sent a message in this chat; from then on the field says
  // "Reply…", since an invitation to start reads oddly mid-conversation.
  started?: boolean;
  // The newest reply shows answer buttons; the field says typing works too.
  choicesOffered?: boolean;
  // The + menu's kinds this chat can use, where it cannot use them all: the
  // printer chat has no project for a 3D model to go into.
  attachKinds?: readonly AttachmentPickKind[];
  // What the field says once a file is staged, where the chat has words for
  // it: a photo in the printer chat waits for the person, it never sends itself.
  stagedPlaceholder?: string;
  // What the band says above the field when the field cannot be used as it
  // is: the notice on an earlier chat, or a recovery alert.
  notice?: ReactNode;
  // False where nothing can be attached at all, so the action is left out
  // rather than shown disabled: a read-only shell has only its field and Send.
  attachable?: boolean;
  // The chat's model and thinking pickers, where the host offers them.
  models?: ModelCatalogPayload;
  chatModel?: ChatModelChoice;
  onChooseModel?: (choice: ChatModelChoice) => void;
  credits?: ComposerCredits;
  // Words the gateway refused, back in the field once per count.
  returnedDraft?: { text: string; count: number };
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
  onPickFiles,
  onRemoveAttachment,
  onDraftChange,
  draftDebounceMs = 300,
  onTyping,
  placeholder,
  started = false,
  choicesOffered = false,
  attachKinds,
  stagedPlaceholder,
  notice,
  attachable = true,
  models,
  chatModel,
  onChooseModel,
  credits,
  returnedDraft,
}: Props) {
  const [text, setText] = useState(initialText ?? '');
  const [dragging, setDragging] = useState(false);
  const draftTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingDraft = useRef(false);
  const latestText = useRef(initialText ?? '');
  const fileInput = useRef<HTMLInputElement>(null);
  const field = useRef<HTMLTextAreaElement>(null);
  const choices = useRef<HTMLDivElement>(null);
  const [narrow, setNarrow] = useState(false);

  // Grow the field to its text, up to the line limit; past it the field
  // scrolls. A field that is not laid out (inside a hidden view) measures 0
  // and keeps its height until its text next changes.
  useLayoutEffect(() => {
    const element = field.current;
    if (!element) return;
    const previous = element.style.height;
    element.style.height = 'auto';
    const content = element.scrollHeight;
    element.style.height = content > 0 ? `${Math.min(content, composerInputMaxHeight())}px` : previous;
  }, [text]);

  // A narrow pane: when the choices at full length do not fit beside Send,
  // the thinking picker drops "Thinking:", and the model picker's name is
  // cut if it still does not fit. Measured with the labels in full (the
  // measuring class undoes the narrow one and the shrinking) so the answer
  // follows the labels' real lengths, not a fixed breakpoint.
  useLayoutEffect(() => {
    const element = choices.current;
    if (!element) return;
    const measure = () => {
      element.classList.add('composer-choices--measuring');
      const needed = element.scrollWidth;
      element.classList.remove('composer-choices--measuring');
      setNarrow(needed > element.clientWidth);
    };
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    return () => observer.disconnect();
  }, [models, chatModel]);

  // A recovered draft arrives after the first connect; apply it only while
  // the composer is untouched so it never clobbers active typing.
  const touched = useRef(false);
  useEffect(() => {
    if (!touched.current && initialText) { setText(initialText); latestText.current = initialText; }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [initialText]);

  const takenReturn = useRef(returnedDraft?.count ?? 0);
  useEffect(() => {
    if (!returnedDraft || returnedDraft.count === takenReturn.current) return;
    takenReturn.current = returnedDraft.count;
    setText(returnedDraft.text);
    latestText.current = returnedDraft.text;
  }, [returnedDraft]);

  useEffect(() => {
    return () => {
      if (draftTimer.current !== null) clearTimeout(draftTimer.current);
      if (pendingDraft.current) onDraftChange?.(latestText.current);
    };
  }, []);

  const reportDraft = (value: string) => {
    if (!onDraftChange) return;
    if (draftTimer.current !== null) clearTimeout(draftTimer.current);
    pendingDraft.current = true;
    latestText.current = value;
    draftTimer.current = setTimeout(() => { pendingDraft.current = false; onDraftChange(value); }, draftDebounceMs);
  };

  const hasSendable = attachments.some((a) => a.state === 'staged');
  const chosenModel = models && chatModel ? findModel(models, chatModel) : undefined;
  const account = credits?.account;
  const block = sendBlock(account, chosenModel);
  const canSend = (!!text.trim() || hasSendable) && !disabled && !streaming && !block;
  // A picture a model without eyes would never see.
  const blind = chosenModel?.images === false;
  const low = lowNotice(account, chosenModel);
  const lowShown = !!low && !block && !credits?.lowNoticeDone;
  const ring = account && chosenModel?.credits !== undefined;

  const send = () => {
    if (!canSend) return;
    if (lowShown) credits?.onLowNoticeDone();
    if (draftTimer.current !== null) clearTimeout(draftTimer.current);
    pendingDraft.current = false;
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

  // The host's dialog when there is one: a web view's file input shows every
  // file on macOS whatever `accept` says.
  const pickFiles = (choice: (typeof ATTACHMENT_CHOICES)[number]) => {
    if (onPickFiles) {
      onPickFiles(choice.kind);
      return;
    }
    if (!fileInput.current) return;
    fileInput.current.accept = choice.accept;
    fileInput.current.click();
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
      {notice}
      {block && <div className="composer-credits" role="status">
        <span>{block}</span>
        <div className="composer-credits-actions">
          <button type="button" className="link" onClick={() => credits?.onGetMore()}>Get more credits</button>
        </div>
      </div>}
      {lowShown && <div className="composer-credits composer-credits--low" role="status">
        <span>{low}</span>
        <button type="button" className="link" onClick={() => credits?.onGetMore()}>Get more credits</button>
        <button type="button" className="composer-credits-close" aria-label="Close" onClick={() => credits?.onLowNoticeDone()}>
          <span className="jp-icon jp-icon-close" aria-hidden="true" />
        </button>
      </div>}
      <div className="composer-box">
        {attachments.length > 0 && (
          <div className="composer-attachments" aria-label="Staged attachments">
            {attachments.map((attachment) => (
              <AttachmentChip key={attachment.id} attachment={attachment} onRemove={onRemoveAttachment}
                notSent={blind && attachment.kind === 'image' && attachment.state === 'staged'} />
            ))}
          </div>
        )}
        <textarea
          ref={field}
          aria-label="Message AI"
          placeholder={
            disabled
              ? disabledReason ?? 'AI is not available'
              : streaming
                ? 'AI is responding…'
                : stagedPlaceholder && hasSendable
                  ? stagedPlaceholder
                  : !started && placeholder
                    ? placeholder
                    : choicesOffered
                      ? 'Or type your own answer…'
                      : started
                        ? 'Reply…'
                        : 'Ask about this print or request a change…'
          }
          value={text}
          disabled={disabled}
          onChange={(event) => {
            touched.current = true;
            onTyping?.();
            setText(event.target.value);
            latestText.current = event.target.value;
            reportDraft(event.target.value);
          }}
          onKeyDown={handleKeyDown}
          onPaste={handlePaste}
          rows={1}
        />
        <div className="composer-toolbar">
          <div ref={choices} className={narrow ? 'composer-choices composer-choices--narrow' : 'composer-choices'}>
            {/* Disabled while the Agent responds, like every choice in the
                toolbar; the field stays open for the next message. */}
            {attachable && (
              <ActionMenu label="Attach a file" icon="plus" above disabled={disabled || streaming}
                items={ATTACHMENT_CHOICES.filter((choice) => !attachKinds || attachKinds.includes(choice.kind))
                  .map((choice) => ({ label: choice.label, onSelect: () => pickFiles(choice) }))} />
            )}
            {models && chatModel && onChooseModel && (
              <ModelPickers catalog={models} choice={chatModel} disabled={disabled || streaming}
                locked={started} account={account} onChoose={onChooseModel} />
            )}
            {ring && <BalancePanel account={account!} low={!!low}
              onGetMore={credits!.onGetMore} onSeeUsage={credits!.onSeeUsage} />}
          </div>
          {streaming ? (
            <button type="button" className="composer-send composer-stop" onClick={onStop} aria-label="Stop generating">
              Stop
            </button>
          ) : (
            <button
              type="button"
              className="composer-send"
              onClick={send}
              disabled={!canSend}
              aria-label="Send message"
            >
              Send
            </button>
          )}
        </div>
      </div>
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
    </div>
  );
}
