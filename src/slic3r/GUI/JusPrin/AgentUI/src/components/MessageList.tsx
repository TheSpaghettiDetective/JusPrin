// Conversation transcript with stable scroll anchoring: the list follows new
// content only while the reader is at the bottom; scrolling up to reread
// pins the viewport until they return to the bottom. Tool activity cards
// render beneath the assistant message that proposed them, a plan's calls as
// one card beneath the message that proposed its first call; the change log and
// the manufacturing history render after the conversation item they follow,
// in the order they happened.

import { Fragment, useLayoutEffect, useRef, useState } from 'react';
import {
  AttachmentInfo,
  BuildInfo,
  ChangeInfo,
  ExportedCopyInfo,
  PhysicalPrintInfo,
  PrinterBlock,
  ToolActivityInfo,
} from '../bridge/protocol';
import { Message } from '../state/store';
import { AttachmentChip } from './AttachmentChip';
import { ChangeRows } from './ChangeRows';
import { MarkdownMessage } from './MarkdownMessage';
import { ToolActivityCard } from './ToolActivityCard';
import { PlanActivityCard, planHeadline, planKey, planMembers } from './PlanActivityCard';
import { ManufacturingHistoryCard, ManufacturingHistoryEntry } from './ManufacturingHistoryCard';
import { PrinterBlockView } from './PrinterPanel';

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
  // The printer panel's own cards, each anchored after the message that drew
  // it, the way history entries are. Absent everywhere else.
  printerBlocks?: PrinterBlock[];
  onPrinterAction?: (
    action: 'network_pick' | 'candidate_pick' | 'add' | 'reject' | 'browse' | 'manual_setup',
    id: string,
  ) => void;
  // "Answered · nothing changed" is about the open project, which the printer
  // panel is not having a conversation about.
  answeredState?: boolean;
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
  printerBlocks = [],
  onPrinterAction,
  answeredState = true,
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
  }, [messages, toolActivities, builds, exportedCopies, physicalPrints, changes, printerBlocks, followBottom]);

  const history: ManufacturingHistoryEntry[] = [
    ...builds.map((record) => ({ kind: 'build' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
    ...exportedCopies.map((record) => ({ kind: 'copy' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
    ...physicalPrints.map((record) => ({ kind: 'print' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
  ];
  const activitiesOf = (messageId: string) => toolActivities.filter((activity) => activity.correlationId === messageId);
  const plans = planMembers(toolActivities);
  const headline = planHeadline(toolActivities);
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
  const printerBlocksAfter = (messageId: string) =>
    printerBlocks.filter((block) => block.afterMessageId === messageId).sort((a, b) => a.seq - b.seq);
  const printerBlockViews = (messageId: string) =>
    onPrinterAction
      ? printerBlocksAfter(messageId).map((block) => (
          <PrinterBlockView key={block.id} block={block} onAction={onPrinterAction} />
        ))
      : null;
  const leadingHistory = history.filter(
    (entry) => entry.afterMessageId === '' || !messages.some((message) => message.id === entry.afterMessageId),
  );

  // A finished reply that changed nothing says so (Figma "Answered
  // response"). Only the Agent's own changes count against it; the person
  // editing by hand while it answered is not the Agent changing something.
  const answeredWithoutChange = (message: Message) =>
    answeredState &&
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
        // Mid-turn, before any reply text has arrived, an assistant message
        // has nothing to show yet: a bubble with only the avatar disc reads
        // as an empty flash before each reply. The printer panel names the
        // work instead, in one grey line that is gone the moment real text
        // lands; elsewhere the bubble still renders (its attachments, error
        // or "Stopped" state may not be empty even when its text is).
        const workingOnCard = Boolean(onPrinterAction) && message.role === 'assistant' && !message.text && message.id === streamingMessageId;
        // A single photo in the printer panel is the message itself, not a
        // filed document beside the words -- elsewhere, or with more than
        // one attachment, the ordinary chip list still applies.
        const printerPhoto =
          onPrinterAction && message.attachments?.length === 1
            ? (() => {
                const attachment = attachmentsById.get(message.attachments![0]);
                return attachment?.kind === 'image' && attachment.previewDataUrl ? attachment : undefined;
              })()
            : undefined;
        const bubble = workingOnCard ? (
          <div className="printer-activity" role="status">
            Looking through the printer list…
          </div>
        ) : (
          <div className={`message ${message.role}`}>
            {/* The agent does not speak in a bubble: a 20px action/primary
                disc stands beside plain text, as the Figma "Chat Bubble"
                component's Agent variant has it. Purely decorative -- the
                author is already carried by the role class and by the
                bubble the user's turn keeps. */}
            {message.role === 'assistant' && <span className="agent-avatar" aria-hidden="true" />}
            <div className="message-content">
              {printerPhoto ? (
                <>
                  {/* The picture is the turn, not a filed document beside
                      it: about 120px wide, any words the person added
                      underneath it as a caption. */}
                  <img className="message-photo" src={printerPhoto.previewDataUrl} alt={printerPhoto.name || 'Photo'} />
                  {message.text && <span className="message-photo-caption">{message.text}</span>}
                </>
              ) : (
                <>
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
                </>
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
              {activitiesOf(message.id).map((activity) => {
                const key = planKey(activity);
                const found = key ? plans.get(key) : undefined;
                // A plan of one change is decided like any other call, once
                // the agent can no longer add to it.
                const members = found && (found.length > 1 || streamingMessageId !== null) ? found : undefined;
                if (members && members[0].actionId !== activity.actionId) return null;
                return members ? (
                  <PlanActivityCard
                    key={activity.actionId}
                    members={members}
                    headline={headline}
                    stillProposing={streamingMessageId !== null}
                    onDecision={onToolDecision}
                    onCancel={onToolCancel}
                  />
                ) : (
                  <ToolActivityCard
                    key={activity.actionId}
                    activity={activity}
                    onDecision={onToolDecision}
                    onCancel={onToolCancel}
                  />
                );
              })}
            </div>
            {printerBlockViews(message.id)}
            <TimelineBlocks blocks={timeline(historyAfter(message.id), changesAfter(message.id))} />
          </Fragment>
        );
      })}
    </div>
  );
}
