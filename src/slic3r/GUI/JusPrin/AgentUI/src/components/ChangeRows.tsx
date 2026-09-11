// The change log between the turns: what the person and the Agent changed,
// drawn as the Figma "Hand edits" frame has it -- one row per run of the same
// edit, hairlines above and below the group, no bubble, no controls. The
// native side records every edit raw; merging and wording happen here only,
// so either can change without touching saved data.

import { ReactNode } from 'react';
import { ChangeInfo } from '../bridge/protocol';
import { chatTimestamp } from './ChatNavigation';

export interface ChangeRun {
  first: ChangeInfo;
  last: ChangeInfo;
  count: number;
}

// Consecutive entries merge when they are the same edit by the same actor:
// the same kind and label, which for a setting is the same setting. Eleven
// rotate steps are one row; a rotate then a move are two.
export function groupChanges(changes: ChangeInfo[]): ChangeRun[] {
  const runs: ChangeRun[] = [];
  for (const change of [...changes].sort((a, b) => a.seq - b.seq)) {
    const run = runs[runs.length - 1];
    if (run && run.last.kind === change.kind && run.last.label === change.label && run.last.actor === change.actor) {
      run.last = change;
      run.count += 1;
    } else {
      runs.push({ first: change, last: change, count: 1 });
    }
  }
  return runs;
}

// OrcaSlicer's own step names where they read as code rather than as what the
// person did. Everything else is shown as OrcaSlicer names it.
const STEP_NAMES: Record<string, string> = {
  'select_arrange partplate': 'Arrange plate',
  smooth_mesh: 'Smooth mesh',
  MoveInMeasure: 'Move',
};

// Seven real edits take an undo step with no name at all: deleting instances
// or parts, adding or editing layer ranges, and the printable and auto-drop
// toggles.
const UNNAMED_STEP = 'Object settings changed';

function stepName(label: string): string {
  return label === '' ? UNNAMED_STEP : STEP_NAMES[label] ?? label;
}

function title(run: ChangeRun): ReactNode {
  const { first, last } = run;
  switch (last.kind) {
    case 'step':
      return stepName(last.label);
    case 'undo':
      return `Undo: ${stepName(last.label)}`;
    case 'redo':
      return `Redo: ${stepName(last.label)}`;
    case 'setting':
      // A run of edits to one setting reads as its net change.
      return (
        <>
          {last.label} {first.from || 'none'} → <strong>{last.to || 'none'}</strong>
        </>
      );
    case 'preset':
      return (
        <>
          Switched to <strong>{last.label}</strong>
        </>
      );
    case 'mark':
      return 'Project changed';
  }
}

function meta(run: ChangeRun): string {
  const who = run.last.actor === 'agent' ? 'Agent' : 'you';
  const parts = [run.last.kind === 'setting' && run.last.preset ? `${who}, in ${run.last.preset}` : who];
  if (run.count > 1) parts.push(`${run.count} steps merged`);
  const time = chatTimestamp(run.last.createdAt);
  if (time) parts.push(time);
  return parts.join(' · ');
}

export function ChangeRows({ changes }: { changes: ChangeInfo[] }) {
  return (
    <div className="change-rows" role="list" aria-label="Changes">
      {groupChanges(changes).map((run) => (
        <div key={run.first.seq} className="change-row" role="listitem" data-testid={`change-${run.first.seq}`}>
          <span className="change-icon" aria-hidden="true">✎</span>
          <div className="change-text">
            <span className="change-title">{title(run)}</span>
            <span className="change-meta">{meta(run)}</span>
          </div>
        </div>
      ))}
    </div>
  );
}
