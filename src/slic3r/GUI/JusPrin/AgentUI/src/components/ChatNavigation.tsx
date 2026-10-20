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
  // The option now in effect, marked with a tick (Menu/Row's Current state).
  current?: boolean;
  // Distinguishes rows whose labels repeat, as models of two providers can.
  key?: string;
  // What a search of the menu matches the row on; its label when absent.
  matches?: string[];
}

// A heading over the rows after it (Menu/Section Title): not a stop for the
// keyboard and not clickable. `detail` follows the name while the menu is
// not being searched ("OpenRouter · 312 models").
export interface MenuSection {
  section: string;
  detail?: string;
}

// A search field at the top of a long menu (the model menu's): `label` is
// its placeholder and name, `noMatch` what the menu says when nothing fits.
export interface MenuSearch {
  label: string;
  noMatch: (query: string) => string;
}

const MENU_ROWS = '[role="menuitem"]:not(:disabled), [role="menuitemradio"]:not(:disabled)';

function isSection(entry: MenuItem | MenuSection): entry is MenuSection {
  return 'section' in entry;
}

const rowKey = (item: MenuItem) => item.key ?? item.label;

// The entries a query leaves: the rows that match, ignoring case, under the
// headings of their groups; a heading whose rows all drop out goes too.
export function filterMenu(items: (MenuItem | MenuSection)[], query: string): (MenuItem | MenuSection)[] {
  const wanted = query.trim().toLowerCase();
  if (!wanted) return items;
  const kept: (MenuItem | MenuSection)[] = [];
  let heading: MenuSection | undefined;
  for (const entry of items) {
    if (isSection(entry)) { heading = entry; continue; }
    if (!(entry.matches ?? [entry.label]).some((name) => name.toLowerCase().includes(wanted))) continue;
    if (heading) { kept.push(heading); heading = undefined; }
    kept.push(entry);
  }
  return kept;
}

