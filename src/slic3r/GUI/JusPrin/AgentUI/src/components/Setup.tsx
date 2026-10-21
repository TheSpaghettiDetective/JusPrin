// The Agent setup surface. It replaces the dock's body in place rather than
// opening a dialog: setting up an Agent changes nothing about the print, so
// it stays inside the same panel the offer was made in, and every screen can
// be backed out of without leaving anything behind.

import { useEffect, useRef, useState } from 'react';
import { SavedKeyInfo, SetupStatusPayload } from '../bridge/protocol';

// The page and the host ship in the same build, so this list and the host's
// setup_provider_supported() are two views of the same fact. The host still
// validates: an unsupported provider comes back as a visible setup error
// rather than a request that never answers.
interface ProviderOption {
  id: string;
  label: string;
  placeholder: string;
  available: boolean;
}

export const PROVIDERS: ProviderOption[] = [
  { id: 'anthropic', label: 'Anthropic', placeholder: 'sk-ant-…', available: true },
  { id: 'openai', label: 'OpenAI', placeholder: 'sk-…', available: true },
  { id: 'openrouter', label: 'OpenRouter', placeholder: 'sk-or-…', available: true },
  { id: 'other', label: 'Other…', placeholder: '', available: false },
];

// OpenAI stays the tab a new setup opens on; the others are a click away.
export const DEFAULT_PROVIDER = 'openai';

export function providerLabel(id: string | undefined): string {
  return PROVIDERS.find((provider) => provider.id === id)?.label ?? 'your provider';
}

function seconds(elapsedMs: number): string {
  return `${(elapsedMs / 1000).toFixed(1)} s`;
}

export interface SetupChooserProps {
  onUseApiKey: () => void;
  onConnectTool: () => void;
  onDismiss: () => void;
}

export function SetupScreenTitle({ label, onBack }: { label: string; onBack: () => void }) {
  return (
    <div className="setup-card-head">
      <button type="button" className="back" onClick={onBack} aria-label="Back to setup options">
        <span className="chevron" aria-hidden="true" />
      </button>
      <h1>{label}</h1>
    </div>
  );
}

export function SetupChooser({ onUseApiKey, onConnectTool, onDismiss }: SetupChooserProps) {
  const laterTitle = 'This way of connecting an Agent arrives in a later JusPrin release';
  return (
    <div className="pane-state setup" data-testid="setup-chooser">
      <div className="setup-card">
        <div className="setup-card-head">
          <h1>Set up the agent</h1>
          <button className="icon" onClick={onDismiss} aria-label="Close setup">
            <span className="jp-icon jp-icon-close" aria-hidden="true" />
          </button>
        </div>
        <p>
          It reads what you ask for and writes the settings underneath. It needs somewhere to think.
        </p>
        <button className="primary wide" disabled title={laterTitle}>
          Continue with JusPrin
        </button>
        <p className="footnote">Free account · sign in with your browser · nothing to install</p>

        <p className="setup-eyebrow">If you know what these are</p>
        <button className="setup-row" onClick={onUseApiKey} data-testid="setup-row-api-key">
          <span>Use your own API key</span>
          <span className="jp-icon jp-icon-chevron-right" aria-hidden="true" />
        </button>
        <button className="setup-row" onClick={onConnectTool} data-testid="setup-row-connect-tool">
          <span>Connect an AI tool you already use</span>
          <span className="jp-icon jp-icon-chevron-right" aria-hidden="true" />
        </button>
      </div>
      <p className="footnote">
        You can change this later. Nothing here is saved in the project.
      </p>
    </div>
  );
}

export interface SetupApiKeyProps {
  setup: SetupStatusPayload;
  // Providers with a stored key, by hint; the key itself never reaches the page.
  savedKeys: SavedKeyInfo[];
  onCheck: (provider: string, apiKey: string) => void;
  onCancel: () => void;
  onRemove: (provider: string) => void;
  onBack: () => void;
}

