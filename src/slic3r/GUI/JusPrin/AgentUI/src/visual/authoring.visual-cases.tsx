// The chat list (Figma 1550:3502), composer (1558:1302) and message
// (1566:1320) matrices. The list is the real App on its list view; composer
// and message states are the production components with the inputs the host
// sends.

import { ComponentProps } from 'react';
import { fireEvent, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { AttachmentInfo, FileReport } from '../bridge/protocol';
import { AttachmentChip } from '../components/AttachmentChip';
import { Composer } from '../components/Composer';
import { FileNotesCard } from '../components/FileNotesCard';
import { MessageList } from '../components/MessageList';
import { Message } from '../state/store';
import { mounted, mountedApp, VisualCase } from '../test/visual';
import { chatState } from './states';

const noop = () => {};

// ---- Chat list ------------------------------------------------------------

const chats = [
  { id: 'conv-1', title: 'Strong print', createdAt: '2026-10-05T14:24:00', updatedAt: '2026-10-05T14:24:00', preview: 'Added two walls and 35% infill' },
  { id: 'conv-2', title: 'Quick print', createdAt: '2026-10-04T09:00:00', updatedAt: '2026-10-04T09:00:00', preview: 'Ready to slice' },
  { id: 'conv-3', title: 'First print', createdAt: '2026-09-28T09:00:00', updatedAt: '2026-09-28T09:00:00' },
];

const toList = async () => { await userEvent.click(screen.getByRole('button', { name: 'Back to chats' })); };

const list = (id: string, name: string, expects: string[], state: Parameters<typeof chatState>[0]): VisualCase => ({
  node: '1550:3502', matrix: 'Chat list', id: `list-${id}`, name, frame: 'pane', expects,
  build: () => mountedApp(chatState({ conversations: chats, ...state }), { after: toList }),
});

// ---- Composer and attachments ---------------------------------------------

// A 1x1 image: the thumbnail's box is the stylesheet's, not the picture's.
const pixel = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==';

const attachment = (overrides: Partial<AttachmentInfo>): AttachmentInfo => ({
  id: 'a-1', name: 'printer-notes.pdf', kind: 'pdf', mime: 'application/pdf', sizeBytes: 430080,
  source: 'picker', state: 'staged', ...overrides,
});
const photo = attachment({ name: 'bracket-photo.png', kind: 'image', mime: 'image/png', sizeBytes: 1258291, previewDataUrl: pixel });

const composer = (id: string, name: string, expects: string[], props: Partial<ComponentProps<typeof Composer>>,
  after?: (root: HTMLElement) => void): VisualCase => ({
  node: '1558:1302', matrix: 'Composer', id: `composer-${id}`, name, frame: 'block', expects,
  build: () => mounted(<Composer disabled={false} streaming={false} attachments={[]} onSend={noop} onStop={noop}
    onAttachFiles={noop} onRemoveAttachment={noop} {...props} />, after),
});

const chip = (id: string, name: string, expects: string[], info: AttachmentInfo, removable: boolean): VisualCase => ({
  node: '1558:1302', matrix: 'Composer', id: `attachment-${id}`, name, frame: 'card', expects,
  build: () => mounted(<AttachmentChip attachment={info} onRemove={removable ? noop : undefined} />),
});

// ---- Messages ---------------------------------------------------------------

const message = (id: string, role: Message['role'], text: string, extra: Partial<Message> = {}): Message =>
  ({ id, role, state: 'complete', text, attempt: 1, lastSeq: 0, ...extra });

const thread = (id: string, name: string, expects: string[], props: Partial<ComponentProps<typeof MessageList>>): VisualCase => ({
  node: '1566:1320', matrix: 'Messages', id: `message-${id}`, name, frame: 'card', within: 'thread', expects,
  build: () => mounted(<MessageList messages={[]} attachments={[]} streamingMessageId={null} toolActivities={[]}
    builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop}
    onToolCancel={noop} onSend={noop} answeredState={false} {...props} />),
});

const rich = `## Print recommendations

- Use 4 walls
- Keep the screw holes unchanged

| Setting | Value |
| --- | --- |
| Walls | 4 |
| Infill | 35% |

\`0.20mm Standard @BBL X1C\`

[Guide](https://example.com/guide)`;

const report = (truncated: boolean): FileReport => ({
  startedBy: 'user', projectOpened: true, loads: [{ files: ['/Users/maker/Desktop/bracket-v3.3mf'], withSettings: true }],
  messages: { truncated, items: [
    { source: 'notification', title: '', text: 'Review wall loops for thin features.', buttons: [], answer: '' },
    { source: 'notification', title: '', text: 'Check first-layer adhesion settings.', buttons: [], answer: '' },
  ] },
  objects: { items: [], truncated: false }, truncated: false,
});

