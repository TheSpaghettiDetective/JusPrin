// One Agent turn's tool work, drawn as the thread's quiet line (the design
// system's Chat/System Line) rather than as a card per call. While the turn
// runs the line names the action in flight and counts the seconds since the
// person's message; once the turn ends it reads how long the turn took and
// how many actions it ran, and opens into one line per action. Stopping a
// turn is the composer's Stop; the line offers none. Work with measured
// progress keeps its own card, with Cancel, under the line.

import { useEffect, useState } from 'react';
import { ToolActivityInfo } from '../bridge/protocol';
import { ToolActivityCard } from './ToolActivityCard';

interface Props {
  activities: ToolActivityInfo[];
  // The turn is still streaming: the model is speaking or waiting on a tool.
  working: boolean;
  // The person's message that started the turn, and the turn's last message.
  // Either missing leaves the elapsed time off the line.
  startedAt?: string;
  endedAt?: string;
  onCancel: (actionId: string) => void;
  readOnly?: boolean;
}

// "44s" under a minute, "1m 9s" from a minute on.
export function elapsedWords(milliseconds: number): string {
  const seconds = Math.max(0, Math.floor(milliseconds / 1000));
  return seconds >= 60 ? `${Math.floor(seconds / 60)}m ${seconds % 60}s` : `${seconds}s`;
}

function stamp(value?: string): number | undefined {
  const time = value ? Date.parse(value) : NaN;
  return Number.isNaN(time) ? undefined : time;
}

// The collapsed line's words. Working: the running action's title, or
// "Working on it…" between calls, then the seconds so far. Finished: the
// duration and the count, then how many failed when any did.
export function turnLineText(activities: ToolActivityInfo[], working: boolean, elapsed?: string): string {
  if (working) {
    const running = activities.find((activity) => activity.state === 'running');
    const words = running ? `${running.title}…` : 'Working on it…';
    return elapsed ? `${words} · ${elapsed}` : words;
  }
  const count = `${activities.length} ${activities.length === 1 ? 'action' : 'actions'}`;
  const failed = activities.filter((activity) => activity.state === 'failed').length;
  const parts = [elapsed ? `Worked for ${elapsed} · ${count}` : count];
  if (failed > 0) parts.push(`${failed} failed`);
  return parts.join(' · ');
}

// One action's line in the opened log: its title, and how it ended when not
// well. A stale call reads as the card reads it.
export function actionLineText(activity: ToolActivityInfo): string {
  switch (activity.state) {
    case 'failed':
      return `${activity.title} · ${
        activity.error?.code === 'stale_revision'
          ? 'The project changed before this could run.'
          : activity.error?.message ?? 'The action failed.'
      }`;
    case 'cancelled':
      return `${activity.title} · Cancelled`;
    default:
      return activity.title;
  }
}

export function TurnActivity({ activities, working, startedAt, endedAt, onCancel, readOnly = false }: Props) {
  const [open, setOpen] = useState(false);
  const started = stamp(startedAt);
  const [now, setNow] = useState(() => Date.now());
  // A ticking clock only while the turn runs and has a start to count from.
  useEffect(() => {
    if (!working || started === undefined) return;
    setNow(Date.now());
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, [working, started]);
  const ended = stamp(endedAt);
  const elapsed =
    started === undefined ? undefined
      : working ? elapsedWords(now - started)
        : ended === undefined ? undefined
          : elapsedWords(ended - started);
  const text = turnLineText(activities, working, elapsed);
  const measured = activities.filter((activity) => activity.state === 'running' && activity.progress.total > 0);

  return (
    <div className="turn-activity" data-testid="turn-activity">
      <button type="button" className="turn-activity-line" aria-expanded={open} disabled={activities.length === 0}
        onClick={() => setOpen(!open)}>
        {working && <span className="jp-icon jp-icon-rotate-cw turn-activity-spinner" aria-hidden="true" />}
        <span className={`turn-activity-text${working ? ' shimmer' : ''}`} role={working ? 'status' : undefined}>{text}</span>
        {/* A disclosure chevron right after the words, as ChatGPT and Claude
            draw theirs: right while closed, down once open. */}
        {activities.length > 0 && (
          <span className={`jp-icon jp-icon-chevron-${open ? 'down' : 'right'}`} aria-hidden="true" />
        )}
      </button>
      {open && (
        <div className="turn-activity-log" role="list" aria-label="Actions">
          {activities.map((activity) => (
            <div key={activity.actionId} role="listitem" data-testid={`action-${activity.actionId}`}
              className={`turn-activity-action state-${activity.state}${activity.actionClass === 'mutation' ? ' edit' : ''}`}
              title={`${activity.tool} · ${activity.server}`}>
              {activity.state === 'running' && <span className="jp-icon jp-icon-rotate-cw turn-activity-spinner" aria-hidden="true" />}
              <span className="turn-activity-action-text">{actionLineText(activity)}</span>
            </div>
          ))}
        </div>
      )}
      {measured.map((activity) => (
        <ToolActivityCard key={activity.actionId} activity={activity} onCancel={onCancel} readOnly={readOnly} />
      ))}
    </div>
  );
}
