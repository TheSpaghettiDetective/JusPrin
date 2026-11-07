// JusPrin's Preferences window (canvas "JusPrin v2", Preferences, decided
// 2026-10-21): a list of sections on the left and the chosen one beside it.
// The app reads and writes every setting, under the keys OrcaSlicer's own
// dialog uses, and sends what is stored after each change; the page only
// says what to change. Account holds the JusPrin account and is where every
// surface that finds no AI opens it; BYOAI (Bring Your Own AI) holds the
// person's own API keys and AI tools, with no JusPrin account in it. Both draw
// the connection rows of AgentConnection.tsx.

import { useEffect, useState } from 'react';
import {
  AccountStatusPayload,
  CreditAccount,
  DragAction,
  McpCatalogPayload,
  McpPreviewPayload,
  McpStatusPayload,
  McpToolInfo,
  PreferencesPayload,
  PreferencesSection,
  SavedKeyInfo,
  SetupStatusPayload,
} from '../bridge/protocol';
import { AccountBox } from './AccountBox';
import { AccountRow, ProviderKeys, ToolGroup } from './AgentConnection';
import { balanceRows } from './BalancePanel';
import { creditCount, creditNumber, TIER_NAMES } from '../credits';
import { MarkdownMessage } from './MarkdownMessage';
import { Progress } from './Progress';
import { ActionMenu } from './ChatNavigation';
import { SetupLocalTool } from './SetupLocalTool';

// A hint is a second, smaller line under the label, for a label that needs it.
const SECTIONS: { id: PreferencesSection; label: string; hint?: string; icon: string }[] = [
  { id: 'account', label: 'Account', icon: 'user' },
  { id: 'agent', label: 'BYOAI', hint: 'Bring Your Own AI', icon: 'key' },
  { id: 'appearance', label: 'Appearance', icon: 'palette' },
  { id: 'view3d', label: '3D view', icon: 'box' },
  { id: 'projects', label: 'Projects & files', icon: 'folder' },
  { id: 'updates', label: 'Updates', icon: 'refresh-cw' },
];

// What each mouse button's drag can do, in OrcaSlicer's order.
const DRAG_ACTIONS: { value: DragAction; label: string }[] = [
  { value: 'rotate', label: 'Turn the view' },
  { value: 'pan', label: 'Pan' },
  { value: 'none', label: 'Nothing' },
];

export interface PreferencesActions {
  // One setting, by the name the app's settings use.
  set: (name: string, value: string | boolean) => void;
  // A command: choose_downloads, show_versions, open_update, check_updates,
  // skip_update (with the version), restart, orca_preferences.
  run: (action: string, details?: Record<string, string>) => void;
  checkKey: (provider: string, apiKey: string) => void;
  cancelCheck: () => void;
  removeKey: (provider: string) => void;
  refreshTools: () => void;
  previewTool: (toolId: string) => void;
  connectTool: (toolId: string) => void;
  revealTool: (toolId: string) => void;
  signIn: () => void;
  cancelSignIn: () => void;
  getMoreCredits: () => void;
  refreshAccountBox: () => void;
}

export interface PreferencesProps {
  preferences: PreferencesPayload;
  setup: SetupStatusPayload;
  savedKeys: SavedKeyInfo[];
  account: AccountStatusPayload;
  // The signed-in account's credits and usage; null with no account or
  // before it answers.
  creditAccount: CreditAccount | null;
  chatTitles: Record<string, string>;
  // The box the server fills near the top of Account; null for none.
  accountBox: string | null;
  mcpCatalog: McpCatalogPayload | null;
  mcpPreview: McpPreviewPayload | null;
  mcpStatus: McpStatusPayload;
  actions: PreferencesActions;
  // Done on an AI tool, for a window opened to close once one is connected.
  onToolDone?: () => void;
}

// The JusPrin account row.
function JusPrinAccount({ savedKeys, account, actions }: {
  savedKeys: SavedKeyInfo[]; account: AccountStatusPayload; actions: PreferencesActions;
}) {
  return (
    <AccountRow account={savedKeys.find((key) => key.provider === 'jusprin')} status={account} onSignIn={actions.signIn}
      onCancel={actions.cancelSignIn} onSignOut={() => actions.removeKey('jusprin')} />
  );
}

