import { describe, expect, it } from 'vitest';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from '../bridge/protocol';
import { AgentUiState, initialState, reducer } from './store';

function host(type: string, payload: unknown): Envelope {
  return { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: 'h-test', type, payload };
}

function apply(state: AgentUiState, type: string, payload: unknown): AgentUiState {
  return reducer(state, { kind: 'host', envelope: host(type, payload) });
}

const context = {
  sessionId: '1',
  revision: 4,
  projectName: 'Fixture',
  projectDirty: false,
  printer: { preset: 'Test Printer', filament: 'PLA' },
  plates: [],
  selection: { status: 'none' as const, objectIds: [] },
  history: { canUndo: false, canRedo: false },
};

const statePayload: StatePayload = {
  agent: { status: 'ready' },
  appearance: 'dark',
  conversation: [
    { id: 'm-1', role: 'user', state: 'complete', text: 'hi', attempt: 1, clientMessageId: 'c-1' },
    { id: 'm-2', role: 'assistant', state: 'complete', text: 'hello', attempt: 1, inReplyTo: 'm-1' },
  ],
  streamingMessageId: null,
  context,
};

describe('store reducer', () => {
  it('replaces everything from a full state payload (reload reconstruction)', () => {
    const state = apply(initialState, 'state', statePayload);
    expect(state.messages).toHaveLength(2);
    expect(state.messages[0].text).toBe('hi');
    expect(state.appearance).toBe('dark');
    expect(state.context?.projectName).toBe('Fixture');
    expect(state.streamingMessageId).toBeNull();
  });

  it('streams deltas in order and completes', () => {
    let state = apply(initialState, 'message_added', {
      message: { id: 'm-1', role: 'user', state: 'complete', text: 'hi', attempt: 1 },
    });
    state = apply(state, 'assistant_started', { messageId: 'm-2', inReplyTo: 'm-1', attempt: 1 });
    expect(state.streamingMessageId).toBe('m-2');
    state = apply(state, 'assistant_delta', { messageId: 'm-2', seq: 0, text: 'Hel' });
    state = apply(state, 'assistant_delta', { messageId: 'm-2', seq: 1, text: 'lo' });
    expect(state.messages.find((m) => m.id === 'm-2')?.text).toBe('Hello');
    state = apply(state, 'assistant_completed', { messageId: 'm-2' });
    expect(state.messages.find((m) => m.id === 'm-2')?.state).toBe('complete');
    expect(state.streamingMessageId).toBeNull();
  });

  it('ignores duplicate deltas and flags gaps for resync', () => {
    let state = apply(initialState, 'assistant_started', { messageId: 'm-2', attempt: 1 });
    state = apply(state, 'assistant_delta', { messageId: 'm-2', seq: 0, text: 'a' });
    state = apply(state, 'assistant_delta', { messageId: 'm-2', seq: 0, text: 'a' });
    expect(state.messages.find((m) => m.id === 'm-2')?.text).toBe('a');
    expect(state.needsResync).toBe(false);

    state = apply(state, 'assistant_delta', { messageId: 'm-2', seq: 5, text: 'z' });
    expect(state.needsResync).toBe(true);
    expect(state.messages.find((m) => m.id === 'm-2')?.text).toBe('a');
  });

  it('records failure with its error and supports retry restarting the stream', () => {
    let state = apply(initialState, 'assistant_started', { messageId: 'm-2', attempt: 1 });
    state = apply(state, 'assistant_delta', { messageId: 'm-2', seq: 0, text: 'partial' });
    state = apply(state, 'assistant_failed', {
      messageId: 'm-2',
      error: { code: 'mock_failure', message: 'failed', retryable: true },
    });
    const failed = state.messages.find((m) => m.id === 'm-2');
    expect(failed?.state).toBe('failed');
    expect(failed?.error?.retryable).toBe(true);
    expect(state.streamingMessageId).toBeNull();

    state = apply(state, 'assistant_started', { messageId: 'm-2', attempt: 2 });
    const retried = state.messages.find((m) => m.id === 'm-2');
    expect(retried?.state).toBe('streaming');
    expect(retried?.text).toBe('');
    expect(retried?.attempt).toBe(2);
  });

  it('marks a stopped stream', () => {
    let state = apply(initialState, 'assistant_started', { messageId: 'm-2', attempt: 1 });
    state = apply(state, 'assistant_stopped', { messageId: 'm-2' });
    expect(state.messages.find((m) => m.id === 'm-2')?.state).toBe('stopped');
    expect(state.streamingMessageId).toBeNull();
  });

  it('updates context, appearance, and agent status from host events', () => {
    let state = apply(initialState, 'context', { context });
    expect(state.context?.printer.preset).toBe('Test Printer');
    state = apply(state, 'appearance', { appearance: 'dark' });
    expect(state.appearance).toBe('dark');
    state = apply(state, 'agent_status', { status: 'unavailable' });
    expect(state.agentStatus).toBe('unavailable');
  });

  it('keeps bridge errors as diagnostics without corrupting the conversation', () => {
    const state = apply(initialState, 'bridge_error', { code: 'invalid_payload', message: 'bad' });
    expect(state.diagnostics.at(-1)).toContain('invalid_payload');
    expect(state.messages).toHaveLength(0);
  });
});
