// The Agent's ways of connecting, drawn once for every surface that offers
// them: the JusPrin account block, one row per provider key, and one row per
// AI tool (canvas "JusPrin v2": Agent/Account Row, Agent/Provider Row,
// Agent/Your Own API Key and Agent/Tool Row, decided 2026-10-21). Preferences
// › Account holds the account; Preferences › BYOAI the keys and the tools.

import { KeyboardEvent, RefObject, useEffect, useRef, useState } from 'react';
import { AccountStatusPayload, McpToolInfo, PageMessageType, SavedKeyInfo, SetupStatusPayload } from '../bridge/protocol';
import { SIGNUP_AFTER, SIGNUP_OFFER, SIGNUP_SCALE } from '../credits';
import { SignInActions, SignInButtons, SignInDoor } from './SignInDoor';

// The page and the host ship in the same build, so this list and the host's
// setup_provider_supported() are two views of the same fact. The host still
// validates: an unsupported provider comes back as a visible setup error
// rather than a request that never answers.
export interface ProviderOption {
  id: string;
  label: string;
  // How the provider's keys start, for the placeholder and the locked field.
  prefix: string;
}

export const PROVIDERS: ProviderOption[] = [
  { id: 'anthropic', label: 'Anthropic', prefix: 'sk-ant-' },
  { id: 'openai', label: 'OpenAI', prefix: 'sk-' },
  { id: 'openrouter', label: 'OpenRouter', prefix: 'sk-or-' },
];

export function providerLabel(id: string | undefined): string {
  return PROVIDERS.find((provider) => provider.id === id)?.label ?? 'your provider';
}

// The Agent page's own buttons, drawn in the shared sign-in.
const AGENT_BUTTONS: SignInButtons = { primary: 'primary', secondary: 'agent-row-action' };

// The shared sign-in's controls, as the Agent page's bridge carries them out.
// Every window that signs in speaks the same messages.
export function accountSignInActions(send: (type: PageMessageType, payload: object) => void): SignInActions {
  return {
    google: () => send('account_sign_in', {}),
    email: (email) => send('account_email', { email }),
    sendCode: () => send('account_send_code', {}),
    code: (code) => send('account_code', { code }),
    password: (password) => send('account_password', { password }),
    cancel: () => send('account_cancel', {}),
    reopen: () => send('account_reopen', {}),
    openLink: (link) => send('account_open_link', { link }),
    getNewVersion: () => send('shell_action', { action: 'check_for_updates' }),
  };
}

export interface AccountSignInProps {
  status: AccountStatusPayload;
  actions: SignInActions;
}

// The sign-in every screen shares (SignInDoor), in a column of its own.
// `children` is what it says above the door while nothing has started.
export function AccountSignIn({ status, actions, children }: AccountSignInProps & { children?: React.ReactNode }) {
  return (
    <div className="account-sign-in">
      <SignInDoor phase={status.phase} email={status.email} error={status.error?.message}
        outdated={status.error?.code === 'update_required'} actions={actions} buttons={AGENT_BUTTONS}>
        {children}
      </SignInDoor>
    </div>
  );
}

// The first-month offer, the headline wherever someone can sign in for the
// first time (canvas "JusPrin v2", boards 1a to 1c), over the sign-in itself.
// Signing up and signing in are one door: an address with no account gets
// one from its first code. `note` is what a surface adds under the offer.
// `freeAfter` adds what a free account gets after the month, for someone who
// has no plan yet.
export function FirstMonthOffer({ note, freeAfter, status, actions }: AccountSignInProps & { note?: string; freeAfter?: boolean }) {
  return (
    <div className="credit-offer" data-testid="first-month-offer">
      <AccountSignIn status={status} actions={actions}>
        <div className="credit-offer-copy">
          <h2 className="credit-offer-title">{SIGNUP_OFFER}</h2>
          <p>{SIGNUP_SCALE}</p>
          {freeAfter && <p>{SIGNUP_AFTER}</p>}
          {note && <p className="credit-offer-note">{note}</p>}
        </div>
      </AccountSignIn>
    </div>
  );
}