function Row({ title, detail, children }: { title: string; detail?: React.ReactNode; children?: React.ReactNode }) {
  return (
    <div className="prefs-row">
      <div className="prefs-row-copy">
        <span className="prefs-row-title">{title}</span>
        {detail && <span className="prefs-row-detail">{detail}</span>}
      </div>
      {children}
    </div>
  );
}

function Check({ title, detail, checked, onChange }: {
  title: string; detail: string; checked: boolean; onChange: (checked: boolean) => void;
}) {
  return (
    <div className="prefs-row">
      <button type="button" role="checkbox" aria-checked={checked} className="printer-form-check prefs-check"
        onClick={() => onChange(!checked)}>
        <span className="printer-form-check-box">{checked && <span className="jp-icon jp-icon-check" aria-hidden="true" />}</span>
        <span className="printer-form-check-copy">
          <span>{title}</span>
          <span className="printer-form-check-detail">{detail}</span>
        </span>
      </button>
    </div>
  );
}

function Segmented<T extends string>({ label, options, value, onChange }: {
  label: string; options: { value: T; label: string }[]; value: T; onChange: (value: T) => void;
}) {
  return (
    <div className="jp-segmented prefs-segmented" role="radiogroup" aria-label={label}>
      {options.map((option) => (
        <button key={option.value} type="button" role="radio" aria-checked={option.value === value}
          className={option.value === value ? 'jp-segment selected' : 'jp-segment'} onClick={() => onChange(option.value)}>
          {option.label}
        </button>
      ))}
    </div>
  );
}

function Select<T extends string>({ label, options, value, onChange, searchable = false }: {
  label: string; options: { value: T; label: string }[]; value: T; onChange: (value: T) => void; searchable?: boolean;
}) {
  return (
    <div className="prefs-select">
      <ActionMenu
        appearance="select"
        label={label}
        text={options.find((option) => option.value === value)?.label ?? value}
        items={options.map((option) => ({
          key: option.value,
          label: option.label,
          current: option.value === value,
          onSelect: () => { if (option.value !== value) onChange(option.value); },
        }))}
        search={searchable ? { label: `Find a ${label.toLowerCase()}`, noMatch: (query) => `No ${label.toLowerCase()} matches “${query}”` } : undefined}
      />
    </div>
  );
}

function Group({ title, tag, children }: { title: string; tag?: string; children: React.ReactNode }) {
  return (
    <div className="prefs-group">
      <div className="prefs-group-head">
        <h3 className="prefs-group-title">{title}</h3>
        {tag && <span className="prefs-group-tag">{tag}</span>}
      </div>
      {children}
    </div>
  );
}

// A gateway day, YYYY-MM-DD, as "5 Nov". The day is the gateway's, so it is
// read as UTC: a local reading would move it a day west of Greenwich.
function dayLabel(date: string): string {
  return new Date(`${date}T00:00:00Z`).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', timeZone: 'UTC' });
}

const USAGE_DAYS = 30;
// Rows shown before "See all activity".
const ACTIVITY_ROWS = 5;

// The 30 days the usage covers, oldest first, today last.
function usageDays(now: Date): string[] {
  return Array.from({ length: USAGE_DAYS }, (_, index) =>
    new Date(now.getTime() - (USAGE_DAYS - 1 - index) * 86_400_000).toISOString().slice(0, 10));
}

