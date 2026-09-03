import { useEffect, useRef, useState } from 'react';
import { ConversationInfo } from '../bridge/protocol';

function ChatIcon() {
  return <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M21 11.5a8.5 8.5 0 0 1-8.5 8.5 9 9 0 0 1-4-.9L3 21l1.9-5.5a9 9 0 0 1-.9-4A8.5 8.5 0 0 1 12.5 3h.5a8.5 8.5 0 0 1 8 8v.5Z" /></svg>;
}

function NewChat({ busy, onCreate }: { busy: boolean; onCreate: () => void }) {
  return <button className="chat-icon" aria-label="New chat" title="New chat" disabled={busy} onClick={onCreate}>
    <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5v14M5 12h14" /></svg>
  </button>;
}

interface HeaderProps {
  title: string;
  busy: boolean;
  onBack: () => void;
  onCreate: () => void;
  onRename: (title: string) => void;
  onDelete: () => void;
}

export function ChatHeader({ title, busy, onBack, onCreate, onRename, onDelete }: HeaderProps) {
  const [menuOpen, setMenuOpen] = useState(false);
  const [editing, setEditing] = useState<'rename' | 'delete' | null>(null);
  const [name, setName] = useState(title);
  const actions = useRef<HTMLDivElement>(null);
  const menuButton = useRef<HTMLButtonElement>(null);
  const dialog = useRef<HTMLDivElement>(null);
  const renameInput = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!menuOpen) return;
    actions.current?.querySelector<HTMLButtonElement>('[role="menuitem"]')?.focus();
    const dismiss = (event: PointerEvent) => {
      if (!actions.current?.contains(event.target as Node)) setMenuOpen(false);
    };
    document.addEventListener('pointerdown', dismiss);
    return () => document.removeEventListener('pointerdown', dismiss);
  }, [menuOpen]);

  useEffect(() => {
    if (editing === 'rename') { renameInput.current?.focus(); renameInput.current?.select(); }
    if (editing === 'delete') dialog.current?.querySelector<HTMLButtonElement>('button')?.focus();
  }, [editing]);

  const closeDialog = () => { setEditing(null); menuButton.current?.focus(); };
  const validName = name.trim().length > 0 && Array.from(name.trim()).length <= 120;

  return <>
    <header className="chat-header">
      <button className="chat-back chat-icon" aria-label="Back to chats" title="Back to chats" onClick={onBack}>
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="m15 5-8 7 8 7" /></svg>
      </button>
      <h1 title={title}>{title}</h1>
      <NewChat busy={busy} onCreate={onCreate} />
      <div className="chat-actions" ref={actions} onKeyDown={(event) => {
        if (event.key === 'Escape') { setMenuOpen(false); menuButton.current?.focus(); }
        if (menuOpen && ['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
          event.preventDefault();
          const items = Array.from(actions.current!.querySelectorAll<HTMLButtonElement>('[role="menuitem"]:not(:disabled)'));
          const index = items.indexOf(document.activeElement as HTMLButtonElement);
          items[event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1 :
            (index + (event.key === 'ArrowUp' ? -1 : 1) + items.length) % items.length]?.focus();
        }
      }} onBlur={(event) => { if (!event.currentTarget.contains(event.relatedTarget)) setMenuOpen(false); }}>
        <button ref={menuButton} className="chat-icon" aria-label="Chat actions" title="Chat actions"
          aria-haspopup="menu" aria-expanded={menuOpen} onClick={() => setMenuOpen(!menuOpen)}>
          <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="5" cy="12" r="1" /><circle cx="12" cy="12" r="1" /><circle cx="19" cy="12" r="1" /></svg>
        </button>
        {menuOpen && <div className="chat-menu" role="menu" aria-label="Chat actions">
          <button role="menuitem" onClick={() => { setName(title); setEditing('rename'); setMenuOpen(false); }}>Rename</button>
          <button role="menuitem" className="danger" disabled={busy}
            onClick={() => { setEditing('delete'); setMenuOpen(false); }}>Delete</button>
        </div>}
      </div>
    </header>
    {editing && <div className="chat-dialog-shade">
      <div ref={dialog} className="chat-dialog" role="dialog" aria-modal="true" aria-labelledby="chat-dialog-title"
        onKeyDown={(event) => {
          if (event.key === 'Escape') { event.stopPropagation(); closeDialog(); }
          if (event.key === 'Tab') {
            const controls = Array.from(dialog.current!.querySelectorAll<HTMLElement>('input, button:not(:disabled)'));
            const first = controls[0], last = controls[controls.length - 1];
            if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
            if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
          }
        }}>
        <h2 id="chat-dialog-title">{editing === 'rename' ? 'Rename chat' : 'Delete chat?'}</h2>
        {editing === 'rename' ? <form onSubmit={(event) => {
          event.preventDefault();
          if (validName) { onRename(name.trim()); closeDialog(); }
        }}>
          <label htmlFor="chat-title-input">Chat title</label>
          <input id="chat-title-input" ref={renameInput} value={name} onChange={(event) => setName(event.target.value)} />
          {!validName && <p role="alert">Enter a title of 1–120 characters.</p>}
          <div className="chat-dialog-buttons"><button type="button" onClick={closeDialog}>Cancel</button>
            <button className="primary" disabled={!validName} type="submit">Save</button></div>
        </form> : <>
          <p>Delete “{title}” and its messages? Your model, builds, and print history will stay. This cannot be undone.</p>
          <div className="chat-dialog-buttons"><button onClick={closeDialog}>Cancel</button>
            <button className="danger" disabled={busy} onClick={() => { onDelete(); closeDialog(); }}>Delete</button></div>
        </>}
      </div>
    </div>}
  </>;
}

