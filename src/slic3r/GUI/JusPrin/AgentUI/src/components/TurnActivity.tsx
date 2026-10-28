// One Agent turn's tool work, drawn as the thread's quiet line (the design
// system's Chat/System Line) rather than as a card per call. While the turn
// runs the line names the action in flight and counts the seconds since the
// person's message; once the turn ends it reads how long the turn took and
// how many actions it ran, and opens into one line per action. Each action
// line opens in turn into what a developer needs to debug it: the tool, its
// arguments, and its result or its error, with a button that copies them.
// A failed action carries the alert glyph, the danger colour and the word
// "Failed", so its state is never read from colour alone. Stopping a
// turn is the composer's Stop; the line offers none. Work with measured
// progress keeps its own card, with Cancel, under the line.

import { useEffect, useState } from 'react';
import { isStale, ToolActivityInfo } from '../bridge/protocol';
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
// well. A stale call did not run at all.
export function actionLineText(activity: ToolActivityInfo): string {
  switch (activity.state) {
    case 'failed':
      return `${activity.title} · ${isStale(activity) ? 'Not run' : 'Failed'}`;
    case 'cancelled':
      return `${activity.title} · Cancelled`;
    default:
      return activity.title;
  }
}

// A result is shown up to this many characters; a workspace read can run to
// tens of kilobytes, and Copy details carries the same cut text.
const RESULT_LIMIT = 4000;

function pretty(value: unknown): string {
  return JSON.stringify(value, null, 2) ?? '';
}

function hasFields(value: Record<string, unknown> | undefined): value is Record<string, unknown> {
  return value !== undefined && Object.keys(value).length > 0;
}

// The error's own fields, less the code and message already shown above them
// (the settings tools repeat both inside details).
function errorDetails(activity: ToolActivityInfo): Record<string, unknown> | undefined {
  const { error } = activity;
  if (!error?.details) return undefined;
  const rest = Object.fromEntries(Object.entries(error.details).filter(([key, value]) =>
    !(key === 'code' && value === error.code) && !(key === 'message' && value === error.message)));
  return hasFields(rest) ? rest : undefined;
}

function resultText(activity: ToolActivityInfo): string | undefined {
  if (!hasFields(activity.result)) return undefined;
  const text = pretty(activity.result);
  return text.length > RESULT_LIMIT
    ? `${text.slice(0, RESULT_LIMIT)}\n… ${text.length - RESULT_LIMIT} more characters`
    : text;
}

function toolLine(activity: ToolActivityInfo): string {
  return `${activity.tool} · ${activity.server} · ${activity.actionId}`;
}

// What Copy details puts on the clipboard: the opened details as plain text.
export function actionDetailsText(activity: ToolActivityInfo): string {
  const lines = [actionLineText(activity), `Tool: ${toolLine(activity)}`];
  if (activity.error) {
    lines.push(`Error: ${activity.error.code}`, activity.error.message);
    const details = errorDetails(activity);
    if (details) lines.push('Details:', pretty(details));
  }
  if (hasFields(activity.arguments)) lines.push('Arguments:', pretty(activity.arguments));
  const result = resultText(activity);
  if (result) lines.push('Result:', result);
  return lines.join('\n');
}

function ActionDetails({ activity }: { activity: ToolActivityInfo }) {
  const [copied, setCopied] = useState(false);
  const { error } = activity;
  const details = errorDetails(activity);
  const result = resultText(activity);
  const copy = () => {
    navigator.clipboard?.writeText(actionDetailsText(activity)).then(() => setCopied(true), () => setCopied(false));
  };
  return (
    <div className={`turn-activity-details${activity.state === 'failed' ? ' failed' : ''}`}
      data-testid={`action-details-${activity.actionId}`}>
      {error && (
        <div className="turn-activity-details-section">
          <span className="turn-activity-details-label">Error</span>
          <span className="turn-activity-code">{error.code}</span>
          <span className="turn-activity-error-message">{error.message}</span>
          {details && <pre className="turn-activity-code">{pretty(details)}</pre>}
        </div>
      )}
      <div className="turn-activity-details-section">
        <span className="turn-activity-details-label">Tool</span>
        <span className="turn-activity-code">{toolLine(activity)}</span>
      </div>
      {hasFields(activity.arguments) && (
        <div className="turn-activity-details-section">
          <span className="turn-activity-details-label">Arguments</span>
          <pre className="turn-activity-code">{pretty(activity.arguments)}</pre>
        </div>
      )}
      {result && (
        <div className="turn-activity-details-section">
          <span className="turn-activity-details-label">Result</span>
          <pre className="turn-activity-code">{result}</pre>
        </div>
      )}
      <button type="button" className="link" onClick={copy}>{copied ? 'Copied' : 'Copy details'}</button>
    </div>
  );
}

function ActionLine({ activity }: { activity: ToolActivityInfo }) {
  const [open, setOpen] = useState(false);
  const failed = activity.state === 'failed';
  return (
    <div role="listitem" data-testid={`action-${activity.actionId}`}
      className={`turn-activity-action state-${activity.state}${activity.actionClass === 'mutation' ? ' edit' : ''}`}>
      <button type="button" className="turn-activity-action-line" aria-expanded={open} onClick={() => setOpen(!open)}>
        {activity.state === 'running' && <span className="jp-icon jp-icon-rotate-cw turn-activity-spinner" aria-hidden="true" />}
        {failed && <span className="jp-icon jp-icon-circle-alert" aria-hidden="true" />}
        <span className="turn-activity-action-text">{actionLineText(activity)}</span>
        <span className={`jp-icon jp-icon-chevron-${open ? 'down' : 'right'}`} aria-hidden="true" />
      </button>
      {open && <ActionDetails activity={activity} />}
    </div>
  );
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
          {activities.map((activity) => <ActionLine key={activity.actionId} activity={activity} />)}
        </div>
      )}
      {measured.map((activity) => (
        <ToolActivityCard key={activity.actionId} activity={activity} onCancel={onCancel} readOnly={readOnly} />
      ))}
    </div>
  );
}
