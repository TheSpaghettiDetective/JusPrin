// A turn's tool work in the project chat: one quiet line in place of a card
// per call, timed from the person's message, opening into one line per
// action; the turn's change rows as one block after its words.

import { act, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ChangeInfo, ToolActivityInfo } from '../bridge/protocol';
import { Message } from '../state/store';
import { MessageList, turnsOf } from './MessageList';
import { actionDetailsText, actionLineText, elapsedWords, turnLineText } from './TurnActivity';

const noop = () => {};

function message(id: string, role: Message['role'], text: string, extra: Partial<Message> = {}): Message {
  return { id, role, state: 'complete', text, attempt: 1, lastSeq: -1, ...extra };
}

function activity(id: string, correlationId: string, overrides: Partial<ToolActivityInfo> = {}): ToolActivityInfo {
  return {
    actionId: id, correlationId, server: 'jusprin-native', tool: 'presets_list', title: `List presets ${id}`,
    arguments: {}, actionClass: 'read_only', sessionId: '2', expectedRevision: 1, state: 'succeeded',
    progress: { current: 1, total: 1 }, ...overrides,
  };
}

// The 2026-10-20 "yes" turn: the person's one word, then sixteen replies that
// only called tools, then the reply with words, all answering m-3.
const asked = message('m-3', 'user', 'yes', { createdAt: '2026-10-20T22:48:06Z' });
const toolOnly = Array.from({ length: 16 }, (_, index) =>
  message(`m-${index + 4}`, 'assistant', '', { inReplyTo: 'm-3', createdAt: `2026-10-20T22:48:${String(10 + index).padStart(2, '0')}Z` }));
const spoke = message('m-21', 'assistant', 'I switched the printer to the **Bambu Lab A1**.', { inReplyTo: 'm-3', createdAt: '2026-10-20T22:48:50Z' });
const yesTurn = [asked, ...toolOnly, spoke];
const yesActivities = toolOnly.map((reply, index) =>
  activity(`t-${index + 1}`, reply.id, index % 4 === 3 ? { tool: 'object_place', title: `Place object ${index}`, actionClass: 'mutation' } : {}));

function thread(messages: Message[], activities: ToolActivityInfo[], extra: {
  streamingMessageId?: string | null; changes?: ChangeInfo[]; printerBlocks?: [];
} = {}) {
  return render(
    <MessageList messages={messages} attachments={[]} streamingMessageId={extra.streamingMessageId ?? null}
      toolActivities={activities} gcodeMarkers={[]} changes={extra.changes ?? []}
      printerBlocks={extra.printerBlocks} onRetry={noop} onToolCancel={noop} onSend={noop} />,
  );
}

describe('turnsOf', () => {
  it('joins every reply to the message it answers, and nothing else', () => {
    const other = message('m-22', 'user', 'thanks');
    const turns = turnsOf([...yesTurn, other, message('m-23', 'assistant', 'Welcome', { inReplyTo: 'm-22' })], false);
    expect(turns.map((turn) => [turn.anchor.id, turn.replies.length])).toEqual([['m-3', 17], ['m-22', 1]]);
    // A reply answering a message that is not the current turn's starts its own item.
    expect(turnsOf([asked, message('m-9', 'assistant', 'late', { inReplyTo: 'm-1' })], false)).toHaveLength(2);
    // The printer panel keeps one item per message.
    expect(turnsOf(yesTurn, true)).toHaveLength(18);
  });
});

