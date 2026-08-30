// One native tool action, rendered from the authoritative activity record
// the host pushes. The card only submits typed decisions; the native
// coordinator owns the state machine, so a stale button click is a benign
// no-op there rather than a second execution.

import { ToolActivityInfo, ToolStateName } from '../bridge/protocol';

interface Props {
  activity: ToolActivityInfo;
  onDecision: (actionId: string, decision: 'approve' | 'reject') => void;
  onCancel: (actionId: string) => void;
}

const stateLabels: Record<ToolStateName, string> = {
  pending: 'Waiting for your approval',
  approved: 'Approved',
  running: 'Running…',
  succeeded: 'Done',
  failed: 'Failed',
  cancelled: 'Cancelled — nothing was changed',
  rejected: 'Rejected — nothing was changed',
};

export function ToolActivityCard({ activity, onDecision, onCancel }: Props) {
  const stale = activity.state === 'failed' && activity.error?.code === 'stale_revision';
  const percent =
    activity.progress.total > 0 ? Math.round((100 * activity.progress.current) / activity.progress.total) : 0;

  return (
    <div className={`tool-card state-${activity.state}`} data-testid={`tool-${activity.actionId}`}>
      <div className="tool-title">{activity.title}</div>
      <div className="tool-meta">
        {activity.tool} · {activity.server}
      </div>

      {activity.state === 'pending' && (
        <div className="tool-actions">
          <span className="tool-state">{stateLabels.pending}</span>
          <button className="primary" onClick={() => onDecision(activity.actionId, 'approve')}>
            Approve
          </button>
          <button onClick={() => onDecision(activity.actionId, 'reject')}>Reject</button>
        </div>
      )}

      {(activity.state === 'approved' || activity.state === 'running') && (
        <div className="tool-actions">
          <progress aria-label={`${activity.title} progress`} max={activity.progress.total} value={activity.progress.current} />
          <span className="tool-state">{activity.state === 'running' ? `${percent}%` : stateLabels.approved}</span>
          <button onClick={() => onCancel(activity.actionId)}>Cancel</button>
        </div>
      )}

      {activity.state === 'succeeded' && <div className="tool-state done">{stateLabels.succeeded}</div>}

      {activity.state === 'failed' && (
        <div className="tool-error">
          {stale
            ? 'The project changed after this was proposed, so it was not run. Ask the Agent again.'
            : activity.error?.message ?? 'The action failed.'}
        </div>
      )}

      {(activity.state === 'cancelled' || activity.state === 'rejected') && (
        <div className="tool-state">{stateLabels[activity.state]}</div>
      )}
    </div>
  );
}
