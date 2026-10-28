// Deterministic conversation and interaction tests: a scripted mock host
// plays the native side of the bridge while the real page runs in jsdom.

import { beforeEach, describe, expect, it, vi } from 'vitest';
import { act, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { fireEvent } from '@testing-library/react';
import { App } from './App';
import { projectInstructions } from './projectInstructions';
import { skills } from './skills';
import {
  Envelope,
  PROTOCOL_NAME,
  PROTOCOL_VERSION,
  StatePayload,
  ToolActivityInfo,
  WorkspaceContext,
} from './bridge/protocol';

// The host the test running now talks to.
let currentHost: MockHost | null = null;

class MockHost {
  received: Envelope[] = [];

  constructor() {
    currentHost = this;
  }

  transport = {
    post: (json: string) => {
      this.received.push(JSON.parse(json));
    },
  };

  deliver(type: string, payload: unknown): void {
    const envelope: Envelope = {
      protocol: PROTOCOL_NAME,
      version: PROTOCOL_VERSION,
      id: `h-${this.received.length}-${type}`,
      type,
      payload,
    };
    act(() => {
      window.__jusprinBridge!.deliver(envelope);
    });
  }

  lastOfType(type: string): Envelope | undefined {
    return [...this.received].reverse().find((e) => e.type === type);
  }
}

const context: WorkspaceContext = {
  sessionId: '1',
  revision: 2,
  projectName: 'Two Cubes',
  projectDirty: false,
  printer: { preset: 'MyKlipper 0.2 nozzle', filament: 'Generic PLA', process: 'Test Printer 0.4' },
  plates: [
    {
      id: '11',
      name: 'Plate 1',
      active: true,
      sliced: false,
      estimate: null,
      estimateStatus: 'current',
      invalidatedBy: '',
      objects: [
        { id: '21', name: 'cube-a', instances: 1, selected: true },
        { id: '22', name: 'cube-b', instances: 1, selected: false },
      ],
    },
  ],
  selection: { status: 'objects', objectIds: ['21'] },
  history: { canUndo: false, canRedo: false },
  presetDeltas: [],
  appliedSetup: null,
  setupIdentity: null,
  printerReview: null,
  currency: 'USD',
  setupIntent: '',
  planHeadline: '',
};

// The same project as reported by a host that reads the applied settings.
const applied: WorkspaceContext = {
  ...context,
  appliedSetup: { version: 1, plateId: '11', printableObjects: 2, spiralMode: false, variableLayerHeight: false,
    objects: ['cube-a', 'cube-b'], localOverrides: [] },
  setupIdentity: { printer: 'MyKlipper 0.2 nozzle', nozzles: [0.2], plateType: 'Textured PEI',
    filaments: [{ preset: 'Generic PLA', material: 'PLA' }] },
};

function emptyState(overrides: Partial<StatePayload> = {}): StatePayload {
  return {
    agent: { status: 'ready' },
    appearance: 'light',
    conversations: [{ id: 'conv-1', title: 'Conversation 1', createdAt: 't' }],
    activeConversationId: 'conv-1',
    conversation: [],
    streamingMessageId: null,
    toolActivities: [],
    gcodeMarkers: [],
    draft: '',
    context,
    ...overrides,
  };
}

function toolActivity(overrides: Partial<ToolActivityInfo> = {}): ToolActivityInfo {
  return {
    actionId: 't-1',
    correlationId: 'm-2',
    server: 'jusprin-native',
    tool: 'duplicate_object',
    title: 'Duplicate "cube-a"',
    arguments: { sessionId: '1', objectId: '21' },
    actionClass: 'mutation',
    sessionId: '1',
    expectedRevision: 2,
    state: 'running',
    progress: { current: 0, total: 3 },
    ...overrides,
  };
}

// A completed exchange whose assistant reply called the tool action.
function toolCallConversation() {
  return [
    { id: 'm-1', role: 'user', state: 'complete', text: 'duplicate the selected object', attempt: 1 },
    { id: 'm-2', role: 'assistant', state: 'complete', text: 'I can duplicate cube-a for you.', attempt: 1, inReplyTo: 'm-1' },
  ] as StatePayload['conversation'];
}

function connect(host: MockHost, statePayload: StatePayload = emptyState()) {
  expect(host.lastOfType('hello')).toBeTruthy();
  host.deliver('hello_ack', { version: PROTOCOL_VERSION, agent: statePayload.agent, appearance: statePayload.appearance });
  host.deliver('state', statePayload);
}

// Every way into agent setup asks the shell for Preferences › Agent; nothing
// of setup is drawn in the pane itself.
function expectAgentPreferencesAsked() {
  const host = currentHost!;
  expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'open_preferences', section: 'agent' });
  expect(screen.queryByTestId('setup-chooser')).not.toBeInTheDocument();
  expect(screen.queryByTestId('setup-api-key')).not.toBeInTheDocument();
  expect(screen.queryByTestId('agent-key-openai')).not.toBeInTheDocument();
}

