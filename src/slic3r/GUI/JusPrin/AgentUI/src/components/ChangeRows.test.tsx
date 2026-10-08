// The change log's rows in the thread: how raw entries merge into rows, what
// the rows say, where they sit among messages and history cards, and when a
// reply says it changed nothing.

import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { BuildInfo, ChangeInfo, ToolActivityInfo } from '../bridge/protocol';
import { Message } from '../state/store';
import { ChangeRows, groupChanges } from './ChangeRows';
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
      onToolCancel={noop}
      onSend={noop}
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

  it('merges identical edits across saved versions', () => {
    const runs = groupChanges([change(1), change(2), change(3)]);
    expect(runs.map((run) => [run.count, run.last.seq])).toEqual([[3, 3]]);
  });
});

it('skips a checkpoint inside a merged run and confirms the endpoint checkpoint', () => {
  const onRevert = vi.fn();
  render(<ChangeRows changes={[change(1), change(2), change(3), change(4, { label: 'Move' })]}
    restorePoints={[{ changeSeq: 2, versionId: 'saved-2' }, { changeSeq: 3, versionId: 'saved-3' }]}
    onRevert={onRevert} />);
  expect(screen.getAllByRole('listitem')).toHaveLength(2);
  expect(screen.getAllByRole('listitem')[0]).toHaveTextContent('3 steps merged');
  const button = screen.getByRole('button', { name: 'Revert to here' });
  expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  fireEvent.click(button);
  expect(screen.getByRole('dialog', { name: 'Revert to here?' })).toBeInTheDocument();
  expect(onRevert).not.toHaveBeenCalled();
  fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
  expect(onRevert).not.toHaveBeenCalled();
  fireEvent.click(button);
  fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Revert to here' }));
  expect(onRevert).toHaveBeenCalledTimes(1);
  expect(onRevert).toHaveBeenCalledWith('saved-3');
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
      change(6, { actor: 'agent', label: 'Duplicate' }),
    ]);
    const text = rows();
    expect(text[0]).toContain('Object settings changed');
    expect(text[1]).toContain('Undo: Rotate');
    expect(text[2]).toContain('Redo: Arrange plate');
    expect(text[3]).toContain('Sparse infill density 35% → 45%');
    expect(text[3]).toContain('you, in Strong');
    expect(text[4]).toContain('Switched to Strong');
    expect(text[5]).toMatch(/^DuplicateAgent/);
    expect(screen.getByTestId('change-6').querySelector('.jp-icon-pencil')).toBeInTheDocument();
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

  it('shows the first four agent settings and reveals the rest on request', () => {
    const settings = ['Brim', 'First-layer speed', 'Plate temp', 'Layer height', 'First-layer line width'];
    thread(settings.map((label, index) => change(index + 1, {
      kind: 'setting', actor: 'agent', label, from: 'old', to: 'new', preset: '0.20 mm Standard',
    })));
    expect(screen.getByRole('region', { name: 'Agent setting changes' })).toHaveTextContent('4 of 5 settings');
    expect(screen.getAllByRole('listitem')).toHaveLength(4);
    expect(screen.queryByText('First-layer line width')).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'See the other 1 →' }));
    expect(screen.getAllByRole('listitem')).toHaveLength(5);
    expect(screen.getByText('First-layer line width')).toBeInTheDocument();
  });

  it('keeps later hand edits after a grouped agent settings card', () => {
    const settings = ['Brim', 'First-layer speed', 'Plate temp', 'Layer height', 'Brim gap'];
    const changes = settings.map((label, index) => change(index + 1, {
      kind: 'setting', actor: 'agent', label, from: 'old', to: 'new',
    }));
    changes.push(change(6, { label: 'Rotated bracket to 45°' }));
    render(<ChangeRows changes={changes} />);
    expect(screen.getByRole('region', { name: 'Agent setting changes' })).toHaveTextContent('4 of 5 settings');
    expect(screen.getByText('Rotated bracket to 45°')).toBeInTheDocument();
    expect(screen.queryByText('Brim gap')).not.toBeInTheDocument();
  });

  it('summarizes an agent setup turn and keeps its other settings behind the pill', () => {
    thread([
      change(1, { actor: 'agent', label: 'Laid flat' }),
      change(2, { actor: 'agent', kind: 'setting', label: 'Wall loops', from: '2', to: '5' }),
      change(3, { actor: 'agent', kind: 'setting', label: 'Sparse infill density', from: '15%', to: '35%' }),
      change(4, { actor: 'agent', kind: 'setting', label: 'Sparse infill pattern', from: 'grid', to: 'gyroid' }),
      change(5, { actor: 'agent', kind: 'setting', label: 'Top shell layers', from: '3', to: '5' }),
      change(6, { actor: 'agent', kind: 'setting', label: 'Outer wall speed', from: '200 mm/s', to: '120 mm/s' }),
    ]);
    const pill = screen.getByRole('button', { name: 'Laid flat 5 settings' });
    expect(pill).toHaveAttribute('aria-expanded', 'false');
    expect(pill.querySelector('.settings-changes-icon')).toBeInTheDocument();
    fireEvent.click(pill);
    expect(screen.getByRole('list', { name: '' })).toHaveTextContent('Outer wall speed');
  });

  it('counts whichever settings the agent changed, with none it favours', () => {
    thread([
      change(1, { actor: 'agent', label: 'Add modifier: Corner tab' }),
      change(2, { actor: 'agent', kind: 'setting', label: 'Brim type', from: 'Auto', to: 'Outer brim only' }),
      change(3, { actor: 'agent', kind: 'setting', label: 'Brim width', from: '0 mm', to: '8 mm' }),
      change(4, { actor: 'agent', kind: 'setting', label: 'First layer', from: '50 mm/s', to: '25 mm/s' }),
    ]);
    const pill = screen.getByRole('button', { name: 'Add modifier: Corner tab 3 settings' });
    fireEvent.click(pill);
    for (const row of ['Brim typeAuto→Outer brim only', 'Brim width0 mm→8 mm', 'First layer50 mm/s→25 mm/s'])
      expect(screen.getByRole('list', { name: '' })).toHaveTextContent(row);
  });

  it('shows a known hand-edit location', () => {
    thread([change(1, { location: 'on the plate' })]);
    expect(rows()[0]).toContain('you, on the plate');
  });

  it('interleaves runs with history cards in seq order after the item they follow', () => {
    const build = {
      id: 'b-1', seq: 5, createdAt: now, projectId: 'p', conversationId: 'conv-1', afterMessageId: 'm-2',
      plateIndex: 0, plateName: 'Plate 1', printer: 'P', material: 'PLA', manufacturingInputHash: 'a', outputHash: 'b',
      slicerVersion: 'v', configurationProvenance: 'c', warnings: [], stale: false,
      statistics: { printTimeSeconds: 60, filamentMm: 1, materialGrams: 1, materialCost: 0, layerCount: 1 },
    } satisfies BuildInfo;
    const { container } = thread([change(4), change(6, { label: 'Move' })], { builds: [build] });
    // Runs and history entries are thread items of their own, after the
    // message group they follow.
    const order = [...container.querySelector('.message-list')!.children].map((child) => child.className.split(' ')[0]);
    expect(order).toEqual(['message-group', 'message-group', 'change-rows', 'history-card', 'change-rows']);
  });

  it('places a change that follows a tool activity after that activity\'s message', () => {
    const activity = {
      actionId: 't-1', correlationId: 'm-1', server: 'jusprin-native', tool: 'duplicate_object', title: 'Duplicate',
      arguments: {}, actionClass: 'mutation', sessionId: '1', expectedRevision: 1,
      state: 'succeeded', progress: { current: 1, total: 1 },
    } satisfies ToolActivityInfo;
    const { container } = thread([change(3, { afterId: 't-1' })], { activities: [activity] });
    const order = [...container.querySelector('.message-list')!.children].map((child) => child.className.split(' ')[0]);
    expect(order).toEqual(['message-group', 'change-rows', 'message-group']);
    expect(within(container.querySelector('.change-rows') as HTMLElement).queryAllByRole('listitem')).toHaveLength(1);
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

  it('waits while the reply or its tool is still running', () => {
    const running = {
      actionId: 't-1', correlationId: 'm-2', server: 'jusprin-native', tool: 'duplicate_object', title: 'Duplicate',
      arguments: {}, actionClass: 'mutation', sessionId: '1', expectedRevision: 1,
      state: 'running', progress: { current: 0, total: 1 },
    } satisfies ToolActivityInfo;
    const { unmount } = thread([], { activities: [running] });
    expect(screen.queryByText('Answered · nothing changed')).not.toBeInTheDocument();
    unmount();
    thread([], { messages: [turns[0], { ...turns[1], state: 'streaming' }] });
    expect(screen.queryByText('Answered · nothing changed')).not.toBeInTheDocument();
  });
});
