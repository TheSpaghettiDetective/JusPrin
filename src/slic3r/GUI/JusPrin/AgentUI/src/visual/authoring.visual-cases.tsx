// The chat list (Figma 1550:3502), composer (1558:1302) and message
// (1566:1320) matrices. The list is the real App on its list view; composer
// and message states are the production components with the inputs the host
// sends.

import { ComponentProps } from 'react';
import { fireEvent, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { AttachmentInfo, FileReport, ModelCatalogPayload, PrinterSessionPayload, StatePayload, WireMessage } from '../bridge/protocol';
import { AttachmentChip } from '../components/AttachmentChip';
import { Composer } from '../components/Composer';
import { FileNotesCard } from '../components/FileNotesCard';
import { MessageList } from '../components/MessageList';
import { Message } from '../state/store';
import { mounted, mountedApp, VisualCase } from '../test/visual';
import { opening } from '../printerWords';
import { chatState, exchange, turn } from './states';

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

// Two providers set up, as the proposal draws them.
const twoProviders: ModelCatalogPayload = {
  defaultProvider: 'anthropic',
  providers: [
    { id: 'anthropic', label: 'Anthropic', defaultModel: 'claude-opus-5-5', state: 'ready', models: [
      { id: 'claude-opus-5-5', label: 'Claude Opus 5.5', efforts: ['low', 'medium', 'high', 'xhigh', 'max'], defaultEffort: 'medium' },
      { id: 'claude-sonnet-5-5', label: 'Claude Sonnet 5.5', efforts: ['low', 'medium', 'high', 'xhigh', 'max'] }] },
    { id: 'openai', label: 'OpenAI', defaultModel: 'gpt-5.4-mini', state: 'ready', models: [
      { id: 'gpt-5.4', label: 'GPT-5.4', efforts: ['low', 'medium', 'high'] },
      { id: 'gpt-5.4-mini', label: 'GPT-5.4 mini', efforts: ['low', 'medium', 'high'] }] },
  ],
};
const oneProvider: ModelCatalogPayload = { defaultProvider: 'openai', providers: [twoProviders.providers[1]] };
const noLevels: ModelCatalogPayload = {
  defaultProvider: 'openrouter',
  providers: [{ id: 'openrouter', label: 'OpenRouter', defaultModel: 'openai/gpt-5.4-mini', state: 'ready',
    models: [{ id: 'deepseek/deepseek-v3', label: 'DeepSeek V3', efforts: [] }] }],
};

// The model-menu-search board's example: three providers, OpenRouter's list
// long enough to need the search field. Names past the board's are filler.
const routerNames = ['Anthropic: Claude Opus 5.5', 'DeepSeek: DeepSeek V3.2', 'Google: Gemini 3 Pro',
  'Anthropic: Claude Sonnet 5.5', 'Anthropic: Claude Sonnet 4.5', 'Meta: Llama 4 Maverick', 'Mistral: Mistral Large 3',
  'Qwen: Qwen3 Max', 'xAI: Grok 4'];
const longList: ModelCatalogPayload = {
  defaultProvider: 'anthropic',
  providers: [
    { ...twoProviders.providers[0], models: [...twoProviders.providers[0].models,
      { id: 'claude-haiku-5-5', label: 'Claude Haiku 5.5', efforts: [] }] },
    { ...twoProviders.providers[1], models: [...twoProviders.providers[1].models,
      { id: 'gpt-5.4-nano', label: 'GPT-5.4 nano', efforts: ['low', 'medium', 'high'] }] },
    { id: 'openrouter', label: 'OpenRouter', defaultModel: 'openai/gpt-5.4-mini', state: 'ready',
      models: Array.from({ length: 312 }, (_, i) =>
        ({ id: `router/model-${i}`, label: routerNames[i] ?? `Vendor ${i}: Model ${i}`, efforts: [] })) },
  ],
};
const openModels = async () => { await userEvent.click(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' })); };

// The proposal that replaced it (the Composer proposal on the "JusPrin v2"
// canvas), in every state the page draws, pickers included.
const redesign = (id: string, name: string, expects: string[], props: Partial<ComponentProps<typeof Composer>>,
  after?: (root: HTMLElement) => Promise<void> | void): VisualCase => ({
  node: '1834:2216', matrix: 'Composer redesign', id: `composer-redesign-${id}`, name, frame: 'block', expects,
  build: () => mounted(<Composer disabled={false} streaming={false} attachments={[]} onSend={noop} onStop={noop}
    onAttachFiles={noop} onRemoveAttachment={noop} models={twoProviders}
    chatModel={{ provider: 'anthropic', model: 'claude-opus-5-5', effort: '' }} onChooseModel={noop}
    {...props} />, after),
});

const longMessage = 'The bracket cracked at the hook after two days on the wall. Make it stronger without moving the screw '
  + 'holes, keep the outside face smooth, and try to keep the print under three hours. If that means a different infill '
  + 'or more walls, tell me what you changed and why, and slice it again so I can check the layer view before I print '
  + 'it. Also check that the hook still clears the shelf above it, and that the screw heads sit flush with the face.';

const chip = (id: string, name: string, expects: string[], info: AttachmentInfo, removable: boolean): VisualCase => ({
  node: '1558:1302', matrix: 'Composer', id: `attachment-${id}`, name, frame: 'card', expects,
  build: () => mounted(<AttachmentChip attachment={info} onRemove={removable ? noop : undefined} />),
});

// The field's hint over a chat's course, in the real App: the invitation
// while the chat is empty, "Reply…" once the person has spoken, and a nudge
// to type while answer buttons show.
const hint = (id: string, name: string, expects: string[], state: Partial<StatePayload>): VisualCase => ({
  node: '1834:2216', matrix: 'Composer placeholder', id: `hint-${id}`, name, frame: 'pane', expects,
  build: () => mountedApp(chatState(state)),
});

// The printer panel's own opening line comes first, as the panel shows it.
const printerHint = (id: string, name: string, expects: string[], mode: PrinterSessionPayload['mode'],
  after: WireMessage[]): VisualCase => {
  const session: PrinterSessionPayload = { mode, printerName: 'Studio A1', blocks: [], context: mode === 'add'
    ? { printers: [], network: [] }
    : { printer: { name: 'Studio A1', model: 'Bambu Lab A1', nozzle: 0.4, nozzles: [0.4], spools: [], connected: false,
      provider: 'bambu' } } };
  return {
    node: '1834:2216', matrix: 'Composer placeholder', id: `hint-printer-${id}`, name, frame: 'pane', expects,
    build: () => mountedApp(chatState({ session, conversation: [turn('m-0', 'assistant', opening(session)), ...after] }),
      { props: { printerPanel: true } }),
  };
};

// ---- Messages ---------------------------------------------------------------

const message = (id: string, role: Message['role'], text: string, extra: Partial<Message> = {}): Message =>
  ({ id, role, state: 'complete', text, attempt: 1, lastSeq: 0, ...extra });

const thread = (id: string, name: string, expects: string[], props: Partial<ComponentProps<typeof MessageList>>): VisualCase => ({
  node: '1566:1320', matrix: 'Messages', id: `message-${id}`, name, frame: 'card', within: 'thread', expects,
  build: () => mounted(<MessageList messages={[]} attachments={[]} streamingMessageId={null} toolActivities={[]}
    builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop}
    onToolCancel={noop} onSend={noop} {...props} />),
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

const notes = (id: string, name: string, expects: string[], truncated: boolean): VisualCase => ({
  node: '1566:1320', matrix: 'Messages', id: `file-notes-${id}`, name, frame: 'card', within: 'thread', expects,
  build: () => mounted(<FileNotesCard report={report(truncated)} />),
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

  redesign('empty', '1. Empty', ['Ask about this print or request a change…', 'jp-icon-plus', 'Send'], {}),
  redesign('typing', '2. Typing grows the box', ['The bracket cracked at the hook'], {
    initialText: 'The bracket cracked at the hook after two days. Make it stronger without moving the screw holes, and keep the print under three hours if you can.' }),
  redesign('long', '3. Long message: 8 lines, then it scrolls', ['keep the outside face smooth'], { initialText: longMessage }),
  redesign('menu', '4. + opens the attachment menu', ['Photo or image', '3D model (STL, 3MF, STEP)', 'G-code', 'Document (PDF)'], {},
    async () => { await userEvent.click(screen.getByRole('button', { name: 'Attach a file' })); }),
  redesign('several-providers', '5. Model menu: several providers', ['chat-menu-section', 'Anthropic', 'GPT-5.4 mini'], {},
    async () => { await userEvent.click(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' })); }),
  redesign('one-provider', '6. Model menu: one provider', ['GPT-5.4', 'aria-checked="true"'],
    { models: oneProvider, chatModel: { provider: 'openai', model: 'gpt-5.4', effort: 'high' } },
    async () => { await userEvent.click(screen.getByRole('button', { name: 'Model: GPT-5.4' })); }),
  redesign('thinking', '7. Thinking level follows the model', ['Thinking: Medium', 'Extra high'], {},
    async () => { await userEvent.click(screen.getByRole('button', { name: 'Thinking: Medium' })); }),
  redesign('no-levels', '8. Model without thinking levels', ['DeepSeek V3'],
    { models: noLevels, chatModel: { provider: 'openrouter', model: 'deepseek/deepseek-v3', effort: '' } }),
  redesign('files', '9. Files attached', ['bracket-on-wall.png', 'bracket-v3.3mf', 'Can you check whether this will fit?'], {
    initialText: 'Can you check whether this will fit?',
    attachments: [{ ...photo, name: 'bracket-on-wall.png' },
      attachment({ id: 'a-2', name: 'bracket-v3.3mf', kind: 'model', mime: 'model/3mf', sizeBytes: 2516582 })] }),
  redesign('responding', '10. Agent responding', ['Agent is responding…', 'Stop', 'disabled'], { streaming: true }),
  redesign('photo', 'Printer panel · Photo', ['Photo'], { photoButton: true }),
  // At the 320 px pane minimum the thinking picker reads "Medium" alone.
  redesign('narrow', '11. Narrow pane', ['Thinking: Medium', 'composer-picker-prefix'], {}),
  redesign('search', 'Model menu search · 1. A long list opens with a search field',
    ['Search models', 'OpenRouter · 312 models', 'Claude Haiku 5.5'], { models: longList }, openModels),
  redesign('search-typed', 'Model menu search · 2. Typing filters by model or provider',
    ['Claude Sonnet 5.5', 'Anthropic: Claude Sonnet 4.5'], { models: longList }, async () => {
      await openModels();
      await userEvent.type(screen.getByRole('searchbox'), 'sonnet');
    }),
  redesign('search-empty', 'Model menu search · 3. Nothing matches', ['No models match “llama 9”.'],
    { models: longList }, async () => {
      await openModels();
      await userEvent.type(screen.getByRole('searchbox'), 'llama 9');
    }),

  hint('empty', 'Project chat · Empty', ['placeholder="Ask about this print or request a change…"'], { conversation: [] }),
  hint('started', 'Project chat · After the first message', ['placeholder="Reply…"'], {}),
  hint('choices', 'Project chat · Answer buttons showing', ['class="reply-chip"', 'placeholder="Or type your own answer…"'],
    { conversation: [...exchange, turn('m-3', 'assistant', 'Shall I slice it now?\nChoices: Slice it | Not yet')] }),
  hint('answered', 'Project chat · Buttons answered', ['placeholder="Reply…"'],
    { conversation: [...exchange, turn('m-3', 'assistant', 'Shall I slice it now?\nChoices: Slice it | Not yet'),
      turn('m-4', 'user', 'Slice it')] }),
  hint('filament-empty', 'Filament chat · Empty', ['placeholder="Ask about this filament or request a change…"'],
    { conversation: [], navigation: { focused: true } }),
  hint('filament-started', 'Filament chat · After the first message', ['placeholder="Reply…"'], { navigation: { focused: true } }),
  printerHint('opening', 'Printer panel · Before the first reply', ['placeholder="e.g. &quot;bambu a1 mini&quot;'], 'add', []),
  printerHint('connect-opening', 'Printer panel · Opening with buttons keeps its own hint',
    ['class="reply-chip"', 'placeholder="Ask anything about connecting it"'], 'connect', []),
  printerHint('started', 'Printer panel · After the first reply', ['placeholder="Reply…"'], 'add',
    [turn('m-1', 'user', 'bambu a1')]),
  printerHint('choices', 'Printer panel · Answer buttons after the first reply', ['placeholder="Or type your own answer…"'], 'add',
    [turn('m-1', 'user', 'bambu a1'), turn('m-2', 'assistant', 'The A1 or the A1 mini?\nChoices: A1 | A1 mini')]),

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
  notes('standard', 'File notes · Standard', ['Notes about this file', 'bracket-v3.3mf'], false),
  notes('truncated', 'File notes · Truncated', ['JusPrin said more than fits here.'], true),
  thread('swatch', 'Colored-swatch note', ['note-swatch', 'Filament color updated.'], {
    messages: [message('m-1', 'note', 'Filament color updated.', { swatch: '#3A2D64' })] }),
];