export function SetupApiKey({ setup, savedKeys, onCheck, onCancel, onRemove, onBack }: SetupApiKeyProps) {
  const [provider, setProvider] = useState(DEFAULT_PROVIDER);
  const [key, setKey] = useState('');
  // Replace opens an empty field beside the saved key, which stays until the
  // new one passes its check.
  const [replacing, setReplacing] = useState(false);
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

  const checking = setup.phase === 'checking';
  const option = PROVIDERS.find((candidate) => candidate.id === provider)!;
  const saved = savedKeys.find((candidate) => candidate.provider === provider);

  // A key stored or removed, or another provider chosen, ends a replacement:
  // what the field would have replaced is no longer there.
  useEffect(() => {
    setReplacing(false);
    setKey('');
  }, [provider, saved?.hint]);

  useEffect(() => {
    if (replacing) input.current?.focus();
  }, [replacing]);

  const paste = () => {
    navigator.clipboard
      .readText()
      .then((text) => {
        setKey(text.trim());
        input.current?.focus();
      })
      .catch(() => {
        // Refused at the OS or view level: leave the field alone and let the
        // user paste the way they always could.
        input.current?.focus();
      });
  };

  const submit = () => {
    if (!key.trim() || checking) return;
    onCheck(provider, key.trim());
  };

  const cancelReplace = () => {
    setReplacing(false);
    setKey('');
  };

  // Typing a key: none is saved, or one is being replaced. While a
  // replacement is checked, the saved key is what stays on show.
  const entering = !saved || (replacing && !checking);

  const keyField = entering ? (
    <div className="setup-key">
      <input
        ref={input}
        type="password"
        className="setup-key-input"
        aria-label={`${option.label} API key`}
        placeholder={saved ? 'Paste the new key' : `Paste your ${option.label} key (${option.placeholder})`}
        value={key}
        disabled={checking}
        onChange={(event) => setKey(event.target.value)}
        onKeyDown={(event) => {
          if (event.key === 'Enter') submit();
          if (event.key === 'Escape' && saved) cancelReplace();
        }}
      />
      {pasteAvailable && (
        <button className="link" onClick={paste} disabled={checking}>
          Paste
        </button>
      )}
    </div>
  ) : (
    <div
      className={saved.rejected && !checking ? 'setup-key setup-key--saved setup-key--rejected' : 'setup-key setup-key--saved'}
      data-testid="setup-key-saved"
    >
      <span className="setup-key-hint" aria-label={`Saved ${option.label} key ${saved.hint}`}>
        {saved.hint}
      </span>
      {!checking && (
        <>
          <button className="link" onClick={() => setReplacing(true)}>
            Replace
          </button>
          <button className="link setup-key-remove" onClick={() => onRemove(provider)}>
            Remove
          </button>
        </>
      )}
    </div>
  );

  return (
    <div className="pane-state setup" data-testid="setup-api-key">
      <div className="setup-card">
        <SetupScreenTitle label="Your own API key" onBack={onBack} />
        <p>Billed by your provider, not JusPrin. Stored on this machine only.</p>

        <div className="setup-tabs" role="tablist" aria-label="Provider">
          {PROVIDERS.map((candidate) => (
            <button
              key={candidate.id}
              role="tab"
              aria-selected={candidate.id === provider}
              className={candidate.id === provider ? 'setup-tab selected' : 'setup-tab'}
              disabled={!candidate.available}
              title={candidate.available ? undefined : `${candidate.label} support arrives in a later JusPrin release`}
              onClick={() => setProvider(candidate.id)}
            >
              {candidate.label}
            </button>
          ))}
        </div>

        {saved && !entering && (
          saved.rejected && !checking ? (
            <span className="setup-key-status setup-key-status--rejected" role="alert" data-testid="setup-key-rejected">
              <span className="jp-icon jp-icon-circle-alert" aria-hidden="true" /> The provider rejected this key
            </span>
          ) : (
            <span className="setup-key-status" data-testid="setup-key-status">
              <span className="jp-icon jp-icon-check" aria-hidden="true" /> Key saved on this computer
            </span>
          )
        )}

        {keyField}

        {saved && !entering && !checking && (
          <p className="setup-key-note">
            {saved.rejected
              ? 'Replace it with a working key, or remove it.'
              : 'JusPrin uses this key for every chat. Replace it to use another one.'}
          </p>
        )}

        {saved && !entering && setup.phase === 'verified' && setup.provider === provider && (
          <div className="setup-actions">
            <SetupResult setup={setup} />
          </div>
        )}

        {saved && !entering && checking ? (
          <div className="setup-actions">
            <span className="setup-result">Checking…</span>
            <button className="link setup-cancel" onClick={onCancel}>
              Cancel
            </button>
          </div>
        ) : (
          entering && (
            <div className="setup-actions">
              <button className="primary" onClick={submit} disabled={checking || key.trim().length === 0}>
                Check key
              </button>
              {checking && (
                <button className="link setup-cancel" onClick={onCancel}>
                  Cancel
                </button>
              )}
              {saved && !checking && (
                <>
                  <button className="link setup-cancel" onClick={cancelReplace}>
                    Cancel
                  </button>
                  <span className="setup-keeps">Keeps {saved.hint}</span>
                </>
              )}
              <SetupResult setup={setup} />
            </div>
          )
        )}

        <p className="footnote">
          Each chat starts on this provider’s recommended model. You can switch the model, or the provider, in any chat.
        </p>
      </div>
    </div>
  );
}

function SetupResult({ setup }: { setup: SetupStatusPayload }) {
  if (setup.phase === 'checking') return <span className="setup-result">Checking…</span>;
  if (setup.phase === 'verified')
    return (
      <span className="setup-result ok" data-testid="setup-verified">
        <span className="jp-icon jp-icon-check" aria-hidden="true" /> replied in {seconds(setup.elapsedMs ?? 0)}
      </span>
    );
  if (setup.phase === 'error')
    return (
      <span className="setup-result error" role="alert" data-testid="setup-error">
        {setup.error?.message ?? 'The key could not be checked.'}
      </span>
    );
  return null;
}

export interface ConnectedBannerProps {
  provider: string;
  warning?: string;
  onDismiss: () => void;
}

// Shown once, immediately after setup succeeds. The durable record of how the
// Agent is connected lives in Preferences; this is only the confirmation that
// the thing the user just did worked.
export function ConnectedBanner({ provider, warning, onDismiss }: ConnectedBannerProps) {
  return (
    <div className={warning ? 'setup-banner warning' : 'setup-banner'} data-testid="setup-connected">
      <span className="setup-banner-text">
        <span className="jp-icon jp-icon-check" aria-hidden="true" />
        <span>Connected · your own {providerLabel(provider)} key{warning ? ` — ${warning}` : ''}</span>
      </span>
      <button className="icon" onClick={onDismiss} aria-label="Dismiss">
        <span className="jp-icon jp-icon-close" aria-hidden="true" />
      </button>
    </div>
  );
}