describe('App', () => {
  let host: MockHost;

  beforeEach(() => {
    host = new MockHost();
  });

  it('keeps the pane toggle in the active Agent header', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);

    await userEvent.click(screen.getByRole('button', { name: 'Hide the Agent panel' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'collapse_agent_pane' });

    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    expect(screen.getAllByRole('button', { name: 'Hide the Agent panel' })).toHaveLength(1);
  });

  // The project chat's composer shows the host's models; a pick goes to the
  // host for the active chat, and the photo composer has no pickers.
  it('sends the chat\'s model and thinking choice to the host', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ chatModel: { provider: 'openai', model: 'gpt-5.4-mini', effort: '' } }));
    expect(screen.queryByRole('button', { name: /^Model:/ })).toBeNull();
    host.deliver('model_catalog', {
      defaultProvider: 'openai',
      providers: [
        { id: 'openai', label: 'OpenAI', defaultModel: 'gpt-5.4-mini', state: 'ready',
          models: [{ id: 'gpt-5.4-mini', label: 'GPT-5.4 mini', efforts: ['low', 'medium', 'high'] }] },
        { id: 'anthropic', label: 'Anthropic', defaultModel: 'claude-opus-5-5', state: 'ready',
          models: [{ id: 'claude-opus-5-5', label: 'Claude Opus 5.5', efforts: ['low', 'medium', 'high', 'xhigh', 'max'] }] },
      ],
    });
    await userEvent.click(screen.getByRole('button', { name: 'Model: GPT-5.4 mini' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Claude Opus 5.5' }));
    expect(host.lastOfType('set_chat_model')?.payload).toEqual({ provider: 'anthropic', model: 'claude-opus-5-5', effort: '' });

    host.deliver('chat_model', { conversationId: 'conv-1', chatModel: { provider: 'anthropic', model: 'claude-opus-5-5', effort: '' } });
    await userEvent.click(screen.getByRole('button', { name: 'Thinking: Default' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Extra high' }));
    expect(host.lastOfType('set_chat_model')?.payload).toEqual({ provider: 'anthropic', model: 'claude-opus-5-5', effort: 'xhigh' });
  });

  it('offers only Back and manual settings from a temporary filament chat', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ navigation: { focused: true } }));
    expect(screen.getByRole('button', { name: 'Back to Prepare' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'New chat' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Chat actions' })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Open filament settings' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'open_filament_settings' });
    await userEvent.click(screen.getByRole('button', { name: 'Back to Prepare' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'return_to_workspace' });
  });

  it('writes a filament chat its instructions from the filament it is about, and again when that changes', () => {
    render(<App getTransport={() => host.transport} />);
    const filament = { kind: 'filament' as const, slot: 1, preset: 'Bambu PLA Basic @BBL X1C', shown: 'Bambu PLA Basic',
                       material: 'PLA', stock: true, copyName: 'Bambu PLA Basic @BBL X1C - Copy' };
    connect(host, emptyState({ navigation: { focused: true }, session: filament }));
    const sent = () => host.received.filter((envelope) => envelope.type === 'filament_instructions');
    expect(sent()).toHaveLength(1);
    expect((sent()[0].payload as { text: string }).text).toContain('Name: Bambu PLA Basic @BBL X1C\n');
    expect((sent()[0].payload as { text: string }).text).toContain('Copy to save as: Bambu PLA Basic @BBL X1C - Copy\n');
    // Saved as a copy: the chat is about the copy.
    connect(host, emptyState({ navigation: { focused: true },
                               session: { ...filament, preset: 'Bambu PLA Basic @BBL X1C - Copy', stock: false, copyName: undefined } }));
    expect(sent()).toHaveLength(2);
    expect((sent()[1].payload as { text: string }).text).toContain('Name: Bambu PLA Basic @BBL X1C - Copy\n');
    expect(host.received.some((envelope) => envelope.type === 'printer_instructions')).toBe(false);
  });

  // The invitation is for an empty chat; once the person has spoken the field
  // says "Reply…", and while answer buttons show it says typing works too.
  it('changes the message field\'s hint as the chat goes on', () => {
    render(<App getTransport={() => host.transport} />);
    const field = () => screen.getByRole('textbox', { name: 'Message the Agent' });
    const asked = [
      { id: 'm-1', role: 'user', state: 'complete', text: 'my print failed', attempt: 1 },
      { id: 'm-2', role: 'assistant', state: 'complete', text: 'What happened?\nChoices: It stopped | Not sure', attempt: 1 },
    ] as StatePayload['conversation'];
    connect(host, emptyState());
    expect(field()).toHaveAttribute('placeholder', 'Ask about this print or request a change…');
    host.deliver('state', emptyState({ conversation: asked }));
    expect(field()).toHaveAttribute('placeholder', 'Or type your own answer…');
    host.deliver('state', emptyState({ conversation: [...asked,
      { id: 'm-3', role: 'user', state: 'complete', text: 'It stopped', attempt: 1 }] }));
    expect(field()).toHaveAttribute('placeholder', 'Reply…');
  });

  it('writes no filament instructions for the project chat', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({}));
    expect(host.received.some((envelope) => envelope.type === 'filament_instructions')).toBe(false);
  });

  it('keeps the unconfigured temporary chat focused on setup and manual settings', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' }, navigation: { focused: true } }));
    expect(screen.getByRole('button', { name: 'Set up the agent' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Open filament settings' })).toBeInTheDocument();
    expect(screen.queryByRole('textbox')).not.toBeInTheDocument();
  });

  it('keeps the focused chat escape visible while the Agent needs setup', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' }, navigation: { focused: true }, conversation: [
      { id: 'n-1', role: 'note', state: 'complete', text: 'Filament settings for slot 1.', attempt: 1 },
    ] }));
    expect(screen.getByRole('button', { name: 'Back to Prepare' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Open filament settings' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Set up the agent' })).toBeInTheDocument();
  });

  it('shows connecting, then reconstructs the conversation from host state', () => {
    render(<App getTransport={() => host.transport} />);
    expect(screen.getByTestId('connecting')).toBeInTheDocument();

    connect(
      host,
      emptyState({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'what is on the plate?', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'complete', text: 'Two cubes.', attempt: 1, inReplyTo: 'm-1' },
        ],
      }),
    );

    expect(screen.getByText('what is on the plate?')).toBeInTheDocument();
    expect(screen.getByText('Two cubes.')).toBeInTheDocument();
    // A host that reports no applied settings and a chat with no intent: the
    // card is the facts alone, under its eyebrow, and claims nothing else.
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('Test Printer 0.4');
    expect(within(card).queryByRole('heading')).toBeNull();
    expect(card).not.toHaveTextContent(/No changes from presets|Preset fallback/);
  });

  it('renders assistant Markdown while keeping user input literal', () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: '**literal user text**', attempt: 1 },
          {
            id: 'm-2',
            role: 'assistant',
            state: 'complete',
            text: '### Recommended settings\n\nUse **four walls**.\n\n- Gyroid infill\n- Add a brim',
            attempt: 1,
            inReplyTo: 'm-1',
          },
        ],
      }),
    );

    expect(screen.getByText('**literal user text**')).toBeInTheDocument();
    expect(screen.getByRole('heading', { level: 3, name: 'Recommended settings' })).toBeInTheDocument();
    expect(screen.getByText('four walls').tagName).toBe('STRONG');
    expect(screen.getByRole('list')).toHaveTextContent('Gyroid infill');
    expect(screen.getByRole('list')).toHaveTextContent('Add a brim');
  });

  it('draws a line where a G-code left from this chat, and none for another chat', () => {
    const marker = {
      seq: 12, at: '2026-08-30T01:02:00Z', afterId: 'removed-message', hash: 'a71f8c04'.repeat(8),
      plateIndex: 0, plateName: 'Plate 1', fileName: '', printerName: 'Garage X1C',
    };
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ gcodeMarkers: [
      { ...marker, id: 'd-1', kind: 'send', conversationId: 'conv-1' },
      { ...marker, id: 'd-2', kind: 'export', conversationId: 'conv-other', fileName: 'elsewhere.gcode.3mf', printerName: '' },
    ] }));

    expect(screen.getByRole('note')).toHaveTextContent('Plate 1 · a71f 8c04 → Garage X1C');
    expect(screen.queryByText(/elsewhere\.gcode\.3mf/)).not.toBeInTheDocument();
  });

  it('opens the Print history at a marker’s G-code, and comes back to the marker', () => {
    const marker = {
      seq: 12, at: '2026-08-30T01:02:00Z', afterId: 'removed-message', hash: 'a71f8c04'.repeat(8),
      plateIndex: 0, plateName: 'Plate 1', fileName: '', printerName: 'Garage X1C',
    };
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ gcodeMarkers: [{ ...marker, id: 'd-1', kind: 'send', conversationId: 'conv-1' }] }));

    fireEvent.click(screen.getByRole('button', { name: 'history' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'open_print_history', hash: marker.hash });

    // The history's Open in chat: the host shows the chat, then names the item.
    expect(screen.getByRole('note')).not.toHaveClass('revealed');
    host.deliver('reveal_item', { itemId: 'd-1' });
    expect(screen.getByRole('note')).toHaveClass('revealed');
  });

  it('leads a filament note with its colour, drawn and never named', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    host.deliver('message_added', {
      message: { id: 'n-1', role: 'note', state: 'complete', text: 'Filament is now Prusament PETG.', attempt: 1, swatch: '#E67E22' },
    });
    host.deliver('message_added', {
      message: { id: 'n-2', role: 'note', state: 'complete', text: 'A note about something else.', attempt: 1 },
    });
    const notes = document.querySelectorAll('.message.note');
    expect(notes).toHaveLength(2);
    const swatch = notes[0].querySelector<HTMLElement>('.note-swatch');
    expect(swatch).not.toBeNull();
    expect(swatch!.style.background).not.toBe('');
    expect(swatch!.getAttribute('aria-hidden')).toBe('true');
    expect(notes[0]).toHaveTextContent('Filament is now Prusament PETG.');
    // A note with no colour draws none.
    expect(notes[1].querySelector('.note-swatch')).toBeNull();
    // One System Line each, its whole text in the tooltip.
    expect(notes[0].querySelector('.note-text')).toHaveAttribute('title', 'Filament is now Prusament PETG.');
  });

  const fileReport = {
    startedBy: 'user' as const,
    projectOpened: true,
    loads: [{ files: ['C:\\models\\bracket.3mf'], withSettings: true }],
    messages: {
      items: [
        { title: 'JusPrin - Load 3MF', text: 'The 3MF is not supported by JusPrin, loading geometry data only.',
          buttons: ['ok' as const], answer: 'ok' as const },
        { title: 'JusPrin - Object too small', text: 'The object from file bracket.stl is too small.',
          buttons: ['yes' as const, 'no' as const], answer: 'no' as const },
      ],
      truncated: false,
    },
    objects: { items: [], truncated: false },
    truncated: false,
  };

  it('gives the app the project assistant\'s whole prompt once per connection, before the skills', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    connect(host, emptyState({}));
    const types = host.received.map((envelope) => envelope.type);
    const sent = host.received.filter((envelope) => envelope.type === 'project_instructions');
    expect(sent).toHaveLength(1);
    expect((sent[0].payload as { text: string }).text).toBe(projectInstructions());
    expect(types.indexOf('project_instructions')).toBeLessThan(types.indexOf('skills'));
  });

  it('gives the app every project skill in one message, once per connection', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    connect(host, emptyState({}));
    const sent = host.received.filter((envelope) => envelope.type === 'skills');
    expect(sent).toHaveLength(1);
    expect(sent[0].payload).toEqual({ skills });
    expect((sent[0].payload as { skills: { trigger: string }[] }).skills.map((skill) => skill.trigger))
      .toEqual(['calibration_started', 'file_loaded', 'print_issues', 'setup_changed', 'slice_failed']);
    // The calibration skill is also read on demand, by the description skill_read lists it by.
    const calibration = (sent[0].payload as { skills: { name: string; description?: string }[] }).skills
      .find((skill) => skill.name === 'calibration');
    expect(calibration?.description).toMatch(/^Read when a print problem may come from the filament/);
  });

  it('draws the open that made the project as one dated line, not the report the Agent speaks about', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    const createdAt = '2026-10-26T12:16:03Z';
    host.deliver('message_added', {
      message: { id: 'n-1', role: 'note', state: 'complete', text: '', attempt: 1, createdAt, fileReport, fileReportCard: false },
    });
    expect(screen.queryByLabelText('Notes about this file')).not.toBeInTheDocument();
    const notes = document.querySelectorAll('.message.note');
    expect(notes).toHaveLength(1);
    const time = new Date(createdAt).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false });
    // The file's name, never its folder.
    expect(notes[0]).toHaveTextContent(new RegExp(`^Opened bracket\\.3mf as a new project · you · (${time}|Yesterday|.+ ago|Last week|[A-Z][a-z]{2} \\d+.*)$`));
    expect(notes[0]).not.toHaveTextContent('models');
  });

  it('draws nothing for an import, whose change row is already in the thread', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    host.deliver('message_added', { message: { id: 'n-1', role: 'note', state: 'complete', text: '', attempt: 1,
      fileReport: { ...fileReport, projectOpened: false }, fileReportCard: false } });
    expect(document.querySelectorAll('.message.note')).toHaveLength(0);
  });

  it('does not call a saved project reopening with its chat new, and names the Agent when it opened the file', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ conversation: [
      { id: 'm-1', role: 'user', state: 'complete', text: 'older message', attempt: 1 },
      { id: 'n-1', role: 'note', state: 'complete', text: '', attempt: 1,
        fileReport: { ...fileReport, startedBy: 'agent' as const }, fileReportCard: false },
    ] }));
    expect(document.querySelector('.message.note')).toHaveTextContent(/^Opened bracket\.3mf · Agent$/);
  });

  it('places file notes above the original setup offer when no Agent is configured', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' }, conversation: [
      { id: 'n-1', role: 'note', state: 'complete', text: '', attempt: 1, fileReport, fileReportCard: true },
    ] }));
    const card = screen.getByLabelText('Notes about this file');
    const offer = screen.getByTestId('agent-not-configured');
    expect(card.compareDocumentPosition(offer) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(screen.queryByTestId('agent-unavailable')).not.toBeInTheDocument();
    expect(card).toHaveTextContent('bracket.3mf');
    // Titled without the app's own name, in OrcaSlicer's words.
    expect(within(card).getByText('Load 3MF')).toBeInTheDocument();
    expect(card).toHaveTextContent('loading geometry data only');
    // A question answered in the person's place says what that left.
    expect(card).toHaveTextContent('JusPrin answered No to this question.');
    expect(within(card).queryByRole('button')).not.toBeInTheDocument();
    expect(screen.getAllByRole('button', { name: /Set up the agent/i })).toHaveLength(1);
    expect(screen.getByLabelText('Message the Agent')).toBeDisabled();
    await userEvent.click(within(offer).getByRole('button', { name: 'Set up the agent' }));
    expectAgentPreferencesAsked();
    expect(screen.getByLabelText('Notes about this file')).toBeInTheDocument();
  });

  it('keeps file notes in history when the unconfigured chat has a user message', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' }, conversation: [
      { id: 'm-1', role: 'user', state: 'complete', text: 'older message', attempt: 1 },
      { id: 'n-1', role: 'note', state: 'complete', text: '', attempt: 1, fileReport, fileReportCard: true },
    ] }));
    expect(screen.queryByTestId('agent-not-configured')).not.toBeInTheDocument();
    expect(screen.getByText('older message')).toBeInTheDocument();
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();
    // One way into setup, under the thread; the card does not repeat it.
    expect(within(screen.getByLabelText('Notes about this file')).queryByRole('button')).toBeNull();
    expect(screen.getAllByRole('button', { name: /Set up the agent/i })).toHaveLength(1);
  });

  it('sends a user message on Enter and streams the reply with stop support', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);

    const textarea = screen.getByLabelText('Message the Agent');
    await userEvent.type(textarea, 'hello agent');
    fireEvent.keyDown(textarea, { key: 'Enter' });

    const sent = host.lastOfType('user_message');
    expect(sent).toBeTruthy();
    const payload = sent!.payload as { clientMessageId: string; text: string };
    expect(payload.text).toBe('hello agent');
    expect(payload.clientMessageId).toMatch(/^c-/);

    host.deliver('message_added', {
      message: { id: 'm-1', role: 'user', state: 'complete', text: 'hello agent', attempt: 1, clientMessageId: payload.clientMessageId },
    });
    host.deliver('assistant_started', { messageId: 'm-2', inReplyTo: 'm-1', attempt: 1 });
    host.deliver('assistant_delta', { messageId: 'm-2', seq: 0, text: 'Looking' });
    host.deliver('assistant_delta', { messageId: 'm-2', seq: 1, text: ' at the plate' });

    expect(screen.getByText('Looking at the plate')).toBeInTheDocument();

    await userEvent.click(screen.getByLabelText('Stop generating'));
    const stop = host.lastOfType('stop_generation');
    expect((stop!.payload as { messageId: string }).messageId).toBe('m-2');
    host.deliver('assistant_stopped', { messageId: 'm-2' });
    expect(screen.getByText('Stopped')).toBeInTheDocument();
  });

  it('does not send on Shift+Enter or while composing with an IME', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);

    const textarea = screen.getByLabelText('Message the Agent');
    await userEvent.type(textarea, 'first line');

    fireEvent.keyDown(textarea, { key: 'Enter', shiftKey: true });
    expect(host.lastOfType('user_message')).toBeUndefined();

    fireEvent.keyDown(textarea, { key: 'Enter', keyCode: 229 });
    expect(host.lastOfType('user_message')).toBeUndefined();

    fireEvent.keyDown(textarea, { key: 'Enter', isComposing: true });
    expect(host.lastOfType('user_message')).toBeUndefined();

    fireEvent.keyDown(textarea, { key: 'Enter' });
    expect(host.lastOfType('user_message')).toBeTruthy();
  });

  it('offers retry on a failed reply and requests it from the host', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);

    host.deliver('message_added', { message: { id: 'm-1', role: 'user', state: 'complete', text: '/fail', attempt: 1 } });
    host.deliver('assistant_started', { messageId: 'm-2', inReplyTo: 'm-1', attempt: 1 });
    host.deliver('assistant_failed', {
      messageId: 'm-2',
      error: { code: 'mock_failure', message: 'The Agent service reported a deterministic test failure.', retryable: true },
    });

    await userEvent.click(screen.getByText('Retry'));
    const retry = host.lastOfType('retry_message');
    expect((retry!.payload as { messageId: string }).messageId).toBe('m-2');
  });

  it('keeps history clear and provides setup from the chat list when the Agent is not configured', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        agent: { status: 'unavailable' },
        conversation: [{ id: 'm-1', role: 'user', state: 'complete', text: 'older message', attempt: 1 }],
      }),
    );

    expect(screen.queryByText('The Agent isn’t set up yet')).not.toBeInTheDocument();
    expect(screen.getByText('older message')).toBeInTheDocument();
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();
    expect(screen.getByTestId('agent-setup-bar')).toBeInTheDocument();
    await userEvent.click(within(screen.getByTestId('agent-setup-bar')).getByRole('button', { name: 'Set up the agent' }));
    expectAgentPreferencesAsked();
    expect(screen.getByText('older message')).toBeInTheDocument();

    // The chat list's row goes to the same place, and the list stays.
    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    const before = host.received.filter((envelope) => envelope.type === 'shell_action').length;
    await userEvent.click(screen.getByRole('button', { name: /Set up the agent/ }));
    expect(host.received.filter((envelope) => envelope.type === 'shell_action')).toHaveLength(before + 1);
    expectAgentPreferencesAsked();
    expect(screen.getByRole('heading', { name: 'Chats' })).toBeInTheDocument();
  });

  it('offers the one setup action and nothing else when no Agent is configured and the chat is empty', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' } }));

    expect(screen.getByTestId('agent-not-configured')).toBeInTheDocument();
    expect(screen.getByText('No agent connected')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Set up the agent' })).toBeEnabled();
    expect(screen.getByText('NOT SET UP')).toBeInTheDocument();
    // The card stays, as a label for the preset in use and nothing more.
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('Test Printer 0.4');
    expect(card).toHaveTextContent('No agent configured · using the selected preset.');
    expect(within(card).queryByRole('button')).toBeNull();
    // The ask box stays in place, inert.
    const composer = screen.getByLabelText('Message the Agent');
    expect(composer).toBeDisabled();
    expect(composer).toHaveAttribute('placeholder', 'ask, or steer this chat\u2026');
  });

  it('shows an external tool\'s edits as the thread when no Agent is configured', async () => {
    const edit = { seq: 3, createdAt: '2026-10-21T09:00:00', kind: 'setting' as const, actor: 'agent' as const, external: true,
      client: 'Claude Code', label: 'Wall loops', from: '2', to: '3', preset: '0.20mm Standard', key: 'wall_loops',
      conversationId: 'conv-1', afterId: '' };
    const slice = { seq: 4, id: 'work-4', startedAt: '2026-10-21T09:00:05', endedAt: '2026-10-21T09:00:40', kind: 'slice',
      title: 'Slicing plate 1', state: 'done' as const, detail: '', conversationId: 'conv-1', afterId: '', cancellable: false };
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' }, changes: [edit], work: [slice] }));

    expect(screen.queryByTestId('agent-not-configured')).not.toBeInTheDocument();
    expect(screen.queryByText('NOT SET UP')).not.toBeInTheDocument();
    expect(screen.getByText('Claude Code, in 0.20mm Standard', { exact: false })).toBeInTheDocument();
    expect(screen.getByTestId('work-work-4')).toHaveTextContent('Slicing plate 1');
    // The thread names who edited; the card states only what is in force.
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('No agent configured');
    expect(card).not.toHaveTextContent(/Edited by|Claude Code/);
    // Setup takes the composer's place: there is nothing to type to.
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();
    await userEvent.click(within(screen.getByTestId('agent-setup-bar')).getByRole('button', { name: 'Set up the agent' }));
    expectAgentPreferencesAsked();
    expect(screen.getByTestId('work-work-4')).toBeInTheDocument();
  });

  it('opens Preferences › Agent from the offer and draws the composer once an agent is there, sending nothing', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' } }));
    await userEvent.click(within(screen.getByTestId('agent-not-configured')).getByRole('button', { name: 'Set up the agent' }));
    expectAgentPreferencesAsked();
    // The window is native and over the pane: the pane keeps its offer.
    expect(screen.getByTestId('agent-not-configured')).toBeInTheDocument();

    const sentBefore = host.received.length;
    host.deliver('agent_status', { status: 'ready' });
    expect(screen.queryByTestId('agent-not-configured')).not.toBeInTheDocument();
    expect(screen.getByLabelText('Message the Agent')).toBeEnabled();
    expect(host.received.slice(sentBefore).filter((envelope) => envelope.type !== 'draft_update')).toEqual([]);
  });

  it('turns the offer into the thread when the first edit arrives', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' } }));
    expect(screen.getByTestId('agent-not-configured')).toBeInTheDocument();
    host.deliver('change_added', { change: { seq: 1, createdAt: '2026-10-21T09:00:00', kind: 'step', actor: 'person',
      label: 'Rotate', conversationId: 'conv-1', afterId: '' } });
    expect(screen.queryByTestId('agent-not-configured')).not.toBeInTheDocument();
    expect(screen.getByText('Rotate')).toBeInTheDocument();
    expect(screen.getByTestId('agent-setup-bar')).toBeInTheDocument();
  });

  it('shows the internal bridge error with retry when the host rejects the protocol version', () => {
    render(<App getTransport={() => host.transport} />);
    host.deliver('hello_reject', { supportedVersions: [99], message: 'version 99 only' });
    expect(screen.getByTestId('bridge-error')).toBeInTheDocument();
    expect(screen.getByText('version 99 only')).toBeInTheDocument();
  });

  it('shows the no-transport bridge error when window.wx is absent', () => {
    render(<App getTransport={() => null} transportRetryLimit={0} />);
    expect(screen.getByTestId('bridge-error')).toBeInTheDocument();
  });

  // stale_workspace is the code; stale_revision is what chats saved before the
  // codes were unified still hold.
  it.each(['stale_workspace', 'stale_revision'])('explains a stale action (%s) distinctly from other failures', (code) => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: toolCallConversation(),
        toolActivities: [
          toolActivity({
            state: 'failed',
            error: { code, message: 'Nothing ran: the project changed after this call was made.' },
          }),
        ],
      }),
    );

    // The stale call reads "Not run" in the turn's opened action log, and
    // opens into the host's explanation.
    fireEvent.click(screen.getByRole('button', { name: /1 action · 1 failed/ }));
    fireEvent.click(screen.getByRole('button', { name: /· Not run/ }));
    expect(screen.getByText('Nothing ran: the project changed after this call was made.')).toBeInTheDocument();
    expect(screen.getByText(code)).toBeInTheDocument();
  });

  it('reconstructs tool cards from host state after a reload', () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: toolCallConversation(),
        toolActivities: [toolActivity({ state: 'running', progress: { current: 2, total: 3 } })],
      }),
    );

    expect(screen.getByLabelText('Duplicate "cube-a" progress')).toBeInTheDocument();
    expect(screen.getByText('Cancel')).toBeInTheDocument();
  });

  it('lists conversations, switches, and creates new ones through the bridge', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversations: [
          { id: 'conv-1', title: 'Conversation 1', createdAt: 't' },
          { id: 'conv-2', title: 'Second', createdAt: 't' },
        ],
        activeConversationId: 'conv-1',
      }),
    );

    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    expect(screen.queryByRole('log')).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Open chat: Second' }));
    const switched = host.lastOfType('switch_conversation');
    expect(switched!.payload).toEqual({ conversationId: 'conv-2' });

    await userEvent.click(screen.getByLabelText('New chat'));
    expect(host.lastOfType('create_conversation')).toBeTruthy();
  });

  it('shows saved historical setup and requires a scoped restore confirmation', async () => {
    render(<App getTransport={() => host.transport} />);
    const earlier = { ...context, setupIntent: 'Quick print', planHeadline: 'Coarse layers and 5% infill for a quick print',
      presetDeltas: [{ key: 'sparse_infill_density', label: 'Sparse infill density', value: '5%', preset: '15%',
        presetType: 'process' as const, presetName: 'Test Printer 0.4' }] };
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Quick print', createdAt: 't' },
        { id: 'conv-b', title: 'Strong print', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a', docRevision: 23,
      chatResume: { status: 'changed', savedAt: '2026-10-03T12:00:00Z', versionId: 'v-a', summary: earlier },
      conversation: toolCallConversation(),
      toolActivities: [toolActivity()],
      draft: 'Keep my current draft',
      context: { ...context, setupIntent: 'Strong print', planHeadline: 'Four walls and gyroid infill for strength' },
    }));

    // The saved card is its own chat's: its title and headline, never the
    // active chat's.
    const saved = screen.getByLabelText('Earlier setup — saved with this conversation');
    expect(saved).toHaveTextContent('Quick print');
    expect(saved).toHaveTextContent('Coarse layers and 5% infill for a quick print');
    expect(saved).not.toHaveTextContent(/Strong print|Four walls/);
    expect(screen.getAllByText(/The canvas shows your current project\./)).toHaveLength(1);
    expect(screen.getByRole('textbox', { name: /message/i })).toBeDisabled();
    await userEvent.click(screen.getAllByRole('button', { name: 'Restore and resume' })[0]);
    expect(screen.getByRole('dialog')).toHaveTextContent('earlier model and settings');
    expect(host.lastOfType('restore_conversation')).toBeUndefined();
    await userEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('restore_conversation')).toBeUndefined();

    await userEvent.click(screen.getAllByRole('button', { name: 'Ask current chat about this' })[0]);
    expect(host.lastOfType('switch_conversation')?.payload).toEqual({ conversationId: 'conv-b' });
    expect(host.lastOfType('draft_update')?.payload).toMatchObject({
      append: expect.stringContaining('Excerpt:'),
    });
    expect(host.lastOfType('user_message')).toBeUndefined();
  });

  it('renders one earlier-chat notice with three actions above a locked field', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a', docRevision: 23,
      chatResume: { status: 'changed', versionId: 'v-a', savedAt: '2026-10-03T10:42:00Z', summary: context },
    }));
    expect(screen.getAllByRole('note')).toHaveLength(1);
    const boundary = screen.getByRole('note');
    expect(boundary).toHaveTextContent('There have been project updates since this chat. Restore its saved project version to continue.');
    expect(within(boundary).getByRole('button', { name: 'Restore and resume' })).toBeEnabled();
    expect(within(boundary).getByRole('button', { name: 'Return to active chat' })).toBeEnabled();
    expect(within(boundary).getByRole('button', { name: 'Ask current chat about this' })).toBeEnabled();
    expect(screen.getByRole('textbox', { name: /message/i })).toBeDisabled();
    // The notice above the field says why; the field itself is an empty shell
    // with nothing to attach.
    expect(screen.getByRole('textbox', { name: /message/i })).toHaveAttribute('placeholder', '');
    expect(screen.getByRole('button', { name: 'Send message' })).toBeDisabled();
    expect(screen.queryByRole('button', { name: 'Attach a file' })).toBeNull();
    // The boundary ends the conversation; it is not part of the setup card.
    expect(within(screen.getByTestId('current-setup')).queryByRole('note')).toBeNull();
    expect(screen.getByTestId('current-setup')).not.toHaveTextContent('project updates');
  });

  it('keeps an earlier chat locked against typing and sending', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a',
      chatResume: { status: 'changed', versionId: 'v-a', summary: context },
      draft: 'A draft that belongs to the active chat',
    }));
    const field = screen.getByRole('textbox', { name: /message/i });
    expect(field).toHaveValue('');
    await userEvent.type(field, 'hello{Enter}');
    expect(field).toHaveValue('');
    // The shell keeps its Send, disabled (Figma "Historical composer · disabled").
    const send = screen.getByRole('button', { name: /send/i });
    expect(send).toBeDisabled();
    await userEvent.click(send);
    expect(host.lastOfType('user_message')).toBeUndefined();
    expect(host.lastOfType('draft_update')).toBeUndefined();
  });

  it('describes an unchanged checkpoint as resumable, not as project updates', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a',
      chatResume: { status: 'unchanged', versionId: 'v-a', summary: context },
    }));
    const boundary = screen.getByRole('note');
    expect(boundary).not.toHaveTextContent('There have been project updates');
    expect(boundary).toHaveTextContent('This chat is inactive. Resume from its saved setup to continue.');
    expect(within(boundary).getByRole('button', { name: 'Resume saved setup' })).toBeEnabled();
    expect(within(boundary).getByRole('button', { name: 'Return to active chat' })).toBeEnabled();
    expect(within(boundary).getByRole('button', { name: 'Ask current chat about this' })).toBeEnabled();
    expect(screen.getByRole('textbox', { name: /message/i })).toBeDisabled();
  });

  it('does not claim project updates when a historical checkpoint is unavailable', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a',
      chatResume: { status: 'unavailable' },
    }));
    expect(screen.queryByText(/There have been project updates/)).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Restore and resume' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Resume saved setup' })).not.toBeInTheDocument();
    const boundary = screen.getByRole('note');
    expect(boundary).toHaveTextContent('This chat has no recoverable project checkpoint. Its saved state is missing or corrupt.');
    expect(within(boundary).getByRole('button', { name: 'Return to active chat' })).toBeEnabled();
    expect(within(boundary).getByRole('button', { name: 'Ask current chat about this' })).toBeEnabled();
    expect(screen.getByRole('textbox', { name: /message/i })).toBeDisabled();
  });

  it('does not let a live workspace update rewrite the saved card', () => {
    render(<App getTransport={() => host.transport} />);
    const saved: WorkspaceContext = {
      ...context, setupIntent: 'Earlier bracket', planHeadline: 'Two walls for a light bracket',
      plates: [{ ...context.plates[0], sliced: true,
        estimate: { printTimeSeconds: 2520, materialGrams: 18, materialCost: null } }],
      appliedSetup: { version: 1, plateId: '11', printableObjects: 1, spiralMode: false,
        variableLayerHeight: false, objects: ['cube-a'], localOverrides: [] },
      presetDeltas: [{ key: 'wall_loops', label: 'Wall loops', preset: '3', value: '2', display: '3 → 2',
        presetType: 'process', presetName: 'Test Printer 0.4', page: 'Strength', group: 'Walls' }],
    };
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a',
      chatResume: { status: 'changed', versionId: 'saved-1', summary: saved },
    }));
    const earlier = screen.getByLabelText('Earlier setup — saved with this conversation');
    expect(earlier).toHaveTextContent('Two walls for a light bracket');
    expect(earlier).toHaveTextContent('~42 min');

    host.deliver('context', { context: { ...context, revision: 3, setupIntent: 'Live project',
      planHeadline: 'Four walls and gyroid infill for strength',
      plates: [{ ...context.plates[0], sliced: true,
        estimate: { printTimeSeconds: 6000, materialGrams: 30, materialCost: null } }] } });
    const after = screen.getByLabelText('Earlier setup — saved with this conversation');
    expect(after).toHaveTextContent('Two walls for a light bracket');
    expect(after).toHaveTextContent('~42 min');
    expect(after).not.toHaveTextContent(/Four walls|Live project|~1h 40m/);
  });

  it('returns to the active chat without restoring the historical project', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a',
      chatResume: { status: 'changed', versionId: 'saved-1', summary: context },
    }));
    await userEvent.click(screen.getAllByRole('button', { name: 'Return to active chat' })[0]);
    expect(host.lastOfType('switch_conversation')?.payload).toEqual({ conversationId: 'conv-b' });
    expect(host.lastOfType('restore_conversation')).toBeUndefined();
  });

  it('resumes an unchanged saved setup through the checked transition', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({
      conversations: [
        { id: 'conv-a', title: 'Earlier', createdAt: 't' },
        { id: 'conv-b', title: 'Current', createdAt: 't' },
      ],
      activeConversationId: 'conv-b', viewedConversationId: 'conv-a', docRevision: 9,
      chatResume: { status: 'unchanged', versionId: 'v-a', summary: context },
    }));
    await userEvent.click(screen.getAllByRole('button', { name: 'Resume saved setup' })[0]);
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(host.lastOfType('restore_conversation')?.payload).toEqual({
      conversationId: 'conv-a', activeConversationId: 'conv-b', docRevision: 9,
    });
  });

  it('offers Rename above Delete and preserves messages when a title changes', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ conversation: toolCallConversation() }));
    await userEvent.click(screen.getByRole('button', { name: 'Chat actions' }));
    expect(screen.getAllByRole('menuitem').map((item) => item.textContent)).toEqual(['Rename', 'Delete']);
    expect(screen.getByRole('menuitem', { name: 'Rename' })).toHaveFocus();
    await userEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));
    const input = screen.getByRole('textbox', { name: 'Chat title' });
    expect(input).toHaveFocus();
    await userEvent.clear(input);
    expect(screen.getByRole('button', { name: 'Save' })).toBeDisabled();
    await userEvent.type(input, 'My backpack frame{Enter}');
    expect(host.lastOfType('rename_conversation')?.payload).toEqual({ conversationId: 'conv-1', title: 'My backpack frame' });
    host.deliver('conversations_updated', { conversations: [{ id: 'conv-1', title: 'My backpack frame', createdAt: 't', preview: 'Five walls' }], busy: false });
    expect(screen.getByRole('heading', { name: 'My backpack frame' })).toBeInTheDocument();
    expect(screen.getByText('duplicate the selected object')).toBeInTheDocument();
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Chat actions' })).toHaveFocus();
    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    expect(screen.getByRole('button', { name: 'Open chat: My backpack frame' })).toHaveTextContent('Five walls');
  });

  it('requires confirmation to delete a chat and returns to the list', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    await userEvent.click(screen.getByRole('button', { name: 'Chat actions' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Delete' }));
    expect(host.lastOfType('delete_conversation')).toBeUndefined();
    expect(screen.getByRole('button', { name: 'Cancel' })).toHaveFocus();
    await userEvent.keyboard('{Escape}');
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Chat actions' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Delete' }));
    await userEvent.click(screen.getByRole('button', { name: 'Delete' }));
    expect(host.lastOfType('delete_conversation')?.payload).toEqual({ conversationId: 'conv-1' });
    host.deliver('state', emptyState({ activeConversationId: 'conv-2', conversations: [{ id: 'conv-2', title: 'New chat', createdAt: 't' }] }));
    expect(screen.getByRole('heading', { name: 'Chats' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Open chat: Conversation 1' })).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Open chat: New chat' })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'New chat' }));
    expect(host.lastOfType('create_conversation')).toBeTruthy();
  });

  it('keeps a draft when browsing chats and opens configuration for a connected agent', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    await userEvent.type(screen.getByRole('textbox', { name: 'Message the Agent' }), 'Unsaved thought');
    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    await userEvent.click(screen.getByRole('button', { name: 'Open chat: Conversation 1' }));
    expect(screen.getByRole('textbox', { name: 'Message the Agent' })).toHaveValue('Unsaved thought');
    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    // A connected agent is changed in Preferences › Agent, over the panel.
    await userEvent.click(screen.getByRole('button', { name: /Configure Agent/ }));
    expectAgentPreferencesAsked();
    expect(screen.getByRole('heading', { name: 'Chats' })).toBeInTheDocument();
  });

  it('opens Preferences on Ctrl+, while the panel has the focus', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    await userEvent.keyboard('{Control>},{/Control}');
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'open_preferences' });
  });

  it('disables changing chats while a tool continuation is pending', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ conversationBusy: true }));
    expect(screen.getByRole('button', { name: 'New chat' })).toBeDisabled();
    await userEvent.click(screen.getByRole('button', { name: 'Chat actions' }));
    expect(screen.getByRole('menuitem', { name: 'Delete' })).toBeDisabled();
    expect(screen.getByRole('menuitem', { name: 'Rename' })).toBeEnabled();
    host.deliver('bridge_error', { code: 'busy', message: 'Resolve pending actions before deleting this chat.' });
    expect(screen.getByRole('alert')).toHaveTextContent('Resolve pending actions');
  });

  it('replaces the chat list when the native project changes', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    host.deliver('state', emptyState({ context: { ...context, sessionId: 'other-project' }, conversations: [{ id: 'other', title: 'Other project chat', createdAt: 't' }], activeConversationId: 'other' }));
    await userEvent.click(screen.getByRole('button', { name: 'Back to chats' }));
    expect(screen.queryByRole('button', { name: 'Open chat: Conversation 1' })).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Open chat: Other project chat' })).toBeInTheDocument();
  });

  it('reports the draft to the host and restores it on reconnect', async () => {
    render(<App getTransport={() => host.transport} draftDebounceMs={1} />);
    connect(host, emptyState({ draft: 'recovered draft' }));

    const textarea = screen.getByLabelText('Message the Agent');
    expect(textarea).toHaveValue('recovered draft');

    await userEvent.type(textarea, ' plus more');
    await new Promise((resolve) => setTimeout(resolve, 20));
    const draft = host.lastOfType('draft_update');
    expect(draft).toBeTruthy();
    expect((draft!.payload as { text: string }).text).toBe('recovered draft plus more');
  });

  // The card is a band docked under the header: a divider at rest, and a
  // shadow too only while the thread is scrolled under it.
  it('lifts the setup band only while messages are scrolled under it', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ conversation: toolCallConversation() }));
    const band = screen.getByTestId('current-setup').parentElement!;
    const thread = screen.getByRole('log', { name: 'Agent conversation' });
    expect(band).toHaveClass('pinned-setup');
    expect(band).not.toHaveClass('pinned-setup--scrolled');

    thread.scrollTop = 40;
    fireEvent.scroll(thread);
    expect(band).toHaveClass('pinned-setup--scrolled');

    thread.scrollTop = 0;
    fireEvent.scroll(thread);
    expect(band).not.toHaveClass('pinned-setup--scrolled');
  });

  it('rebuilds the setup card when the native workspace changes', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ context: applied }));
    const card = () => screen.getByTestId('current-setup');
    expect(card()).toHaveTextContent('Test Printer 0.4');
    expect(card()).not.toHaveTextContent('No changes from presets');
    expect(card()).toHaveTextContent('Not sliced yet');

    // A hand edit in Orca, a slice, an intent and a plan: no chat turn in
    // between.
    const changed: WorkspaceContext = {
      ...applied,
      revision: 3,
      setupIntent: "Strong - it'll bear weight",
      planHeadline: 'Four walls and gyroid infill for strength',
      presetDeltas: [{ key: 'wall_loops', label: 'Wall loops', preset: '2', value: '4', display: '2 → 4',
        presetType: 'process', presetName: 'Test Printer 0.4', page: 'Strength', group: 'Walls' }],
      plates: [{ ...applied.plates[0], sliced: true, estimate: { printTimeSeconds: 13800, materialGrams: 47, materialCost: null } }],
    };
    host.deliver('context', { context: changed });

    expect(card()).toHaveAccessibleName('Current setup');
    expect(card()).toHaveTextContent("Strong - it'll bear weight");
    expect(card()).toHaveTextContent('Four walls and gyroid infill for strength');
    expect(card()).not.toHaveTextContent('Wall loops');
    expect(card()).toHaveTextContent('Test Printer 0.4 · Generic PLA · 1 setting changed');
    expect(card()).toHaveTextContent('~3h 50m');
    expect(card()).toHaveTextContent('47 g');
  });

  // A host that can list the settings says so in its hello.
  function connectListing(statePayload: StatePayload, capabilities = ['all_settings']) {
    expect(host.lastOfType('hello')).toBeTruthy();
    host.deliver('hello_ack', { version: PROTOCOL_VERSION, agent: statePayload.agent, appearance: statePayload.appearance,
      capabilities });
    host.deliver('state', statePayload);
  }
  const catalog = (settings: { key: string; label: string; page: string; group: string; display: string; preset?: string }[]) => ({
    available: true,
    process: { name: 'Test Printer 0.4', settings: settings.map((setting) => ({ ...setting, value: setting.display,
      presetDisplay: setting.preset })) },
    printer: { name: 'MyKlipper 0.2 nozzle', settings: [] },
    filaments: [{ slot: 1, preset: 'Generic PLA', material: 'PLA', colour: '#1E90FF', settings: [] }],
    plates: [{ id: '11', name: 'Plate 1', active: true, bedType: 'Textured PEI Plate', ownBedType: false, bedTempKey: '',
      bedTempInitialKey: '', own: [] }],
    objects: [{ id: '21', name: 'cube-a', plateId: '11', overrides: [] }, { id: '22', name: 'cube-b', plateId: '11', overrides: [] }],
  });

  it('opens the All settings panel in place of the chat and returns to the thread as it was', async () => {
    render(<App getTransport={() => host.transport} draftDebounceMs={0} />);
    connectListing(emptyState({ context: { ...applied, selection: { status: 'none', objectIds: [] }, setupIntent: 'Strong enough to bear weight' },
      conversation: [{ id: 'm-1', role: 'user', state: 'complete', text: 'make it strong', attempt: 1 }] }));
    await userEvent.type(screen.getByRole('textbox'), 'half a thought');

    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    const panel = screen.getByTestId('all-settings');
    // The panel asks the host for the table and says so until it arrives.
    expect(host.lastOfType('settings_catalog_request')).toBeTruthy();
    expect(panel).toHaveTextContent('Reading the settings…');
    host.deliver('settings_catalog', catalog([{ key: 'wall_loops', label: 'Wall loops', page: 'Strength', group: 'Walls', display: '2' }]));
    expect(panel).toHaveTextContent('Showing');
    expect(panel).toHaveTextContent('MyKlipper 0.2 nozzle · 0.2 mm nozzle');
    expect(panel).toHaveTextContent('Slot 1 · Generic PLA');
    expect(panel).toHaveTextContent('Test Printer 0.4from the preset');
    expect(panel).toHaveTextContent('Strengthfrom the preset');
    // Back takes the focus the "All settings" link had, so the keyboard is
    // never left on nothing.
    expect(screen.getByRole('button', { name: 'Back to chat' })).toHaveFocus();
    await userEvent.click(within(panel).getByRole('button', { name: /^Strength/ }));
    expect(panel).toHaveTextContent('WallsWall loops2');
    // The panel is not a place to chat: no thread, no card, no message box.
    expect(screen.queryByRole('textbox')).not.toBeInTheDocument();
    expect(screen.queryByText('make it strong')).not.toBeInTheDocument();
    expect(screen.queryByTestId('current-setup')).not.toBeInTheDocument();
    // The panel can still be put away from here.
    await userEvent.click(screen.getByRole('button', { name: 'Hide the Agent panel' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'collapse_agent_pane' });

    await userEvent.click(screen.getByRole('button', { name: 'Back to chat' }));
    expect(screen.queryByTestId('all-settings')).not.toBeInTheDocument();
    expect(screen.getByText('make it strong')).toBeInTheDocument();
    expect(screen.getByTestId('current-setup')).toBeInTheDocument();
    // What was being typed is still there.
    expect(screen.getByRole('textbox')).toHaveValue('half a thought');
  });

  it('asks for the table again as the project changes under the panel', async () => {
    render(<App getTransport={() => host.transport} />);
    const unselected = { ...applied, selection: { status: 'none' as const, objectIds: [] } };
    connectListing(emptyState({ context: unselected }));
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    const requests = () => host.received.filter((envelope) => envelope.type === 'settings_catalog_request').length;
    expect(requests()).toBe(1);
    host.deliver('settings_catalog', catalog([{ key: 'wall_loops', label: 'Wall loops', page: 'Strength', group: 'Walls', display: '2' }]));
    expect(screen.getByTestId('all-settings')).toHaveTextContent('Strengthfrom the preset');

    host.deliver('context', { context: { ...unselected, revision: 3,
      presetDeltas: [{ key: 'wall_loops', label: 'Wall loops', preset: '2', value: '4', display: '2 → 4',
        presetType: 'process', presetName: 'Test Printer 0.4', page: 'Strength', group: 'Walls' }] } });
    expect(requests()).toBe(2);
    host.deliver('settings_catalog', catalog([{ key: 'wall_loops', label: 'Wall loops', page: 'Strength', group: 'Walls',
      display: '4', preset: '2' }]));
    const panel = screen.getByTestId('all-settings');
    expect(panel).toHaveTextContent('Test Printer 0.41 change from the preset');
    expect(panel).toHaveTextContent('Strength1 changed');
    expect(panel).toHaveTextContent('Wall loops4Preset: 2');
  });

  it('opens on the whole project and then follows the 3D view into one object', async () => {
    render(<App getTransport={() => host.transport} />);
    connectListing(emptyState({ context: applied }));
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    const table = { ...catalog([]), objects: [{ id: '21', name: 'cube-a', plateId: '11',
      overrides: [{ target: 'cube-a / Rim', kind: 'modifier', key: 'wall_loops', value: '6', label: 'Wall loops', display: '6' }] },
      { id: '22', name: 'cube-b', plateId: '11', overrides: [] }] };
    host.deliver('settings_catalog', table);
    // cube-a is selected in the fixture; the panel still opens on the project.
    const panel = screen.getByTestId('all-settings');
    expect(screen.getByRole('button', { name: 'Showing: Whole project' })).toBeInTheDocument();
    host.deliver('context', { context: { ...applied, revision: 3, selection: { status: 'none', objectIds: [] } } });
    host.deliver('settings_catalog', table);
    expect(screen.getByRole('button', { name: 'Showing: Whole project' })).toBeInTheDocument();
    host.deliver('context', { context: { ...applied, revision: 4, selection: { status: 'objects', objectIds: ['21'] } } });
    host.deliver('settings_catalog', table);
    expect(screen.getByRole('button', { name: 'Showing: cube-a' })).toBeInTheDocument();
    expect(panel).toHaveTextContent('cube-a uses the project’s settings, except in 1 region: Rim.');
    expect(panel).toHaveTextContent('Wall loops6RegionRim · project: preset');
    host.deliver('context', { context: { ...applied, revision: 5, selection: { status: 'none', objectIds: [] } } });
    expect(screen.getByRole('button', { name: 'Showing: Whole project' })).toBeInTheDocument();
  });

  it('sends a press on an object row\'s link to the host, when the host takes them', async () => {
    const objects = [{ id: '21', name: 'cube-a', plateId: '11', overrides: [
      { target: 'cube-a / Rim', kind: 'modifier', key: 'wall_loops', value: '6', label: 'Wall loops', display: '6',
        action: 'use_project_value' as const },
      { target: 'cube-a / Pin', kind: 'support blocker', key: '', value: '', action: 'remove' as const }] }];
    const { unmount } = render(<App getTransport={() => host.transport} />);
    connectListing(emptyState({ context: applied }), ['all_settings', 'settings_actions']);
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    host.deliver('settings_catalog', { ...catalog([]), objects });
    // The panel opens on the whole project; the object's view has the links.
    await userEvent.click(screen.getByRole('button', { name: 'Showing: Whole project' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'cube-a' }));
    await userEvent.click(screen.getByRole('button', { name: 'Use project value for Wall loops' }));
    expect(host.lastOfType('settings_action')?.payload).toEqual({ sessionId: '1', objectId: '21', action: 'use_project_value',
      kind: 'modifier', target: 'cube-a / Rim', key: 'wall_loops', value: '6' });
    await userEvent.click(screen.getByRole('button', { name: 'Remove support blocker' }));
    expect(host.lastOfType('settings_action')?.payload).toEqual({ sessionId: '1', objectId: '21', action: 'remove',
      kind: 'support blocker', target: '', key: '', value: '' });
    // A refusal says why, over the panel.
    host.deliver('bridge_error', { code: 'settings_action_failed', message: 'cube-a no longer sets that itself' });
    expect(screen.getByRole('alert')).toHaveTextContent('cube-a no longer sets that itself');
    unmount();

    // A host without settings_action gets nothing to press.
    host = new MockHost();
    render(<App getTransport={() => host.transport} />);
    connectListing(emptyState({ context: applied }));
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    host.deliver('settings_catalog', { ...catalog([]), objects });
    await userEvent.click(screen.getByRole('button', { name: 'Showing: Whole project' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'cube-a' }));
    expect(screen.getByTestId('all-settings')).toHaveTextContent('Wall loops6Region');
    expect(screen.queryByRole('button', { name: /Use project value|Remove/ })).toBeNull();
  });

  it('shows a saved chat its own summary, and asks the host for nothing', async () => {
    render(<App getTransport={() => host.transport} />);
    connectListing(emptyState({ context: applied, conversations: [
      { id: 'conv-1', title: 'Conversation 1', createdAt: 't' }, { id: 'conv-0', title: 'Earlier', createdAt: 's' }],
      viewedConversationId: 'conv-0',
      chatResume: { status: 'unchanged', savedAt: 'Oct 8', summary: { ...applied, selection: { status: 'none', objectIds: [] },
        presetDeltas: [{ key: 'brim_width', label: 'Brim width', preset: '0', value: '8', display: '0 → 8 mm',
          presetType: 'process', presetName: 'Test Printer 0.4', page: 'Others', group: 'Brim' }] } } }));
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    expect(host.lastOfType('settings_catalog_request')).toBeUndefined();
    const panel = screen.getByTestId('all-settings');
    expect(panel).toHaveTextContent('A saved chat’s setup');
    expect(panel).toHaveTextContent('Brim width8 mmPreset: 0 mm');
    expect(screen.getByRole('radio', { name: 'All' })).toBeDisabled();
  });

  it('says so when the host cannot list the settings', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ context: applied }));
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    expect(host.lastOfType('settings_catalog_request')).toBeUndefined();
    expect(screen.getByTestId('all-settings')).toHaveTextContent('This JusPrin build cannot list the settings.');
  });

  it('leaves the panel when the project under it is replaced', async () => {
    render(<App getTransport={() => host.transport} />);
    connectListing(emptyState({ context: applied }));
    await userEvent.click(screen.getByRole('button', { name: 'All settings' }));
    expect(screen.getByTestId('all-settings')).toBeInTheDocument();

    host.deliver('context', { context: { ...applied, sessionId: 'another-project', revision: 1 } });
    expect(screen.queryByTestId('all-settings')).not.toBeInTheDocument();
    expect(screen.getByRole('textbox')).toBeInTheDocument();
  });

  it('asks the host to compute estimates for the plate the card shows', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ context: applied }));
    await userEvent.click(screen.getByRole('button', { name: 'Compute estimates' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({
      action: 'compute_setup_estimates', sessionId: '1', plateId: '11' });
  });

  it('draws the plan\'s mark the host sends, until the plan is recorded again', () => {
    render(<App getTransport={() => host.transport} />);
    const planned: WorkspaceContext = { ...applied, setupIntent: 'Halloween headband costume for my kid',
      planHeadline: 'P1S PLA, sturdier walls for the clip', planValidity: 'needs_reassessment',
      planInvalidatedBy: 'Import Object: Print_Bed_Level_Test.stl, by hand' };
    connect(host, emptyState({ context: planned, changes: [{ seq: 4, createdAt: new Date().toISOString(), kind: 'step',
      actor: 'person', label: 'Import Object: Print_Bed_Level_Test.stl', conversationId: 'conv-1', afterId: 'm-1' }] }));
    const card = () => screen.getByTestId('current-setup');
    expect(card()).toHaveTextContent('Import Object: Print_Bed_Level_Test.stl, by hand');
    expect(card()).toHaveTextContent('Halloween headband costume for my kid');
    // The import is the thread's row, never the card's.
    expect(card()).not.toHaveTextContent(/Edited by|Updated|Undo/);
    host.deliver('context', { context: { ...planned, revision: 3, planValidity: 'current', planInvalidatedBy: '' } });
    expect(card()).not.toHaveTextContent('by hand');
    expect(card()).toHaveTextContent('P1S PLA, sturdier walls for the clip');
  });

  it('sends the person\'s request to check the plan from the mark\'s link', async () => {
    render(<App getTransport={() => host.transport} />);
    const planned: WorkspaceContext = { ...applied, setupIntent: 'Garage wall hook for a 1.5 kg bike helmet',
      planHeadline: 'Printed on its side for strength', planValidity: 'needs_reassessment',
      planInvalidatedBy: 'Filament changed to Generic ABS by hand' };
    connect(host, emptyState({ context: planned }));
    await userEvent.click(screen.getByRole('button', { name: 'Check plan' }));
    expect(host.lastOfType('user_message')?.payload).toMatchObject({
      text: 'Check the plan against what changed.', attachmentIds: [] });
  });
});

