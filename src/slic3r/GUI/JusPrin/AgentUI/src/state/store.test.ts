import { describe, expect, it } from 'vitest';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload, ToolActivityInfo } from '../bridge/protocol';
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
    expectedRevision: 4,
    state: 'pending',
    progress: { current: 0, total: 3 },
    ...overrides,
  };
}

const statePayload: StatePayload = {
  agent: { status: 'ready' },
  appearance: 'dark',
  conversations: [
    { id: 'c-1', title: 'Conversation 1', createdAt: 't' },
    { id: 'c-2', title: 'Second', createdAt: 't' },
  ],
  activeConversationId: 'c-2',
  conversation: [
    { id: 'm-1', role: 'user', state: 'complete', text: 'hi', attempt: 1, clientMessageId: 'c-1' },
    { id: 'm-2', role: 'assistant', state: 'complete', text: 'hello', attempt: 1, inReplyTo: 'm-1' },
  ],
  streamingMessageId: null,
  toolActivities: [toolActivity({ state: 'succeeded', progress: { current: 3, total: 3 } })],
  revisions: [
    { id: 'r-1', createdAt: 't', cause: 'initial', conversationId: 'c-1', afterMessageId: '', current: false, revertible: true },
    { id: 'r-2', createdAt: 't', cause: 'contents', conversationId: 'c-2', afterMessageId: 'm-2', current: true, revertible: true },
  ],
  draft: 'unfinished thought',
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
    expect(state.toolActivities).toHaveLength(1);
    expect(state.toolActivities[0].state).toBe('succeeded');
    expect(state.conversations).toHaveLength(2);
    expect(state.activeConversationId).toBe('c-2');
    expect(state.revisions).toHaveLength(2);
    expect(state.draft).toBe('unfinished thought');
  });

  it('appends revision_added events and moves the current flag', () => {
    let state = apply(initialState, 'state', statePayload);
    state = apply(state, 'revision_added', {
      revision: { id: 'r-3', createdAt: 't', cause: 'transform', conversationId: 'c-2', afterMessageId: 'm-2', current: true, revertible: true },
    });
    expect(state.revisions).toHaveLength(3);
    expect(state.revisions.filter((r) => r.current).map((r) => r.id)).toEqual(['r-3']);
  });

  it('upserts tool activities by action id as their lifecycle advances', () => {
    let state = apply(initialState, 'tool_activity', { activity: toolActivity() });
    expect(state.toolActivities).toHaveLength(1);
    expect(state.toolActivities[0].state).toBe('pending');

    state = apply(state, 'tool_activity', { activity: toolActivity({ state: 'running', progress: { current: 1, total: 3 } }) });
    expect(state.toolActivities).toHaveLength(1);
    expect(state.toolActivities[0].state).toBe('running');
    expect(state.toolActivities[0].progress.current).toBe(1);

    state = apply(state, 'tool_activity', { activity: toolActivity({ actionId: 't-2', correlationId: 'm-4' }) });
    expect(state.toolActivities).toHaveLength(2);
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

describe('attachments reducer', () => {
  const staged = {
    id: 'a-1',
    name: 'notes.txt',
    kind: 'text' as const,
    mime: 'text/plain',
    sizeBytes: 5,
    source: 'picker' as const,
    state: 'staged' as const,
    previewText: 'hello',
  };

  it('adds then updates an attachment in place on attachment_updated', () => {
    const added = apply(initialState, 'attachment_updated', { attachment: staged });
    expect(added.attachments).toHaveLength(1);
    expect(added.attachments[0].previewText).toBe('hello');

    const sent = apply(added, 'attachment_updated', { attachment: { ...staged, state: 'sent' } });
    expect(sent.attachments).toHaveLength(1);
    expect(sent.attachments[0].state).toBe('sent');
  });

  it('replaces attachments from a full state payload', () => {
    const withOne = apply(initialState, 'attachment_updated', { attachment: staged });
    const replaced = apply(withOne, 'state', {
      ...statePayload,
      attachments: [{ ...staged, id: 'a-2', name: 'other.txt' }],
    });
    expect(replaced.attachments.map((a) => a.id)).toEqual(['a-2']);
  });
});
