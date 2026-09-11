// The change log's rows in the thread: how raw entries merge into rows, what
// the rows say, where they sit among messages and history cards, and when a
// reply says it changed nothing.

import { render, screen, within } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { BuildInfo, ChangeInfo, ToolActivityInfo } from '../bridge/protocol';
import { Message } from '../state/store';
import { groupChanges } from './ChangeRows';
import { MessageList } from './MessageList';

const now = new Date().toISOString();

function change(seq: number, overrides: Partial<ChangeInfo> = {}): ChangeInfo {
  return { seq, createdAt: now, kind: 'step', actor: 'person', label: 'Rotate', conversationId: 'conv-1', afterId: 'm-2', ...overrides };
}

function message(id: string, role: Message['role'], text: string): Message {
  return { id, role, state: 'complete', text, attempt: 1, lastSeq: -1 };
}

const turns = [message('m-1', 'user', 'Make it strong'), message('m-2', 'assistant', 'Done, laid flat.')];

const noop = () => {};

function thread(changes: ChangeInfo[], extra: { builds?: BuildInfo[]; activities?: ToolActivityInfo[]; messages?: Message[] } = {}) {
  return render(
    <MessageList
      messages={extra.messages ?? turns}
      attachments={[]}
      streamingMessageId={null}
      toolActivities={extra.activities ?? []}
      builds={extra.builds ?? []}
      exportedCopies={[]}
      physicalPrints={[]}
      changes={changes}
      onRetry={noop}
      onToolDecision={noop}
      onToolCancel={noop}
    />,
  );
}

function rows() {
  return screen.queryAllByRole('listitem').map((row) => row.textContent);
}

describe('groupChanges', () => {
  it('merges a run of the same edit into one row and keeps different edits apart', () => {
    const rotates = Array.from({ length: 11 }, (_, index) => change(index + 1));
    expect(groupChanges(rotates)).toHaveLength(1);
    expect(groupChanges(rotates)[0].count).toBe(11);
    expect(groupChanges([change(1), change(2, { label: 'Move' }), change(3)]).map((run) => run.count)).toEqual([1, 1, 1]);
    expect(groupChanges([change(1), change(2, { actor: 'agent' })])).toHaveLength(2);
    expect(groupChanges([change(1), change(2, { kind: 'undo' })])).toHaveLength(2);
  });

  it('orders by seq whatever order the entries arrive in', () => {
    const runs = groupChanges([change(3, { label: 'Move' }), change(1), change(2)]);
    expect(runs.map((run) => [run.first.seq, run.count])).toEqual([[1, 2], [3, 1]]);
  });
});