describe('App attachments', () => {
  let host: MockHost;

  beforeEach(() => {
    host = new MockHost();
  });

  const stagedText = {
    id: 'a-1',
    name: 'notes.txt',
    kind: 'text' as const,
    mime: 'text/plain',
    sizeBytes: 5,
    source: 'picker' as const,
    state: 'staged' as const,
    previewText: 'hello',
  };

  it('renders a staged attachment and includes it when sending', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);

    host.deliver('attachment_updated', { attachment: stagedText });
    expect(screen.getByText('notes.txt')).toBeInTheDocument();

    // A message may be sent with the attachment even when the text is empty.
    const textarea = screen.getByLabelText('Message the Agent');
    fireEvent.keyDown(textarea, { key: 'Enter' });
    const sent = host.lastOfType('user_message');
    expect(sent).toBeTruthy();
    expect((sent!.payload as { attachmentIds: string[] }).attachmentIds).toEqual(['a-1']);
  });

  it('shows a rejected attachment error', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    host.deliver('attachment_updated', {
      attachment: {
        id: 'a-9',
        name: 'firmware.bin',
        kind: 'unsupported',
        mime: '',
        sizeBytes: 4,
        source: 'picker',
        state: 'error',
        error: { code: 'unsupported_type', message: "This file type can't be read by the Agent." },
      },
    });
    expect(screen.getByText("This file type can't be read by the Agent.")).toBeInTheDocument();
  });

  it('removes a staged attachment through the bridge', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    host.deliver('attachment_updated', { attachment: stagedText });

    await userEvent.click(screen.getByLabelText('Remove notes.txt'));
    const removed = host.lastOfType('remove_attachment');
    expect(removed).toBeTruthy();
    expect((removed!.payload as { attachmentId: string }).attachmentId).toBe('a-1');
  });

  it('renders a sent attachment on its message', () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'look', attempt: 1, attachments: ['a-1'] },
        ],
        attachments: [{ ...stagedText, state: 'sent' }],
      }),
    );
    // The chip appears inside the transcript, not the composer.
    expect(screen.getByText('look')).toBeInTheDocument();
    expect(screen.getByText('notes.txt')).toBeInTheDocument();
  });

  it('reads a picked file and sends attach_file', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);

    const input = document.querySelector('input[type="file"]') as HTMLInputElement;
    const file = new File(['hello world'], 'notes.txt', { type: 'text/plain' });
    await act(async () => {
      await userEvent.upload(input, file);
    });

    // FileReader is async; wait for the attach_file envelope to arrive.
    await vi.waitFor(() => {
      const attached = host.lastOfType('attach_file');
      expect(attached).toBeTruthy();
      const payload = attached!.payload as { name: string; source: string; dataBase64: string };
      expect(payload.name).toBe('notes.txt');
      expect(payload.source).toBe('picker');
      expect(payload.dataBase64).toContain('base64,');
    });
  });
});

