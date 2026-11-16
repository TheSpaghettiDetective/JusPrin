// JusPrin's Preferences window (canvas "JusPrin v2", Preferences, decided
// 2026-10-21): a list of sections on the left and the chosen one beside it.
// The app reads and writes every setting, under the keys OrcaSlicer's own
// dialog uses, and sends what is stored after each change; the page only
// says what to change. Account holds the JusPrin account and is where every
// surface that finds no AI opens it; BYOAI (Bring Your Own AI) holds the
// person's own API keys and AI tools, with no JusPrin account in it. Both draw
// the connection rows of AgentConnection.tsx.

import { useEffect, useState } from 'react';
import { t, plural } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import {
  AccountStatusPayload,
  CreditAccount,
  DragAction,
  McpCatalogPayload,
  McpPreviewPayload,
  McpStatusPayload,
  McpToolInfo,
  ModelCatalogPayload,
  PreferencesPayload,
  PreferencesSection,
  SavedKeyInfo,
  SetupStatusPayload,
} from '../bridge/protocol';
import { AccountBox } from './AccountBox';
import { AccountRow, ProviderKeys, ToolGroup } from './AgentConnection';
import type { SignInActions } from './SignInDoor';
import { balanceRows, EXPIRING_FIRST, showsExpiringFirst } from './BalancePanel';
import { creditCount, creditNumber, MESSAGE_PRICES } from '../credits';
import { MarkdownMessage } from './MarkdownMessage';
import { Progress } from './Progress';
import { ActionMenu, Dialog } from './ChatNavigation';
import { SetupLocalTool } from './SetupLocalTool';
import { dateLocale } from '../i18n';

// A hint is a second, smaller line under the label, for a label that needs it.
const SECTIONS: { id: PreferencesSection; label: string; hint?: string; icon: string }[] = [
  { id: 'account', label: t`Account`, icon: 'user' },
  { id: 'agent', label: 'BYOAI', hint: t`Bring Your Own AI`, icon: 'key' },
  { id: 'appearance', label: t`Appearance`, icon: 'palette' },
  { id: 'view3d', label: t`3D view`, icon: 'box' },
  { id: 'projects', label: t`Projects & files`, icon: 'folder' },
  { id: 'updates', label: t`Updates`, icon: 'refresh-cw' },
];

// What each mouse button's drag can do, in OrcaSlicer's order.
const DRAG_ACTIONS: { value: DragAction; label: string }[] = [
  { value: 'rotate', label: t`Turn the view` },
  { value: 'pan', label: t`Pan` },
  { value: 'none', label: t`Nothing` },
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
  // The JusPrin account's sign-in, as every screen that signs in has it.
  signIn: SignInActions;
  // One of the server's pages in the app's sheet: Credits and plans
  // ("prices") or Billing and usage ("billing").
  openServerPage: (page: 'prices' | 'billing') => void;
  // Deletes the JusPrin account, once the person has confirmed it; the
  // answer comes back as accountDeleted.
  deleteAccount: () => void;
  refreshAccountBox: () => void;
  // Reads the account again, and any model list that failed.
  refreshAccount: () => void;
}

export interface PreferencesProps {
  preferences: PreferencesPayload;
  setup: SetupStatusPayload;
  savedKeys: SavedKeyInfo[];
  account: AccountStatusPayload;
  // The signed-in account's plan and credits; null with no account or
  // before it answers.
  creditAccount: CreditAccount | null;
  // The box the server fills near the top of Account; null for none.
  accountBox: string | null;
  // The host's last answer to deleteAccount.
  accountDeleted: { deleted: boolean } | null;
  // Each configured provider's model list; the JusPrin account's says whether
  // its AI can be reached.
  modelCatalog?: ModelCatalogPayload;
  mcpCatalog: McpCatalogPayload | null;
  mcpPreview: McpPreviewPayload | null;
  mcpStatus: McpStatusPayload;
  actions: PreferencesActions;
  // Done on an AI tool, for a window opened to close once one is connected.
  onToolDone?: () => void;
}

