// Board 5b's card: a settings preview that saves a preset, in the Agent's
// bubble, saved only by the person's Save preset; and board 6's receipt.

import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { ToolActivityInfo } from '../bridge/protocol';
import { Message } from '../state/store';
import { MessageList } from './MessageList';

const noop = () => {};

function message(id: string, role: Message['role'], text: string, extra: Partial<Message> = {}): Message {
  return { id, role, state: 'complete', text, attempt: 1, lastSeq: -1, ...extra };
}

const proposal = {
  title: 'Save 225 °C as a new filament preset',
  rows: [
    { icon: 'save', text: 'New preset · Gray PETG · MK4 calibrated' },
    { icon: 'thermometer', text: 'Nozzle temperature 240 → 225 °C' },
    { icon: 'info', text: 'Gray PETG itself is unchanged' },
  ],
  save: 'Save preset',
  other: 'Pick another block',
  receipt: 'Filament preset · nozzle temperature 225 °C',
};

function activity(id: string, correlationId: string, overrides: Partial<ToolActivityInfo> = {}): ToolActivityInfo {
  return {
    actionId: id, correlationId, server: 'jusprin-native', tool: 'settings_preview_patch', title: 'Preview settings',
    arguments: {}, actionClass: 'read_only', sessionId: '2', expectedRevision: 1, state: 'succeeded',
    progress: { current: 1, total: 1 }, ...overrides,
  };
}

const photo = message('m-1', 'user', 'Here it is.');
const called = message('m-2', 'assistant', '', { inReplyTo: 'm-1' });
const spoke = message('m-3', 'assistant', 'I would set Gray PETG to 225 °C on this printer.', { inReplyTo: 'm-1' });
const preview = activity('a-1', 'm-2', { proposal });

function thread(messages: Message[], activities: ToolActivityInfo[], handlers: {
  onSaveProposal?: (actionId: string) => void; onSend?: (text: string) => void;
} = {}) {
  return render(
    <MessageList messages={messages} attachments={[]} streamingMessageId={null} toolActivities={activities}
      builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop} onToolCancel={noop}
      onSaveProposal={handlers.onSaveProposal} onSend={handlers.onSend ?? noop} />,
  );
}

