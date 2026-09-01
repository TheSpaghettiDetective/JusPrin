// Conversation transcript with stable scroll anchoring: the list follows new
// content only while the reader is at the bottom; scrolling up to reread
// pins the viewport until they return to the bottom. Tool activity cards
// render beneath the assistant message that proposed them, and revision
// markers of this conversation render where they happened.

import { useLayoutEffect, useRef, useState } from 'react';
import { AttachmentInfo, RevisionInfo, ToolActivityInfo } from '../bridge/protocol';
import { Message } from '../state/store';
import { AttachmentChip } from './AttachmentChip';
import { RevisionMarker } from './RevisionMarker';
import { ToolActivityCard } from './ToolActivityCard';

interface Props {
  messages: Message[];
  attachments: AttachmentInfo[];
  streamingMessageId: string | null;
  toolActivities: ToolActivityInfo[];
  revisions: RevisionInfo[]; // already filtered to this conversation
  onRetry: (messageId: string) => void;
  onToolDecision: (actionId: string, decision: 'approve' | 'reject') => void;
  onToolCancel: (actionId: string) => void;
  onRevert: (revisionId: string) => void;
}

export function MessageList({
  messages,
  attachments,
  streamingMessageId,
  toolActivities,
  revisions,
  onRetry,
  onToolDecision,
  onToolCancel,
  onRevert,
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
  }, [messages, toolActivities, revisions, followBottom]);

  const markersAfter = (messageId: string) => revisions.filter((r) => r.afterMessageId === messageId);
  const leadingMarkers = revisions.filter(
    (r) => r.afterMessageId === '' || !messages.some((m) => m.id === r.afterMessageId),
  );

  return (
    <div className="message-list" role="log" aria-label="Agent conversation" ref={listRef} onScroll={handleScroll}>
      {leadingMarkers.map((revision) => (
        <RevisionMarker key={revision.id} revision={revision} onRevert={onRevert} />
      ))}
      {messages.length === 0 && (
        <div className="notice">
          <h2>Ask the Agent about your print</h2>
          <p>
            The Agent can describe the open project, the plates and objects on them, the current selection, and the
            printer setup — or duplicate the selected object with your approval.
          </p>
        </div>
      )}
      {messages.map((message) => (
        <div key={message.id} className="message-group">
          <div className={`message ${message.role}`}>
            {message.text && (
              <span className={message.id === streamingMessageId ? 'streaming-cursor' : undefined}>{message.text}</span>
            )}
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
          {toolActivities
            .filter((activity) => activity.correlationId === message.id)
            .map((activity) => (
              <ToolActivityCard
                key={activity.actionId}
                activity={activity}
                onDecision={onToolDecision}
                onCancel={onToolCancel}
              />
            ))}
          {markersAfter(message.id).map((revision) => (
            <RevisionMarker key={revision.id} revision={revision} onRevert={onRevert} />
          ))}
        </div>
      ))}
    </div>
  );
}
