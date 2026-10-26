// The setup card: the one place in the Agent pane that is always now. Two
// parts. Above a rule, the words someone wrote: the person's purpose and the
// agent's plan, each present only once written. Below it, OrcaSlicer's facts:
// the presets with how many settings differ from them, and the estimates.
// Native code supplies everything; this page presents it. A record of a
// moment (a step just taken, an edit by hand) belongs to the timeline rows,
// never here, and a warning is only ever an annotation of the line it
// contradicts, one per line. Every setting is on the All settings panel.

import { WorkspaceContext } from '../bridge/protocol';
import { testStepsLost } from '../calibration';

export interface SetupCardProps {
  context: WorkspaceContext | null;
  working?: boolean;
  historical?: boolean;
  savedAt?: string;
  agentAvailable?: boolean;
  sent?: { at: string; printer: string };
  onCompute?: () => void;
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

export interface EstimateView {
  // The figures, or the one line that stands in for them.
  metrics: { icon: string; text: string }[];
  cost: string;
  plate: string;
  line: string;
  status: 'current' | 'recomputing' | 'stale' | 'none';
  sliced: boolean;
}

export function estimateView(context: WorkspaceContext): EstimateView {
  const plate = context.plates.find((item) => item.active);
  const view: EstimateView = { metrics: [], cost: '', plate: plate?.name ?? '', line: '', status: 'none', sliced: !!plate?.sliced };
  if (!plate) return { ...view, line: 'Estimate unavailable' };
  const estimate = plate.estimate;
  const metrics: EstimateView['metrics'] = [];
  if (estimate && (estimate.timeAvailable ?? estimate.printTimeSeconds > 0))
    metrics.push({ icon: 'clock', text: formatPrintTime(estimate.printTimeSeconds) });
  if (estimate && (estimate.materialAvailable ?? estimate.materialGrams > 0))
    metrics.push({ icon: 'package', text: formatGrams(estimate.materialGrams) });
  if (plate.estimateStatus === 'recomputing')
    return { ...view, status: 'recomputing', line: 'Time & material estimates recomputing…' };
  if (metrics.length === 0)
    return { ...view, line: plate.sliced ? 'Estimate unavailable' : 'Not sliced yet' };
  const cost = estimate && estimate.materialCost !== null && estimate.materialCost >= 0.005
    ? formatCost(estimate.materialCost, context.currency) : '';
  return { ...view, status: plate.estimateStatus === 'stale' ? 'stale' : 'current', metrics, cost };
}

function shortPreset(name: string): string {
  const separator = name.indexOf(' @');
  return separator < 0 ? name : name.slice(0, separator);
}

// The settings in force that differ from the presets: the project's own,
// and those an object, part, modifier or height range sets for itself.
// Support painting and blockers set no setting and are not counted.
export function changedSettingCount(context: WorkspaceContext): number {
  return context.presetDeltas.length + (context.appliedSetup?.localOverrides ?? []).filter((local) => local.key).length;
}

// Process · first filament (+ how many more) · how many settings changed.
export function presetsLine(context: WorkspaceContext): string {
  const process = context.printer.process.trim();
  if (!process) return '';
  const filaments = context.setupIdentity?.filaments ?? [];
  const first = (filaments[0]?.preset || context.printer.filament).trim();
  const filament = first ? `${shortPreset(first)}${filaments.length > 1 ? ` +${filaments.length - 1}` : ''}` : '';
  const changed = changedSettingCount(context);
  return [shortPreset(process), filament, changed > 0 ? `${changed} ${changed === 1 ? 'setting' : 'settings'} changed` : '']
    .filter(Boolean).join(' · ');
}

// The plan line's annotation: the app's own mark that the project moved on
// since the plan was recorded, which only the agent recording it again
// clears; failing that, a calibration test whose steps the file lost, since
// in a calibration chat the test is the plan.
export function planNote(context: WorkspaceContext): string {
  if (context.planHeadline.trim() && context.planValidity === 'needs_reassessment')
    return context.planInvalidatedBy ? `Plan needs another look · ${context.planInvalidatedBy}` : 'Plan needs another look';
  return testStepsLost(context);
}

// The presets line's annotation: the printer last reported another material
// than the setup's. The nozzle and the printer model are not lines of this
// card; the header's printer chip and the chat own those.
export function presetsNote(context: WorkspaceContext): string {
  const review = context.printerReview;
  if (!review?.observed?.observedAt) return '';
  const material = review.mismatches.find((mismatch) => mismatch.what === 'filament' && mismatch.source === 'device');
  return material ? `Printer last reported ${material.observed}` : '';
}

function Note({ text, action }: { text: string; action?: { label: string; onClick: () => void } }) {
  if (!text) return null;
  return <p className={action ? 'current-setup-annotation current-setup-annotation--action' : 'current-setup-annotation'}>
    <span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />
    <span className="current-setup-annotation-text">{text}</span>
    {action && <button type="button" className="current-setup-link" onClick={action.onClick}>{action.label}</button>}
  </p>;
}

export function SetupCard({ context, working: busy = false, historical = false, savedAt, agentAvailable = true,
  sent, onCompute, onViewSetup }: SetupCardProps) {
  if (!context) return null;
  // A saved card is never working, whatever the active chat happens to be
  // doing, and with no agent nothing is.
  const working = busy && !historical && agentAvailable;
  const ariaLabel = historical ? 'Earlier setup — saved with this conversation' : 'Current setup';
  const eyebrow = sent ? 'Saved sliced snapshot' : historical ? 'Earlier setup' : 'Setup summary';
  const saved = formatMoment(savedAt);
  const state = historical ? (sent ? 'Sliced & sent' : saved ? `Saved ${saved}` : '') : working ? 'Agent working' : '';

  const purpose = context.setupIntent.trim();
  const plan = context.planHeadline.trim();
  const words = !!(purpose || plan);
  const presets = presetsLine(context);
  const estimate = estimateView(context);
  // With no agent the card is the preset's label and offers nothing, as it
  // offers nothing to open when it read no settings.
  const offers = historical || agentAvailable;
  const live = !historical && agentAvailable && !working && !!onCompute;
  const sentWhen = sent ? formatMoment(sent.at) : '';

  return <section className={historical ? 'current-setup' : 'current-setup current-setup--live'}
    data-testid="current-setup" aria-label={ariaLabel} aria-busy={working || undefined}>
    <div className="current-setup-header"><span className="current-setup-label">{eyebrow}</span>
      {state && <span className="current-setup-state">{state}</span>}</div>
    {words && <>
      {purpose && <h2 className="current-setup-title" title={purpose}>{purpose}</h2>}
      {plan && <p className="current-setup-strategy">{plan}</p>}
      <Note text={planNote(context)} />
      <hr className="current-setup-divider" />
    </>}
    <div className="current-setup-facts">
      <p className="current-setup-facts-text" title={presets}>{presets || 'No settings read from the project'}</p>
      {onViewSetup && presets && offers && <button type="button" className="current-setup-link" onClick={onViewSetup}>All settings</button>}
    </div>
    <Note text={presetsNote(context)} />
    {estimate.metrics.length > 0
      ? <div className="current-setup-metrics">
        {estimate.metrics.map((metric) => <span className="current-setup-metric" key={metric.icon}>
          <span className={`jp-icon jp-icon-${metric.icon}`} aria-hidden="true" />{metric.text}</span>)}
        {estimate.plate && <span className="current-setup-metric">
          <span className="jp-icon jp-icon-grid-3x3" aria-hidden="true" />{estimate.plate}</span>}
        {estimate.cost && <span className="current-setup-metric">{estimate.cost}</span>}
      </div>
      : <div className="current-setup-facts">
        <p className="current-setup-line">{estimate.line}</p>
        {live && estimate.status === 'none' && !estimate.sliced &&
          <button type="button" className="current-setup-link" onClick={onCompute}>Compute estimates</button>}
      </div>}
    <Note text={estimate.status === 'stale' ? 'Previous estimates · not current' : ''}
      action={live ? { label: 'Recompute', onClick: onCompute! } : undefined} />
    {!historical && !agentAvailable && <p className="current-setup-line">{changedSettingCount(context) > 0
      ? 'No agent configured.' : 'No agent configured · using the selected preset.'}</p>}
    {historical && <p className="current-setup-line">{sent
      ? `${sent.printer ? `Sent to ${sent.printer}` : 'Sent'}${sentWhen ? ` · ${sentWhen}` : ''}. Saved snapshot: the canvas shows your current project.`
      : 'Saved with this chat. The canvas shows your current project.'}</p>}
  </section>;
}
