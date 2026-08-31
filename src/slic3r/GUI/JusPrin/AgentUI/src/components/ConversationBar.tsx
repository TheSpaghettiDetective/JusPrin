// Conversation switcher. Conversations are views into one shared project
// timeline — creating one never branches the manufacturing history — so the
// bar is a flat list plus a New button. The host refuses switches while a
// reply streams; the bar disables itself to make that visible.

import { ConversationInfo } from '../bridge/protocol';

interface Props {
  conversations: ConversationInfo[];
  activeConversationId: string;
  busy: boolean;
  onSwitch: (conversationId: string) => void;
  onCreate: () => void;
}

export function ConversationBar({ conversations, activeConversationId, busy, onSwitch, onCreate }: Props) {
  if (conversations.length === 0) return null;
  return (
    <div className="conversation-bar" role="tablist" aria-label="Conversations">
      {conversations.map((conversation) => (
        <button
          key={conversation.id}
          role="tab"
          aria-selected={conversation.id === activeConversationId}
          className={conversation.id === activeConversationId ? 'conversation-tab active' : 'conversation-tab'}
          disabled={busy || conversation.id === activeConversationId}
          onClick={() => onSwitch(conversation.id)}
        >
          {conversation.title}
        </button>
      ))}
      <button className="conversation-new" aria-label="New conversation" disabled={busy} onClick={onCreate}>
        +
      </button>
    </div>
  );
}
