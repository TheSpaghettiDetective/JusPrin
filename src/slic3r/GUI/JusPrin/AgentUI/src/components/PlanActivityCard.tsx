// Tool calls that share a plan id, rendered as the one card the person
// decides. The native coordinator approves or rejects every waiting member on
// one decision and runs them in order; the card sends that decision once, for
// the first member still waiting.

import { ActionClassName, ToolActivityInfo, ToolStateName } from '../bridge/protocol';

interface Props {
  members: ToolActivityInfo[];
  headline?: string;
  // The in-app agent's turn is still running and may add members; the
  // decision waits for the whole plan.
  stillProposing?: boolean;
  onDecision: (actionId: string, decision: 'approve' | 'reject') => void;
  onCancel: (actionId: string) => void;
}

const classRank: Record<ActionClassName, number> = { read_only: 0, mutation: 1, destructive: 2 };

const memberLabels: Record<ToolStateName, string> = {
  pending: 'Waiting',
  approved: 'Queued',
  running: 'Running…',
  succeeded: 'Done',
  failed: 'Failed',
  cancelled: 'Cancelled',
  rejected: 'Rejected',
};

function groupState(members: ToolActivityInfo[]): ToolStateName {
  const states = members.map((member) => member.state);
  if (states.includes('pending')) return 'pending';
  if (states.includes('running') || states.includes('approved')) return 'running';
  if (states.includes('failed')) return 'failed';
  if (states.every((state) => state === 'succeeded')) return 'succeeded';
  return states.includes('rejected') ? 'rejected' : 'cancelled';
}

// The plan headline from the latest plan_set, when there is one.
export function planHeadline(activities: ToolActivityInfo[]): string | undefined {
  for (let index = activities.length - 1; index >= 0; --index) {
    const activity = activities[index];
    if (activity.tool === 'plan_set' && activity.state === 'succeeded' && typeof activity.arguments.headline === 'string')
      return activity.arguments.headline;
  }
  return undefined;
}

// A plan is its source, scope and id together, as the native coordinator
// groups it: two clients or chats choosing the same id never share a card.
export function planKey(activity: ToolActivityInfo): string | undefined {
  if (!activity.planId) return undefined;
  return [activity.source ?? 'agent', activity.planScope ?? '', activity.planId].join('\u001f');
}

// Each plan's members in proposal order, keyed by planKey.
export function planMembers(activities: ToolActivityInfo[]): Map<string, ToolActivityInfo[]> {
  const plans = new Map<string, ToolActivityInfo[]>();
  for (const activity of activities) {
    const key = planKey(activity);
    if (!key) continue;
    plans.set(key, [...(plans.get(key) ?? []), activity]);
  }
  return plans;
}

export function PlanActivityCard({ members, headline, stillProposing, onDecision, onCancel }: Props) {
  const state = groupState(members);
  const highest = members.reduce<ActionClassName>(
    (top, member) => (classRank[member.actionClass] > classRank[top] ? member.actionClass : top), 'read_only');
  const destructive = members.find((member) => member.actionClass === 'destructive');
  const waiting = members.find((member) => member.state === 'pending');
  const active = members.filter((member) => member.state === 'approved' || member.state === 'running');
  const done = members.filter((member) => member.state === 'succeeded').length;

  return (
    <div className={`tool-card plan-card state-${state}`} data-testid={`plan-${members[0].planId}`}>
      <div className="tool-title">{headline ?? `A plan of ${members.length} changes`}</div>
      <div className="tool-meta">
        {members.length} {members.length === 1 ? 'change' : 'changes'} · {highest === 'destructive' ? 'destructive' : 'changes the project'}
        {destructive && ` — “${destructive.title}” cannot be undone in JusPrin`}
      </div>
      <ol className="plan-members">
        {members.map((member) => (
          <li key={member.actionId} className={`plan-member state-${member.state}`}>
            <span className="plan-member-title">{member.title}</span>
            <span className="tool-state">
              {member.actionClass === 'destructive' ? 'Destructive · ' : ''}
              {memberLabels[member.state]}
            </span>
            {member.state === 'failed' && member.error && <div className="tool-error">{member.error.message}</div>}
          </li>
        ))}
      </ol>

      {waiting && (
        <div className="tool-actions">
          <span className="tool-state">{stillProposing ? 'The Agent is still adding to this plan' : 'Waiting for your approval'}</span>
          <button className="primary" disabled={stillProposing} onClick={() => onDecision(waiting.actionId, 'approve')}>
            Approve all
          </button>
          <button disabled={stillProposing} onClick={() => onDecision(waiting.actionId, 'reject')}>Reject all</button>
        </div>
      )}

      {!waiting && active.length > 0 && (
        <div className="tool-actions">
          <progress aria-label="Plan progress" max={members.length} value={done} />
          <span className="tool-state">{done} of {members.length}</span>
          <button onClick={() => active.forEach((member) => onCancel(member.actionId))}>Cancel</button>
        </div>
      )}

      {state === 'succeeded' && <div className="tool-state done">Done</div>}
      {state === 'rejected' && <div className="tool-state">Rejected — nothing was changed</div>}
    </div>
  );
}