export interface AccountRowProps extends AccountSignInProps {
  // The signed-in account, from setup_keys; absent when signed out.
  account?: SavedKeyInfo;
  // Signed in, but the account's AI could not be reached: its model list
  // failed without the server refusing the sign-in. `retrying` while Try
  // again waits for the answer.
  aiUnavailable?: boolean;
  // Signed in, but the server no longer serves this release of JusPrin: its
  // words, from the account's model list. Trying again would fail the same
  // way, so the row offers the new version instead.
  outdatedMessage?: string;
  retrying?: boolean;
  onRetry?: () => void;
  onSignOut: () => void;
}

// The JusPrin account (canvas "JusPrin v2": Agent/Account Row): signed out,
// signed in, signed in with AI unavailable, or signed in with a sign-in that
// has ended. Signing in, or in again, is the sign-in every screen shares.
export function AccountRow({ account, status, actions, aiUnavailable, outdatedMessage, retrying, onRetry, onSignOut }: AccountRowProps) {
  if (!account) {
    return (
      <div className="agent-account-offer" data-testid="agent-account">
        <span className="agent-account-eyebrow">JusPrin account</span>
        <FirstMonthOffer freeAfter status={status} actions={actions} />
        {status.warning && <p className="agent-row-note">{status.warning}</p>}
      </div>
    );
  }
  if (account.rejected) {
    return (
      <div className="agent-account-offer" data-testid="agent-account">
        <div className="agent-row-copy">
          <h2 className="agent-account-title">JusPrin account</h2>
          <span className="agent-row-status agent-row-status--rejected" data-testid="agent-account-status">
            <span className="agent-row-status-label">
              <span className="jp-icon jp-icon-circle-alert" aria-hidden="true" />
              <span>Your sign-in for {account.account || 'this account'} has ended</span>
            </span>
          </span>
        </div>
        <p className="agent-row-note">Sign in again to keep using AI with your JusPrin account.</p>
        <AccountSignIn status={status} actions={actions} />
      </div>
    );
  }
  // The stored account being checked again: its sign-in may have ended.
  const checking = status.phase === 'checking';
  const email = account.account ? `Signed in as ${account.account}` : 'Signed in';
  if (!checking && outdatedMessage) {
    return (
      <div className="agent-account" data-testid="agent-account">
        <div className="agent-row-copy">
          <h2 className="agent-account-title">JusPrin account</h2>
          <span className="agent-row-status agent-row-status--unavailable" data-testid="agent-account-status">
            <span className="agent-row-status-label">
              <span className="agent-row-status-dot" aria-hidden="true" />
              <span>{email} · AI unavailable</span>
            </span>
          </span>
        </div>
        <button type="button" className="primary" onClick={actions.getNewVersion}>Get the new version…</button>
        <div className="agent-account-detail">
          <p className="agent-row-note">{outdatedMessage}</p>
        </div>
      </div>
    );
  }
  if (!checking && aiUnavailable) {
    return (
      <div className="agent-account" data-testid="agent-account">
        <div className="agent-row-copy">
          <h2 className="agent-account-title">JusPrin account</h2>
          <span className="agent-row-status agent-row-status--unavailable" data-testid="agent-account-status">
            <span className="agent-row-status-label">
              <span className="agent-row-status-dot" aria-hidden="true" />
              <span>{email} · AI unavailable</span>
            </span>
          </span>
        </div>
        <button type="button" className="agent-row-action" onClick={onRetry} disabled={retrying}>Try again</button>
        <div className="agent-account-detail">
          <p className="agent-row-note">Your account and AI setup stay intact. Your own key or AI tool still works meanwhile.</p>
        </div>
      </div>
    );
  }
  return (
    <div className="agent-account" data-testid="agent-account">
      <div className="agent-row-copy">
          <h2 className="agent-account-title">JusPrin account</h2>
          <span className={`agent-row-status${checking ? '' : ' agent-row-status--saved'}`} data-testid="agent-account-status">
            {checking ? 'Checking your account…' : (
              <span className="agent-row-status-label">
                <span className="jp-icon jp-icon-check" aria-hidden="true" />
                <span>{email}</span>
              </span>
            )}
          </span>
      </div>
      {checking ? (
        <button type="button" className="link" onClick={actions.cancel}>Cancel</button>
      ) : (
        <button type="button" className="agent-row-action" onClick={onSignOut}>Sign out</button>
      )}
      <div className="agent-account-detail">
      {!checking && status.error && (
        <p className="agent-key-error" role="alert" data-testid="account-error">{status.error.message}</p>
      )}
      {!checking && status.error?.code === 'update_required' && (
        <button type="button" className="primary" onClick={actions.getNewVersion}>Get the new version…</button>
      )}
      {!checking && status.warning && <p className="agent-row-note">{status.warning}</p>}
      </div>
    </div>
  );
}

