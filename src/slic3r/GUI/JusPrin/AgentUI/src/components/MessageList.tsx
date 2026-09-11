// Conversation transcript with stable scroll anchoring: the list follows new
// content only while the reader is at the bottom; scrolling up to reread
// pins the viewport until they return to the bottom. Tool activity cards
// render beneath the assistant message that proposed them; the change log and
// the manufacturing history render after the conversation item they follow,
// in the order they happened.

import { Fragment, useLayoutEffect, useRef, useState } from 'react';
import { AttachmentInfo, BuildInfo, ChangeInfo, ExportedCopyInfo, PhysicalPrintInfo, ToolActivityInfo } from '../bridge/protocol';
import { Message } from '../state/store';
import { AttachmentChip } from './AttachmentChip';
import { ChangeRows } from './ChangeRows';
import { MarkdownMessage } from './MarkdownMessage';
import { ToolActivityCard } from './ToolActivityCard';
import { ManufacturingHistoryCard, ManufacturingHistoryEntry } from './ManufacturingHistoryCard';

interface Props {
  messages: Message[];
  attachments: AttachmentInfo[];
  streamingMessageId: string | null;
  toolActivities: ToolActivityInfo[];
  builds: BuildInfo[];
  exportedCopies: ExportedCopyInfo[];
  physicalPrints: PhysicalPrintInfo[];
  changes: ChangeInfo[]; // already filtered to this conversation
  onRetry: (messageId: string) => void;
  onToolDecision: (actionId: string, decision: 'approve' | 'reject') => void;
  onToolCancel: (actionId: string) => void;
  // The setup card's expansion is a layer over this thread; the thread dims
  // rather than being covered, so the conversation stays legibly there.
  dimmed?: boolean;
}

// What follows one conversation item: history cards and runs of changes, in
// seq order. Consecutive changes form one block, which draws as one group.
type TimelineBlock =
  | { kind: 'history'; seq: number; entry: ManufacturingHistoryEntry }
  | { kind: 'changes'; seq: number; changes: ChangeInfo[] };

function timeline(history: ManufacturingHistoryEntry[], changes: ChangeInfo[]): TimelineBlock[] {
  const items = [
    ...history.map((entry) => ({ seq: entry.seq, history: entry, change: undefined })),
    ...changes.map((change) => ({ seq: change.seq, history: undefined, change })),
  ].sort((a, b) => a.seq - b.seq);
  const blocks: TimelineBlock[] = [];
  for (const item of items) {
    const previous = blocks[blocks.length - 1];
    if (item.change && previous?.kind === 'changes') previous.changes.push(item.change);
    else if (item.change) blocks.push({ kind: 'changes', seq: item.seq, changes: [item.change] });
    else blocks.push({ kind: 'history', seq: item.seq, entry: item.history! });
  }
  return blocks;
}

function TimelineBlocks({ blocks }: { blocks: TimelineBlock[] }) {
  return (
    <>
      {blocks.map((block) =>
        block.kind === 'history' ? (
          <ManufacturingHistoryCard key={`${block.entry.kind}-${block.entry.record.id}`} entry={block.entry} />
        ) : (
          <ChangeRows key={`changes-${block.seq}`} changes={block.changes} />
        ),
      )}
    </>
  );
}

const inFlight = new Set(['pending', 'approved', 'running']);

