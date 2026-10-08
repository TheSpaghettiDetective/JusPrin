// A compact, chat-owned account of the selected presets and every value that
// differs from them. Native code supplies facts and formatted values; this
// page only orders and presents them.

import { ReactNode, useState } from 'react';
import { ChangeInfo, PresetDeltaInfo, SliceEstimateInfo, WorkspaceContext } from '../bridge/protocol';

export interface SetupCardProps {
  context: WorkspaceContext | null;
  working?: boolean;
  historical?: boolean;
  savedAt?: string;
  agentAvailable?: boolean;
  changes?: ChangeInfo[];
  now?: number;
  estimateBefore?: SliceEstimateInfo | null;
  sent?: { at: string; printer: string };
  attention?: Finding[];
  onCompute?: () => void;
  onUndo?: (changeSeq: number) => void;
  onViewSetup?: () => void;
}

export function formatPrintTime(seconds: number): string {
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `~${minutes} min`;
  return `~${Math.floor(minutes / 60)}h ${String(minutes % 60).padStart(2, '0')}m`;
}

export function formatGrams(grams: number): string {
  return `${grams < 10 ? Math.round(grams * 10) / 10 : Math.round(grams)} g`;
}

export function formatCost(cost: number, currency: string): string {
  if (currency) {
    try {
      return new Intl.NumberFormat(undefined, { style: 'currency', currency }).format(cost);
    } catch {
      // An unknown regional code does not make the native amount disappear.
    }
  }
  return `cost ${cost.toFixed(2)}`;
}

export function formatMoment(value: string | undefined): string {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString(undefined, {
    month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hour12: false,
  });
}

export const RECENT_CHANGE_MS = 60000;

export interface EstimateView {
  heading: string;
  metrics: { icon: string; text: string }[];
  cost: string;
  plate: string;
  description: string;
  status: 'current' | 'recomputing' | 'stale' | 'none';
  sliced: boolean;
}

export function estimateView(context: WorkspaceContext): EstimateView {
  const plate = context.plates.find((item) => item.active);
  const view: EstimateView = {
    heading: '', metrics: [], cost: '', plate: plate?.name ?? '', description: '', status: 'none', sliced: !!plate?.sliced,
  };
  if (!plate) return { ...view, description: 'Estimate unavailable' };
  const estimate = plate.estimate;
  const scoped = (text: string) => plate.name ? `${text} · ${plate.name}` : text;
  const metrics: EstimateView['metrics'] = [];
  if (estimate && (estimate.timeAvailable ?? estimate.printTimeSeconds > 0))
    metrics.push({ icon: 'clock', text: formatPrintTime(estimate.printTimeSeconds) });
  if (estimate && (estimate.materialAvailable ?? estimate.materialGrams > 0))
    metrics.push({ icon: 'package', text: formatGrams(estimate.materialGrams) });
  if (plate.estimateStatus === 'recomputing')
    return { ...view, status: 'recomputing', heading: 'Time & material estimates recomputing…',
      description: scoped('Settings confirmed') };
  if (metrics.length === 0)
    return { ...view, description: scoped(plate.sliced ? 'Estimate unavailable' : 'Not sliced yet · estimates unavailable') };
  const cost = estimate && estimate.materialCost !== null && estimate.materialCost >= 0.005
    ? formatCost(estimate.materialCost, context.currency) : '';
  if (plate.estimateStatus === 'stale')
    return { ...view, status: 'stale', heading: 'Previous estimates · not current', metrics, cost };
  return { ...view, status: 'current', metrics, cost };
}

export interface Finding { title: string; detail?: string; facts?: string[]; }