// What is stored for a provider, as its row's status line says it.
// 'sessionOnly' is a key that passed its check but could not be written to
// this computer: it works until JusPrin restarts.
export type KeyStanding = 'none' | 'saved' | 'rejected' | 'sessionOnly';

// The row's key form. 'unreachable' is a check that never got an answer, so
// the key stays locked for another try; 'refused' is one the provider
// answered no to, so the key stays open to correct.
export type KeyForm = 'closed' | 'entering' | 'checking' | 'unreachable' | 'refused';

const STANDING_LINE: Record<Exclude<KeyStanding, 'none'>, { icon: string; text: string }> = {
  saved: { icon: 'check', text: 'Key saved on this computer' },
  sessionOnly: { icon: 'triangle-alert', text: 'This key works now, but JusPrin couldn’t save it.' },
  rejected: { icon: 'circle-alert', text: 'The provider rejected this key' },
};

// A key being checked stays visible so it is clear which one is in question,
// but only its known start: the rest is never echoed back.
function maskedKey(key: string, prefix: string): string {
  return `${key.startsWith(prefix) ? prefix : ''}${'•'.repeat(16)}`;
}

export interface ProviderKeyRowProps {
  provider: ProviderOption;
  standing: KeyStanding;
  // The host's start-and-last-4 hint of the stored key; never the key.
  hint?: string;
  form: KeyForm;
  error?: string;
  value: string;
  // Another check is running; nothing else may start until it ends.
  busy: boolean;
  pasteAvailable: boolean;
  inputRef: RefObject<HTMLInputElement>;
  onChange: (value: string) => void;
  onOpen: () => void;
  onRemove: () => void;
  onCheck: () => void;
  onCancel: () => void;
  onPaste: () => void;
}

export function ProviderKeyRow({
  provider, standing, hint, form, error, value, busy, pasteAvailable, inputRef,
  onChange, onOpen, onRemove, onCheck, onCancel, onPaste,
}: ProviderKeyRowProps) {
  const name = provider.label;
  const replacing = form !== 'closed' && standing !== 'none';
  const locked = form === 'checking' || form === 'unreachable';
  const fieldId = `agent-key-${provider.id}`;
  const line = standing === 'none' ? null : STANDING_LINE[standing];
  const keyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Enter' && value.trim()) onCheck();
    if (event.key === 'Escape') onCancel();
  };

  return (
    <div className="agent-row" data-testid={`agent-key-${provider.id}`}>
      <div className="agent-row-line">
        <div className="agent-row-copy">
          <span className="agent-row-name">{name}</span>
          {line ? (
            <span className={`agent-row-status agent-row-status--${standing}`} data-testid={`agent-key-${provider.id}-standing`}>
              <span className="agent-row-status-label">
                <span className={`jp-icon jp-icon-${line.icon}`} aria-hidden="true" />
                <span>{line.text}</span>
              </span>
              {hint && <code className="agent-key-hint">{hint}</code>}
            </span>
          ) : (
            <span className="agent-row-status">No key</span>
          )}
        </div>
        {form === 'closed' && (standing === 'none' ? (
          <button type="button" className="agent-row-action" onClick={onOpen} disabled={busy}
            aria-label={`Add ${name} key`}>
            <span className="jp-icon jp-icon-plus" aria-hidden="true" />
            Add key
          </button>
        ) : (
          <div className="agent-row-actions">
            <button type="button" className="agent-row-action" onClick={onOpen} disabled={busy}
              aria-label={`Replace ${name} key`}>Replace</button>
            <button type="button" className="agent-row-action agent-row-action--danger" onClick={onRemove} disabled={busy}
              aria-label={`Remove ${name} key`}>Remove</button>
          </div>
        ))}
      </div>

      {form !== 'closed' && (
        <div className="agent-key-form">
          <label className="agent-key-label" htmlFor={fieldId}>{replacing ? `New ${name} key` : `${name} API key`}</label>
          <div className={`agent-key-field${locked ? ' agent-key-field--locked' : error ? ' agent-key-field--error' : ''}`}>
            {locked ? (
              <input id={fieldId} className="agent-key-input" value={maskedKey(value, provider.prefix)} disabled readOnly />
            ) : (
              <input
                ref={inputRef}
                id={fieldId}
                type="password"
                className="agent-key-input"
                placeholder={replacing ? 'Paste the new key' : `Paste your ${name} key (${provider.prefix}…)`}
                value={value}
                aria-invalid={error ? true : undefined}
                onChange={(event) => onChange(event.target.value)}
                onKeyDown={keyDown}
              />
            )}
            {!locked && pasteAvailable && (
              <button type="button" className="link" onClick={onPaste}>Paste</button>
            )}
          </div>
          {error && <p className="agent-key-error" role="alert" data-testid="setup-error">{error}</p>}
          <div className="agent-key-actions">
            {form === 'checking' ? (
              <span className="agent-key-checking">Checking with {name}…</span>
            ) : (
              <button type="button" className="primary" onClick={onCheck} disabled={!value.trim()}>Check key</button>
            )}
            <button type="button" className="link" onClick={onCancel}>Cancel</button>
            {replacing && form !== 'checking' && hint && (
              <span className="agent-key-keeps">Keeps <code>{hint}</code> until the new key passes</span>
            )}
          </div>
        </div>
      )}

      {form === 'closed' && standing === 'sessionOnly' && (
        <p className="agent-row-note">
          You’ll need to add it again after JusPrin restarts.
        </p>
      )}
      {form === 'closed' && standing === 'rejected' && (
        <p className="agent-row-note">Replace it with a working key, or remove it.</p>
      )}
    </div>
  );
}