describe('the turn line', () => {
  it('words the line from the clock and the count', () => {
    expect(elapsedWords(44_000)).toBe('44s');
    expect(elapsedWords(69_400)).toBe('1m 9s');
    expect(turnLineText(yesActivities, false, '44s')).toBe('Worked for 44s · 16 actions');
    expect(turnLineText(yesActivities.slice(0, 1), false)).toBe('1 action');
    expect(turnLineText([activity('t-1', 'm-4', { state: 'failed' })], false, '3s')).toBe('Worked for 3s · 1 action · 1 failed');
    expect(turnLineText([activity('t-1', 'm-4', { state: 'running' })], true, '6s')).toBe('List presets t-1… · 6s');
    expect(turnLineText([], true)).toBe('Working on it…');
    expect(actionLineText(activity('t-1', 'm-4', { state: 'failed', error: { code: 'x', message: 'No such preset.' } })))
      .toBe('List presets t-1 · Failed');
    expect(actionLineText(activity('t-1', 'm-4', { state: 'failed', error: { code: 'stale_workspace', message: 'Changed.' } })))
      .toBe('List presets t-1 · Not run');
    expect(actionLineText(activity('t-1', 'm-4', { state: 'cancelled' }))).toBe('List presets t-1 · Cancelled');
  });

  it('draws sixteen calls as one line, no cards and no empty bubbles, then the words', () => {
    thread(yesTurn, yesActivities);
    expect(screen.queryAllByTestId(/^tool-t-/)).toHaveLength(0);
    expect(document.querySelectorAll('.message.assistant')).toHaveLength(1);
    const line = screen.getByRole('button', { name: 'Worked for 44s · 16 actions' });
    expect(line).toHaveAttribute('aria-expanded', 'false');
    // The line sits between the person's words and the Agent's.
    const group = line.closest('.message-group')!;
    const order = Array.from(group.children).map((child) => child.className.split(' ')[0]);
    expect(order).toEqual(['message', 'turn-activity', 'message']);
    expect(screen.getByText('I switched the printer to the', { exact: false })).toBeInTheDocument();
  });

  it('opens into one line per action, in order, with edits and failures told apart', () => {
    const activities = [...yesActivities, activity('t-17', 'm-19', { state: 'failed', error: { code: 'x', message: 'It did not fit.' } })];
    thread(yesTurn, activities);
    fireEvent.click(screen.getByRole('button', { name: 'Worked for 44s · 17 actions · 1 failed' }));
    const log = screen.getByRole('list', { name: 'Actions' });
    const lines = within(log).getAllByRole('listitem');
    expect(lines).toHaveLength(17);
    expect(lines[0]).toHaveTextContent('List presets t-1');
    expect(lines[3]).toHaveTextContent('Place object 3');
    expect(lines[3]).toHaveClass('edit');
    expect(lines[0]).not.toHaveClass('edit');
    expect(lines[16]).toHaveClass('state-failed');
    expect(lines[16]).toHaveTextContent('List presets t-17 · Failed');
    fireEvent.click(screen.getByRole('button', { name: /Worked for/ }));
    expect(screen.queryByRole('list', { name: 'Actions' })).not.toBeInTheDocument();
  });

  it('leaves the time off when the host sent no stamps', () => {
    thread(yesTurn.map((item) => ({ ...item, createdAt: undefined })), yesActivities);
    expect(screen.getByRole('button', { name: '16 actions' })).toBeInTheDocument();
  });

  it('lets the turn’s change rows merge as one block after its words', () => {
    const at = '2026-10-20T22:48:30Z';
    const changes: ChangeInfo[] = [
      { seq: 20, createdAt: at, kind: 'preset', actor: 'agent', label: 'Bambu Lab A1', location: 'printer', conversationId: 'c-1', afterId: 't-4' },
      { seq: 23, createdAt: at, kind: 'step', actor: 'agent', label: 'Place object', conversationId: 'c-1', afterId: 't-4' },
      { seq: 30, createdAt: at, kind: 'step', actor: 'agent', label: 'Place object', conversationId: 'c-1', afterId: 't-8' },
      { seq: 35, createdAt: at, kind: 'step', actor: 'agent', label: 'Place object', conversationId: 'c-1', afterId: 't-12' },
    ];
    thread(yesTurn, yesActivities, { changes });
    const rows = screen.getAllByRole('listitem').map((row) => row.textContent);
    expect(rows).toHaveLength(2);
    expect(rows[1]).toContain('Place object');
    expect(rows[1]).toContain('3 steps merged');
    // One block, after the turn's words.
    const block = document.querySelector('.change-rows')!;
    expect(block.previousElementSibling).toHaveClass('message-group');
  });
});

// The 2026-10-28 walkthrough's failed call, as the project record kept it:
// the turn line said "1 failed" and nothing could be opened to say what.
const failedRead = activity('t-3', 'm-10', {
  tool: 'settings_get', title: 'Read settings', state: 'failed',
  arguments: { keys: ['textured_plate_temp'], scope: 'filament', target: { preset: 'Bambu PLA Basic @BBL A1M' } },
  error: {
    code: 'unknown_preset', message: 'No filament preset is named "Bambu PLA Basic @BBL A1M".',
    details: {
      code: 'unknown_preset', message: 'No filament preset is named "Bambu PLA Basic @BBL A1M".',
      suggestions: ['Bambu PLA Basic @BBL A1M'],
    },
  },
});
const askAi = message('m-9', 'user', 'Explain this issue and my options.', { createdAt: '2026-10-28T12:55:17Z' });
const answer = message('m-10', 'assistant', 'What the warning means…', { inReplyTo: 'm-9', createdAt: '2026-10-28T12:55:21Z' });

function openAction(name: RegExp) {
  fireEvent.click(screen.getByRole('button', { name: /Worked for/ }));
  const line = screen.getByRole('button', { name });
  fireEvent.click(line);
  return line;
}