export function deterministicAttention(context: WorkspaceContext): Finding[] {
  const review = context.printerReview;
  if (!review) return [];
  const priority: Record<string, number> = { filament: 0, nozzle: 1, model: 2, plate: 3 };
  const noun: Record<string, string> = { filament: 'material', nozzle: 'nozzle', model: 'printer', plate: 'build plate' };
  const when = formatMoment(review.observed?.observedAt);
  return [...review.mismatches].sort((a, b) => (priority[a.what] ?? 4) - (priority[b.what] ?? 4)).flatMap((mismatch) => {
    const thing = noun[mismatch.what] ?? 'printer setup';
    if (mismatch.source === 'device') {
      if (!when) return [];
      const facts = [`Setup: ${mismatch.configured}`, `Printer last reported: ${mismatch.observed}`, `Reported ${when}`];
      if (review.observed?.connection) facts.push(`Printer is ${review.observed.connection} now`);
      return [{ title: `Last reported ${thing} differs`,
        detail: `Setup uses ${mismatch.configured}. Printer last reported ${mismatch.observed}.`, facts }];
    }
    return [{ title: `Confirmed ${thing} differs`,
      detail: `Setup uses ${mismatch.configured}. You confirmed ${mismatch.observed}.`,
      facts: [`Setup: ${mismatch.configured}`, `You confirmed: ${mismatch.observed}`] }];
  });
}

export function recentAgentChange(changes: ChangeInfo[] | undefined, now: number): ChangeInfo[] {
  const log = [...(changes ?? [])].sort((a, b) => a.seq - b.seq);
  const last = log.at(-1);
  if (!last || last.actor !== 'agent' || (last.kind !== 'setting' && last.kind !== 'step')) return [];
  const made = Date.parse(last.createdAt);
  if (!Number.isFinite(made) || now - made >= RECENT_CHANGE_MS || now < made) return [];
  let start = log.length - 1;
  while (start > 0 && log[start - 1].actor === 'agent' && log[start - 1].afterId === last.afterId &&
    (log[start - 1].kind === 'setting' || log[start - 1].kind === 'step')) start -= 1;
  return log.slice(start);
}

export function handEdits(changes: ChangeInfo[] | undefined): ChangeInfo[] {
  const log = [...(changes ?? [])].sort((a, b) => a.seq - b.seq);
  let start = log.length;
  while (start > 0 && log[start - 1].actor === 'person' && log[start - 1].kind !== 'restore') start -= 1;
  const net = new Map<string, ChangeInfo>();
  for (const change of log.slice(start))
    if (change.kind === 'setting') net.set(change.key || change.label, change);
  return [...net.values()];
}

function changeText(change: ChangeInfo): string {
  if (change.kind !== 'setting') return change.label || 'Object settings changed';
  return `${change.label} ${change.from || 'none'} → ${change.to || 'none'}`;
}

function estimatePair(estimate: SliceEstimateInfo): string {
  return `${formatPrintTime(estimate.printTimeSeconds)} / ${formatGrams(estimate.materialGrams)}`;
}

function plural(count: number, one: string, many: string): string {
  return `${count} ${count === 1 ? one : many}`;
}

function shortPreset(name: string): string {
  const separator = name.indexOf(' @');
  return separator < 0 ? name : name.slice(0, separator);
}

export function presetsLine(context: WorkspaceContext, title: string): string {
  const process = context.printer.process.trim();
  if (!process) return '';
  const filaments = context.setupIdentity?.filaments ?? [];
  const first = (filaments[0]?.preset || context.printer.filament).trim();
  const filament = first ? `${shortPreset(first)}${filaments.length > 1 ? ` +${filaments.length - 1}` : ''}` : '';
  const parts = title === process ? [filament] : [shortPreset(process), filament];
  return parts.filter(Boolean).join(' · ');
}

// The order the card and the page list differences in: the agent's before the
// person's, and within each the most recently changed first. Settings changed
// in one go -- one patch from the agent, one profile loaded by hand -- are
// equally new: the log holds them in the host's order, and which of them was
// written last says nothing. They, and differences this chat's log does not
// mention, keep the host's order, which is OrcaSlicer's own.
export function orderedPresetDeltas(deltas: PresetDeltaInfo[], changes?: ChangeInfo[]): PresetDeltaInfo[] {
  const changedAt = new Map<string, number>();
  let batch = 0;
  let previous: ChangeInfo | undefined;
  for (const change of [...(changes ?? [])].sort((a, b) => a.seq - b.seq)) {
    if (change.kind !== 'setting' || !change.key) { previous = undefined; continue; }
    const together = previous && previous.actor === change.actor &&
      Math.abs(Date.parse(change.createdAt) - Date.parse(previous.createdAt)) <= 1000;
    if (!together) batch = change.seq;
    changedAt.set(change.key, batch);
    previous = change;
  }
  return deltas.map((delta, index) => ({ delta, index })).sort((left, right) => {
    if (left.delta.origin !== right.delta.origin) return left.delta.origin === 'agent' ? -1 : 1;
    const newer = (changedAt.get(right.delta.key) ?? -1) - (changedAt.get(left.delta.key) ?? -1);
    return newer !== 0 ? newer : left.index - right.index;
  }).map(({ delta }) => delta);
}