// A check that never reached the provider says nothing about the key.
const UNREACHED = new Set(['network_error', 'timeout', 'service_unavailable', 'service_overloaded', 'setup_failed']);

export interface ProviderKeysProps {
  // Preferences lists the ways one under another and names each one.
  heading?: boolean;
  setup: SetupStatusPayload;
  // Providers with a stored key, by hint; the key itself never reaches the page.
  savedKeys: SavedKeyInfo[];
  onCheck: (provider: string, apiKey: string) => void;
  onCancel: () => void;
  onRemove: (provider: string) => void;
}

// One row per provider. At most one row has its form open: the host checks
// one key at a time, and a key typed into one row means nothing to another.
export function ProviderKeys({ heading = false, setup, savedKeys, onCheck, onCancel, onRemove }: ProviderKeysProps) {
  const [open, setOpen] = useState<string | null>(null);
  const [key, setKey] = useState('');
  // The open form's key has been sent, so the host's status now speaks for
  // it. Typing again makes it a different key that nothing has said anything
  // about yet.
  const [sent, setSent] = useState(false);
  const [pasteAvailable, setPasteAvailable] = useState(false);
  const input = useRef<HTMLInputElement>(null);

  // Clipboard reads are not granted in every embedded web view; the paste
  // shortcut appears only where it will actually work. Typing and the
  // platform's own paste always do.
  useEffect(() => {
    setPasteAvailable(
      typeof navigator !== 'undefined' && !!navigator.clipboard && typeof navigator.clipboard.readText === 'function',
    );
  }, []);

  const close = () => {
    setOpen(null);
    setKey('');
    setSent(false);
  };

  // A key that passes becomes the provider's key (or works until restart),
  // so its form has nothing left to do.
  useEffect(() => {
    if (setup.phase === 'verified') close();
  }, [setup.phase]);

  useEffect(() => {
    if (open) input.current?.focus();
  }, [open]);

  const busy = setup.phase === 'checking';

  const formFor = (provider: ProviderOption): KeyForm => {
    if (open !== provider.id) return 'closed';
    if (sent && setup.provider === provider.id) {
      if (setup.phase === 'checking') return 'checking';
      if (setup.phase === 'error') return UNREACHED.has(setup.error?.code ?? '') ? 'unreachable' : 'refused';
    }
    return 'entering';
  };

  const errorFor = (provider: ProviderOption, form: KeyForm): string | undefined => {
    if (form === 'unreachable') return `Couldn’t reach ${provider.label} to check this key. Nothing was saved. Try again.`;
    if (form !== 'refused') return undefined;
    if (setup.error?.code === 'invalid_credentials') return `${provider.label} rejected this key. Nothing was saved.`;
    return `${setup.error?.message ?? 'The key could not be checked.'} Nothing was saved.`;
  };

  const paste = () => {
    navigator.clipboard
      .readText()
      .then((text) => {
        setKey(text.trim());
        setSent(false);
        input.current?.focus();
      })
      .catch(() => {
        // Refused at the OS or view level: leave the field alone and let the
        // user paste the way they always could.
        input.current?.focus();
      });
  };

  return (
    <section className="agent-group" aria-label="Your own API key">
      <div className="agent-group-head">
        {heading && <h2 className="agent-group-title">Your own API key</h2>}
        <p className="agent-group-intro">Billed by your provider, not JusPrin. Stored on this computer only.</p>
      </div>
      {PROVIDERS.map((provider) => {
        const saved = savedKeys.find((candidate) => candidate.provider === provider.id);
        const sessionOnly = setup.phase === 'verified' && !!setup.warning && setup.provider === provider.id;
        // A key that could not be stored has no hint: the host never kept it.
        const standing: KeyStanding = sessionOnly ? 'sessionOnly' : saved ? (saved.rejected ? 'rejected' : 'saved') : 'none';
        const form = formFor(provider);
        return (
          <ProviderKeyRow
            key={provider.id}
            provider={provider}
            standing={standing}
            hint={sessionOnly ? undefined : saved?.hint}
            form={form}
            error={errorFor(provider, form)}
            value={open === provider.id ? key : ''}
            busy={busy}
            pasteAvailable={pasteAvailable}
            inputRef={input}
            onChange={(value) => {
              setKey(value);
              setSent(false);
            }}
            onOpen={() => {
              setOpen(provider.id);
              setKey('');
              setSent(false);
            }}
            onRemove={() => onRemove(provider.id)}
            onCheck={() => {
              if (!key.trim() || busy) return;
              setSent(true);
              onCheck(provider.id, key.trim());
            }}
            onCancel={() => {
              if (form === 'checking') onCancel();
              close();
            }}
            onPaste={paste}
          />
        );
      })}
      <div className="agent-group-foot">
        <p className="agent-row-note">Every chat can pick a model from these providers in its message box.</p>
      </div>
    </section>
  );
}