describe('change rows in the thread', () => {
  it('draws eleven steps as one row with the merged count', () => {
    thread(Array.from({ length: 11 }, (_, index) => change(index + 1)));
    expect(rows()).toHaveLength(1);
    expect(rows()[0]).toContain('Rotate');
    expect(rows()[0]).toContain('you · 11 steps merged');
  });

  it('words each kind, with a fallback for the steps OrcaSlicer leaves unnamed', () => {
    thread([
      change(1, { label: '' }),
      change(2, { kind: 'undo' }),
      change(3, { kind: 'redo', label: 'select_arrange partplate' }),
      change(4, { kind: 'setting', label: 'Sparse infill density', from: '35%', to: '45%', preset: 'Strong' }),
      change(5, { kind: 'preset', label: 'Strong' }),
      change(6, { kind: 'mark', label: '' }),
      change(7, { actor: 'agent', label: 'Duplicate' }),
    ]);
    const text = rows();
    expect(text[0]).toContain('Object settings changed');
    expect(text[1]).toContain('Undo: Rotate');
    expect(text[2]).toContain('Redo: Arrange plate');
    expect(text[3]).toContain('Sparse infill density 35% → 45%');
    expect(text[3]).toContain('you, in Strong');
    expect(text[4]).toContain('Switched to Strong');
    expect(text[5]).toContain('Project changed');
    expect(text[6]).toMatch(/^✎DuplicateAgent/);
  });

  it('shows a run of edits to one setting as its net change', () => {
    thread([
      change(1, { kind: 'setting', label: 'Wall loops', from: '2', to: '3', preset: 'Strong' }),
      change(2, { kind: 'setting', label: 'Wall loops', from: '3', to: '5', preset: 'Strong' }),
    ]);
    expect(rows()).toHaveLength(1);
    expect(rows()[0]).toContain('Wall loops 2 → 5');
    expect(rows()[0]).toContain('2 steps merged');
  });

  it('interleaves runs with history cards in seq order after the item they follow', () => {
    const build = {
      id: 'b-1', seq: 5, createdAt: now, projectId: 'p', conversationId: 'conv-1', afterMessageId: 'm-2',
      plateIndex: 0, plateName: 'Plate 1', printer: 'P', material: 'PLA', manufacturingInputHash: 'a', outputHash: 'b',
      slicerVersion: 'v', configurationProvenance: 'c', warnings: [], stale: false,
      statistics: { printTimeSeconds: 60, filamentMm: 1, materialGrams: 1, materialCost: 0, layerCount: 1 },
    } satisfies BuildInfo;
    const { container } = thread([change(4), change(6, { label: 'Move' })], { builds: [build] });
    const group = container.querySelectorAll('.message-group')[1];
    const order = [...group.children].map((child) => child.className.split(' ')[0]);
    expect(order).toEqual(['answered-turn', 'change-rows', 'history-card', 'change-rows']);
  });

  it('places a change that follows a tool activity after that activity\'s message', () => {
    const activity = {
      actionId: 't-1', correlationId: 'm-1', server: 'jusprin-native', tool: 'duplicate_object', title: 'Duplicate',
      arguments: {}, actionClass: 'mutation', requiresApproval: true, sessionId: '1', expectedRevision: 1,
      state: 'succeeded', progress: { current: 1, total: 1 },
    } satisfies ToolActivityInfo;
    const { container } = thread([change(3, { afterId: 't-1' })], { activities: [activity] });
    const [first, second] = container.querySelectorAll('.message-group');
    expect(within(first as HTMLElement).queryAllByRole('listitem')).toHaveLength(1);
    expect(within(second as HTMLElement).queryAllByRole('listitem')).toHaveLength(0);
  });

  it('puts changes made before the first message at the top', () => {
    const { container } = thread([change(1, { afterId: '' })]);
    expect(container.querySelector('.message-list')!.firstElementChild!.className).toBe('change-rows');
  });
});

describe('Answered · nothing changed', () => {
  it('marks a finished reply the Agent changed nothing after', () => {
    thread([]);
    expect(screen.getByText('Answered · nothing changed')).toBeInTheDocument();
  });

  it('is absent when the Agent changed something, and not for the person\'s own edits', () => {
    const { unmount } = thread([change(3, { actor: 'agent' })]);
    expect(screen.queryByText('Answered · nothing changed')).not.toBeInTheDocument();
    unmount();
    thread([change(3, { actor: 'person' })]);
    expect(screen.getByText('Answered · nothing changed')).toBeInTheDocument();
  });

  it('waits while the reply streams or its proposal is still pending', () => {
    const pending = {
      actionId: 't-1', correlationId: 'm-2', server: 'jusprin-native', tool: 'duplicate_object', title: 'Duplicate',
      arguments: {}, actionClass: 'mutation', requiresApproval: true, sessionId: '1', expectedRevision: 1,
      state: 'pending', progress: { current: 0, total: 1 },
    } satisfies ToolActivityInfo;
    const { unmount } = thread([], { activities: [pending] });
    expect(screen.queryByText('Answered · nothing changed')).not.toBeInTheDocument();
    unmount();
    thread([], { messages: [turns[0], { ...turns[1], state: 'streaming' }] });
    expect(screen.queryByText('Answered · nothing changed')).not.toBeInTheDocument();
  });
});