export function overrideObjectCount(context: WorkspaceContext): number {
  return new Set((context.appliedSetup?.localOverrides ?? [])
    .map((local) => local.object).filter((object): object is number => object !== undefined)).size;
}

export interface SetupPresentationOptions {
  historical?: boolean;
  working?: boolean;
  agentAvailable?: boolean;
  changes?: ChangeInfo[];
  now?: number;
  sent?: { at: string; printer: string };
  savedAt?: string;
}

// What the card and the setup page both say about themselves: their label,
// their one state, their title. Conversation status outranks live activity: a
// saved card is never working, whatever the active chat happens to be doing.
export function setupPresentation(context: WorkspaceContext, options: SetupPresentationOptions = {}) {
  const historical = !!options.historical;
  const working = !!options.working && !historical;
  const estimate = estimateView(context);
  const intent = context.setupIntent.trim();
  const title = intent || context.printer.process.trim();
  // Nothing vouches for this setup but the preset the person picked: no agent
  // is configured, or the host read no settings to compare with it.
  const noAgent = !historical && options.agentAvailable === false;
  if (noAgent || (!historical && !context.appliedSetup && !intent))
    return { label: 'Preset fallback', state: noAgent ? 'No agent' : null as ReactNode, title: context.printer.process.trim(),
      estimate, justChanged: [], edited: [], editedByYou: false, outOfDate: false, live: true, fallback: true, noAgent };
  const justChanged = historical ? [] : recentAgentChange(options.changes, options.now ?? Date.now());
  const edited = historical || working || justChanged.length > 0 ? [] : handEdits(options.changes);
  // "Edited by you" needs a setup the agent had a hand in; without one a
  // hand edit is simply how the project was made.
  const agentManaged = !!intent || (options.changes ?? []).some((change) => change.actor === 'agent');
  const editedByYou = edited.length > 0 && agentManaged;
  // Out of date stands the card down: it drops the accent, as a saved card
  // does, because it no longer vouches for the numbers under it.
  const outOfDate = estimate.status === 'stale' && !working && justChanged.length === 0 && !editedByYou;
  const saved = formatMoment(options.savedAt);
  const label = options.sent ? 'Saved sliced snapshot' : historical || outOfDate ? 'Earlier setup' : 'Setup summary';
  const state: ReactNode = historical
    ? options.sent ? 'Sliced & sent' : estimate.status === 'stale' ? 'Out of date'
      : saved ? <><span className="jp-icon jp-icon-clock" aria-hidden="true" />Saved {saved}</> : null
    : working ? 'Agent working' : justChanged.length > 0 ? 'Updated'
    : estimate.status === 'recomputing' ? 'Confirmed' : editedByYou ? 'Edited by you'
    : outOfDate ? 'Out of date' : estimate.status === 'none' && !estimate.sliced ? 'Not sliced' : 'Current';
  return { label, state, title, estimate, justChanged, edited, editedByYou, outOfDate, live: !historical && !outOfDate,
    fallback: false, noAgent: false };
}

function Metrics({ estimate }: { estimate: EstimateView }) {
  return <div className="current-setup-metrics">
    {estimate.metrics.map((metric) => <span className="current-setup-metric" key={metric.icon}>
      <span className={`jp-icon jp-icon-${metric.icon}`} aria-hidden="true" />{metric.text}</span>)}
    {estimate.plate && <span className="current-setup-metric">
      <span className="jp-icon jp-icon-grid-3x3" aria-hidden="true" />{estimate.plate}</span>}
    {estimate.cost && <span className="current-setup-metric">{estimate.cost}</span>}
  </div>;
}