describe('an action’s details', () => {
  it('opens a failed action into its error, its arguments and the tool it called', () => {
    thread([askAi, answer], [failedRead]);
    const line = openAction(/Read settings · Failed/);
    expect(line).toHaveAttribute('aria-expanded', 'true');
    expect(line.querySelector('.jp-icon-circle-alert')).not.toBeNull();
    const details = screen.getByTestId('action-details-t-3');
    expect(details).toHaveClass('failed');
    expect(within(details).getByText('unknown_preset')).toHaveClass('turn-activity-code');
    // The whole message, not a line cut off at the pane's edge.
    expect(within(details).getByText('No filament preset is named "Bambu PLA Basic @BBL A1M".')).toHaveClass('turn-activity-error-message');
    expect(details).toHaveTextContent('settings_get · jusprin-native · t-3');
    expect(details).toHaveTextContent('"preset": "Bambu PLA Basic @BBL A1M"');
    expect(details).toHaveTextContent('"suggestions"');
    // The details' copies of the code and the message are not shown twice.
    expect(details).not.toHaveTextContent('"code"');
    expect(details).not.toHaveTextContent('"message"');
    fireEvent.click(line);
    expect(screen.queryByTestId('action-details-t-3')).not.toBeInTheDocument();
  });

  it('opens every action, a successful one into its result, cut at a readable length', () => {
    const big = { items: Array.from({ length: 400 }, (_, index) => ({ name: `Preset ${index}` })) };
    thread(yesTurn, [activity('t-1', 'm-4', { result: big })]);
    openAction(/List presets t-1/);
    const details = screen.getByTestId('action-details-t-1');
    expect(details).not.toHaveClass('failed');
    expect(details).toHaveTextContent('"name": "Preset 0"');
    expect(details).not.toHaveTextContent('Preset 399');
    expect(details).toHaveTextContent(/more characters/);
  });

  it('copies the details as plain text for a bug report', async () => {
    const writeText = vi.fn(() => Promise.resolve());
    Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true });
    thread([askAi, answer], [failedRead]);
    openAction(/Read settings · Failed/);
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Copy details' })); });
    expect(writeText).toHaveBeenCalledTimes(1);
    expect(actionDetailsText(failedRead)).toBe([
      'Read settings · Failed',
      'Tool: settings_get · jusprin-native · t-3',
      'Error: unknown_preset',
      'No filament preset is named "Bambu PLA Basic @BBL A1M".',
      'Details:',
      JSON.stringify({ suggestions: ['Bambu PLA Basic @BBL A1M'] }, null, 2),
      'Arguments:',
      JSON.stringify(failedRead.arguments, null, 2),
    ].join('\n'));
    expect(writeText).toHaveBeenCalledWith(actionDetailsText(failedRead));
    expect(screen.getByRole('button', { name: 'Copied' })).toBeInTheDocument();
  });
});

describe('while the turn runs', () => {
  beforeEach(() => vi.useFakeTimers({ now: new Date('2026-10-20T22:48:12Z') }));
  afterEach(() => vi.useRealTimers());

  it('names the running action, counts the seconds, and offers no Stop of its own', () => {
    const running = activity('t-3', 'm-6', { state: 'running', title: 'Preview a printer setup', progress: { current: 0, total: 0 } });
    thread([asked, ...toolOnly.slice(0, 3)], [...yesActivities.slice(0, 2), running], { streamingMessageId: 'm-6' });
    const status = screen.getByRole('status');
    expect(status).toHaveTextContent('Preview a printer setup… · 6s');
    expect(status).toHaveClass('shimmer');
    expect(screen.queryByRole('button', { name: /stop/i })).not.toBeInTheDocument();
    expect(screen.queryAllByTestId(/^tool-t-/)).toHaveLength(0);
    act(() => { vi.advanceTimersByTime(2000); });
    expect(status).toHaveTextContent('Preview a printer setup… · 8s');
  });

  it('says it is working before the first call, and keeps a measured action’s card', () => {
    thread([asked, message('m-4', 'assistant', '', { inReplyTo: 'm-3' })], [], { streamingMessageId: 'm-4' });
    expect(screen.getByRole('status')).toHaveTextContent('Working on it… · 6s');
    expect(screen.getByRole('button', { name: /Working on it/ })).toBeDisabled();
  });

  it('keeps a measured running action’s progress card under the line', () => {
    const measured = activity('t-1', 'm-4', { state: 'running', title: 'Arrange', progress: { current: 2, total: 10 } });
    thread([asked, message('m-4', 'assistant', '', { inReplyTo: 'm-3' })], [measured], { streamingMessageId: 'm-4' });
    expect(screen.getByTestId('tool-t-1')).toHaveTextContent('Running… · 20%');
    expect(screen.getByRole('button', { name: 'Cancel' })).toBeInTheDocument();
  });
});

describe('the printer panel', () => {
  it('still draws a card per call under the message that called it', () => {
    thread([asked, ...toolOnly.slice(0, 2)], yesActivities.slice(0, 2), { printerBlocks: [] });
    expect(screen.getAllByTestId(/^tool-t-/)).toHaveLength(2);
    expect(screen.queryByTestId('turn-activity')).not.toBeInTheDocument();
  });
});

describe('the line’s glyphs', () => {
  it('points the chevron right while closed and down once open, and spins while working', () => {
    thread(yesTurn, yesActivities);
    const line = screen.getByRole('button', { name: /Worked for/ });
    expect(line.querySelector('.jp-icon-chevron-right')).not.toBeNull();
    fireEvent.click(line);
    expect(line.querySelector('.jp-icon-chevron-down')).not.toBeNull();
    expect(line.querySelector('.turn-activity-spinner')).toBeNull();
  });

  it('turns the running glyph', () => {
    thread([asked, message('m-4', 'assistant', '', { inReplyTo: 'm-3' })], [], { streamingMessageId: 'm-4' });
    expect(document.querySelector('.turn-activity-spinner')).not.toBeNull();
  });
});
