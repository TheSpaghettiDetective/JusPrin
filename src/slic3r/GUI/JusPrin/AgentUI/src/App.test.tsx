// Deterministic conversation and interaction tests: a scripted mock host
// plays the native side of the bridge while the real page runs in jsdom.

import { beforeEach, describe, expect, it } from 'vitest';
import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { fireEvent } from '@testing-library/react';
import { App } from './App';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload, WorkspaceContext } from './bridge/protocol';

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
    conversation: [],
    streamingMessageId: null,
    context,
    ...overrides,
  };
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
