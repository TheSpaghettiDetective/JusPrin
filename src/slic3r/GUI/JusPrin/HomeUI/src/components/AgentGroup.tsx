// The Agent group at the top of Home's right column (boards first-run-09 and
// 10). While no agent is set up it holds the welcome's three doors under "Set
// up the agent"; right after signing in it shows the account and its
// credits, once, until the person dismisses it. The host says which.

import type { OnboardingState } from '../bridge/protocol';
import { balanceRows } from '../../../AgentUI/src/components/BalancePanel';
import { creditCount, creditNumber } from '../../../AgentUI/src/credits';
import { CloseGlyph, ConnectedGlyph } from './Glyphs';
import { FreeDoors, SignInDoor } from './Onboarding';

type Action = (type: string, payload?: object) => void;

function Account({ onboarding, onAction }: { onboarding: OnboardingState; onAction: Action }) {
  const account = onboarding.account;
  return (
    <div className="agent-card">
      <div className="agent-card-head">
        <ConnectedGlyph />
        <span className="agent-card-title">
          <strong>JusPrin account</strong>
          <span>Connected</span>
        </span>
        <button type="button" className="agent-card-close" aria-label="Dismiss" onClick={() => onAction('agent_card_dismiss')}>
          <CloseGlyph />
        </button>
      </div>
      {account && (
        <>
          <strong>{creditCount(account.credits.total)} left</strong>
          {balanceRows(account).map((row) => (
            <div key={row.key} className="agent-card-lot">
              <div className="agent-card-lot-line">
                <strong>{row.name}</strong>
                <span>{creditNumber(row.left)} of {creditNumber(row.of)} left</span>
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

export function AgentGroup({ onboarding, onAction }: { onboarding: OnboardingState; onAction: Action }) {
  if (onboarding.agentCard === 'none') return null;
  return (
    <section className="agent-group" aria-label="Agent">
      <span className="section-label">Agent</span>
      {onboarding.agentCard === 'account' ? (
        <Account onboarding={onboarding} onAction={onAction} />
      ) : (
        <div className="agent-card">
          <strong className="agent-card-heading">Set up the agent</strong>
          <SignInDoor onboarding={onboarding} onAction={onAction} />
          <FreeDoors onAction={onAction} disabled={onboarding.signIn === 'checking'} />
        </div>
      )}
    </section>
  );
}