export function MessageList({
  messages,
  attachments,
  streamingMessageId,
  toolActivities,
  builds,
  exportedCopies,
  physicalPrints,
  changes,
  onRetry,
  onToolDecision,
  onToolCancel,
  dimmed,
}: Props) {
  const attachmentsById = new Map(attachments.map((attachment) => [attachment.id, attachment]));
  const listRef = useRef<HTMLDivElement>(null);
  const [followBottom, setFollowBottom] = useState(true);

  const handleScroll = () => {
    const list = listRef.current;
    if (!list) return;
    const distanceFromBottom = list.scrollHeight - list.scrollTop - list.clientHeight;
    setFollowBottom(distanceFromBottom < 24);
  };

  useLayoutEffect(() => {
    const list = listRef.current;
    if (list && followBottom) list.scrollTop = list.scrollHeight;
  }, [messages, toolActivities, builds, exportedCopies, physicalPrints, changes, followBottom]);

  const history: ManufacturingHistoryEntry[] = [
    ...builds.map((record) => ({ kind: 'build' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
    ...exportedCopies.map((record) => ({ kind: 'copy' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
    ...physicalPrints.map((record) => ({ kind: 'print' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
  ];
  const activitiesOf = (messageId: string) => toolActivities.filter((activity) => activity.correlationId === messageId);
  // A change follows a message or one of its tool activities; both place it
  // after that message's group.
  const messageOfItem = new Map<string, string>();
  for (const message of messages) {
    messageOfItem.set(message.id, message.id);
    for (const activity of activitiesOf(message.id)) messageOfItem.set(activity.actionId, message.id);
  }
  const changesAfter = (messageId: string) => changes.filter((change) => messageOfItem.get(change.afterId) === messageId);
  const leadingChanges = changes.filter((change) => !messageOfItem.has(change.afterId));
  const historyAfter = (messageId: string) => history.filter((entry) => entry.afterMessageId === messageId);
  const leadingHistory = history.filter(
    (entry) => entry.afterMessageId === '' || !messages.some((message) => message.id === entry.afterMessageId),
  );

  // A finished reply that changed nothing says so (Figma "Answered
  // response"). Only the Agent's own changes count against it; the person
  // editing by hand while it answered is not the Agent changing something.
  const answeredWithoutChange = (message: Message) =>
    message.role === 'assistant' &&
    message.state === 'complete' &&
    message.id !== streamingMessageId &&
    !activitiesOf(message.id).some((activity) => inFlight.has(activity.state)) &&
    !changesAfter(message.id).some((change) => change.actor === 'agent');

  return (
    <div className={dimmed ? 'message-list thread-dimmed' : 'message-list'} role="log" aria-label="Agent conversation"
      ref={listRef} onScroll={handleScroll}>
      <TimelineBlocks blocks={timeline(leadingHistory, leadingChanges)} />
      {messages.length === 0 && (
        <div className="notice">
          <h2>Ask the Agent about your print</h2>
          <p>
            The Agent can describe the open project, the plates and objects on them, the current selection, and the
            printer setup — or duplicate the selected object with your approval.
          </p>
        </div>
      )}
      {messages.map((message) => {
        if (message.role === 'note')
          return (
            <div key={message.id} className="message-group">
              <div className="message note" role="status">
                {message.text}
              </div>
            </div>
          );
        const bubble = (
          <div className={`message ${message.role}`}>
            {/* The agent does not speak in a bubble: a 20px action/primary
                disc stands beside plain text, as the Figma "Chat Bubble"
                component's Agent variant has it. Purely decorative -- the
                author is already carried by the role class and by the
                bubble the user's turn keeps. */}
            {message.role === 'assistant' && <span className="agent-avatar" aria-hidden="true" />}
            <div className="message-content">
              {message.text &&
                (message.role === 'assistant' ? (
                  <MarkdownMessage streaming={message.id === streamingMessageId}>{message.text}</MarkdownMessage>
                ) : (
                  <span>{message.text}</span>
                ))}
              {message.attachments && message.attachments.length > 0 && (
                <div className="message-attachments">
                  {message.attachments.map((id) => {
                    const attachment = attachmentsById.get(id);
                    return attachment ? <AttachmentChip key={id} attachment={attachment} /> : null;
                  })}
                </div>
              )}
              {message.state === 'failed' && message.error && (
                <div className="error">
                  {message.error.message}
                  {message.error.retryable && (
                    <div>
                      <button onClick={() => onRetry(message.id)}>Retry</button>
                    </div>
                  )}
                </div>
              )}
              {message.state === 'stopped' && <div className="meta">Stopped</div>}
            </div>
          </div>
        );
        // The message group is one thread item: the turn and the tool cards
        // it proposed, 8 apart. What follows it -- change runs and history
        // entries -- are thread items of their own.
        return (
          <Fragment key={message.id}>
            <div className="message-group">
              {answeredWithoutChange(message) ? (
                <div className="answered-turn">
                  {bubble}
                  <div className="answered-state">Answered · nothing changed</div>
                </div>
              ) : (
                bubble
              )}
              {activitiesOf(message.id).map((activity) => (
                <ToolActivityCard
                  key={activity.actionId}
                  activity={activity}
                  onDecision={onToolDecision}
                  onCancel={onToolCancel}
                />
              ))}
            </div>
            <TimelineBlocks blocks={timeline(historyAfter(message.id), changesAfter(message.id))} />
          </Fragment>
        );
      })}
    </div>
  );
}