// The account's plan, credits and usage (boards 4a to 4c on the canvas's
// "Agent setup & Preferences" page): a bar for each kind of credit as the
// composer's panel draws them, then usage per day with totals per chat.
function AccountCredits({ account, chatTitles, onGetMore }: {
  account: CreditAccount; chatTitles: Record<string, string>; onGetMore: () => void;
}) {
  const [allActivity, setAllActivity] = useState(false);
  const usage = account.usage_30d;
  const byDay = new Map(usage.days.map((day) => [day.date, day.credits]));
  const days = usageDays(new Date());
  const most = Math.max(0, ...days.map((day) => byDay.get(day) ?? 0));
  const activity = allActivity ? usage.activity : usage.activity.slice(0, ACTIVITY_ROWS);
  return (
    <>
      <div className="prefs-credits-plan">
        <div className="prefs-row-copy">
          <span className="prefs-credits-plan-name">{account.plan?.name ?? 'Pay as you go'}</span>
          <span className="prefs-intro">
            {account.plan
              ? (account.plan.renews_at ? `Renews ${dayLabel(account.plan.renews_at.slice(0, 10))}.` : '')
              : 'Only messages you send cost credits.'}
          </span>
        </div>
        <button type="button" className="primary" onClick={onGetMore}>Get more credits</button>
      </div>
      <div className="prefs-credits">
        <div className="prefs-credits-card">
          <strong className="prefs-credits-total">{creditCount(account.credits.total)} left</strong>
          {balanceRows(account).map((row) => (
            <div key={row.key} className="credit-lot prefs-credits-lot">
              <div className="credit-lot-line">
                <strong>{row.name}</strong>
                <span>{creditNumber(row.left)} of {creditNumber(row.of)} left</span>
              </div>
              <div className="credit-lot-bar">
                <div className="credit-lot-fill" style={{ width: `${row.of > 0 ? Math.min(row.left / row.of, 1) * 100 : 0}%` }} />
              </div>
              <span className="credit-lot-date">{row.date}</span>
            </div>
          ))}
        </div>
        <p className="prefs-note">Expiring credits are used first.</p>
      </div>
      <div className="prefs-usage" aria-label="Usage" role="group">
        <h3 className="prefs-group-title">Usage</h3>
        <div className="prefs-row-copy">
          <span className="prefs-credits-plan-name">{creditCount(usage.credits)}</span>
          <span className="prefs-note">in the last 30 days</span>
        </div>
        <div className="prefs-usage-chart">
          <div className="prefs-usage-bars">
            {days.map((day) => {
              const credits = byDay.get(day) ?? 0;
              return <div key={day} className="prefs-usage-bar" title={`${dayLabel(day)}: ${creditCount(credits)}`}
                style={{ height: `${most > 0 ? (credits / most) * 100 : 0}%` }} />;
            })}
            {most > 0 && <span className="prefs-usage-most">{creditNumber(most)}</span>}
          </div>
          <div className="prefs-usage-axis">
            <span>{dayLabel(days[0])}</span>
            <span>{dayLabel(days[Math.floor(USAGE_DAYS / 2)])}</span>
            <span>{dayLabel(days[USAGE_DAYS - 1])}</span>
          </div>
        </div>
        {usage.activity.length > 0 ? (
          <table className="prefs-usage-table">
            <thead>
              <tr><th>Date</th><th>Chat</th><th>Tier</th><th>Messages</th><th>Credits</th></tr>
            </thead>
            <tbody>
              {activity.map((row, index) => 'kind' in row ? (
                <tr key={index}>
                  <td>{dayLabel(row.date)}</td><td>{row.title}</td><td /><td /><td>+{creditNumber(row.credits)}</td>
                </tr>
              ) : (
                <tr key={index}>
                  <td>{dayLabel(row.date)}</td>
                  <td>{(row.chat_id !== null && chatTitles[row.chat_id]) || 'A chat not on this computer'}</td>
                  <td>{TIER_NAMES[row.tier]}</td>
                  <td>{creditNumber(row.messages)}</td>
                  <td>{creditNumber(row.credits)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : (
          <p className="prefs-note">No messages in the last 30 days.</p>
        )}
        {!allActivity && usage.activity.length > ACTIVITY_ROWS && (
          <button type="button" className="link prefs-inline-link" onClick={() => setAllActivity(true)}>See all activity</button>
        )}
      </div>
    </>
  );
}

function AccountSection({ savedKeys, account, creditAccount, chatTitles, accountBox, actions, onAgent }: {
  savedKeys: SavedKeyInfo[]; account: AccountStatusPayload; creditAccount: CreditAccount | null;
  chatTitles: Record<string, string>; accountBox: string | null; actions: PreferencesActions; onAgent: () => void;
}) {
  const signedIn = savedKeys.some((key) => key.provider === 'jusprin');
  // On the person's own API key there is no account to offer anything for.
  const ownKey = !signedIn && savedKeys.length > 0;
  // The server's box is asked for when Account opens, after a sign-in or
  // out, and on coming back from the browser, where a pack may have been
  // bought.
  useEffect(() => {
    if (ownKey)
      return;
    actions.refreshAccountBox();
    const refresh = () => actions.refreshAccountBox();
    window.addEventListener('focus', refresh);
    return () => window.removeEventListener('focus', refresh);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [signedIn, ownKey]);
  return (
    <section className="prefs-section prefs-account" aria-label="Account">
      <h2 className="prefs-title">Account</h2>
      {!ownKey && accountBox && <AccountBox key={accountBox} html={accountBox} />}
      <JusPrinAccount savedKeys={savedKeys} account={account} actions={actions} />
      {signedIn && creditAccount && (
        <AccountCredits account={creditAccount} chatTitles={chatTitles} onGetMore={actions.getMoreCredits} />
      )}
      <div className="prefs-callout">
        <span className="jp-icon jp-icon-lock" aria-hidden="true" />
        <p>
          With a JusPrin account, JusPrin receives your messages, the open project’s settings and anything you attach,
          and passes them on to the model provider it uses.
        </p>
      </div>
      {signedIn ? (
        <p className="prefs-note">Signing out keeps your projects, printers and API keys on this computer.</p>
      ) : (
        <p className="prefs-note">
          Local work never needs an account. Your own API key or AI tool works without one; set those up in the{' '}
          <button type="button" className="link prefs-inline-link" onClick={onAgent}>BYOAI</button> section.
        </p>
      )}
    </section>
  );
}

function AgentSection({ agent, setup, savedKeys, mcpCatalog, actions, onConnectTool, onAccount }: {
  agent: PreferencesPayload['agent']; setup: SetupStatusPayload; savedKeys: SavedKeyInfo[];
  mcpCatalog: McpCatalogPayload | null; actions: PreferencesActions; onConnectTool: (tool: McpToolInfo) => void;
  onAccount: () => void;
}) {
  // The tools are looked for once each time the section opens.
  useEffect(() => {
    actions.refreshTools();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  return (
    <section className="prefs-section" aria-label="BYOAI">
      <div className="prefs-head">
        <h2 className="prefs-title">BYOAI</h2>
        <p className="prefs-intro">
          Bring Your Own AI: an API key or an AI app you already pay for. No JusPrin account needed. Set up as many as
          you like; each chat picks a model from them.
        </p>
      </div>
      <ProviderKeys heading setup={setup} savedKeys={savedKeys} onCheck={actions.checkKey} onCancel={actions.cancelCheck}
        onRemove={actions.removeKey} />
      <ToolGroup heading tools={mcpCatalog?.tools ?? []} onConnect={onConnectTool} onLookAgain={actions.refreshTools} />
      <Group title="New chats start with">
        {agent.newChatChoices.length > 0 ? (
          <Row title="Model" detail="A chat keeps the model it started with.">
            <Select label="Model" value={agent.newChatProvider} onChange={(provider) => actions.set('newChatProvider', provider)}
              options={agent.newChatChoices.map((choice) => ({ value: choice.provider, label: `${choice.model} · ${choice.label}` }))} />
          </Row>
        ) : (
          <Row title="Model" detail="Add a key above, and new chats start with that provider’s model." />
        )}
      </Group>
      <div className="prefs-callout">
        <span className="jp-icon jp-icon-lock" aria-hidden="true" />
        <p>
          The provider a chat uses receives your messages, the open project’s settings and anything you attach. JusPrin
          doesn’t send your keys anywhere else.
        </p>
      </div>
      {!savedKeys.some((key) => key.provider === 'jusprin') && (
        <p className="prefs-note">
          Rather use JusPrin’s own AI? Sign up in{' '}
          <button type="button" className="link prefs-inline-link" onClick={onAccount}>Account</button>.
        </p>
      )}
    </section>
  );
}

function AppearanceSection({ appearance, set, run }: {
  appearance: PreferencesPayload['appearance']; set: PreferencesActions['set']; run: PreferencesActions['run'];
}) {
  // "Later" puts the offer away for as long as the same language waits.
  const [later, setLater] = useState<string | null>(null);
  return (
    <section className="prefs-section" aria-label="Appearance">
      <h2 className="prefs-title">Appearance</h2>
      <div>
        <Row title="Language" detail="Takes effect when JusPrin restarts. Your project is saved first.">
          <Select label="Language" value={appearance.language} searchable
            options={appearance.languages.map((language) => ({ value: language.code, label: language.name }))}
            onChange={(code) => set('language', code)} />
        </Row>
        {appearance.restartFor && later !== appearance.restartFor && (
          <div className="prefs-callout prefs-restart" role="status">
            <span className="jp-icon jp-icon-refresh-cw" aria-hidden="true" />
            <p>JusPrin will use {appearance.restartFor} after it restarts.</p>
            <button type="button" className="primary" onClick={() => run('restart')}>Restart now</button>
            <button type="button" className="link" onClick={() => setLater(appearance.restartFor)}>Later</button>
          </div>
        )}
        <Row title="Theme" detail="Changes every window, including the 3D view.">
          <Segmented label="Theme" value={appearance.theme} onChange={(theme) => set('theme', theme)}
            options={[{ value: 'light', label: 'Light' }, { value: 'dark', label: 'Dark' }]} />
        </Row>
        <Row title="Units" detail="For sizes, positions and distances.">
          <Segmented label="Units" value={appearance.units} onChange={(units) => set('units', units)}
            options={[{ value: 'mm', label: 'Millimetres' }, { value: 'in', label: 'Inches' }]} />
        </Row>
      </div>
    </section>
  );
}

function View3dSection({ view, set }: { view: PreferencesPayload['view3d']; set: PreferencesActions['set'] }) {
  return (
    <section className="prefs-section" aria-label="3D view">
      <h2 className="prefs-title">3D view</h2>
      <Group title="Moving the view">
        <Row title="Controls" detail="Mouse: drag to turn, right-drag to pan. Touchpad: Alt-drag to turn, Shift-drag to pan.">
          <Select label="Controls" value={view.navigation} onChange={(style) => set('navigation', style)}
            options={[{ value: 'mouse', label: 'Mouse' }, { value: 'touchpad', label: 'Touchpad' }]} />
        </Row>
        <Check title="Zoom toward the pointer" detail="Instead of toward the middle of the view." checked={view.zoomToPointer}
          onChange={(on) => set('zoomToPointer', on)} />
        <Check title="Reverse scroll-wheel zoom" detail="Scroll up to zoom out." checked={view.reverseZoom}
          onChange={(on) => set('reverseZoom', on)} />
        <Check title="Turn freely" detail="Turn the view in any direction. Off keeps the bed level." checked={view.freeCamera}
          onChange={(on) => set('freeCamera', on)} />
      </Group>
      <Group title="Mouse buttons">
        <Row title="Left-drag">
          <Select label="Left-drag" value={view.leftDrag} options={DRAG_ACTIONS} onChange={(action) => set('leftDrag', action)} />
        </Row>
        <Row title="Middle-drag">
          <Select label="Middle-drag" value={view.middleDrag} options={DRAG_ACTIONS} onChange={(action) => set('middleDrag', action)} />
        </Row>
        <Row title="Right-drag">
          <Select label="Right-drag" value={view.rightDrag} options={DRAG_ACTIONS} onChange={(action) => set('rightDrag', action)} />
        </Row>
      </Group>
    </section>
  );
}

function ProjectsSection({ projects, actions }: { projects: PreferencesPayload['projects']; actions: PreferencesActions }) {
  return (
    <section className="prefs-section" aria-label="Projects and files">
      <h2 className="prefs-title">Projects &amp; files</h2>
      <Group title="Version history">
        <Row title="Every project saves itself as you work"
          detail="JusPrin keeps up to 200 versions of each project, about 4 GB at most. Pinned versions are always kept.">
          <button type="button" className="prefs-button" onClick={() => actions.run('show_versions')}>Show folder</button>
        </Row>
      </Group>
      <Group title="Downloads">
        <Row title="Save downloaded models to"
          detail={<code className="prefs-path" title={projects.downloadPath}>{projects.downloadPath || 'Not chosen yet'}</code>}>
          <button type="button" className="prefs-button" onClick={() => actions.run('choose_downloads')}>Change…</button>
        </Row>
      </Group>
      {projects.associations && (
        <Group title="Open with JusPrin" tag="Windows">
          <Check title="3MF projects" detail="Double-clicking a .3mf file opens it here." checked={projects.open3mf}
            onChange={(on) => actions.set('open3mf', on)} />
          <Check title="STL and STEP models" detail="Also .stp and .drc files." checked={projects.openModels}
            onChange={(on) => actions.set('openModels', on)} />
          <Check title="“Open in slicer” buttons on model sites" detail="Printables and Thingiverse send the model straight here."
            checked={projects.openLinks} onChange={(on) => actions.set('openLinks', on)} />
        </Group>
      )}
    </section>
  );
}

// "2 hours ago": how long ago the last successful check was.
export function checkedAgo(checked: number, now: number = Date.now()): string {
  const minutes = Math.floor((now / 1000 - checked) / 60);
  if (minutes < 1) return 'just now';
  if (minutes < 60) return minutes === 1 ? '1 minute ago' : `${minutes} minutes ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return hours === 1 ? '1 hour ago' : `${hours} hours ago`;
  const days = Math.floor(hours / 24);
  return days === 1 ? 'yesterday' : `${days} days ago`;
}

function releaseDate(published: string): string {
  const date = new Date(published);
  return Number.isNaN(date.getTime()) ? '' : date.toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' });
}

function UpdateStatus({ updates, actions }: { updates: PreferencesPayload['updates']; actions: PreferencesActions }) {
  const title = <span className="prefs-card-title">JusPrin {updates.version}</span>;
  if (updates.status === 'store') {
    return (
      <div className="prefs-card">
        <div className="prefs-row-copy">
          {title}
          <span className="prefs-row-detail">Updates come from the Microsoft Store.</span>
        </div>
      </div>
    );
  }
  if (updates.available && updates.status !== 'checking') {
    const { available } = updates;
    const released = releaseDate(available.published);
    return (
      <div className="prefs-card prefs-card--update">
        <div className="prefs-row-copy">
          <span className="prefs-card-title">
            <span className="prefs-dot" aria-hidden="true" />
            JusPrin {available.version} is available
          </span>
          <span className="prefs-row-detail">
            {released ? `Released ${released} · you have ${updates.version}` : `You have ${updates.version}`}
          </span>
        </div>
        <div className="prefs-card-actions">
          <button type="button" className="primary" onClick={() => actions.run('open_update')}>
            Download
            <span className="jp-icon jp-icon-arrow-up-right" aria-hidden="true" />
          </button>
          <span className="prefs-card-note">Opens the release page on GitHub</span>
          <button type="button" className="link prefs-card-end"
            onClick={() => actions.run('skip_update', { version: available.version })}>
            Skip this version
          </button>
        </div>
        {available.notes && (
          <div className="prefs-release-notes">
            <h3 className="prefs-release-notes-title">Release notes</h3>
            <MarkdownMessage>{available.notes}</MarkdownMessage>
            <button type="button" className="link prefs-release-notes-link" onClick={() => actions.run('open_update')}>
              Full release notes on GitHub
              <span className="jp-icon jp-icon-arrow-up-right" aria-hidden="true" />
            </button>
          </div>
        )}
      </div>
    );
  }
  if (updates.status === 'checking') {
    return (
      <div className="prefs-card">
        <div className="prefs-row-copy">
          {title}
          <span className="prefs-row-detail">Looking for a newer version on GitHub…</span>
          <span className="prefs-update-progress"><Progress label="Checking for a newer version" /></span>
        </div>
        <button type="button" className="prefs-button" disabled>Checking…</button>
      </div>
    );
  }
  const checked = updates.checked === null ? null : checkedAgo(updates.checked);
  if (updates.status === 'failed') {
    return (
      <div className="prefs-card">
        <div className="prefs-row-copy">
          {title}
          <span className="prefs-update-line">
            <span className="prefs-dot prefs-dot--warning" aria-hidden="true" />
            <span className="prefs-row-detail">
              {checked ? `Couldn’t reach GitHub · last checked ${checked}` : 'Couldn’t reach GitHub'}
            </span>
          </span>
          <span className="prefs-row-hint">JusPrin tries again tomorrow, or the next time it starts.</span>
        </div>
        <button type="button" className="prefs-button" onClick={() => actions.run('check_updates')}>Try again</button>
      </div>
    );
  }
  return (
    <div className="prefs-card">
      <div className="prefs-row-copy">
        {title}
        {checked ? (
          <span className="prefs-update-line">
            <span className="prefs-dot prefs-dot--success" aria-hidden="true" />
            <span className="prefs-row-detail">Up to date · checked {checked}</span>
          </span>
        ) : (
          <span className="prefs-row-detail">Not checked yet</span>
        )}
      </div>
      <button type="button" className="prefs-button" onClick={() => actions.run('check_updates')}>Check now</button>
    </div>
  );
}

function UpdatesSection({ updates, actions }: { updates: PreferencesPayload['updates']; actions: PreferencesActions }) {
  return (
    <section className="prefs-section" aria-label="Updates">
      <h2 className="prefs-title">Updates</h2>
      <UpdateStatus updates={updates} actions={actions} />
      <Group title="What to check for">
        {updates.status !== 'store' && (
          <Check title="New JusPrin versions"
            detail="Once a day JusPrin asks GitHub for its list of releases. GitHub sees your IP address, as with any web page. Nothing about you or your projects is sent."
            checked={updates.checkUpdates} onChange={(on) => actions.set('checkUpdates', on)} />
        )}
        <Row title="Built-in printer and filament profiles"
          detail="Update with each JusPrin release. Your own presets never change." />
      </Group>
    </section>
  );
}

// The section on screen, kept for this window only: switching the theme
// reloads the page (OrcaSlicer's dark-mode pass walks every child window), and
// the person should stay where they were. A new window starts a new session.
const SECTION_KEY = 'jusprin.preferences.section';

function rememberedSection(): PreferencesSection | null {
  try {
    const stored = window.sessionStorage.getItem(SECTION_KEY);
    return SECTIONS.some((entry) => entry.id === stored) ? (stored as PreferencesSection) : null;
  } catch {
    return null;
  }
}

export function Preferences({
  preferences, setup, savedKeys, account, creditAccount, chatTitles, accountBox, mcpCatalog, mcpPreview, mcpStatus, actions,
  onToolDone,
}: PreferencesProps) {
  // Opened where the app was asked to open it; the list moves on from there.
  const [section, setSection] = useState<PreferencesSection>(() => rememberedSection() ?? preferences.section);
  // The AI tool being connected, which takes the Agent section's place.
  const [tool, setTool] = useState<string | null>(null);
  const go = (next: PreferencesSection) => {
    setSection(next);
    setTool(null);
    try {
      window.sessionStorage.setItem(SECTION_KEY, next);
    } catch {
      // Without storage a reload opens where the window was opened.
    }
  };

  return (
    <div className="prefs" data-testid="preferences">
      <nav className="prefs-nav" aria-label="Preference sections">
        {SECTIONS.map((entry) => (
          <button key={entry.id} type="button" className="prefs-nav-item" aria-current={section === entry.id ? 'page' : undefined}
            onClick={() => go(entry.id)}>
            <span className={`jp-icon jp-icon-${entry.icon}`} aria-hidden="true" />
            {entry.hint ? (
              <span className="prefs-nav-copy" aria-label={`${entry.label}, ${entry.hint}`}>
                <span>{entry.label}</span>
                <span className="prefs-nav-hint">{entry.hint}</span>
              </span>
            ) : (
              <span>{entry.label}</span>
            )}
            {entry.id === 'updates' && preferences.updates.available && (
              <span className="prefs-dot prefs-nav-dot" role="img" aria-label="Update available" />
            )}
          </button>
        ))}
        <div className="prefs-orca">
          <button type="button" className="link prefs-orca-link" onClick={() => actions.run('orca_preferences')}>
            OrcaSlicer preferences…
            <span className="jp-icon jp-icon-arrow-up-right" aria-hidden="true" />
          </button>
          <p className="prefs-orca-note">Every OrcaSlicer setting, as OrcaSlicer shows it.</p>
        </div>
      </nav>
      <main className="prefs-content">
        {section === 'account' && <AccountSection savedKeys={savedKeys} account={account} creditAccount={creditAccount}
          chatTitles={chatTitles} accountBox={accountBox} actions={actions} onAgent={() => go('agent')} />}
        {section === 'agent' && (tool ? (
          <SetupLocalTool
            startTool={tool}
            catalog={mcpCatalog}
            preview={mcpPreview}
            status={mcpStatus}
            onRefresh={actions.refreshTools}
            onPreview={actions.previewTool}
            onConnect={actions.connectTool}
            onReveal={actions.revealTool}
            onBack={() => setTool(null)}
            onDone={() => {
              setTool(null);
              onToolDone?.();
            }}
          />
        ) : (
          <AgentSection agent={preferences.agent} setup={setup} savedKeys={savedKeys} mcpCatalog={mcpCatalog}
            actions={actions}
            onConnectTool={(chosen) => setTool(chosen.id)} onAccount={() => go('account')} />
        ))}
        {section === 'appearance' && <AppearanceSection appearance={preferences.appearance} set={actions.set} run={actions.run} />}
        {section === 'view3d' && <View3dSection view={preferences.view3d} set={actions.set} />}
        {section === 'projects' && <ProjectsSection projects={preferences.projects} actions={actions} />}
        {section === 'updates' && <UpdatesSection updates={preferences.updates} actions={actions} />}
      </main>
    </div>
  );
}
