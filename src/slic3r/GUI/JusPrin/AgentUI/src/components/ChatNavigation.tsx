import { useEffect, useId, useRef, useState } from 'react';
import { ConversationInfo } from '../bridge/protocol';

function ChatIcon() {
  return <span className="jp-icon jp-icon-message-circle" aria-hidden="true" />;
}

function NewChat({ busy, onCreate }: { busy: boolean; onCreate: () => void }) {
  return <button className="chat-icon" aria-label="New chat" title="New chat" disabled={busy} onClick={onCreate}>
    <span className="jp-icon jp-icon-plus" aria-hidden="true" />
  </button>;
}

// The open-pane half of the shell's two-button collapse control. Keeping it
// in the page makes the conversation controls and pane toggle one header row;
// the native project header owns the other half while the pane is hidden.
export function AgentPaneToggle({ onCollapse }: { onCollapse: () => void }) {
  return <button className="chat-icon agent-pane-toggle" aria-label="Hide the Agent panel"
    title="Hide the Agent panel" onClick={onCollapse}>
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <rect x="4" y="5" width="16" height="14" rx="1" />
      <path d="M14 5v14" />
      <path className="agent-pane-toggle-fill" d="M14 5h6v14h-6Z" />
    </svg>
  </button>;
}

export interface MenuItem {
  label: string;
  onSelect: () => void;
  disabled?: boolean;
  danger?: boolean;
}

