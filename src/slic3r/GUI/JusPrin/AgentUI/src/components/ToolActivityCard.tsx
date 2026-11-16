// One native tool action, rendered from the authoritative activity record
// the host pushes. The native coordinator owns the state machine.

import { ReactNode } from 'react';
import { isStale, ToolActivityInfo, ToolStateName } from '../bridge/protocol';
import { Progress } from './Progress';
import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';

interface Props {
  activity: ToolActivityInfo;
  onCancel: (actionId: string) => void;
  readOnly?: boolean;
}

const stateLabels: Record<ToolStateName, string> = {
  running: t`Running…`,
  succeeded: t`Done`,
  failed: t`The action failed.`,
  cancelled: t`Cancelled — nothing was changed`,
};

// Which glyph a state's line leads with. Colour is never the only signal: the
// line always has its icon and its words.
const stateIcons: Record<ToolStateName, string> = {
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

// A refusal whose message is written for the model can carry the card's own
// line in its details as "shown".
function shownLine(activity: ToolActivityInfo): string | undefined {
  const shown = activity.error?.details?.shown;
  return typeof shown === 'string' && shown ? shown : undefined;
}

export function ToolActivityCard({ activity, onCancel, readOnly = false }: Props) {
  const { state } = activity;
  const stale = isStale(activity);
  const measured = activity.progress.total > 0;
  const title = activity.title;
  const percent = measured ? Math.round((100 * activity.progress.current) / activity.progress.total) : 0;
  const message =
    state === 'running' && measured
      ? t`Running… · ${percent}%`
      : state === 'failed'
        ? stale
          ? t`The project changed before this could run. Ask AI again.`
          : shownLine(activity) ?? activity.error?.message ?? stateLabels.failed
        : stateLabels[state];

  return (
    <div className={`tool-card state-${state}`} data-testid={`tool-${activity.actionId}`}
      title={`${activity.tool} · ${activity.server}`}>
      <div className="tool-title">{activity.title}</div>
      <ToolStatus state={state}>{message}</ToolStatus>

      {state === 'running' && (
        <>
          <Progress label={t`${title} progress`}
            value={state === 'running' && measured ? activity.progress.current : undefined}
            max={state === 'running' && measured ? activity.progress.total : undefined} />
          <div className="tool-actions">
            <button disabled={readOnly} onClick={() => onCancel(activity.actionId)}><Trans>Cancel</Trans></button>
          </div>
        </>
      )}
    </div>
  );
}