// The JusPrin account row. Its AI is unavailable when the account's model
// list failed without the server refusing the sign-in (a refusal shows as a
// sign-in that has ended); Try again asks for the list again, and the row
// waits for the next catalog the host sends. A list the server refused for
// this release of JusPrin offers the new version instead.
function JusPrinAccount({ savedKeys, account, modelCatalog, actions }: {
  savedKeys: SavedKeyInfo[]; account: AccountStatusPayload; modelCatalog?: ModelCatalogPayload; actions: PreferencesActions;
}) {
  const [retriedAt, setRetriedAt] = useState<ModelCatalogPayload | undefined>();
  const models = modelCatalog?.providers.find((provider) => provider.id === 'jusprin');
  return (
    <AccountRow account={savedKeys.find((key) => key.provider === 'jusprin')} status={account} actions={actions.signIn}
      aiUnavailable={models?.state === 'failed'}
      outdatedMessage={models?.state === 'failed' ? models.updateMessage : undefined} retrying={retriedAt !== undefined && retriedAt === modelCatalog}
      onRetry={() => { setRetriedAt(modelCatalog); actions.refreshAccount(); }}
      onSignOut={() => actions.removeKey('jusprin')} />
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

// `search`, for a long list, is the find field's label and what it says when
// nothing matches, worded by the caller for what the list holds.
function Select<T extends string>({ label, options, value, onChange, search }: {
  label: string; options: { value: T; label: string }[]; value: T; onChange: (value: T) => void;
  search?: { label: string; noMatch: (query: string) => string };
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
        search={search}
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

// A gateway day, YYYY-MM-DD, as "5 Nov", or "5 Nov 2027" outside this year (a
// yearly plan's renewal). The day is the gateway's, so it is read as UTC: a
// local reading would move it a day west of Greenwich.
function dayLabel(date: string): string {
  const day = new Date(`${date}T00:00:00Z`);
  return day.toLocaleDateString(dateLocale('en-GB'), {
    day: 'numeric', month: 'short', timeZone: 'UTC',
    ...(day.getUTCFullYear() !== new Date().getUTCFullYear() ? { year: 'numeric' } : {}),
  });
}

// The account's plan and credits (board money-06 on the canvas's "Plan and
// billing" page): the plan's name and the price line the server writes, its
// two doors into the server's pages, and a bar for each kind of credit as the
// composer's panel draws them. Usage is on the server's Billing and usage.
function AccountCredits({ account, onOpenPage }: {
  account: CreditAccount; onOpenPage: (page: 'prices' | 'billing') => void;
}) {
  const rows = balanceRows(account);
  const renews = account.plan?.renews_at && dayLabel(account.plan.renews_at.slice(0, 10));
  const ends = account.plan?.ends_at && dayLabel(account.plan.ends_at.slice(0, 10));
  const total = creditCount(account.credits.total);
  return (
    <>
      <div className="prefs-credits-plan">
        <div className="prefs-row-copy">
          <span className="prefs-credits-plan-name">{account.plan?.name ?? t`No plan`}</span>
          <span className="prefs-intro">
            {account.plan
              ? [account.plan.price_line,
                renews && t`Renews ${renews}.`,
                ends && t`Ends ${ends}.`]
                .filter(Boolean).join(' ')
              : MESSAGE_PRICES}
          </span>
        </div>
        <div className="prefs-credits-plan-actions">
          <button type="button" onClick={() => onOpenPage('billing')}>{t`Billing and usage`}</button>
          <button type="button" className="primary" onClick={() => onOpenPage('prices')}>{t`Get more credits`}</button>
        </div>
      </div>
      <div className="prefs-credits">
        <div className="prefs-credits-card">
          <strong className="prefs-credits-total">{t`${total} left`}</strong>
          {rows.map((row) => {
            const left = creditNumber(row.left);
            const of = creditNumber(row.of);
            return (
            <div key={row.key} className="credit-lot prefs-credits-lot">
              <div className="credit-lot-line">
                <strong>{row.name}</strong>
                <span>{t`${left} of ${of} left`}</span>
              </div>
              <div className="credit-lot-bar">
                <div className="credit-lot-fill" style={{ width: `${row.of > 0 ? Math.min(row.left / row.of, 1) * 100 : 0}%` }} />
              </div>
              <span className="credit-lot-date">{row.date}</span>
            </div>
            );
          })}
        </div>
        {showsExpiringFirst(rows) && <p className="prefs-note">{EXPIRING_FIRST}</p>}
      </div>
    </>
  );
}

// The last row of Account (board money-10): the app's own confirmation, in
// the server's words about which plan ends and which credits go. Once the
// server has deleted the account the host signs out of it, and Account shows
// the signed-out form; a failure leaves the dialog open and says so.
function DeleteAccount({ email, warning, accountDeleted, onDelete }: {
  email?: string; warning?: string; accountDeleted: { deleted: boolean } | null; onDelete: () => void;
}) {
  const [confirming, setConfirming] = useState(false);
  // The answer the dialog is waiting past; undefined when it waits for none.
  const [sentAt, setSentAt] = useState<{ deleted: boolean } | null | undefined>();
  const deleting = sentAt !== undefined && accountDeleted === sentAt;
  const failed = sentAt !== undefined && accountDeleted !== sentAt && accountDeleted?.deleted === false;
  const close = () => { setConfirming(false); setSentAt(undefined); };
  return (
    <Group title={t`Delete account`}>
      <Row title={t`Delete this JusPrin account`}
        detail={t`Ends any plan and removes this account’s credits and everything JusPrin keeps for it on its servers.`}>
        <button type="button" className="danger prefs-danger" onClick={() => setConfirming(true)}>{t`Delete account…`}</button>
      </Row>
      {confirming && (
        <Dialog title={t`Delete your JusPrin account?`} className="confirmation" onClose={() => { if (!deleting) close(); }}>
          <p>
            {warning ?? (email
              ? t`Any plan ends, and the credits on ${email} are removed with everything JusPrin keeps for this account. This can’t be undone.`
              : t`Any plan ends, and the credits on this account are removed with everything JusPrin keeps for this account. This can’t be undone.`)}
          </p>
          {failed && <p className="chat-dialog-field-error" role="alert">{t`The account couldn’t be deleted. Check your internet connection, then try again.`}</p>}
          <div className="chat-dialog-buttons">
            <button type="button" disabled={deleting} onClick={close}>{t`Cancel`}</button>
            <button type="button" className="danger decisive" disabled={deleting}
              onClick={() => { setSentAt(accountDeleted); onDelete(); }}>
              {deleting ? t`Deleting…` : t`Delete account`}
            </button>
          </div>
        </Dialog>
      )}
    </Group>
  );
}

function AccountSection({ savedKeys, account, creditAccount, accountBox, accountDeleted, modelCatalog, actions, onAgent }: {
  savedKeys: SavedKeyInfo[]; account: AccountStatusPayload; creditAccount: CreditAccount | null;
  accountBox: string | null; accountDeleted: { deleted: boolean } | null;
  modelCatalog?: ModelCatalogPayload; actions: PreferencesActions; onAgent: () => void;
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
    <section className="prefs-section prefs-account" aria-label={t`Account`}>
      <h2 className="prefs-title">{t`Account`}</h2>
      {!ownKey && accountBox && <AccountBox key={accountBox} html={accountBox} />}
      <JusPrinAccount savedKeys={savedKeys} account={account} modelCatalog={modelCatalog} actions={actions} />
      {signedIn && creditAccount && (
        <AccountCredits account={creditAccount} onOpenPage={actions.openServerPage} />
      )}
      <div className="prefs-callout">
        <span className="jp-icon jp-icon-lock" aria-hidden="true" />
        <p>
          <Trans>
            With a JusPrin account, JusPrin receives your messages, the open project’s settings and anything you attach,
            and passes them on to the model provider it uses.
          </Trans>
        </p>
      </div>
      {signedIn ? (
        <>
          <p className="prefs-note">{t`Signing out keeps your projects, printers and API keys on this computer.`}</p>
          <DeleteAccount email={savedKeys.find((key) => key.provider === 'jusprin')?.account}
            warning={creditAccount?.delete_warning} accountDeleted={accountDeleted} onDelete={actions.deleteAccount} />
        </>
      ) : (
        <p className="prefs-note">
          <Trans>
            Local work never needs an account. Your own API key or AI tool works without one; set those up in the{' '}
            <button type="button" className="link prefs-inline-link" onClick={onAgent}>BYOAI</button> section.
          </Trans>
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
          <Trans>
            Bring Your Own AI: an API key or an AI app you already pay for. No JusPrin account needed. Set up as many as
            you like; each chat picks a model from them.
          </Trans>
        </p>
      </div>
      <ProviderKeys heading setup={setup} savedKeys={savedKeys} onCheck={actions.checkKey} onCancel={actions.cancelCheck}
        onRemove={actions.removeKey} />
      <ToolGroup heading tools={mcpCatalog?.tools ?? []} onConnect={onConnectTool} onLookAgain={actions.refreshTools} />
      <Group title={t`New chats start with`}>
        {agent.newChatChoices.length > 0 ? (
          <Row title={t`Model`} detail={t`A chat keeps the model it started with.`}>
            <Select label={t`Model`} value={agent.newChatProvider} onChange={(provider) => actions.set('newChatProvider', provider)}
              options={agent.newChatChoices.map((choice) => ({ value: choice.provider,
                // JusPrin's model id is an alias that means nothing to anyone.
                label: choice.provider === 'jusprin' ? choice.label : `${choice.model} · ${choice.label}` }))} />
          </Row>
        ) : (
          <Row title={t`Model`} detail={t`Add a key above, or sign in under Account, to choose the model new chats start with.`} />
        )}
      </Group>
      <div className="prefs-callout">
        <span className="jp-icon jp-icon-lock" aria-hidden="true" />
        <p>
          <Trans>
            The provider a chat uses receives your messages, the open project’s settings and anything you attach. JusPrin
            doesn’t send your keys anywhere else.
          </Trans>
        </p>
      </div>
      {!savedKeys.some((key) => key.provider === 'jusprin') && (
        <p className="prefs-note">
          <Trans>
            Rather use JusPrin’s own AI, which specializes in 3D printing? Sign up in{' '}
            <button type="button" className="link prefs-inline-link" onClick={onAccount}>Account</button>.
          </Trans>
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
    <section className="prefs-section" aria-label={t`Appearance`}>
      <h2 className="prefs-title">{t`Appearance`}</h2>
      <div>
        <Row title={t`Language`} detail={t`Takes effect when JusPrin restarts. Your project is saved first.`}>
          <Select label={t`Language`} value={appearance.language}
            search={{ label: t`Find a language`, noMatch: (query) => t`No language matches “${query}”` }}
            options={appearance.languages.map((language) => ({ value: language.code, label: language.name }))}
            onChange={(code) => set('language', code)} />
        </Row>
        {appearance.restartFor && later !== appearance.restartFor && (
          <div className="prefs-callout prefs-restart" role="status">
            <span className="jp-icon jp-icon-refresh-cw" aria-hidden="true" />
            <p><Trans>JusPrin will use {appearance.restartFor} after it restarts.</Trans></p>
            <button type="button" className="primary" onClick={() => run('restart')}>{t`Restart now`}</button>
            <button type="button" className="link" onClick={() => setLater(appearance.restartFor)}>{t`Later`}</button>
          </div>
        )}
        <Row title={t`Theme`} detail={t`Changes every window, including the 3D view.`}>
          <Segmented label={t`Theme`} value={appearance.theme} onChange={(theme) => set('theme', theme)}
            options={[{ value: 'light', label: t`Light` }, { value: 'dark', label: t`Dark` }]} />
        </Row>
        <Row title={t`Units`} detail={t`For sizes, positions and distances.`}>
          <Segmented label={t`Units`} value={appearance.units} onChange={(units) => set('units', units)}
            options={[{ value: 'mm', label: t`Millimetres` }, { value: 'in', label: t`Inches` }]} />
        </Row>
      </div>
    </section>
  );
}

function View3dSection({ view, set }: { view: PreferencesPayload['view3d']; set: PreferencesActions['set'] }) {
  return (
    <section className="prefs-section" aria-label={t`3D view`}>
      <h2 className="prefs-title">{t`3D view`}</h2>
      <Group title={t`Moving the view`}>
        <Row title={t`Controls`} detail={t`Mouse: drag to turn, right-drag to pan. Touchpad: Alt-drag to turn, Shift-drag to pan.`}>
          <Select label={t`Controls`} value={view.navigation} onChange={(style) => set('navigation', style)}
            options={[{ value: 'mouse', label: t`Mouse` }, { value: 'touchpad', label: t`Touchpad` }]} />
        </Row>
        <Check title={t`Zoom toward the pointer`} detail={t`Instead of toward the middle of the view.`} checked={view.zoomToPointer}
          onChange={(on) => set('zoomToPointer', on)} />
        <Check title={t`Reverse scroll-wheel zoom`} detail={t`Scroll up to zoom out.`} checked={view.reverseZoom}
          onChange={(on) => set('reverseZoom', on)} />
        <Check title={t`Turn freely`} detail={t`Turn the view in any direction. Off keeps the bed level.`} checked={view.freeCamera}
          onChange={(on) => set('freeCamera', on)} />
      </Group>
      <Group title={t`Mouse buttons`}>
        <Row title={t`Left-drag`}>
          <Select label={t`Left-drag`} value={view.leftDrag} options={DRAG_ACTIONS} onChange={(action) => set('leftDrag', action)} />
        </Row>
        <Row title={t`Middle-drag`}>
          <Select label={t`Middle-drag`} value={view.middleDrag} options={DRAG_ACTIONS} onChange={(action) => set('middleDrag', action)} />
        </Row>
        <Row title={t`Right-drag`}>
          <Select label={t`Right-drag`} value={view.rightDrag} options={DRAG_ACTIONS} onChange={(action) => set('rightDrag', action)} />
        </Row>
      </Group>
    </section>
  );
}

function ProjectsSection({ projects, actions }: { projects: PreferencesPayload['projects']; actions: PreferencesActions }) {
  return (
    <section className="prefs-section" aria-label={t`Projects and files`}>
      <h2 className="prefs-title">{t`Projects & files`}</h2>
      <Group title={t`Version history`}>
        <Row title={t`Every project saves itself as you work`}
          detail={t`JusPrin keeps up to 200 versions of each project, about 4 GB at most. Pinned versions are always kept.`}>
          <button type="button" className="prefs-button" onClick={() => actions.run('show_versions')}>{t`Show folder`}</button>
        </Row>
      </Group>
      <Group title={t`Downloads`}>
        <Row title={t`Save downloaded models to`}
          detail={<code className="prefs-path" title={projects.downloadPath}>{projects.downloadPath || t`Not chosen yet`}</code>}>
          <button type="button" className="prefs-button" onClick={() => actions.run('choose_downloads')}>{t`Change…`}</button>
        </Row>
      </Group>
      {projects.associations && (
        <Group title={t`Open with JusPrin`} tag="Windows">
          <Check title={t`3MF projects`} detail={t`Double-clicking a .3mf file opens it here.`} checked={projects.open3mf}
            onChange={(on) => actions.set('open3mf', on)} />
          <Check title={t`STL and STEP models`} detail={t`Also .stp and .drc files.`} checked={projects.openModels}
            onChange={(on) => actions.set('openModels', on)} />
          <Check title={t`“Open in slicer” buttons on model sites`} detail={t`Printables and Thingiverse send the model straight here.`}
            checked={projects.openLinks} onChange={(on) => actions.set('openLinks', on)} />
        </Group>
      )}
    </section>
  );
}

// "2 hours ago": how long ago the last successful check was.
export function checkedAgo(checked: number, now: number = Date.now()): string {
  const minutes = Math.floor((now / 1000 - checked) / 60);
  if (minutes < 1) return t`just now`;
  if (minutes < 60) return plural(minutes, { one: '# minute ago', other: '# minutes ago' });
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return plural(hours, { one: '# hour ago', other: '# hours ago' });
  const days = Math.floor(hours / 24);
  return days === 1 ? t`yesterday` : plural(days, { one: '# day ago', other: '# days ago' });
}

function releaseDate(published: string): string {
  const date = new Date(published);
  return Number.isNaN(date.getTime()) ? '' : date.toLocaleDateString(dateLocale(undefined), { day: 'numeric', month: 'short', year: 'numeric' });
}

function UpdateStatus({ updates, actions }: { updates: PreferencesPayload['updates']; actions: PreferencesActions }) {
  const title = <span className="prefs-card-title">JusPrin {updates.version}</span>;
  if (updates.status === 'store') {
    return (
      <div className="prefs-card">
        <div className="prefs-row-copy">
          {title}
          <span className="prefs-row-detail">{t`Updates come from the Microsoft Store.`}</span>
        </div>
      </div>
    );
  }
  if (updates.available && updates.status !== 'checking') {
    const { available } = updates;
    const released = releaseDate(available.published);
    const latest = available.version;
    const current = updates.version;
    return (
      <div className="prefs-card prefs-card--update">
        <div className="prefs-row-copy">
          <span className="prefs-card-title">
            <span className="prefs-dot" aria-hidden="true" />
            {t`JusPrin ${latest} is available`}
          </span>
          <span className="prefs-row-detail">
            {released ? t`Released ${released} · you have ${current}` : t`You have ${current}`}
          </span>
        </div>
        <div className="prefs-card-actions">
          <button type="button" className="primary" onClick={() => actions.run('open_update')}>
            {t`Download`}
            <span className="jp-icon jp-icon-arrow-up-right" aria-hidden="true" />
          </button>
          <span className="prefs-card-note">{t`Opens the release page on GitHub`}</span>
          <button type="button" className="link prefs-card-end"
            onClick={() => actions.run('skip_update', { version: available.version })}>
            {t`Skip this version`}
          </button>
        </div>
        {available.notes && (
          <div className="prefs-release-notes">
            <h3 className="prefs-release-notes-title">{t`Release notes`}</h3>
            <MarkdownMessage>{available.notes}</MarkdownMessage>
            <button type="button" className="link prefs-release-notes-link" onClick={() => actions.run('open_update')}>
              {t`Full release notes on GitHub`}
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
          <span className="prefs-row-detail">{t`Looking for a newer version on GitHub…`}</span>
          <span className="prefs-update-progress"><Progress label={t`Checking for a newer version`} /></span>
        </div>
        <button type="button" className="prefs-button" disabled>{t`Checking…`}</button>
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
              {checked
                ? t({ message: `Couldn’t reach GitHub · last checked ${checked}`, comment: '{checked} is how long ago, such as "just now", "2 hours ago" or "yesterday".' })
                : t`Couldn’t reach GitHub`}
            </span>
          </span>
          <span className="prefs-row-hint">
            {updates.checkUpdates ? t`JusPrin tries again tomorrow, or the next time it starts.` : t`Automatic checks are off.`}
          </span>
        </div>
        <button type="button" className="prefs-button" onClick={() => actions.run('check_updates')}>{t`Try again`}</button>
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
            <span className="prefs-row-detail">
              {t({ message: `Up to date · checked ${checked}`, comment: '{checked} is how long ago, such as "just now", "2 hours ago" or "yesterday".' })}
            </span>
          </span>
        ) : (
          <span className="prefs-row-detail">{t`Not checked yet`}</span>
        )}
      </div>
      <button type="button" className="prefs-button" onClick={() => actions.run('check_updates')}>{t`Check now`}</button>
    </div>
  );
}

function UpdatesSection({ updates, actions }: { updates: PreferencesPayload['updates']; actions: PreferencesActions }) {
  return (
    <section className="prefs-section" aria-label={t`Updates`}>
      <h2 className="prefs-title">{t`Updates`}</h2>
      <UpdateStatus updates={updates} actions={actions} />
      <Group title={t`What to check for`}>
        {updates.status !== 'store' && (
          <Check title={t`New JusPrin versions`}
            detail={t`Once a day JusPrin asks GitHub for its list of releases. GitHub sees your IP address, as with any web page. Nothing about you or your projects is sent.`}
            checked={updates.checkUpdates} onChange={(on) => actions.set('checkUpdates', on)} />
        )}
        <Row title={t`Built-in printer and filament profiles`}
          detail={t`Update with each JusPrin release. Your own presets never change.`} />
      </Group>
      <Group title={t`Error reports`}>
        <Check title={t`Report errors to the JusPrin team automatically`}
          detail={t`When something goes wrong on our side, JusPrin sends an anonymous report so the team can fix it. It has no chat messages, no files and nothing that says who you are. JusPrin’s server sees your IP address, as with any web page.`}
          checked={updates.reportErrors} onChange={(on) => actions.set('reportErrors', on)} />
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
  preferences, setup, savedKeys, account, creditAccount, accountBox, accountDeleted, modelCatalog, mcpCatalog, mcpPreview, mcpStatus,
  actions, onToolDone,
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

  // Get the new version… is answered here, in Updates.
  const accountActions: PreferencesActions = { ...actions, signIn: { ...actions.signIn, getNewVersion: () => {
    actions.signIn.getNewVersion();
    go('updates');
  } } };

  return (
    <div className="prefs" data-testid="preferences">
      <nav className="prefs-nav" aria-label={t`Preference sections`}>
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
              <span className="prefs-dot prefs-nav-dot" role="img" aria-label={t`Update available`} />
            )}
          </button>
        ))}
        <div className="prefs-orca">
          <button type="button" className="link prefs-orca-link" onClick={() => actions.run('orca_preferences')}>
            {t`More preferences…`}
          </button>
          <p className="prefs-orca-note">{t`The full preferences window from OrcaSlicer, which JusPrin is built on.`}</p>
        </div>
      </nav>
      <main className="prefs-content">
        {section === 'account' && <AccountSection savedKeys={savedKeys} account={account} creditAccount={creditAccount}
          accountBox={accountBox} accountDeleted={accountDeleted} modelCatalog={modelCatalog} actions={accountActions}
          onAgent={() => go('agent')} />}
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
