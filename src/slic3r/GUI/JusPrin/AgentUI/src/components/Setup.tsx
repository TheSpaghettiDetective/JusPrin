// The Agent setup surface. It replaces the dock's body in place rather than
// opening a dialog: setting up an Agent changes nothing about the print, so
// it stays inside the same panel the offer was made in, and every screen can
// be backed out of without leaving anything behind. Setup shows one way of
// connecting at a time, drawn with the same rows Preferences › Agent uses
// (AgentConnection.tsx).

import { SavedKeyInfo, SetupStatusPayload } from '../bridge/protocol';
import { AccountRow, ProviderKeys, providerLabel } from './AgentConnection';

export interface SetupChooserProps {
  onUseApiKey: () => void;
  onConnectTool: () => void;
  onDismiss: () => void;
}

// The screen's title is its way back: one target, the chevron and the name.
export function SetupScreenTitle({ label, onBack }: { label: string; onBack: () => void }) {
  return (
    <h1 className="setup-title">
      <button type="button" className="setup-back" onClick={onBack} aria-label="Back to setup options">
        <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" />
        {label}
      </button>
    </h1>
  );
}

// Preferences › Agent is designed but not built, so the footnote cannot point
// there yet.
function SetupFootnote() {
  return <p className="footnote setup-footnote">You can change this later. Nothing here is saved in the project.</p>;
}

export function SetupChooser({ onUseApiKey, onConnectTool, onDismiss }: SetupChooserProps) {
  return (
    <div className="pane-state setup" data-testid="setup-chooser">
      <div className="setup-card">
        <div className="setup-card-head">
          <h1>Set up the agent</h1>
          <button className="icon" onClick={onDismiss} aria-label="Close setup">
            <span className="jp-icon jp-icon-close" aria-hidden="true" />
          </button>
        </div>
        <p>It reads what you ask for and writes the settings underneath. It needs somewhere to think.</p>
        <AccountRow />
        <div className="setup-ways">
          <p className="setup-ways-title">If you know what these are</p>
          <button className="setup-row" onClick={onUseApiKey} data-testid="setup-row-api-key">
            <span>Your own API key</span>
            <span className="jp-icon jp-icon-chevron-right" aria-hidden="true" />
          </button>
          <button className="setup-row" onClick={onConnectTool} data-testid="setup-row-connect-tool">
            <span>Connect an AI tool you already use</span>
            <span className="jp-icon jp-icon-chevron-right" aria-hidden="true" />
          </button>
        </div>
      </div>
      <SetupFootnote />
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
  return (
    <div className="pane-state setup" data-testid="setup-api-key">
      <SetupScreenTitle label="Your own API key" onBack={onBack} />
      <ProviderKeys setup={setup} savedKeys={savedKeys} onCheck={onCheck} onCancel={onCancel} onRemove={onRemove} />
      <SetupFootnote />
    </div>
  );
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
    <div className={warning ? 'setup-banner warning' : 'setup-banner'} role="status" data-testid="setup-connected">
      <span className={`jp-icon jp-icon-${warning ? 'triangle-alert' : 'check'}`} aria-hidden="true" />
      <span className="setup-banner-text">
        Connected · your own {providerLabel(provider)} key{warning ? ` — ${warning}` : ''}
      </span>
      <button className="icon" onClick={onDismiss} aria-label="Dismiss">
        <span className="jp-icon jp-icon-close" aria-hidden="true" />
      </button>
    </div>
  );
}
