// Conversation transcript with stable scroll anchoring: the list follows new
// content only while the reader is at the bottom; scrolling up to reread
// pins the viewport until they return to the bottom. In the project chat a
// turn is one thread item: the person's message, one line for the tool work
// the Agent did in reply, then its words; the change log and the
// manufacturing history render after the turn they follow, in the order
// they happened. The printer panel keeps a card per call under the message
// that called it.

import { Fragment, ReactNode, useEffect, useLayoutEffect, useRef, useState } from 'react';
import {
  AttachmentInfo,
  BuildInfo,
  ChangeInfo,
  WorkInfo,
  RestorePointInfo,
  ExportedCopyInfo,
  PhysicalPrintInfo,
  PrinterBlock,
  ToolActivityInfo,
} from '../bridge/protocol';
import { Message } from '../state/store';
import { AttachmentChip } from './AttachmentChip';
import { ChangeRows } from './ChangeRows';
import { FileNotesCard } from './FileNotesCard';
import { MarkdownMessage } from './MarkdownMessage';
import { ToolActivityCard } from './ToolActivityCard';
import { TurnActivity } from './TurnActivity';
import { collapseWork, WorkCard } from './WorkCard';
import { ManufacturingHistoryCard, ManufacturingHistoryEntry } from './ManufacturingHistoryCard';
import { PrinterBlockView } from './PrinterPanel';
import { openChoices, splitChoices, withoutPartialChoices } from '../replyChoices';

interface Props {
  messages: Message[];
  attachments: AttachmentInfo[];
  streamingMessageId: string | null;
  toolActivities: ToolActivityInfo[];
  builds: BuildInfo[];
  exportedCopies: ExportedCopyInfo[];
  physicalPrints: PhysicalPrintInfo[];
  changes: ChangeInfo[]; // already filtered to this conversation
  work?: WorkInfo[]; // already filtered to this conversation
  onWorkCancel?: (id: string) => void;
  restorePoints?: RestorePointInfo[];
  onRevert?: (versionId: string) => void;
  onRetry: (messageId: string) => void;
  onToolCancel: (actionId: string) => void;
  // Sends a reply chip's text as the person's message.
  onSend: (text: string) => void;
  replyDisabled?: boolean;
  readOnly?: boolean;
  endNotice?: ReactNode;
  onDiscussFailure?: (text: string) => void;
  // The printer panel's own cards, each anchored after the message that drew
  // it, the way history entries are. Absent everywhere else, and its
  // presence is what makes this the printer panel's thread.
  printerBlocks?: PrinterBlock[];
  // Undo on an added printer's receipt, by block id.
  onUndoAdd?: (blockId: string) => void;
  // Install on the notice that Bambu's network plug-in is missing.
  onInstallPlugin?: () => void;
  // A surface's own card for one of its tool calls, in place of the generic
  // one; undefined keeps the generic card.
  renderActivity?: (activity: ToolActivityInfo) => ReactNode | undefined;
  // Whether any of the thread is scrolled out under what is pinned above it,
  // reported as it changes and as false when the thread goes away.
  onScrolledChange?: (scrolled: boolean) => void;
}

// What follows one conversation item: history cards and runs of changes, in
// seq order. Consecutive changes form one block, which draws as one group.
type TimelineBlock =
  | { kind: 'history'; seq: number; entry: ManufacturingHistoryEntry }
  | { kind: 'changes'; seq: number; changes: ChangeInfo[] }
  | { kind: 'work'; seq: number; work: WorkInfo[] };

function timeline(history: ManufacturingHistoryEntry[], changes: ChangeInfo[], work: WorkInfo[] = []): TimelineBlock[] {
  const items = [
    ...history.map((entry) => ({ seq: entry.seq, history: entry, change: undefined, work: undefined })),
    ...changes.map((change) => ({ seq: change.seq, history: undefined, change, work: undefined })),
    ...work.map((entry) => ({ seq: entry.seq, history: undefined, change: undefined, work: entry })),
  ].sort((a, b) => a.seq - b.seq);
  const blocks: TimelineBlock[] = [];
  for (const item of items) {
    const previous = blocks[blocks.length - 1];
    if (item.change && previous?.kind === 'changes') previous.changes.push(item.change);
    else if (item.change) blocks.push({ kind: 'changes', seq: item.seq, changes: [item.change] });
    else if (item.work && previous?.kind === 'work') previous.work.push(item.work);
    else if (item.work) blocks.push({ kind: 'work', seq: item.seq, work: [item.work] });
    else blocks.push({ kind: 'history', seq: item.seq, entry: item.history! });
  }
  return blocks;
}

