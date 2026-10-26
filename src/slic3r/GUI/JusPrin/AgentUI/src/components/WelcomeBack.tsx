// The welcome-back window: what JusPrin copied from JusPrin v1 on this
// computer, and the JusPrin account v1 was signed in to (canvas "JusPrin v2",
// onboarding page, row "When JusPrin v1 is on this computer": V1-01 imported,
// V1-02 partly imported, V1-03 sign-in expired). Shown once, after the first
// start that imported it.

import { useEffect, useRef, useState } from 'react';
import { AccountStatusPayload, SavedKeyInfo, WelcomeBackPayload } from '../bridge/protocol';

export interface WelcomeBackActions {
  checkAccount: () => void;
  signIn: () => void;
  cancelSignIn: () => void;
  close: () => void;
}

export interface WelcomeBackProps {
  report: WelcomeBackPayload;
  // The account v1 was signed in to, from setup_keys; absent when v1 was not.
  account?: SavedKeyInfo;
  status: AccountStatusPayload;
  actions: WelcomeBackActions;
}

// "Generic PLA, Silk PETG, 0.20 mm Strong and 7 more".
function someOf(names: string[], shown: number, separator: string): string {
  if (names.length <= shown) return names.join(separator);
  return `${names.slice(0, shown).join(separator)} and ${names.length - shown} more`;
}

function ImportedRow({ title, detail, count, partial = false }: {
  title: string; detail: string; count?: string; partial?: boolean;
}) {
  return (
    <div className="welcome-row">
      <span className={`jp-icon ${partial ? 'jp-icon-triangle-alert welcome-icon--warning' : 'jp-icon-check welcome-icon--done'}`}
        aria-hidden="true" />
      <div className="welcome-row-copy">
        <span className="welcome-row-title">{title}</span>
        <span className="welcome-row-detail">{detail}</span>
      </div>
      {count && <span className="welcome-row-count">{count}</span>}
    </div>
  );
}

function AccountBox({ account, status, actions }: Pick<WelcomeBackProps, 'account' | 'status' | 'actions'>) {
  const [copied, setCopied] = useState(false);
  if (status.phase === 'browser') {
    return (
      <div className="welcome-account">
        <div className="welcome-account-line">
          <div className="welcome-row-copy">
            <span className="welcome-account-title">Finish signing in in your browser</span>
            <span className="welcome-row-detail">
              A JusPrin tab just opened. Sign in there, or create an account, and come back. This notices on its own.
            </span>
          </div>
          <button type="button" className="link" onClick={actions.cancelSignIn}>Cancel</button>
        </div>
        {status.url && (
          <p className="agent-row-note">
            Didn’t open?{' '}
            <button type="button" className="link" onClick={() => {
              navigator.clipboard.writeText(status.url!).then(() => setCopied(true), () => setCopied(false));
            }}>{copied ? 'Link copied' : 'Copy link'}</button>
          </p>
        )}
      </div>
    );
  }
  if (status.phase === 'checking') {
    return (
      <div className="welcome-account" data-testid="welcome-account">
        <span className="welcome-account-title">Checking your JusPrin v1 sign-in…</span>
      </div>
    );
  }
  if (account && !account.rejected) {
    return (
      <div className="welcome-account" data-testid="welcome-account">
        <div className="welcome-account-line">
          <span className="jp-icon jp-icon-check welcome-icon--done" aria-hidden="true" />
          <div className="welcome-row-copy">
            <span className="welcome-account-title">{account.account ? `Signed in as ${account.account}` : 'Signed in'}</span>
            <span className="welcome-row-detail">The same account you used in JusPrin v1</span>
          </div>
        </div>
      </div>
    );
  }
  return (
    <div className="welcome-account" data-testid="welcome-account">
      <div className="welcome-account-line">
        <span className="jp-icon jp-icon-triangle-alert welcome-icon--warning" aria-hidden="true" />
        <div className="welcome-row-copy">
          <span className="welcome-account-title">{account ? 'Sign in again to use the agent' : 'Sign in to use the agent'}</span>
          <span className="welcome-row-detail">
            {account
              ? `Your JusPrin v1 sign-in${account.account ? ` for ${account.account}` : ''} has expired.`
              : 'JusPrin v1 wasn’t signed in on this computer.'}
          </span>
        </div>
      </div>
      {status.error && <p className="agent-key-error" role="alert">{status.error.message}</p>}
      <button type="button" className="secondary welcome-account-action" onClick={actions.signIn}>Continue with JusPrin</button>
    </div>
  );
}

export function WelcomeBack({ report, account, status, actions }: WelcomeBackProps) {
  // The sign-in v1 left is checked once, when the window opens: it may have
  // been revoked or ended since v1 last used it.
  const checked = useRef(false);
  useEffect(() => {
    if (account && !checked.current) {
      checked.current = true;
      actions.checkAccount();
    }
  }, [account, actions]);

  const unusable = new Set(report.unusable);
  const usablePrinters = report.printers.filter((name) => !unusable.has(name));
  const usablePresets = report.presets.filter((name) => !unusable.has(name));
  const partial = report.unusable.length > 0 || report.failed.length > 0;
  const count = (usable: string[], all: string[]) => usable.length === all.length ? `${all.length}` : `${usable.length} of ${all.length}`;

  return (
    <div className="welcome" data-testid="welcome-back">
      <div className="welcome-card">
        <div className="welcome-head">
          <span className="welcome-eyebrow">WELCOME BACK</span>
          <h1 className="welcome-title">{partial ? 'Most of your JusPrin v1 setup is here' : 'Your JusPrin v1 setup is here'}</h1>
          <p className="welcome-intro">We copied it from JusPrin v1 on this computer. JusPrin v1 itself is unchanged.</p>
        </div>

        <AccountBox account={account} status={status} actions={actions} />

        <div className="welcome-list" aria-label="Imported">
          <span className="welcome-eyebrow">IMPORTED</span>
          {report.printers.length > 0 && (
            <ImportedRow title="Printers" detail={someOf(usablePrinters.length ? usablePrinters : report.printers, 2, ' · ')}
              count={count(usablePrinters, report.printers)} partial={usablePrinters.length < report.printers.length} />
          )}
          {report.presets.length > 0 && (
            <ImportedRow title="Filament and process presets" detail={someOf(usablePresets.length ? usablePresets : report.presets, 3, ', ')}
              count={count(usablePresets, report.presets)} partial={usablePresets.length < report.presets.length} />
          )}
          {report.connections.length > 0 && (
            <ImportedRow title="Printer connections" detail={someOf(report.connections, 2, ' · ')} count={`${report.connections.length}`} />
          )}
          {report.preferences && <ImportedRow title="Preferences" detail="Units, language and recent files" />}
          <p className="agent-row-note">
            {report.unusable.length > 0 &&
              `“${report.unusable[0]}”${report.unusable.length > 1 ? ` and ${report.unusable.length - 1} more` : ''} can’t be used here: ${report.unusable.length > 1 ? 'they are' : 'it is'} based on a printer JusPrin doesn’t have. `}
            {report.failed.length > 0 && `${report.failed.length} ${report.failed.length > 1 ? 'files' : 'file'} couldn’t be copied. `}
            Chats from JusPrin v1 stay in JusPrin v1.
          </p>
        </div>

        <button type="button" className="primary welcome-continue" onClick={actions.close}>Continue</button>
        <p className="agent-row-note">Imported presets are in your printer and filament menus. Rename or delete them there.</p>
      </div>
    </div>
  );
}