describe('App confirmation', () => {
  // A Calibrate… row's question (board 3): the design system's Confirmation
  // dialog over the project chat, answered once with its request id.
  const question = {
    requestId: 'calibrate-1',
    title: 'Calibrate Gray PETG on Prusa MK4?',
    body: 'Backpack hook will be saved and closed. A new calibration project opens for Gray PETG on Prusa MK4.',
    confirm: 'Start',
    cancel: 'Cancel',
  };

  it("asks the host's question and sends Start back with its id", async () => {
    const host = new MockHost();
    render(<App getTransport={() => host.transport} />);
    connect(host);
    host.deliver('confirm', question);
    const dialog = screen.getByRole('dialog', { name: question.title });
    expect(dialog).toHaveTextContent(question.body);
    expect(within(dialog).getAllByRole('button').map((button) => button.textContent)).toEqual(['Cancel', 'Start']);
    await userEvent.click(within(dialog).getByRole('button', { name: 'Start' }));
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'confirm_answer', requestId: 'calibrate-1', confirmed: true });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  });

  it('answers no on Escape, as Cancel does', async () => {
    const host = new MockHost();
    render(<App getTransport={() => host.transport} />);
    connect(host);
    host.deliver('confirm', question);
    await userEvent.keyboard('{Escape}');
    expect(host.lastOfType('shell_action')?.payload).toEqual({ action: 'confirm_answer', requestId: 'calibrate-1', confirmed: false });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  });
});

