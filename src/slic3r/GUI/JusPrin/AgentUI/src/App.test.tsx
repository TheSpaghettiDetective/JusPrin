// Deterministic conversation and interaction tests: a scripted mock host
// plays the native side of the bridge while the real page runs in jsdom.

import { beforeEach, describe, expect, it, vi } from 'vitest';
import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { fireEvent } from '@testing-library/react';
import { App } from './App';
import {
  Envelope,
  PROTOCOL_NAME,
  PROTOCOL_VERSION,
  StatePayload,
  ToolActivityInfo,
  WorkspaceContext,
} from './bridge/protocol';

class MockHost {
  received: Envelope[] = [];

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
  printer: { preset: 'Test Printer 0.4', filament: 'Generic PLA' },
  plates: [
    {
      id: '11',
      name: 'Plate 1',
      active: true,
      sliced: false,
      objects: [
        { id: '21', name: 'cube-a', instances: 1, selected: true },
        { id: '22', name: 'cube-b', instances: 1, selected: false },
      ],
    },
  ],
  selection: { status: 'objects', objectIds: ['21'] },
  history: { canUndo: false, canRedo: false },
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
    revisions: [],
    builds: [],
    exportedCopies: [],
    physicalPrints: [],
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
    requiresApproval: true,
    sessionId: '1',
    expectedRevision: 2,
    state: 'pending',
    progress: { current: 0, total: 3 },
    ...overrides,
  };
}