describe('the proposal card', () => {
  it('draws the preview in the reply with words, and saves nothing until Save preset', () => {
    const onSaveProposal = vi.fn();
    const onSend = vi.fn();
    thread([photo, called, spoke], [preview], { onSaveProposal, onSend });
    const card = screen.getByRole('group', { name: proposal.title });
    expect(card.closest('.message.assistant')).toHaveTextContent(spoke.text);
    expect(card).toHaveTextContent('New preset · Gray PETG · MK4 calibrated');
    expect(card).toHaveTextContent('Nozzle temperature 240 → 225 °C');
    expect(card).toHaveTextContent('Gray PETG itself is unchanged');
    expect(card.querySelector('.jp-icon-thermometer')).not.toBeNull();
    expect(onSaveProposal).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Pick another block' }));
    expect(onSend).toHaveBeenCalledWith('Pick another block');
    fireEvent.click(screen.getByRole('button', { name: 'Save preset' }));
    expect(onSaveProposal).toHaveBeenCalledWith('a-1');
  });

  it('waits while the save runs, says why it failed, and goes once saved', () => {
    const save = activity('a-2', 'm-2', { tool: 'settings_apply_patch', approves: 'a-1', state: 'running' });
    const { rerender } = thread([photo, called, spoke], [preview, save]);
    expect(screen.getByRole('button', { name: 'Saving…' })).toBeDisabled();
    // The person's save is not part of the Agent's work in the turn line.
    expect(screen.getByText(/1 action/)).toBeInTheDocument();
    const failed = { ...save, state: 'failed' as const, error: { code: 'stale_workspace', message: 'The workspace changed. Read and preview again.' } };
    rerender(
      <MessageList messages={[photo, called, spoke]} attachments={[]} streamingMessageId={null} toolActivities={[preview, failed]}
        builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop} onToolCancel={noop} onSend={noop} />,
    );
    expect(screen.getByRole('alert')).toHaveTextContent('The workspace changed.');
    expect(screen.getByRole('button', { name: 'Save preset' })).toBeEnabled();
    rerender(
      <MessageList messages={[photo, called, spoke]} attachments={[]} streamingMessageId={null}
        toolActivities={[preview, { ...save, state: 'succeeded' }]}
        builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop} onToolCancel={noop} onSend={noop} />,
    );
    expect(screen.queryByRole('group', { name: proposal.title })).toBeNull();
  });

  it('says it was not saved once the person answered another way', () => {
    thread([photo, called, spoke, message('m-4', 'user', 'Pick another block')], [preview]);
    const card = screen.getByRole('group', { name: proposal.title });
    expect(card).toHaveTextContent('Not saved');
    expect(card.querySelector('button')).toBeNull();
  });

  it('draws a saved preset as a receipt, not as its note text', () => {
    const receipt = message('m-5', 'note', 'The person pressed Save preset on your proposal.', {
      receipt: { name: 'Gray PETG · MK4 calibrated', detail: proposal.receipt, actionId: 'a-2' },
    });
    thread([photo, called, spoke, receipt], [preview]);
    const status = screen.getByRole('status');
    expect(status).toHaveTextContent('Saved: Gray PETG · MK4 calibrated');
    expect(status).toHaveTextContent(proposal.receipt);
    expect(screen.queryByText(/pressed Save preset/)).toBeNull();
  });

  it('takes a save back with Undo, and then says what it removed', () => {
    const onUndoReceipt = vi.fn();
    const saved = message('m-5', 'note', 'The person pressed Save preset on your proposal.', {
      receipt: { name: 'Gray PETG · MK4 calibrated', detail: proposal.receipt, actionId: 'a-2', undo: { base: 'Gray PETG @BBL', label: 'Gray PETG', newCopy: true } },
    });
    const { rerender } = render(
      <MessageList messages={[photo, called, spoke, saved]} attachments={[]} streamingMessageId={null} toolActivities={[preview]}
        builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop} onToolCancel={noop} onSend={noop}
        onUndoReceipt={onUndoReceipt} />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
    expect(onUndoReceipt).toHaveBeenCalledWith('m-5');
    const undone = { ...saved, receipt: { ...saved.receipt!, undone: true } };
    rerender(
      <MessageList messages={[photo, called, spoke, undone]} attachments={[]} streamingMessageId={null} toolActivities={[preview]}
        builds={[]} exportedCopies={[]} physicalPrints={[]} changes={[]} onRetry={noop} onToolCancel={noop} onSend={noop}
        onUndoReceipt={onUndoReceipt} />,
    );
    const status = screen.getByRole('status');
    expect(status).toHaveTextContent('Removed: Gray PETG · MK4 calibrated');
    expect(status).toHaveTextContent('This test uses Gray PETG again.');
    expect(screen.queryByRole('button', { name: 'Undo' })).toBeNull();
  });

  it('draws the receipt in place of the change rows a save and its Undo made', () => {
    const save = activity('a-2', 'm-2', { tool: 'settings_apply_patch', approves: 'a-1' });
    const receipt = message('m-5', 'note', 'Saved.', {
      receipt: { name: 'Gray PETG · MK4 calibrated', detail: proposal.receipt, actionId: 'a-2' },
    });
    const change = (seq: number, afterId: string) => ({
      seq, createdAt: '2026-10-22T16:00:00Z', kind: 'preset' as const, actor: 'agent' as const, label: `Preset ${seq}`,
      conversationId: 'c-1', afterId,
    });
    render(
      <MessageList messages={[photo, called, spoke, receipt]} attachments={[]} streamingMessageId={null}
        toolActivities={[preview, save]} builds={[]} exportedCopies={[]} physicalPrints={[]}
        changes={[change(1, 'a-2'), change(2, 'm-5'), change(3, 'm-3')]} onRetry={noop} onToolCancel={noop} onSend={noop} />,
    );
    expect(screen.queryByText(/Preset 1/)).toBeNull();
    expect(screen.queryByText(/Preset 2/)).toBeNull();
    expect(screen.getByText(/Preset 3/)).toBeInTheDocument();
  });
});