export interface ToolRowProps {
  tool: McpToolInfo;
  onConnect: () => void;
}

// "Found" is only what JusPrin could see on disk: the app or its settings
// file. Any tool can be connected, found or not.
export function ToolRow({ tool, onConnect }: ToolRowProps) {
  return (
    <div className="agent-row" data-testid={`agent-tool-${tool.id}`}>
      <div className="agent-row-line">
        <div className="agent-row-copy">
          <span className="agent-row-name">
            {tool.name}
            {tool.subtitle && <span className="agent-row-detail">{tool.subtitle}</span>}
          </span>
          <span className="agent-row-status">{tool.detected ? 'Found on this computer' : 'Not found on this computer'}</span>
        </div>
        <button type="button" className="agent-row-action" aria-label={`Connect ${tool.name}`} onClick={onConnect}>
          Connect…
        </button>
      </div>
    </div>
  );
}

export interface ToolGroupProps {
  heading?: boolean;
  tools: McpToolInfo[];
  onConnect: (tool: McpToolInfo) => void;
  onLookAgain: () => void;
}

export function ToolGroup({ heading = false, tools, onConnect, onLookAgain }: ToolGroupProps) {
  return (
    <section className="agent-group" aria-label="Connect an AI tool you already use">
      <div className="agent-group-head">
        {heading && <h2 className="agent-group-title">Connect an AI tool you already use</h2>}
        <p className="agent-group-intro">
          Let an AI app on this computer, such as Claude Desktop, work with the open project. JusPrin adds itself to
          that app’s settings.
        </p>
      </div>
      {tools.map((tool) => <ToolRow key={tool.id} tool={tool} onConnect={() => onConnect(tool)} />)}
      <div className="agent-group-foot">
        <p className="agent-row-note">
          “Found” means JusPrin found the app or its settings file here, not that the app is signed in. You can connect
          one that wasn’t found.
        </p>
        <button type="button" className="agent-row-action" onClick={onLookAgain}>
          <span className="jp-icon jp-icon-refresh-cw" aria-hidden="true" />
          Look again
        </button>
      </div>
    </section>
  );
}
