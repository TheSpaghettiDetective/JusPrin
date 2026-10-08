// One native tool action, rendered from the authoritative activity record
// the host pushes. The native coordinator owns the state machine.

import { ReactNode } from 'react';
import { ToolActivityInfo, ToolStateName } from '../bridge/protocol';
import { Progress } from './Progress';

interface Props {
  activity: ToolActivityInfo;
  onCancel: (actionId: string) => void;
  readOnly?: boolean;
}

const stateLabels: Record<ToolStateName, string> = {
  pending: 'Starting…',
  input_required: 'Waiting for input',
  running: 'Running…',
  succeeded: 'Done',
  failed: 'The action failed.',
  cancelled: 'Cancelled — nothing was changed',
};

// Which glyph a state's line leads with. Colour is never the only signal: the
// line always has its icon and its words.
const stateIcons: Record<ToolStateName, string> = {
  pending: 'pending',
  input_required: 'pending',
  running: 'running',
  succeeded: 'done',
  failed: 'failed',
  cancelled: 'cancelled',
};

// The one status line every state of a tool card has.
export function ToolStatus({ state, children }: { state: ToolStateName; children: ReactNode }) {
  return (
    <div className={`tool-state ${stateIcons[state]}`}>
      <span className={`tool-status-icon ${stateIcons[state]}`} aria-hidden="true" />
      <span className="tool-state-text">{children}</span>
    </div>
  );
}

export function ToolActivityCard({ activity, onCancel, readOnly = false }: Props) {
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

      {state === 'running' && (
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