// A completed exchange whose assistant reply proposed the tool action.
function proposalConversation() {
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

describe('App', () => {
  let host: MockHost;

  beforeEach(() => {
    host = new MockHost();
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
    expect(screen.getByTestId('context-summary')).toHaveTextContent('Two Cubes');
    expect(screen.getByTestId('context-summary')).toHaveTextContent('Selected: cube-a');
  });

  it('renders build copy and retained physical-print facts with derived statuses', () => {
    const hash = 'a'.repeat(64);
    const statistics = { printTimeSeconds: 3720, filamentMm: 1842.5, materialGrams: 14.7, materialCost: 0.44, layerCount: 124 };
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        builds: [{
          id: 'b-1', seq: 10, createdAt: '2026-08-30T00:00:00Z', projectId: 'project-1', revisionId: 'r-2',
          conversationId: 'conv-1', afterMessageId: '', plateIndex: 0, plateName: 'Plate 1',
          printer: 'Test Printer 0.4', material: 'Generic PLA', manufacturingInputHash: hash, outputHash: hash,
          slicerVersion: 'JusPrin deterministic Phase 6', configurationProvenance: 'A very long configuration provenance value that must wrap at narrow widths',
          statistics, warnings: ['A deterministic warning'], stale: true,
        }],
        exportedCopies: [{
          id: 'e-1', seq: 11, createdAt: '2026-08-30T00:00:00Z', buildId: 'b-1', conversationId: 'conv-1',
          afterMessageId: '', destination: '/a/very/long/path/that/must/not/overflow/phase-six-demo.gcode',
          expectedOutputHash: hash, observedOutputHash: hash, verified: true, modified: false,
        }],
        physicalPrints: [{
          id: 'p-1', seq: 12, startedAt: '2026-08-30T00:00:00Z', endedAt: '2026-08-30T01:02:00Z',
          outcome: 'completed', failure: '', buildId: 'b-1', projectId: 'project-1', revisionId: 'r-2',
          conversationId: 'conv-1', afterMessageId: 'removed-message', plateIndex: 0, plateName: 'Plate 1',
          printer: 'Test Printer 0.4', material: 'Generic PLA', manufacturingInputHash: hash, outputHash: hash,
          gcodeHash: hash, statistics, timelineRemoved: true,
        }],
      }),
    );

    expect(screen.getByLabelText('Build b-1')).toHaveTextContent('Stale');
    expect(screen.getByLabelText('Exported copy e-1')).toHaveTextContent('Checksum verified');
    expect(screen.getByLabelText('Physical print p-1')).toHaveTextContent('Project timeline removed');
    expect(screen.getByLabelText('Physical print p-1')).toHaveTextContent('Test Printer 0.4');
    expect(screen.getAllByTitle(hash).length).toBeGreaterThanOrEqual(6);
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

  it('shows the clean unavailable state and keeps history when the Agent service is not configured', () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        agent: { status: 'unavailable' },
        conversation: [{ id: 'm-1', role: 'user', state: 'complete', text: 'older message', attempt: 1 }],
      }),
    );

    expect(screen.getByTestId('agent-unavailable')).toBeInTheDocument();
    expect(screen.getByText(/requires your consent to send your message/)).toBeInTheDocument();
    expect(screen.getByText(/only the attachments you include/)).toBeInTheDocument();
    expect(screen.getByText('older message')).toBeInTheDocument();
    expect(screen.getByLabelText('Message the Agent')).toBeDisabled();
  });

  it('offers the one setup action and nothing else when no Agent is configured and the chat is empty', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' } }));

    expect(screen.getByTestId('agent-not-configured')).toBeInTheDocument();
    expect(screen.getByText('No agent connected')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Set up the agent' })).toBeEnabled();
    expect(screen.getByText('NOT SET UP')).toBeInTheDocument();
    // The conversation chrome and the consent banner give way to the offer.
    expect(screen.queryByTestId('agent-unavailable')).not.toBeInTheDocument();
    expect(screen.queryByTestId('context-summary')).not.toBeInTheDocument();
    // The ask box stays in place, inert.
    const composer = screen.getByLabelText('Message the Agent');
    expect(composer).toBeDisabled();
    expect(composer).toHaveAttribute('placeholder', 'ask, or steer this chat\u2026');
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

  it('renders a pending tool card and submits the approval decision', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ conversation: proposalConversation() }));

    host.deliver('tool_activity', { activity: toolActivity() });
    expect(screen.getByText('Duplicate "cube-a"')).toBeInTheDocument();
    expect(screen.getByText('Waiting for your approval')).toBeInTheDocument();

    await userEvent.click(screen.getByText('Approve'));
    const decision = host.lastOfType('tool_decision');
    expect(decision).toBeTruthy();
    expect(decision!.payload).toEqual({ actionId: 't-1', decision: 'approve' });

    host.deliver('tool_activity', { activity: toolActivity({ state: 'running', progress: { current: 1, total: 3 } }) });
    expect(screen.getByLabelText('Duplicate "cube-a" progress')).toBeInTheDocument();

    await userEvent.click(screen.getByText('Cancel'));
    const cancel = host.lastOfType('tool_cancel');
    expect(cancel!.payload).toEqual({ actionId: 't-1' });

    host.deliver('tool_activity', { activity: toolActivity({ state: 'succeeded', progress: { current: 3, total: 3 } }) });
    expect(screen.getByText('Done')).toBeInTheDocument();
    expect(screen.queryByText('Approve')).not.toBeInTheDocument();
  });

  it('submits a rejection and shows that nothing was changed', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ conversation: proposalConversation(), toolActivities: [toolActivity()] }));

    await userEvent.click(screen.getByText('Reject'));
    const decision = host.lastOfType('tool_decision');
    expect(decision!.payload).toEqual({ actionId: 't-1', decision: 'reject' });

    host.deliver('tool_activity', { activity: toolActivity({ state: 'rejected' }) });
    expect(screen.getByText('Rejected — nothing was changed')).toBeInTheDocument();
  });

  it('explains a stale proposal distinctly from other failures', () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: proposalConversation(),
        toolActivities: [
          toolActivity({
            state: 'failed',
            error: { code: 'stale_revision', message: 'The project changed after this action was proposed.' },
          }),
        ],
      }),
    );

    expect(screen.getByText(/The project changed after this was proposed/)).toBeInTheDocument();
    expect(screen.queryByText('Approve')).not.toBeInTheDocument();
  });

  it('reconstructs tool cards from host state after a reload', () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: proposalConversation(),
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

    await userEvent.click(screen.getByRole('tab', { name: 'Second' }));
    const switched = host.lastOfType('switch_conversation');
    expect(switched!.payload).toEqual({ conversationId: 'conv-2' });

    await userEvent.click(screen.getByLabelText('New conversation'));
    expect(host.lastOfType('create_conversation')).toBeTruthy();
  });

  it('renders revision markers and requires explicit confirmation to revert', async () => {
    render(<App getTransport={() => host.transport} />);
    connect(
      host,
      emptyState({
        conversation: [{ id: 'm-1', role: 'user', state: 'complete', text: 'change it', attempt: 1 }],
        revisions: [
          { id: 'r-1', createdAt: 't', cause: 'initial', conversationId: 'conv-1', afterMessageId: '', current: false, revertible: true },
          { id: 'r-2', createdAt: 't', cause: 'contents', conversationId: 'conv-1', afterMessageId: 'm-1', current: true, revertible: true },
        ],
      }),
    );

    expect(screen.getByTestId('revision-r-1')).toHaveTextContent('Project start');
    expect(screen.getByTestId('revision-r-2')).toHaveTextContent('current');
    // The current revision offers no revert control.
    expect(screen.getAllByText('Revert here')).toHaveLength(1);

    await userEvent.click(screen.getByText('Revert here'));
    // Nothing is sent until the destructive action is explicitly confirmed.
    expect(host.lastOfType('revert_to_revision')).toBeUndefined();
    expect(screen.getByRole('alertdialog')).toHaveTextContent('cannot be undone');

    await userEvent.click(screen.getByText('Keep everything'));
    expect(host.lastOfType('revert_to_revision')).toBeUndefined();

    await userEvent.click(screen.getByText('Revert here'));
    await userEvent.click(screen.getByText('Revert permanently'));
    expect(host.lastOfType('revert_to_revision')!.payload).toEqual({ revisionId: 'r-1' });
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

  it('updates the context header when the native selection changes', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host);
    expect(screen.getByTestId('context-summary')).toHaveTextContent('Selected: cube-a');

    const changed: WorkspaceContext = {
      ...context,
      revision: 3,
      selection: { status: 'objects', objectIds: ['22'] },
      plates: [
        {
          ...context.plates[0],
          objects: [
            { id: '21', name: 'cube-a', instances: 1, selected: false },
            { id: '22', name: 'cube-b', instances: 1, selected: true },
          ],
        },
      ],
    };
    host.deliver('context', { context: changed });
    expect(screen.getByTestId('context-summary')).toHaveTextContent('Selected: cube-b');
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

describe('App agent setup', () => {
  let host: MockHost;

  beforeEach(() => {
    host = new MockHost();
  });

  function openApiKeyScreen() {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' } }));
    fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
    fireEvent.click(screen.getByTestId('setup-row-api-key'));
  }

  it('walks from the offer to the three ways of connecting an Agent', () => {
    render(<App getTransport={() => host.transport} />);
    connect(host, emptyState({ agent: { status: 'unavailable' } }));

    fireEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));

    expect(screen.getByTestId('setup-chooser')).toBeInTheDocument();
    expect(screen.queryByTestId('agent-not-configured')).not.toBeInTheDocument();
    // The two paths this build cannot complete are visible but inert rather
    // than leading nowhere.
    expect(screen.getByRole('button', { name: 'Continue with JusPrin' })).toBeDisabled();
    expect(screen.getByRole('button', { name: /Run a model on this machine/ })).toBeDisabled();
    // Nothing is asked of the host merely by looking at the options.
    expect(host.received.filter((e) => e.type.startsWith('setup_'))).toHaveLength(0);

    fireEvent.click(screen.getByRole('button', { name: 'Close setup' }));
    expect(screen.getByTestId('agent-not-configured')).toBeInTheDocument();
  });

  it('offers only the providers this build can verify, and defaults to one of them', () => {
    openApiKeyScreen();

    expect(screen.getByRole('tab', { name: 'OpenAI' })).toHaveAttribute('aria-selected', 'true');
    expect(screen.getByRole('tab', { name: 'OpenAI' })).toBeEnabled();
    expect(screen.getByRole('tab', { name: 'Anthropic' })).toBeDisabled();
    expect(screen.getByRole('tab', { name: 'Other…' })).toBeDisabled();
  });

  it('sends the key to the host and reports the round trip it measured', async () => {
    openApiKeyScreen();

    // An empty field cannot be submitted.
    expect(screen.getByRole('button', { name: 'Check key' })).toBeDisabled();

    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-test-key');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));

    const sent = host.lastOfType('setup_check_key');
    expect(sent).toBeTruthy();
    expect(sent!.payload).toEqual({ provider: 'openai', apiKey: 'sk-test-key' });

    host.deliver('setup_status', { phase: 'checking', provider: 'openai' });
    expect(screen.getByText('Checking…')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Check key' })).toBeDisabled();

    host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 812 });
    expect(screen.getByTestId('setup-verified')).toHaveTextContent('replied in 0.8 s');
  });

  it('turns into a working chat once the host reports the Agent ready', () => {
    openApiKeyScreen();
    host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 500 });
    host.deliver('agent_status', { status: 'ready' });

    // The setup surface gives way to the conversation, with a one-time note
    // of how the Agent got connected.
    expect(screen.queryByTestId('setup-api-key')).not.toBeInTheDocument();
    expect(screen.getByTestId('setup-connected')).toHaveTextContent('your own OpenAI key');
    expect(screen.getByLabelText('Message the Agent')).toBeEnabled();

    fireEvent.click(screen.getByRole('button', { name: 'Dismiss' }));
    expect(screen.queryByTestId('setup-connected')).not.toBeInTheDocument();
  });

  it('says when a working key could not be stored', () => {
    openApiKeyScreen();
    host.deliver('setup_status', {
      phase: 'verified',
      provider: 'openai',
      elapsedMs: 500,
      warning: 'This key could not be saved to the system credential store.',
    });
    host.deliver('agent_status', { status: 'ready' });

    expect(screen.getByTestId('setup-connected')).toHaveTextContent('could not be saved');
  });

  it('keeps the user on the key screen when the provider rejects the key', async () => {
    openApiKeyScreen();
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-wrong');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));

    host.deliver('setup_status', {
      phase: 'error',
      provider: 'openai',
      error: { code: 'invalid_credentials', message: 'The OpenAI API key was rejected.', retryable: false },
    });

    expect(screen.getByTestId('setup-error')).toHaveTextContent('The OpenAI API key was rejected.');
    expect(screen.getByTestId('setup-api-key')).toBeInTheDocument();
    // The key is still there to correct, and can be resubmitted.
    expect(screen.getByRole('button', { name: 'Check key' })).toBeEnabled();
  });

  it('abandons a check when the user cancels or backs out', async () => {
    openApiKeyScreen();
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'sk-slow');
    fireEvent.click(screen.getByRole('button', { name: 'Check key' }));
    host.deliver('setup_status', { phase: 'checking', provider: 'openai' });

    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('setup_cancel')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Back to setup options' }));
    expect(screen.getByTestId('setup-chooser')).toBeInTheDocument();
  });
});
