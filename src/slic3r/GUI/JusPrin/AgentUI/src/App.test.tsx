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
    expect(screen.getByText('older message')).toBeInTheDocument();
    expect(screen.getByLabelText('Message the Agent')).toBeDisabled();
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
