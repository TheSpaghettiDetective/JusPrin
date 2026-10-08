// The change log between the turns: what the person and the Agent changed,
// drawn as the Figma "Hand edits" frame has it -- one row per run of the same
// edit, hairlines above and below the group, no bubble. The
// native side records every edit raw; merging and wording happen here only,
// so either can change without touching saved data.

import { ReactNode, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { ChangeInfo, RestorePointInfo } from '../bridge/protocol';
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
    case 'restore':
      return last.label;
  }
}

function meta(run: ChangeRun): string {
  const who = run.last.actor === 'agent' ? 'Agent' : 'you';
  const context = run.last.kind === 'setting' && run.last.preset ? `in ${run.last.preset}` : run.last.location;
  const parts = [context ? `${who}, ${context}` : who];
  if (run.count > 1) parts.push(`${run.count} steps merged`);
  const time = chatTimestamp(run.last.createdAt);
  if (time) parts.push(time);
  return parts.join(' · ');
}

export function ChangeRows({ changes, restorePoints = [], onRevert }: {
  changes: ChangeInfo[];
  restorePoints?: RestorePointInfo[];
  onRevert?: (versionId: string) => void;
}) {
  const [open, setOpen] = useState<string | null>(null);
  const [settingsExpanded, setSettingsExpanded] = useState(false);
  const [summaryExpanded, setSummaryExpanded] = useState(false);
  const [position, setPosition] = useState({ top: 0, left: 0 });
  const anchor = useRef<HTMLButtonElement | null>(null);
  const popover = useRef<HTMLDivElement | null>(null);
  const cancel = useRef<HTMLButtonElement | null>(null);
  const points = new Map(restorePoints.map((point) => [point.changeSeq, point.versionId]));
  const runs = groupChanges(changes);
  const isAgentSetting = (run: ChangeRun) =>
    run.last.kind === 'setting' && run.last.actor === 'agent' && !points.has(run.last.seq);

  useLayoutEffect(() => {
    if (!open || !anchor.current || !popover.current) return;
    const update = () => {
      const button = anchor.current!.getBoundingClientRect();
      const panel = popover.current!;
      const left = Math.max(12, Math.min(button.right - panel.offsetWidth, window.innerWidth - panel.offsetWidth - 12));
      const above = button.top - panel.offsetHeight - 4;
      const top = above >= 12 ? above
        : Math.min(button.bottom + 4, window.innerHeight - panel.offsetHeight - 12);
      setPosition({ top, left });
    };
    update();
    window.addEventListener('scroll', update, true);
    window.addEventListener('resize', update);
    return () => {
      window.removeEventListener('scroll', update, true);
      window.removeEventListener('resize', update);
    };
  }, [open]);

  useEffect(() => {
    if (!open) return;
    cancel.current?.focus();
    const dismiss = (event: MouseEvent) => {
      if (!popover.current?.contains(event.target as Node) && !anchor.current?.contains(event.target as Node))
        setOpen(null);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { setOpen(null); anchor.current?.focus(); }
    };
    document.addEventListener('pointerdown', dismiss);
    document.addEventListener('keydown', escape);
    return () => {
      document.removeEventListener('pointerdown', dismiss);
      document.removeEventListener('keydown', escape);
    };
  }, [open]);

  const entries: ReactNode[] = [];
  for (let index = 0; index < runs.length;) {
    const step = runs[index];
    if (step.last.kind === 'step' && step.last.actor === 'agent') {
      let end = index + 1;
      while (end < runs.length && isAgentSetting(runs[end])) end += 1;
      const settings = runs.slice(index + 1, end);
      if (settings.length >= 2) {
        // The pill counts the settings; which ones is behind it. Naming a few
        // here would mean choosing which settings matter, and any fixed choice
        // names nothing for a turn that changed other ones. The count sits
        // where it cannot be cut: a long step name gives way, not the number.
        const summary = stepName(step.last.label);
        entries.push(
          <section className="agent-change-summary" aria-label="Agent changes" key={step.first.seq}>
            <button type="button" aria-expanded={summaryExpanded} onClick={() => setSummaryExpanded(!summaryExpanded)}>
              <span className="settings-changes-icon" aria-hidden="true" />
              <span className="agent-change-summary-label">{summary}</span>
              <span className="agent-change-summary-more">{settings.length} settings<span className={`jp-icon jp-icon-chevron-${summaryExpanded ? 'up' : 'down'}`} aria-hidden="true" /></span>
            </button>
            {summaryExpanded && <div className="agent-change-summary-details" role="list">
              {settings.map((run) => <div className="settings-change-row change-row" role="listitem" key={run.first.seq}>
                <span className="settings-change-name">{run.last.label}</span>
                <span className="settings-change-from">{run.first.from || 'none'}</span>
                <span className="settings-change-arrow" aria-hidden="true">→</span>
                <strong>{run.last.to || 'none'}</strong>
              </div>)}
            </div>}
          </section>,
        );
        index = end;
        continue;
      }
    }
    let end = index;
    while (end < runs.length && isAgentSetting(runs[end])) end += 1;
    if (end - index > 1) {
      const group = runs.slice(index, end);
      const visible = settingsExpanded ? group : group.slice(0, 4);
      const remaining = group.length - visible.length;
      entries.push(
        <section className="settings-changes" aria-label="Agent setting changes" key={group[0].first.seq}>
          <div className="settings-changes-heading">
            <span className="settings-changes-icon" aria-hidden="true" />
            <span>{visible.length} {remaining > 0 ? `of ${group.length}` : ''} settings</span>
          </div>
          <div className="settings-changes-rows" role="list">
            {visible.map((run) => (
              <div className="settings-change-row change-row" role="listitem" key={run.first.seq}>
                <span className="settings-change-name">{run.last.label}</span>
                <span className="settings-change-from">{run.first.from || 'none'}</span>
                <span className="settings-change-arrow" aria-hidden="true">→</span>
                <strong>{run.last.to || 'none'}</strong>
              </div>
            ))}
          </div>
          {remaining > 0 && (
            <button type="button" className="settings-changes-more" onClick={() => setSettingsExpanded(true)}>
              See the other {remaining} →
            </button>
          )}
          {settingsExpanded && group.length > 4 && (
            <button type="button" className="settings-changes-more" onClick={() => setSettingsExpanded(false)}>Show fewer</button>
          )}
        </section>,
      );
      index = end;
      continue;
    }
    const run = runs[index];
    entries.push(
      <div key={run.first.seq} className="change-row" role="listitem" data-testid={`change-${run.first.seq}`}>
        <span className="change-icon jp-icon jp-icon-pencil" aria-hidden="true" />
        <div className="change-text">
          <span className="change-title">{title(run)}</span>
          <span className="change-meta">{meta(run)}</span>
        </div>
        {/* An inner checkpoint cannot represent the entire merged row. */}
        {onRevert && points.has(run.last.seq) && (
          <span className="change-revert-action">
            <button type="button" className="change-revert-button" aria-label="Revert to here"
              aria-expanded={open === points.get(run.last.seq)}
              onClick={(event) => {
                anchor.current = event.currentTarget;
                setOpen(open === points.get(run.last.seq) ? null : points.get(run.last.seq)!);
              }}>
              <span className="change-revert-icon" aria-hidden="true" />
            </button>
            <span className="change-revert-tooltip" role="tooltip">Revert to here</span>
          </span>
        )}
      </div>,
    );
    index += 1;
  }

  return (
    <div className="change-rows" role="list" aria-label="Changes">
      {entries}
      {open && createPortal(
        <div ref={popover} className="change-revert-popover" role="dialog" aria-label="Revert to here?"
          style={{ top: position.top, left: position.left }}>
          <strong>Revert to here?</strong>
          <p>Restore the model and project settings from this saved point. Your current model is saved in Version history first. Conversation, exported files, completed prints, and printer activity stay as they are.</p>
          <div className="change-revert-buttons">
            <button ref={cancel} type="button" onClick={() => { setOpen(null); anchor.current?.focus(); }}>Cancel</button>
            <button type="button" className="confirm" onClick={() => { onRevert?.(open); setOpen(null); }}>Revert to here</button>
          </div>
        </div>, document.body,
      )}
    </div>
  );
}