export function chatTimestamp(timestamp: string, now = new Date()): string {
  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) return '';
  const day = (value: Date) => Date.UTC(value.getFullYear(), value.getMonth(), value.getDate());
  const days = Math.max(0, Math.round((day(now) - day(date)) / 86400000));
  if (days === 0) return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false });
  if (days === 1) return 'Yesterday';
  if (days < 7) return `${days} days ago`;
  if (days < 14) return 'Last week';
  if (days < 28) return `${Math.floor(days / 7)} weeks ago`;
  return date.toLocaleDateString([], { month: 'short', day: 'numeric', ...(date.getFullYear() !== now.getFullYear() ? { year: 'numeric' } : {}) });
}

export function ChatList({ conversations, activeId, busy, onSwitch, onCreate, onConfigure }: {
  conversations: ConversationInfo[];
  activeId: string;
  busy: boolean;
  onSwitch: (id: string) => void;
  onCreate: () => void;
  onConfigure: () => void;
}) {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => { const timer = window.setInterval(() => setNow(new Date()), 60000); return () => clearInterval(timer); }, []);
  return <section className="chat-list-pane" aria-label="Project chats">
    <header className="chat-list-header"><h1>Chats</h1><NewChat busy={busy} onCreate={onCreate} /></header>
    <div className="chat-list-scroll">
      {conversations.length === 0 && <p className="chat-list-empty">No chats yet. Start a new chat about this project.</p>}
      {conversations.map((chat) => {
        const preview = (chat.preview || 'No messages yet').replace(/\[([^\]]+)\]\([^)]*\)/g, '$1').replace(/[*#`_]/g, '').replace(/\s+/g, ' ');
        return <button key={chat.id} className={`chat-list-row${chat.id === activeId ? ' selected' : ''}`}
          aria-current={chat.id === activeId ? 'true' : undefined} aria-label={`Open chat: ${chat.title}`}
          disabled={busy && chat.id !== activeId} onClick={() => onSwitch(chat.id)}>
          <span className="chat-list-row-top"><ChatIcon /><strong title={chat.title}>{chat.title}</strong>
            <time dateTime={chat.updatedAt || chat.createdAt} title={chat.updatedAt || chat.createdAt}>{chatTimestamp(chat.updatedAt || chat.createdAt, now)}</time></span>
          <span className="chat-list-preview">{preview}</span>
        </button>;
      })}
    </div>
    <footer className="chat-list-footer">
      <button className="configure-agent" onClick={onConfigure} disabled={busy}>
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="m9 3-.5 3-2 1-2.5-1-2 3 2 2v2l-2 2 2 3 2.5-1 2 1 .5 3h4l.5-3 2-1 2.5 1 2-3-2-2v-2l2-2-2-3-2.5 1-2-1L13 3Z" /><circle cx="11" cy="12" r="3" /></svg>
        <span><strong>Configure Agent</strong><small>Adjust defaults, models, and slice preferences</small></span>
      </button>
    </footer>
  </section>;
}