function Attention({ findings }: { findings: Finding[] }) {
  const [open, setOpen] = useState(false);
  if (findings.length === 0) return null;
  const first = findings[0];
  const reviewable = findings.length > 1 || (first.facts ?? []).length > 0;
  const subject = findings.length > 1 ? plural(findings.length, 'finding', 'findings')
    : first.title.replace(/^(Last reported|Confirmed) | differs$/g, '');
  return <>
    <div className="current-setup-attention">
      <p className="current-setup-warning"><span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />{first.title}</p>
      {first.detail && <p className="current-setup-line">{first.detail}</p>}
      {reviewable && <button type="button" className="current-setup-link" aria-expanded={open} onClick={() => setOpen(!open)}>
        Review {subject}</button>}
      {open && <ul className="current-setup-findings">{findings.map((finding, index) => <li key={index}>
        {findings.length > 1 && <strong>{finding.title}</strong>}
        {(finding.facts ?? []).map((fact) => <span key={fact}>{fact}</span>)}
      </li>)}</ul>}
    </div>
    <hr className="current-setup-divider" />
  </>;
}

// One row per setting that differs: its name, and "preset → value" as the
// host formatted it. A summary saved before the host formatted values has
// only the raw pair. The name gives way on a narrow dock, with the setting's
// place in OrcaSlicer's tabs in its tooltip; the value is never cut.
export function ChangeRows({ deltas }: { deltas: PresetDeltaInfo[] }) {
  return <dl className="current-setup-rows">{deltas.map((delta) => {
    const name = delta.label || delta.key;
    return <div className="current-setup-row current-setup-row--change" key={`${delta.presetType ?? ''}:${delta.key}`}>
      <dt title={[delta.page, delta.group, name].filter(Boolean).join(' · ')}>{name}</dt>
      <dd>{delta.display || `${delta.preset} → ${delta.value}`}</dd>
    </div>;
  })}</dl>;
}

