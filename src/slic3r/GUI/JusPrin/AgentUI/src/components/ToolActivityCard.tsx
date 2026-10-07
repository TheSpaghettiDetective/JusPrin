// One native tool action, rendered from the authoritative activity record
// the host pushes. The card only submits typed decisions; the native
// coordinator owns the state machine, so a stale button click is a benign
// no-op there rather than a second execution.

import { ReactNode } from 'react';
import { ToolActivityInfo, ToolStateName } from '../bridge/protocol';
import { Progress } from './Progress';

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
  failed: 'The action failed.',
  cancelled: 'Cancelled — nothing was changed',
  rejected: 'Rejected — nothing was changed',
};

// Which glyph a state's line leads with. Colour is never the only signal: the
// line always has its icon and its words.
const stateIcons: Record<ToolStateName, string> = {
  pending: 'pending',
  approved: 'running',
  running: 'running',
  succeeded: 'done',
  failed: 'failed',
  cancelled: 'cancelled',
  rejected: 'cancelled',
};

// The one status line every state of a tool or plan card has.
export function ToolStatus({ state, children }: { state: ToolStateName; children: ReactNode }) {
  return (
    <div className={`tool-state ${stateIcons[state]}`}>
      <span className={`tool-status-icon ${stateIcons[state]}`} aria-hidden="true" />
      <span className="tool-state-text">{children}</span>
    </div>
  );
}

export function ToolActivityCard({ activity, onDecision, onCancel, readOnly = false }: Props) {
  const { state } = activity;
  const stale = state === 'failed' && activity.error?.code === 'stale_revision';
  const measured = activity.progress.total > 0;
  const percent = measured ? Math.round((100 * activity.progress.current) / activity.progress.total) : 0;
  const message =
    state === 'running' && measured
      ? `Running… · ${percent}%`
      : state === 'failed'
        ? stale
          ? 'The project changed after this was proposed, so it was not run. Ask the Agent again.'
          : activity.error?.message ?? stateLabels.failed
        : stateLabels[state];

  return (
    <div className={`tool-card state-${state}`} data-testid={`tool-${activity.actionId}`}
      title={`${activity.tool} · ${activity.server}`}>
      <div className="tool-title">{activity.title}</div>
      <ToolStatus state={state}>{message}</ToolStatus>

      {state === 'pending' && (
        <div className="tool-actions">
          <button className="primary" disabled={readOnly} onClick={() => onDecision(activity.actionId, 'approve')}>
            Approve
          </button>
          <button disabled={readOnly} onClick={() => onDecision(activity.actionId, 'reject')}>Reject</button>
        </div>
      )}

      {(state === 'approved' || state === 'running') && (
        <>
          <Progress label={`${activity.title} progress`}
            value={state === 'running' && measured ? activity.progress.current : undefined}
            max={state === 'running' && measured ? activity.progress.total : undefined} />
          <div className="tool-actions">
            <button disabled={readOnly} onClick={() => onCancel(activity.actionId)}>Cancel</button>
          </div>
        </>
      )}
    </div>
  );
}
