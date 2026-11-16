// The Agent group at the top of Home's right column (boards first-run-09, 10
// and 10b). While no agent is set up it holds the welcome's three doors under
// "Set up AI", until the person asks never to see them again; right after
// signing in it shows the account and its credits, once, until the person
// dismisses it. The host says which.

import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import type { OnboardingState } from '../bridge/protocol';
import { balanceRows, lotLeft } from '../../../AgentUI/src/components/BalancePanel';
import { SIGNUP_CREDITS, creditNumber, creditsLeft } from '../../../AgentUI/src/credits';
import { ConnectedGlyph } from './Glyphs';
import { OwnAccountDoor, SignInDoor } from './Onboarding';

type Action = (type: string, payload?: object) => void;

function Account({ onboarding, onAction }: { onboarding: OnboardingState; onAction: Action }) {
  const account = onboarding.account;
  const email = onboarding.accountEmail;
  return (
    <div className="agent-card">
      <div className="agent-card-head">
        <ConnectedGlyph />
        <span className="agent-card-title">
          <strong><Trans>Signed in as {email}</Trans></strong>
        </span>
        <button type="button" className="onboarding-link agent-card-hide" onClick={() => onAction('agent_card_dismiss')}>
          <Trans>Hide</Trans>
        </button>
      </div>
      {account && (
        <>
          <strong>{creditsLeft(account.credits.total)}</strong>
          {balanceRows(account).map((row) => (
            <div key={row.key} className="agent-card-lot">
              <div className="agent-card-lot-line">
                <strong>{row.name}</strong>
                <span>{lotLeft(row)}</span>
              </div>
              <div className="agent-card-bar">
                <span style={{ width: `${row.of > 0 ? Math.min(row.left / row.of, 1) * 100 : 0}%` }} />
              </div>
              <span className="agent-card-date">{row.date}</span>
            </div>
          ))}
        </>
      )}
    </div>
  );
}

export function AgentGroup({ onboarding, onAction, setupHidden, onHideSetup, onOpenPreferences }: {
  onboarding: OnboardingState;
  onAction: Action;
  setupHidden: boolean;
  onHideSetup: () => void;
  onOpenPreferences: () => void;
}) {
  const signupCredits = creditNumber(SIGNUP_CREDITS);
  if (onboarding.agentCard === 'none') {
    if (!setupHidden) return null;
    return (
      <section className="agent-group" aria-label={t`AI`}>
        <p className="agent-setup-hidden">
          <Trans>
            AI setup won’t show here again. You can set it up later in{' '}
            <button type="button" className="onboarding-link agent-setup-hidden-link" onClick={onOpenPreferences}>
              Preferences
            </button>, under the gear at the top right.
          </Trans>
        </p>
      </section>
    );
  }
  return (
    <section className="agent-group" aria-label={t`AI`}>
      {onboarding.agentCard === 'account' ? (
        <Account onboarding={onboarding} onAction={onAction} />
      ) : (
        <div className="agent-card">
          <div className="agent-card-head">
            <strong className="agent-card-heading"><Trans>Set up AI</Trans></strong>
            <button type="button" className="onboarding-link agent-card-hide" onClick={onHideSetup}>
              <Trans>Don’t show again</Trans>
            </button>
          </div>
          {onboarding.signIn === 'idle' && !onboarding.accountEmail && (
            <p className="agent-card-why">
              <Trans comment="{signupCredits} is the credits a new account is given, 500">Get {signupCredits} free credits for JusPrin’s AI, which specializes in 3D printing.</Trans>
            </p>
          )}
          <SignInDoor onboarding={onboarding} onAction={onAction} />
          <OwnAccountDoor onAction={onAction} disabled={onboarding.signIn === 'checking'} />
        </div>
      )}
    </section>
  );
}
