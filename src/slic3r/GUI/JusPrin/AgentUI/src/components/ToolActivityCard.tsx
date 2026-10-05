// One native tool action, rendered from the authoritative activity record
// the host pushes. The card only submits typed decisions; the native
// coordinator owns the state machine, so a stale button click is a benign
// no-op there rather than a second execution.

import { ToolActivityInfo, ToolStateName } from '../bridge/protocol';

interface Props {
  activity: ToolActivityInfo;
  onDecision: (actionId: string, decision: 'approve' | 'reject') => void;
  onCancel: (actionId: string) => void;
  readOnly?: boolean;
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

export function ToolActivityCard({ activity, onDecision, onCancel, readOnly = false }: Props) {
  const stale = activity.state === 'failed' && activity.error?.code === 'stale_revision';
  const percent =
    activity.progress.total > 0 ? Math.round((100 * activity.progress.current) / activity.progress.total) : 0;

  return (
    <div className={`tool-card state-${activity.state}`} data-testid={`tool-${activity.actionId}`}
      title={`${activity.tool} · ${activity.server}`}>
      <div className="tool-title">{activity.title}</div>

      {activity.state === 'pending' && (
        <>
          <div className="tool-state"><span className="tool-status-icon pending" aria-hidden="true" />{stateLabels.pending}</div>
          <div className="tool-actions">
            <button className="primary" disabled={readOnly} onClick={() => onDecision(activity.actionId, 'approve')}>
              Approve
            </button>
            <button disabled={readOnly} onClick={() => onDecision(activity.actionId, 'reject')}>Reject</button>
          </div>
        </>
      )}

      {(activity.state === 'approved' || activity.state === 'running') && (
        <>
          <div className="tool-state"><span className="tool-status-icon running" aria-hidden="true" />
            {activity.state === 'running' ? `Running…${activity.progress.total > 0 ? ` · ${percent}%` : ''}` : stateLabels.approved}
          </div>
          <progress aria-label={`${activity.title} progress`}
            max={activity.progress.total > 0 ? activity.progress.total : undefined}
            value={activity.progress.total > 0 ? activity.progress.current : undefined} />
          <button className="tool-cancel" disabled={readOnly} onClick={() => onCancel(activity.actionId)}>Cancel</button>
        </>
      )}

      {activity.state === 'succeeded' && <div className="tool-state done"><span className="tool-status-icon done" aria-hidden="true" />{stateLabels.succeeded}</div>}

      {activity.state === 'failed' && (
        <div className="tool-error"><span className="tool-status-icon failed" aria-hidden="true" />
          {stale
            ? 'The project changed after this was proposed, so it was not run. Ask the Agent again.'
            : activity.error?.message ?? 'The action failed.'}
        </div>
      )}

      {(activity.state === 'cancelled' || activity.state === 'rejected') && (
        <div className="tool-state"><span className="tool-status-icon cancelled" aria-hidden="true" />{stateLabels[activity.state]}</div>
      )}
    </div>
  );
}