function TimelineBlocks({ blocks, restorePoints, onRevert, onDiscussFailure, discussDisabled, onWorkCancel, readOnly }: {
  blocks: TimelineBlock[];
  onWorkCancel?: (id: string) => void;
  readOnly?: boolean;
  restorePoints: RestorePointInfo[];
  onRevert?: (versionId: string) => void;
  onDiscussFailure: (print: PhysicalPrintInfo) => void;
  discussDisabled: boolean;
}) {
  return (
    <>
      {blocks.map((block) =>
        block.kind === 'history' ? (
          <ManufacturingHistoryCard key={`${block.entry.kind}-${block.entry.record.id}`} entry={block.entry}
            onDiscussFailure={onDiscussFailure} discussDisabled={discussDisabled} />
        ) : block.kind === 'work' ? (
          collapseWork(block.work).map(({ work, times }) => (
            <WorkCard key={work.id} work={work} times={times} onCancel={onWorkCancel ?? (() => {})} readOnly={readOnly || !onWorkCancel} />
          ))
        ) : (
          <ChangeRows key={`changes-${block.seq}`} changes={block.changes} restorePoints={restorePoints} onRevert={onRevert} />
        ),
      )}
    </>
  );
}

// One thread item: a message and the Agent's replies to it. In the project
// chat every assistant message that answers the same person's message joins
// that message's turn, however many tool calls split the model's reply into
// messages. The printer panel keeps one item per message.
interface Turn {
  anchor: Message;
  replies: Message[];
}

export function turnsOf(messages: Message[], perMessage: boolean): Turn[] {
  const turns: Turn[] = [];
  for (const message of messages) {
    const current = turns[turns.length - 1];
    if (!perMessage && current && message.role === 'assistant' && message.inReplyTo === current.anchor.id)
      current.replies.push(message);
    else turns.push({ anchor: message, replies: [] });
  }
  return turns;
}

