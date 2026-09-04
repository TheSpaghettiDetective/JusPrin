// The Agent setup surface. It replaces the dock's body in place rather than
// opening a dialog: setting up an Agent changes nothing about the print, so
// it stays inside the same panel the offer was made in, and every screen can
// be backed out of without leaving anything behind.

import { useEffect, useRef, useState } from 'react';
import { SetupStatusPayload } from '../bridge/protocol';

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
  { id: 'anthropic', label: 'Anthropic', placeholder: 'sk-ant-…', available: false },
  { id: 'openai', label: 'OpenAI', placeholder: 'sk-…', available: true },
  { id: 'other', label: 'Other…', placeholder: '', available: false },
];

export const DEFAULT_PROVIDER = PROVIDERS.find((provider) => provider.available)!.id;

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

export function SetupChooser({ onUseApiKey, onConnectTool, onDismiss }: SetupChooserProps) {
  const laterTitle = 'This way of connecting an Agent arrives in a later JusPrin release';
  return (
    <div className="pane-state setup" data-testid="setup-chooser">
      <div className="setup-card">
        <div className="setup-card-head">
          <h1>Set up the agent</h1>
          <button className="icon" onClick={onDismiss} aria-label="Close setup">
            ✕
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
          <span aria-hidden="true">›</span>
        </button>
        <button className="setup-row" onClick={onConnectTool} data-testid="setup-row-connect-tool">
          <span>Connect an AI tool you already use</span>
          <span aria-hidden="true">›</span>
        </button>
      </div>
      <p className="footnote">
        Change this any time in Preferences › Agent. Nothing here is saved in the project.
      </p>
    </div>
  );
}

export interface SetupApiKeyProps {
  setup: SetupStatusPayload;
  onCheck: (provider: string, apiKey: string) => void;
  onCancel: () => void;
  onBack: () => void;
}

export function SetupApiKey({ setup, onCheck, onCancel, onBack }: SetupApiKeyProps) {
  const [provider, setProvider] = useState(DEFAULT_PROVIDER);
  const [key, setKey] = useState('');
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

  return (
    <div className="pane-state setup" data-testid="setup-api-key">
      <div className="setup-card">
        <div className="setup-card-head">
          <button className="icon" onClick={onBack} aria-label="Back to setup options">
            ‹
          </button>
          <h1>Your own API key</h1>
        </div>
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

        <div className="setup-key">
          <input
            ref={input}
            type="password"
            className="setup-key-input"
            aria-label={`${option.label} API key`}
            placeholder={option.placeholder}
            value={key}
            disabled={checking}
            onChange={(event) => setKey(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === 'Enter') submit();
            }}
          />
          {pasteAvailable && (
            <button className="link" onClick={paste} disabled={checking}>
              paste
            </button>
          )}
        </div>

        <div className="setup-actions">
          <button className="primary" onClick={submit} disabled={checking || key.trim().length === 0}>
            Check key
          </button>
          {checking && (
            <button className="link" onClick={onCancel}>
              Cancel
            </button>
          )}
          <SetupResult setup={setup} />
        </div>

        <p className="footnote">
          Which model to use is decided for you — there is a picker under Preferences, and you should not need it.
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
        ✓ replied in {seconds(setup.elapsedMs ?? 0)}
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
        ✓ Connected · your own {providerLabel(provider)} key
        {warning ? ` — ${warning}` : ''}
      </span>
      <button className="icon" onClick={onDismiss} aria-label="Dismiss">
        ✕
      </button>
    </div>
  );
}
