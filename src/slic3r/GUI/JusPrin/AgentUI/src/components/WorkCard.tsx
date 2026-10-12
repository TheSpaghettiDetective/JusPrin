// Work the application ran outside the chat: a slice, an arrange, an export.
// While it runs the card carries its progress and, where the work can be
// stopped, a Cancel button; when it ends the same card says how. It is drawn
// from the tool card's own pieces, so it reads as the same kind of thing.

import type { ToolStateName, WorkInfo, WorkStateName } from '../bridge/protocol';
import { Progress } from './Progress';
import { ToolStatus } from './ToolActivityCard';

interface Props {
  work: WorkInfo;
  // How many consecutive runs this card stands for; see `collapseWork`.
  times?: number;
  onCancel: (id: string) => void;
  readOnly?: boolean;
}

// The glyph each ending leads with. An interrupted run is shown as a failure
// would be: it did not finish.
const icons: Record<WorkStateName, ToolStateName> = {
  running: 'running',
  done: 'succeeded',
  cancelled: 'cancelled',
  failed: 'failed',
  interrupted: 'failed',
};

export function workStatus(work: WorkInfo): string {
  const total = work.progress?.total ?? 0;
  switch (work.state) {
    case 'running': {
      const words = work.detail || 'Running…';
      return total > 0 ? `${words} · ${Math.round((100 * (work.progress?.current ?? 0)) / total)}%` : words;
    }
    case 'done':
      return 'Done';
    case 'cancelled':
      return 'Cancelled';
    case 'failed':
      return work.detail || 'It did not finish.';
    case 'interrupted':
      return 'Interrupted when the project was closed';
  }
}

// Consecutive runs that ended the same way and have the same title read as one
// card with a count, so slicing ten times is not ten cards. Nothing is merged
// in what is saved, and a run that is still going always stands alone.
export function collapseWork(entries: WorkInfo[]): { work: WorkInfo; times: number }[] {
  const cards: { work: WorkInfo; times: number }[] = [];
  for (const work of entries) {
    const last = cards[cards.length - 1];
    if (last && work.state !== 'running' && last.work.state === work.state && last.work.kind === work.kind &&
        last.work.title === work.title && last.work.detail === work.detail)
      cards[cards.length - 1] = { work, times: last.times + 1 };
    else cards.push({ work, times: 1 });
  }
  return cards;
}

export function WorkCard({ work, times = 1, onCancel, readOnly = false }: Props) {
  const running = work.state === 'running';
  const total = work.progress?.total ?? 0;
  return (
    <div className={`tool-card state-${icons[work.state]}`} data-testid={`work-${work.id}`}>
      <div className="tool-title">
        {work.title}
        {times > 1 && <span className="work-times"> · {times} times</span>}
      </div>
      <ToolStatus state={icons[work.state]}>{workStatus(work)}</ToolStatus>
      {running && (
        <>
          <Progress label={`${work.title} progress`} value={total > 0 ? work.progress?.current : undefined}
            max={total > 0 ? total : undefined} />
          {work.cancellable && (
            <div className="tool-actions">
              <button disabled={readOnly} onClick={() => onCancel(work.id)}>Cancel</button>
            </div>
          )}
        </>
      )}
    </div>
  );
}