export function MessageList({
  messages,
  attachments,
  streamingMessageId,
  toolActivities,
  builds,
  exportedCopies,
  physicalPrints,
  changes,
  work = [],
  onWorkCancel,
  restorePoints = [],
  onRevert,
  onRetry,
  onToolCancel,
  onSend,
  replyDisabled = false,
  readOnly = false,
  endNotice,
  onDiscussFailure,
  printerBlocks,
  onUndoAdd,
  onInstallPlugin,
  renderActivity,
  onScrolledChange,
}: Props) {
  const attachmentsById = new Map(attachments.map((attachment) => [attachment.id, attachment]));
  const listRef = useRef<HTMLDivElement>(null);
  const [followBottom, setFollowBottom] = useState(true);

  const handleScroll = () => {
    const list = listRef.current;
    if (!list) return;
    const distanceFromBottom = list.scrollHeight - list.scrollTop - list.clientHeight;
    setFollowBottom(distanceFromBottom < 24);
    onScrolledChange?.(list.scrollTop > 0);
  };

  useEffect(() => () => onScrolledChange?.(false), [onScrolledChange]);

  useLayoutEffect(() => {
    const list = listRef.current;
    if (list && followBottom) list.scrollTop = list.scrollHeight;
  }, [messages, toolActivities, builds, exportedCopies, physicalPrints, changes, printerBlocks, followBottom]);
  const printerPanel = printerBlocks !== undefined;

  // A copy made immediately from a build is one G-code event in the thread.
  // Keep later copies separate so an intervening edit never moves in history.
  const pairedCopies = new Set<string>();
  const history: ManufacturingHistoryEntry[] = [
    ...builds.map((record) => {
      const copy = exportedCopies.find((item) => item.buildId === record.id && item.seq === record.seq + 1);
      if (copy) pairedCopies.add(copy.id);
      return { kind: 'build' as const, seq: record.seq, afterMessageId: record.afterMessageId, record, copy };
    }),
    ...exportedCopies.filter((record) => !pairedCopies.has(record.id))
      .map((record) => ({ kind: 'copy' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
    ...physicalPrints.map((record) => ({ kind: 'print' as const, seq: record.seq, afterMessageId: record.afterMessageId, record })),
  ];
  const activitiesOf = (messageId: string) => toolActivities.filter((activity) => activity.correlationId === messageId);
  const turns = turnsOf(messages, printerPanel);
  const messagesOf = (turn: Turn) => [turn.anchor, ...turn.replies];
  // A change, work entry or history card follows a message or one of its
  // tool activities; all of them place it after that message's turn.
  const turnOfItem = new Map<string, string>();
  for (const turn of turns)
    for (const message of messagesOf(turn)) {
      turnOfItem.set(message.id, turn.anchor.id);
      for (const activity of activitiesOf(message.id)) turnOfItem.set(activity.actionId, turn.anchor.id);
    }
  const changesAfter = (turnId: string) => changes.filter((change) => turnOfItem.get(change.afterId) === turnId);
  const leadingChanges = changes.filter((change) => !turnOfItem.has(change.afterId));
  // Work keeps the place it started at: after the item that was last then,
  // so a message that arrived later reads below it.
  const workAfter = (turnId: string) => work.filter((entry) => turnOfItem.get(entry.afterId) === turnId);
  const leadingWork = work.filter((entry) => !turnOfItem.has(entry.afterId));
  const historyAfter = (turnId: string) => history.filter((entry) => turnOfItem.get(entry.afterMessageId) === turnId);
  const printerBlockViews = (messageId: string) =>
    (printerBlocks ?? [])
      .filter((block) => block.afterMessageId === messageId)
      .sort((a, b) => a.seq - b.seq)
      .map((block) => <PrinterBlockView key={block.id} block={block} onUndoAdd={onUndoAdd} onInstallPlugin={onInstallPlugin} />);
  const leadingHistory = history.filter(
    (entry) => entry.afterMessageId === '' || !turnOfItem.has(entry.afterMessageId),
  );
  const discussFailure = (print: PhysicalPrintInfo) =>
    (onDiscussFailure ?? onSend)(`Help me understand why this print failed: ${print.failure || 'The print stopped unexpectedly.'}`);

  // A message's bubble, or null for a tool-only reply that has nothing to
  // say. A file's load report is for the model: the person sees it only
  // when the Agent could not speak about it, as the card of what OrcaSlicer
  // said.
  const bubble = (message: Message): ReactNode => {
    if (message.role === 'note' && message.fileReport)
      return message.fileReportCard ? <FileNotesCard report={message.fileReport} /> : null;
    if (message.role === 'note')
      return (
        <div className="message note" role="status">
          {/* The colour a change landed on, drawn and never named: the
              app holds a colour as a value, not a word. */}
          {message.swatch && <span className="note-swatch" style={{ background: message.swatch }} aria-hidden="true" />}
          {message.text}
        </div>
      );
    // In the printer panel a photo is how the person says which printer
    // it is, or shows what changed: it draws inline, above any words as
    // their caption, rather than as a named file chip below them.
    const attached = (message.attachments ?? []).map((id) => attachmentsById.get(id)).filter((a): a is AttachmentInfo => !!a);
    const photos = printerPanel ? attached.filter((a) => a.kind === 'image' && a.previewDataUrl) : [];
    const otherAttachments = attached.filter((a) => !photos.includes(a));
    // The model often calls a tool with no words yet: its actual reply lands
    // in a later, separate message once it sees the tool's result. A bare,
    // empty bubble either mid-stream or once it settles reads as a
    // rendering bug, not a pause. The project chat's turn line names the
    // work instead; the printer panel names it here while streaming. Once
    // settled still empty, nothing is drawn. A failed or stopped turn
    // always still renders, so its error and Retry stay reachable.
    const emptyTurn =
      message.role === 'assistant' &&
      !message.text &&
      photos.length === 0 &&
      otherAttachments.length === 0 &&
      message.state !== 'stopped' &&
      !(message.state === 'failed' && message.error);
    const streaming = message.id === streamingMessageId;
    if (emptyTurn && printerPanel && streaming)
      return (
        // Chat/System Line: a quiet line of status while the model works.
        <div className="message assistant">
          <div className="printer-activity" role="status">
            <span className="jp-icon jp-icon-rotate-cw" aria-hidden="true" />
            Working on it…
          </div>
        </div>
      );
    if (emptyTurn) return null;
    const reply =
      message.role !== 'assistant'
        ? { body: message.text, choices: [] }
        : streaming
          ? { body: withoutPartialChoices(message.text), choices: [] }
          : splitChoices(message.text);
    const choices = !streaming && message.id === messages[messages.length - 1].id ? openChoices(messages) : [];
    return (
      <div className={`message ${message.role}`}>
        <div className="message-content">
          {photos.length > 0 && (
            <div className="message-photos">
              {photos.map((attachment) => (
                <img
                  key={attachment.id}
                  className="message-photo"
                  src={attachment.previewDataUrl}
                  alt={attachment.name || 'Photo'}
                />
              ))}
            </div>
          )}
          {reply.body &&
            (message.role === 'assistant' ? (
              <MarkdownMessage streaming={streaming}>{reply.body}</MarkdownMessage>
            ) : (
              <span>{reply.body}</span>
            ))}
          {choices.length > 0 && (
            <div className="reply-chips">
              {choices.map((choice) => (
                <button key={choice} type="button" className="reply-chip" disabled={replyDisabled}
                  onClick={() => onSend(choice)}>
                  {choice}
                </button>
              ))}
            </div>
          )}
          {otherAttachments.length > 0 && (
            <div className="message-attachments">
              {otherAttachments.map((attachment) => (
                <AttachmentChip key={attachment.id} attachment={attachment} />
              ))}
            </div>
          )}
          {message.state === 'failed' && message.error && (
            <div className="message-failure">
              <div className="error">{message.error.message}</div>
              {message.error.retryable && !readOnly && (
                <button className="primary" onClick={() => onRetry(message.id)}>Retry</button>
              )}
            </div>
          )}
          {message.state === 'stopped' && <div className="meta">Stopped</div>}
        </div>
      </div>
    );
  };
  // The printer panel's cards for one message's calls, 8 under its bubble.
  const activityCards = (message: Message) =>
    activitiesOf(message.id).map((activity) => {
      const own = renderActivity?.(activity);
      if (own !== undefined) return <Fragment key={activity.actionId}>{own}</Fragment>;
      return <ToolActivityCard key={activity.actionId} activity={activity} onCancel={onToolCancel} readOnly={readOnly} />;
    });

  return (
    <div className="message-list" role="log" aria-label="Agent conversation"
      ref={listRef} onScroll={handleScroll}>
      <TimelineBlocks blocks={timeline(leadingHistory, leadingChanges, leadingWork)} onWorkCancel={onWorkCancel} readOnly={readOnly} restorePoints={readOnly ? [] : restorePoints} onRevert={readOnly ? undefined : onRevert}
        onDiscussFailure={discussFailure} discussDisabled={readOnly || streamingMessageId !== null} />
      {turns.map((turn) => {
        const turnMessages = messagesOf(turn);
        const turnActivities = turnMessages.flatMap((message) => activitiesOf(message.id));
        const working = turnMessages.some((message) => message.id === streamingMessageId);
        const last = turnMessages[turnMessages.length - 1];
        // The project chat's turn: the person's words, the line for the
        // Agent's work (whenever it worked, or is still working), its
        // replies that have words. The group is one thread item, 8 apart.
        // What follows it -- change runs, work and history entries -- are
        // thread items of their own.
        return (
          <Fragment key={turn.anchor.id}>
            <div className="message-group">
              {bubble(turn.anchor)}
              {printerPanel && activityCards(turn.anchor)}
              {!printerPanel && turn.anchor.role !== 'assistant' && (turnActivities.length > 0 || working) && (
                <TurnActivity activities={turnActivities} working={working} startedAt={turn.anchor.createdAt}
                  endedAt={last.createdAt} onCancel={onToolCancel} readOnly={readOnly} />
              )}
              {turn.replies.map((reply) => (
                <Fragment key={reply.id}>
                  {bubble(reply)}
                  {printerPanel && activityCards(reply)}
                </Fragment>
              ))}
            </div>
            {turnMessages.map((message) => printerBlockViews(message.id))}
            <TimelineBlocks blocks={timeline(historyAfter(turn.anchor.id), changesAfter(turn.anchor.id), workAfter(turn.anchor.id))} onWorkCancel={onWorkCancel} readOnly={readOnly} restorePoints={readOnly ? [] : restorePoints} onRevert={readOnly ? undefined : onRevert}
              onDiscussFailure={discussFailure} discussDisabled={readOnly || streamingMessageId !== null} />
          </Fragment>
        );
      })}
      {endNotice}
    </div>
  );
}