const notes = (id: string, name: string, expects: string[], truncated: boolean, setUp: boolean): VisualCase => ({
  node: '1566:1320', matrix: 'Messages', id: `file-notes-${id}`, name, frame: 'card', within: 'thread', expects,
  build: () => mounted(<FileNotesCard report={report(truncated)} onSetUpAgent={setUp ? noop : undefined} />),
});

const question = [message('m-1', 'assistant', 'Would you like to connect it now?\n\nChoices: Connect it | Not now')];

export const authoringCases: VisualCase[] = [
  list('current', 'State=Current', ['aria-current="true"', 'Configure Agent', 'No messages yet'], {}),
  list('empty', 'State=Empty', ['No chats yet. Start a new chat about this project.'], { conversations: [] }),
  list('busy', 'State=Busy', ['class="configure-agent" disabled'], { conversationBusy: true }),
  list('unavailable', 'State=Agent unavailable', ['Set up the agent', 'Connect an Agent to continue this conversation'],
    { agent: { status: 'unavailable' } }),

  composer('idle', '1. Idle', ['Attach a file', 'disabled', 'Send'], {}),
  composer('text-ready', '2. Text ready', ['Make the bracket stronger'], { initialText: 'Make the bracket stronger' }),
  chip('photo', '3. Staged photo', ['attachment-thumb', 'PNG · 1.2 MB', 'Remove'], photo, true),
  chip('file', '4. Staged file', ['PDF · 420 KB', 'attachment-kind', 'Remove'], attachment({}), true),
  chip('error', '5. Error', ['Could not be attached'],
    attachment({ state: 'error', error: { code: 'decode_failed', message: 'Could not be attached' } }), true),
  chip('sent', '6. Sent file', ['printer-notes.pdf'], attachment({ state: 'sent' }), false),
  composer('photo-ready', '7. Photo mode · attachment ready', ['Photo', 'add a note, or just send', 'bracket-photo.png'],
    { photoButton: true, attachments: [photo] }),
  composer('streaming', '8. Streaming', ['Agent is responding…', 'Stop'], { streaming: true }),
  composer('drag-over', '10. Drag over', ['class="composer dragging"'], {},
    (root) => { fireEvent.dragOver(root.querySelector('.composer')!); }),
  composer('unavailable', 'Agent unavailable', ['The Agent is not available', 'disabled'],
    { disabled: true, disabledReason: 'The Agent is not available' }),

  thread('streaming', 'Assistant · Streaming partial', ['streaming-cursor', 'I checked the bracket.'], {
    messages: [message('m-1', 'assistant', 'I checked the bracket. Use **four walls', { state: 'streaming' })], streamingMessageId: 'm-1' }),
  thread('rich', 'Assistant · Complete rich Markdown', ['<table>', '<code>', 'markdown-link', 'Print recommendations'], {
    messages: [message('m-1', 'assistant', rich)] }),
  thread('failed', 'Assistant · Failed retryable', ['The Agent couldn’t finish this response.', 'Retry'], {
    messages: [message('m-1', 'assistant', '', { state: 'failed',
      error: { code: 'provider_error', message: 'The Agent couldn’t finish this response.', retryable: true } })] }),
  thread('stopped', 'Assistant · Stopped', ['Stopped'], {
    messages: [message('m-1', 'assistant', 'I checked the bracket. Use **four walls', { state: 'stopped' })] }),
  thread('choices', 'Assistant · Reply choices ready', ['Connect it', 'Not now', 'class="reply-chip"'], { messages: question }),
  thread('choices-disabled', 'Assistant · Reply choices disabled', ['class="reply-chip" disabled'],
    { messages: question, replyDisabled: true }),
  thread('working', 'Assistant · Working on it', ['Working on it…', 'jp-icon-rotate-cw'], {
    messages: [message('m-1', 'assistant', '', { state: 'streaming' })], streamingMessageId: 'm-1', printerBlocks: [] }),
  notes('standard', 'File notes · Standard', ['Notes about this file', 'bracket-v3.3mf'], false, false),
  notes('truncated', 'File notes · Truncated', ['OrcaSlicer said more than fits here.'], true, false),
  notes('unavailable', 'File notes · Agent unavailable', ['Set up the Agent', 'it can explain them'], false, true),
  thread('empty', 'Empty conversation notice', ['Ask the Agent about your print'], {}),
  thread('swatch', 'Colored-swatch note', ['note-swatch', 'Filament color updated.'], {
    messages: [message('m-1', 'note', 'Filament color updated.', { swatch: '#3A2D64' })] }),
];