// A button that opens a menu: the header's ⋯ menu, the composer's + menu,
// and the composer's model and thinking pickers, which show their choice as
// `text` with a chevron, after the menu's name when `prefixed` (which a
// narrow pane hides). The composer's open upward because they sit at the bottom of
// the pane. A menu opens on click, moves with the arrow keys, closes on
// Escape, a pick, or a click anywhere else. `buttonRef` lets the owner put
// focus back on the button once something the menu opened is done.
//
// With `search`, the menu starts with a field that has the focus: typing
// filters the rows, the first match is highlighted, Up and Down move the
// highlight, Enter picks it, and Escape clears the field before it closes
// the menu.
export function ActionMenu({ label, items, buttonRef, icon = 'ellipsis', text, prefixed = false, above = false,
  disabled = false, search, className, appearance = 'picker' }: {
  label: string;
  items: (MenuItem | MenuSection)[];
  buttonRef?: React.RefObject<HTMLButtonElement>;
  icon?: 'ellipsis' | 'plus';
  text?: string;
  prefixed?: boolean;
  above?: boolean;
  disabled?: boolean;
  search?: MenuSearch;
  className?: string;
  // With `text`: the composer's picker (a bare label with a chevron), or the
  // design system's Select (a 26 DIP field with a stroke, as wide as its
  // row, whose menu opens under its left edge).
  appearance?: 'picker' | 'select';
}) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [activeKey, setActiveKey] = useState<string>();
  const container = useRef<HTMLDivElement>(null);
  const field = useRef<HTMLInputElement>(null);
  const ownButton = useRef<HTMLButtonElement>(null);
  const button = buttonRef ?? ownButton;
  const menuId = useId();

  const shownEntries = search ? filterMenu(items, query) : items;
  const rows = shownEntries.filter((entry): entry is MenuItem => !isSection(entry) && !entry.disabled);
  // The row Enter picks: the current choice until something is typed, then
  // the first match, until the arrows move it.
  const active = rows.find((row) => rowKey(row) === activeKey) ?? rows[0];
  const rowId = (item: MenuItem) => `${menuId}-${shownEntries.indexOf(item)}`;

  useEffect(() => {
    if (!open) return;
    if (field.current) field.current.focus();
    // The current choice when there is one, else the first row.
    else (container.current?.querySelector<HTMLButtonElement>('[aria-checked="true"]') ??
      container.current?.querySelector<HTMLButtonElement>(MENU_ROWS))?.focus();
    const dismiss = (event: PointerEvent) => {
      if (!container.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', dismiss);
    return () => document.removeEventListener('pointerdown', dismiss);
  }, [open]);

  useEffect(() => {
    if (search && open && active) document.getElementById(rowId(active))?.scrollIntoView({ block: 'nearest' });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active && rowKey(active), open]);

  // A menu closes when its button is disabled under it, as when a reply
  // starts, and stays closed when the button comes back.
  useEffect(() => { if (disabled) setOpen(false); }, [disabled]);
  const shown = open && !disabled;

  const current = items.find((entry): entry is MenuItem => !isSection(entry) && !!entry.current);
  const restart = () => { setQuery(''); setActiveKey(current && rowKey(current)); };
  const toggle = () => { restart(); setOpen(!open); };
  const pick = (item: MenuItem) => { setOpen(false); item.onSelect(); };
  const name = text === undefined ? label : `${label}: ${text}`;

  return <div className={['chat-actions', above && 'chat-actions--above', appearance === 'select' && 'chat-actions--select', className]
    .filter(Boolean).join(' ')}
    ref={container} onKeyDown={(event) => {
    if (event.key === 'Escape') { setOpen(false); button.current?.focus(); }
    // The search field keeps its own keys; Home and End move its cursor.
    if (shown && event.target !== field.current && ['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
      event.preventDefault();
      const buttons = Array.from(container.current!.querySelectorAll<HTMLButtonElement>(MENU_ROWS));
      const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
      buttons[event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 :
        (index + (event.key === 'ArrowUp' ? -1 : 1) + buttons.length) % buttons.length]?.focus();
    }
  }} onBlur={(event) => { if (!event.currentTarget.contains(event.relatedTarget)) setOpen(false); }}>
    {text === undefined
      ? <button ref={button} className="chat-icon" aria-label={label} title={label} disabled={disabled}
          aria-haspopup="menu" aria-expanded={shown} onClick={toggle}>
          <span className={`jp-icon jp-icon-${icon}`} aria-hidden="true" />
        </button>
      // Named, and titled, in full even when a narrow pane shortens it.
      : <button ref={button} className={appearance === 'select' ? 'jp-select' : 'composer-picker'} disabled={disabled}
          aria-label={name} title={name} aria-haspopup="menu" aria-expanded={shown} onClick={toggle}>
          <span className="composer-picker-text">
            {prefixed && <span className="composer-picker-prefix">{label}: </span>}{text}
          </span>
          <span className="jp-icon jp-icon-chevron-down" aria-hidden="true" />
        </button>}
    {shown && <div className={search ? 'chat-menu chat-menu--search' : 'chat-menu'}>
      {search && <div className="chat-menu-search">
        <label className="field-search">
          <span className="jp-icon jp-icon-search" aria-hidden="true" />
          <input ref={field} type="search" placeholder={search.label} aria-label={search.label} value={query}
            aria-controls={rows.length > 0 ? menuId : undefined}
            aria-activedescendant={active ? rowId(active) : undefined}
            onChange={(event) => {
              setQuery(event.target.value);
              setActiveKey(undefined);
            }}
            onKeyDown={(event) => {
              if (event.key === 'Escape' && query) {
                event.stopPropagation();
                restart();
              } else if ((event.key === 'ArrowDown' || event.key === 'ArrowUp') && rows.length > 0) {
                event.preventDefault();
                const index = active ? rows.indexOf(active) : -1;
                setActiveKey(rowKey(rows[(index + (event.key === 'ArrowUp' ? -1 : 1) + rows.length) % rows.length]));
              } else if (event.key === 'Enter' && active) {
                event.preventDefault();
                pick(active);
                button.current?.focus();
              }
            }} />
        </label>
      </div>}
      {search && rows.length === 0
        ? <p className="chat-menu-empty" role="status">{search.noMatch(query.trim())}</p>
        // A press on a row must not move the focus: WebKit focuses nothing
        // on a click, so the focused row would blur to nothing, closing the
        // menu before the click arrived.
        : <div id={menuId} className="chat-menu-list" role="menu" aria-label={label}
            onMouseDown={(event) => event.preventDefault()}>
          {shownEntries.map((item, index) => isSection(item)
            ? <div key={`section-${index}`} className="chat-menu-section" role="presentation">
                {item.detail && !query.trim() ? `${item.section} · ${item.detail}` : item.section}
              </div>
            : <button key={rowKey(item)} id={rowId(item)}
                role={item.current === undefined ? 'menuitem' : 'menuitemradio'}
                className={[item.danger && 'danger', search && item === active && 'active'].filter(Boolean).join(' ') || undefined}
                aria-checked={item.current === undefined ? undefined : item.current}
                disabled={item.disabled} onClick={() => pick(item)}
                onMouseEnter={search && !item.disabled ? () => setActiveKey(rowKey(item)) : undefined}>
                <span className="chat-menu-label">{item.label}</span>
                {item.current && <span className="jp-icon jp-icon-check" aria-hidden="true" />}
              </button>)}
        </div>}
    </div>}
  </div>;
}

// A modal question over the panel: focus starts on its first control and
// stays inside it, and Escape is the same as `onClose` (its Cancel).
export function Dialog({ title, onClose, children }: {
  title: string; onClose: () => void; children: React.ReactNode;
}) {
  const dialog = useRef<HTMLDivElement>(null);
  const titleId = useId();
  const controls = () => Array.from(dialog.current!.querySelectorAll<HTMLElement>('input, button:not(:disabled)'));

  useEffect(() => { controls()[0]?.focus(); }, []);

  return <div className="chat-dialog-shade">
    <div ref={dialog} className="chat-dialog"
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
    {editing && <Dialog title={editing === 'rename' ? 'Rename chat' : 'Delete chat?'} onClose={closeDialog}>
      {editing === 'rename' ? <form onSubmit={(event) => {
        event.preventDefault();
        if (validName) { onRename(name.trim()); closeDialog(); }
      }}>
        <div className="chat-dialog-field">
          <label htmlFor="chat-title-input">Chat title</label>
          <input id="chat-title-input" ref={renameInput} value={name} aria-invalid={!validName}
            aria-describedby={validName ? undefined : 'chat-title-error'} onChange={(event) => setName(event.target.value)} />
          {!validName && <p id="chat-title-error" className="chat-dialog-field-error" role="alert">Enter a title of 1–120 characters.</p>}
        </div>
        <div className="chat-dialog-buttons"><button type="button" onClick={closeDialog}>Cancel</button>
          <button className="primary" disabled={!validName} type="submit">Save</button></div>
      </form> : <>
        <p>Delete ‘{title}’ and its messages? Your model, builds, and print history will stay. This cannot be undone.</p>
        {busy && <p className="chat-dialog-note">Wait for the current action to finish before deleting.</p>}
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