export function SetupCard({ context, working: busy = false, historical = false, savedAt, agentAvailable = true,
  changes, now = Date.now(), estimateBefore, sent, attention = [], onCompute, onUndo, onViewSetup }: SetupCardProps) {
  if (!context) return null;
  // A saved card is never working, whatever the active chat happens to be
  // doing.
  const working = busy && !historical;
  const ariaLabel = historical ? 'Earlier setup — saved with this conversation' : 'Current setup';
  const presentation = setupPresentation(context, { historical, working, agentAvailable, changes, now, sent, savedAt });

  if (presentation.fallback) {
    const { estimate, title } = presentation;
    return <section className="current-setup current-setup--live" data-testid="current-setup" aria-label={ariaLabel}>
      <div className="current-setup-header"><span className="current-setup-label">{presentation.label}</span>
        {presentation.state && <span className="current-setup-state">{presentation.state}</span>}</div>
      {title && <h2 className="current-setup-title" title={title}>{title}</h2>}
      {presentation.noAgent ? <p className="current-setup-line">No agent configured · using the selected preset.</p>
        : estimate.metrics.length > 0 ? <Metrics estimate={estimate} />
        : <p className="current-setup-line">{estimate.description}</p>}
    </section>;
  }

  const { estimate, justChanged, edited, editedByYou, outOfDate } = presentation;
  const ordered = orderedPresetDeltas(context.presetDeltas, changes);
  const shown = ordered.slice(0, 2);
  const remaining = ordered.length - shown.length;
  const objects = overrideObjectCount(context);
  const note = [remaining > 0 ? plural(remaining, 'more change', 'more changes') : '',
    objects > 0 ? plural(objects, 'object with overrides', 'objects with overrides') : ''].filter(Boolean).join(' · ');
  const presets = presetsLine(context, presentation.title);

  const withheld = editedByYou && estimate.status === 'stale';
  const heading = withheld ? '' : estimate.heading ? estimate.heading : estimate.metrics.length === 0 ? ''
    : sent ? 'Saved pre-slice estimates' : working ? 'Last confirmed estimates' : '';
  const description = withheld ? (estimate.plate ? `Estimate unavailable for these edits · ${estimate.plate}`
    : 'Estimate unavailable for these edits') : estimate.description;
  const canCompute = !historical && !working && !!onCompute && estimate.status === 'none';
  const step = justChanged.at(-1);
  const undoable = !!onUndo && !working && step?.kind === 'step' && context.history.canUndo;
  const plate = context.plates.find((item) => item.active);
  const after = plate?.estimate;
  const estimateDelta = justChanged.length > 0 && estimateBefore && after && estimate.status === 'current' &&
    (estimateBefore.printTimeSeconds !== after.printTimeSeconds || estimateBefore.materialGrams !== after.materialGrams)
    ? `Estimate: ${estimatePair(estimateBefore)} → ${estimatePair(after)}` : '';
  const named = edited.slice(0, 2).map((change) => `${change.label} ${change.to || 'none'}`);
  const editedText = edited.length > 2 ? `${named.join(', ')} and ${plural(edited.length - 2, 'more setting', 'more settings')}`
    : named.join(' and ');
  const findings = [...deterministicAttention(context), ...attention];

  return <section className={presentation.live ? 'current-setup current-setup--live' : 'current-setup'}
    data-testid="current-setup" aria-label={ariaLabel} aria-busy={working || undefined}>
    <div className="current-setup-header"><span className="current-setup-label">{presentation.label}</span>
      {presentation.state && <span className="current-setup-state">{presentation.state}</span>}</div>
    {presentation.title && <h2 className="current-setup-title" title={presentation.title}>{presentation.title}</h2>}
    {presets && <p className="current-setup-line current-setup-line--single" title={presets}>{presets}</p>}
    {shown.length > 0 ? <ChangeRows deltas={shown} /> : <p className="current-setup-line">No changes from presets</p>}
    {note && <p className="current-setup-note">{note}</p>}
    {heading && <p className="current-setup-heading">{heading}</p>}
    {!withheld && estimate.metrics.length > 0 && <Metrics estimate={estimate} />}
    {description && <p className="current-setup-line">{description}</p>}
    {sent && estimate.metrics.length > 0 && <p className="current-setup-line">Actual time &amp; material unavailable.</p>}
    {canCompute && <button type="button" className="current-setup-link" onClick={onCompute}>Compute estimates</button>}
    <hr className="current-setup-divider" />
    {working && <><p className="current-setup-line">Agent is applying and evaluating changes…<br />
      Shown settings remain the last confirmed setup.</p><hr className="current-setup-divider" /></>}
    {!working && justChanged.length > 0 && <>
      <p className="current-setup-line">{justChanged.map(changeText).join(' · ')}</p>
      {estimateDelta && <p className="current-setup-line">{estimateDelta}</p>}
      {undoable && <button type="button" className="current-setup-link" onClick={() => onUndo!(step!.seq)}>Undo</button>}
      <hr className="current-setup-divider" />
    </>}
    {outOfDate && <><div className="current-setup-attention">
      <p className="current-setup-warning"><span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />
        {historical ? 'These estimates were already out of date when saved.' : 'Estimates no longer match the current project.'}</p>
      {plate?.invalidatedBy && <p className="current-setup-line">{plate.invalidatedBy}</p>}
      {!historical && onCompute && <button type="button" className="current-setup-link" onClick={onCompute}>
        Refresh setup &amp; recompute</button>}
    </div><hr className="current-setup-divider" /></>}
    {editedByYou && <><p className="current-setup-info"><span className="jp-icon jp-icon-info" aria-hidden="true" />
      <span>You set {editedText} outside the agent. Your current settings are preserved.</span></p>
      <hr className="current-setup-divider" /></>}
    <Attention findings={findings} />
    <button type="button" className="current-setup-link" onClick={onViewSetup}>View setup</button>
    {historical && <><hr className="current-setup-divider" />
      {sent && <p className="current-setup-sent">{[formatMoment(sent.at), sent.printer && `sent to ${sent.printer}`]
        .filter(Boolean).join(' · ')}</p>}
      <p className="current-setup-info"><span className="jp-icon jp-icon-info" aria-hidden="true" />
        <span>{sent ? 'Saved snapshot.' : 'Saved with this chat.'} The canvas shows your current project.</span></p>
    </>}
  </section>;
}
