// Work the application ran outside the chat: what its card says in each state,
// where it sits among the messages, and how repeats read.

import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { WorkInfo } from '../bridge/protocol';
import { initialState, Message, reducer } from '../state/store';
import { PROTOCOL_NAME, PROTOCOL_VERSION } from '../bridge/protocol';
import { MessageList } from './MessageList';
import { collapseWork, WorkCard, workStatus } from './WorkCard';

function work(seq: number, overrides: Partial<WorkInfo> = {}): WorkInfo {
  return {
    seq, id: `work-${seq}`, startedAt: '2026-10-13T10:00:00Z', endedAt: '', kind: 'slice', title: 'Slicing plate 1',
    state: 'running', detail: '', conversationId: 'conv-1', afterId: 'm-2', cancellable: true,
    progress: { current: 0, total: 0 }, ...overrides,
  };
}

function done(seq: number, overrides: Partial<WorkInfo> = {}): WorkInfo {
  return work(seq, { state: 'done', endedAt: '2026-10-13T10:00:09Z', cancellable: false, progress: undefined, ...overrides });
}

function message(id: string, role: Message['role'], text: string): Message {
  return { id, role, state: 'complete', text, attempt: 1, lastSeq: -1 };
}

const noop = () => {};

function thread(entries: WorkInfo[], messages: Message[], onWorkCancel?: (id: string) => void) {
  return render(
    <MessageList messages={messages} attachments={[]} streamingMessageId={null} toolActivities={[]} gcodeMarkers={[]} changes={[]} work={entries} onWorkCancel={onWorkCancel}
      onRetry={noop} onToolCancel={noop} onSend={noop} />,
  );
}

describe('WorkCard', () => {
  it('shows how far running work is and stops it with Cancel', () => {
    const onCancel = vi.fn();
    render(<WorkCard work={work(7, { detail: 'Generating supports', progress: { current: 40, total: 100 } })} onCancel={onCancel} />);
    expect(screen.getByText('Slicing plate 1')).toBeTruthy();
    expect(screen.getByText('Generating supports · 40%')).toBeTruthy();
    expect(screen.getByRole('progressbar').getAttribute('aria-valuenow')).toBe('40');
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(onCancel).toHaveBeenCalledWith('work-7');
  });

  it('has no Cancel for work that cannot be stopped, and no number when the amount is unknown', () => {
    render(<WorkCard work={work(8, { kind: 'export', title: 'Exporting.', cancellable: false })} onCancel={noop} />);
    expect(screen.queryByRole('button')).toBeNull();
    expect(screen.getByRole('progressbar').getAttribute('aria-valuenow')).toBeNull();
    expect(screen.getByText('Running…')).toBeTruthy();
  });

  it('says how the work ended, with no bar and no button', () => {
    const { rerender } = render(<WorkCard work={done(9)} onCancel={noop} />);
    expect(screen.getByText('Done')).toBeTruthy();
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.queryByRole('button')).toBeNull();
    rerender(<WorkCard work={done(9, { state: 'cancelled' })} onCancel={noop} />);
    expect(screen.getByText('Cancelled')).toBeTruthy();
    rerender(<WorkCard work={done(9, { state: 'failed', detail: 'No layers were detected.' })} onCancel={noop} />);
    expect(screen.getByText('No layers were detected.')).toBeTruthy();
    expect(workStatus(done(9, { state: 'interrupted' }))).toBe('Interrupted when the project was closed');
  });
});

describe('work in the thread', () => {
  const turns = [message('m-1', 'user', 'Make it strong'), message('m-2', 'assistant', 'Done, laid flat.'),
                 message('m-3', 'user', 'And lighter')];

  it('keeps the place it started at: a later message reads below it', () => {
    thread([work(5)], turns);
    const text = document.body.textContent ?? '';
    expect(text.indexOf('Done, laid flat.')).toBeLessThan(text.indexOf('Slicing plate 1'));
    expect(text.indexOf('Slicing plate 1')).toBeLessThan(text.indexOf('And lighter'));
  });

  it('is shown before any message when nothing had been said yet', () => {
    thread([work(1, { afterId: '', kind: 'job', title: 'Arranging...' })], []);
    expect(screen.getByText('Arranging...')).toBeTruthy();
  });

  it('sends the press on Cancel, and offers none in a chat that is only being read', () => {
    const onWorkCancel = vi.fn();
    const { unmount } = thread([work(5)], turns, onWorkCancel);
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(onWorkCancel).toHaveBeenCalledWith('work-5');
    unmount();
    thread([work(5)], turns);
    expect((screen.getByRole('button', { name: 'Cancel' }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('reads repeated runs as one card with a count, and never folds a run that is still going', () => {
    expect(collapseWork([done(1), done(2), done(3)]).map((card) => [card.work.id, card.times])).toEqual([['work-3', 3]]);
    expect(collapseWork([done(1), done(2, { state: 'failed', detail: 'x' }), done(3)]).map((card) => card.times)).toEqual([1, 1, 1]);
    expect(collapseWork([done(1), work(2), done(3)]).map((card) => card.times)).toEqual([1, 1, 1]);
    thread([done(1), done(2), done(3)], turns);
    expect(screen.getAllByText('Slicing plate 1')).toHaveLength(1);
    expect(screen.getByText('· 3 times', { exact: false })).toBeTruthy();
  });
});

describe('work in the store', () => {
  const host = (type: string, payload: unknown) =>
    ({ protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: `h-${type}`, type, payload });

  it('adds an entry when work starts and replaces it in place as it moves on', () => {
    let state = reducer(initialState, { kind: 'host', envelope: host('work_updated', { work: work(4) }) });
    state = reducer(state, { kind: 'host', envelope: host('work_updated', { work: work(6, { kind: 'job', title: 'Arranging...' }) }) });
    state = reducer(state, { kind: 'host', envelope: host('work_updated', { work: done(4) }) });
    expect(state.work.map((entry) => [entry.id, entry.state])).toEqual([['work-4', 'done'], ['work-6', 'running']]);
  });
});