// A header's ⋯ menu: opens on click, moves with the arrow keys, closes on
// Escape, a pick, or a click anywhere else. `buttonRef` lets the owner put
// focus back on the button once something the menu opened is done.
export function ActionMenu({ label, items, buttonRef }: {
  label: string;
  items: MenuItem[];
  buttonRef?: React.RefObject<HTMLButtonElement>;
}) {
  const [open, setOpen] = useState(false);
  const container = useRef<HTMLDivElement>(null);
  const ownButton = useRef<HTMLButtonElement>(null);
  const button = buttonRef ?? ownButton;

  useEffect(() => {
    if (!open) return;
    container.current?.querySelector<HTMLButtonElement>('[role="menuitem"]')?.focus();
    const dismiss = (event: PointerEvent) => {
      if (!container.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', dismiss);
    return () => document.removeEventListener('pointerdown', dismiss);
  }, [open]);

  return <div className="chat-actions" ref={container} onKeyDown={(event) => {
    if (event.key === 'Escape') { setOpen(false); button.current?.focus(); }
    if (open && ['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
      event.preventDefault();
      const buttons = Array.from(container.current!.querySelectorAll<HTMLButtonElement>('[role="menuitem"]:not(:disabled)'));
      const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
      buttons[event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 :
        (index + (event.key === 'ArrowUp' ? -1 : 1) + buttons.length) % buttons.length]?.focus();
    }
  }} onBlur={(event) => { if (!event.currentTarget.contains(event.relatedTarget)) setOpen(false); }}>
    <button ref={button} className="chat-icon" aria-label={label} title={label}
      aria-haspopup="menu" aria-expanded={open} onClick={() => setOpen(!open)}>
      <span className="jp-icon jp-icon-ellipsis" aria-hidden="true" />
    </button>
    {open && <div className="chat-menu" role="menu" aria-label={label}>
      {items.map((item) => <button key={item.label} role="menuitem" className={item.danger ? 'danger' : undefined}
        disabled={item.disabled} onClick={() => { setOpen(false); item.onSelect(); }}>{item.label}</button>)}
    </div>}
  </div>;
}

// A modal question over the panel: focus starts on its first control and
// stays inside it, and Escape is the same as `onClose` (its Cancel).
export function Dialog({ title, onClose, children, destructive = false }: {
  title: string; onClose: () => void; children: React.ReactNode; destructive?: boolean;
}) {
  const dialog = useRef<HTMLDivElement>(null);
  const titleId = useId();
  const controls = () => Array.from(dialog.current!.querySelectorAll<HTMLElement>('input, button:not(:disabled)'));

  useEffect(() => { controls()[0]?.focus(); }, []);

  return <div className={`chat-dialog-shade${destructive ? ' chat-dialog-shade--destructive' : ''}`}>
    <div ref={dialog} className={`chat-dialog${destructive ? ' chat-dialog--destructive' : ''}`}
      role="dialog" aria-modal="true" aria-labelledby={titleId}
      onKeyDown={(event) => {
        if (event.key === 'Escape') { event.stopPropagation(); onClose(); }
        if (event.key === 'Tab') {
          const all = controls(), first = all[0], last = all[all.length - 1];
          if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
          if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
        }
      }}>
      <h2 id={titleId}>{title}</h2>
      {children}
    </div>
  </div>;
}

interface HeaderProps {
  title: string;
  busy: boolean;
  onBack: () => void;
  onCreate: () => void;
  onRename: (title: string) => void;
  onDelete: () => void;
  onCollapse: () => void;
}

export function ChatHeader({ title, busy, onBack, onCreate, onRename, onDelete, onCollapse }: HeaderProps) {
  const [editing, setEditing] = useState<'rename' | 'delete' | null>(null);
  const [name, setName] = useState(title);
  const menuButton = useRef<HTMLButtonElement>(null);
  const renameInput = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (editing === 'rename') renameInput.current?.select();
  }, [editing]);

  const closeDialog = () => { setEditing(null); menuButton.current?.focus(); };
  const validName = name.trim().length > 0 && Array.from(name.trim()).length <= 120;

  return <>
    <header className="chat-header">
      <button className="chat-back chat-icon" aria-label="Back to chats" title="Back to chats" onClick={onBack}>
        <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" />
      </button>
      <h1 title={title}>{title}</h1>
      <NewChat busy={busy} onCreate={onCreate} />
      <ActionMenu label="Chat actions" buttonRef={menuButton} items={[
        { label: 'Rename', onSelect: () => { setName(title); setEditing('rename'); } },
        { label: 'Delete', danger: true, disabled: busy, onSelect: () => setEditing('delete') },
      ]} />
      <AgentPaneToggle onCollapse={onCollapse} />
    </header>
    {editing && <Dialog title={editing === 'rename' ? 'Rename chat' : 'Delete chat?'} onClose={closeDialog}
      destructive={editing === 'delete'}>
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
    </Dialog>}
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

export function ChatList({ conversations, activeId, busy, agentUnavailable, onSwitch, onCreate, onConfigure, onCollapse }: {
  conversations: ConversationInfo[];
  activeId: string;
  busy: boolean;
  agentUnavailable: boolean;
  onSwitch: (id: string) => void;
  onCreate: () => void;
  onConfigure: () => void;
  onCollapse: () => void;
}) {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => { const timer = window.setInterval(() => setNow(new Date()), 60000); return () => clearInterval(timer); }, []);
  return <section className="chat-list-pane" aria-label="Project chats">
    <header className="chat-list-header"><h1>Chats</h1><NewChat busy={busy} onCreate={onCreate} />
      <AgentPaneToggle onCollapse={onCollapse} /></header>
    <div className="chat-list-scroll">
      {conversations.length === 0 && <p className="chat-list-empty">No chats yet. Start a new chat about this project.</p>}
      {conversations.map((chat) => {
        const preview = (chat.preview || 'No messages yet').replace(/\[([^\]]+)\]\([^)]*\)/g, '$1').replace(/[*#`_]/g, '').replace(/\s+/g, ' ');
        return <button key={chat.id} className={`chat-list-row${chat.id === activeId ? ' selected' : ''}`}
          aria-current={chat.id === activeId ? 'true' : undefined} aria-label={`Open chat: ${chat.title}`}
          onClick={() => onSwitch(chat.id)}>
          <span className="chat-list-row-top"><ChatIcon /><strong title={chat.title}>{chat.title}</strong>
            <time dateTime={chat.updatedAt || chat.createdAt} title={chat.updatedAt || chat.createdAt}>{chatTimestamp(chat.updatedAt || chat.createdAt, now)}</time></span>
          <span className="chat-list-preview">{preview}</span>
        </button>;
      })}
    </div>
    <footer className="chat-list-footer">
      <button className="configure-agent" onClick={onConfigure} disabled={busy}>
        <span className="jp-icon jp-icon-settings" aria-hidden="true" />
        <span>
          <strong>{agentUnavailable ? 'Set up the agent' : 'Configure Agent'}</strong>
          <small>{agentUnavailable ? 'Connect an Agent to continue this conversation' : 'Adjust defaults, models, and slice preferences'}</small>
        </span>
      </button>
    </footer>
  </section>;
}
