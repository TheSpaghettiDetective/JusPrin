// The setup card: what this chat asked the setup to be, the settings actually
// in force on the plate, and what the slice says it will cost. The anatomy is
// the Figma "Current Setup Card" page, sections 5 to 7; the facts come from
// OrcaSlicer through WorkspaceContext, never from the conversation.
//
// The card's states are combinations of independent facts, not separate
// cards: whose chat it is (live or saved), where the setup came from, what
// the agent is doing, and what the estimate is worth. Every row has its own
// presence rule and every fallback ends in an honest sentence or in omitting
// the row -- never a zero, a guess, or today's value under a saved heading.

import { ReactNode, useState } from 'react';
import { AppliedSetupInfo, ChangeInfo, PresetDeltaInfo, SetupIdentityInfo, SliceEstimateInfo,
  WorkspaceContext } from '../bridge/protocol';

export interface SetupCardProps {
  context: WorkspaceContext | null;
  expanded: boolean;
  onToggle: () => void;
  // The agent is mid-turn. The card keeps showing what is confirmed.
  working?: boolean;
  // An earlier chat's saved summary: nothing here follows the live project.
  historical?: boolean;
  savedAt?: string;
  // False when no agent is configured: the card is then a label for the
  // selected preset and claims nothing more.
  agentAvailable?: boolean;
  // This chat's change log, oldest first. It is the only source of who
  // changed what, so without it the card says nothing about authorship.
  changes?: ChangeInfo[];
  // The clock the "just changed" window is measured against.
  now?: number;
  // The plate's last current estimate before the newest change, when the
  // caller saw one; pairs with the estimate now in force.
  estimateBefore?: SliceEstimateInfo | null;
  // The send this saved summary belongs to, when the chat sliced and sent.
  sent?: { at: string; printer: string };
  // Findings another deterministic check produced, shown after the printer
  // comparison's own. Never a model's opinion or an unanswered question.
  attention?: Finding[];
  onCompute?: () => void;
  onUndo?: (changeSeq: number) => void;
}

type Setting = AppliedSetupInfo['settings'][number];
type LocalOverride = AppliedSetupInfo['localOverrides'][number];

// "~2h 18m" over an hour, "~42 min" under it. Seconds are never shown: the
// number is an estimate and false precision reads as a promise.
export function formatPrintTime(seconds: number): string {
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `~${minutes} min`;
  return `~${Math.floor(minutes / 60)}h ${String(minutes % 60).padStart(2, '0')}m`;
}

export function formatGrams(grams: number): string {
  return `${grams < 10 ? Math.round(grams * 10) / 10 : Math.round(grams)} g`;
}

// OrcaSlicer has no currency concept -- filament_cost's unit is literally
// "money/kg" -- so the host reads the ISO code from the machine's regional
// settings and the page formats it the way that viewer's locale writes money.
// Without a code there is nothing to denominate the number in, so it carries
// the word "cost" the way the slicer's own G-code legend does.
export function formatCost(cost: number, currency: string): string {
  if (currency) {
    try {
      return new Intl.NumberFormat(undefined, { style: 'currency', currency }).format(cost);
    } catch {
      // An unknown code is not worth losing the number over.
    }
  }
  return `cost ${cost.toFixed(2)}`;
}

// "Oct 3, 10:42". A value that is not a timestamp is shown as it came.
export function formatMoment(value: string | undefined): string {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleString(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hour12: false });
}

// How long a completed agent change is "just changed".
export const RECENT_CHANGE_MS = 60000;

// Display names for the stable config keys a local override may carry. Only
// these are listed by name; anything else is counted, not described.
const SETTING_NAMES: Record<string, string> = {
  layer_height: 'Layer height',
  wall_loops: 'Wall loops',
  sparse_infill_density: 'Sparse infill',
  sparse_infill_pattern: 'Infill pattern',
  enable_support: 'Supports',
  support_type: 'Support type',
  support_on_build_plate_only: 'Supports from build plate only',
  top_shell_layers: 'Top shell layers',
  bottom_shell_layers: 'Bottom shell layers',
  top_shell_thickness: 'Top minimum thickness',
  bottom_shell_thickness: 'Bottom minimum thickness',
  brim_type: 'Brim type',
  brim_width: 'Brim width',
  extruder: 'Filament',
};


const on = (value: string) => value === '1' || value === 'true';
const sentence = (text: string) => text.charAt(0).toUpperCase() + text.slice(1);
const plural = (count: number, one: string, many: string) => `${count} ${count === 1 ? one : many}`;

function millimetres(value: string): string {
  const number = Number(value);
  return Number.isFinite(number) ? `${number.toFixed(2)} mm` : `${value} mm`;
}

// An enum's stable value, readable: "adaptive_cubic" is "adaptive cubic".
const enumWord = (value: string) => value.replace(/[_-]+/g, ' ').trim();

// A modifier is "bracket.stl / Mounting tabs"; inside its own object's
// section the volume's name is enough.
const shortTarget = (target: string) => target.includes(' / ') ? target.slice(target.indexOf(' / ') + 3) : target;

function settingOf(setup: AppliedSetupInfo, key: string): Setting | undefined {
  return setup.settings.find((setting) => setting.key === key);
}

// The overrides somewhere inside an object -- a modifier, a part, a height
// range -- that set this key. An object's own value is not one of them: it is
// already the value the object's scope reports.
function exceptions(setup: AppliedSetupInfo, key: string): LocalOverride[] {
  return setup.localOverrides.filter((local) => local.kind !== 'object' && local.key === key);
}

function wallsText(value: string): string { return `${value} ${value === '1' ? 'wall' : 'walls'}`; }

function infillText(density: string, pattern: string | undefined): string {
  const percent = Number.parseFloat(density);
  if (percent === 0) return 'no sparse infill';
  // Fully dense is not printed in the sparse pattern, so the pattern goes.
  if (percent === 100) return '100% infill';
  return `${density}${pattern ? ` ${enumWord(pattern)}` : ''} infill`;
}

function supportsText(enabled: string, type: string | undefined, plateOnly: string | undefined): string {
  if (!on(enabled)) return 'supports off';
  const tree = !!type && type.startsWith('tree');
  if (plateOnly && on(plateOnly)) return `${tree ? 'tree supports' : 'supports'} from build plate`;
  return tree ? 'tree supports' : 'supports on';
}

// One summary clause for one fact. A value is stated outright only when it
// holds for the whole plate; a single named exception is quoted, and anything
// more involved is pointed at rather than approximated.
function clause(setup: AppliedSetupInfo, setting: Setting | undefined, text: (value: string) => string,
  exception: (value: string) => string, varies: string, local: string): string {
  if (!setting || setting.coverage === 'unavailable') return '';
  if (setting.coverage === 'exact') return text(setting.value);
  if (setting.coverage === 'mixed') return varies;
  const found = exceptions(setup, setting.key);
  const quotable = found.length === 1 && (found[0].kind === 'modifier' || found[0].kind === 'part') && found[0].value;
  return quotable ? `${text(setting.value)}; ${shortTarget(found[0].target)}: ${exception(found[0].value)}` : local;
}

// The objects whose settings are not simply the plate's: an object-level
// value that differs from the plate default, or any customization inside it.
function objectsThatDiffer(setup: AppliedSetupInfo): number {
  const differing = new Set<number>();
  for (const setting of setup.settings)
    for (const scope of setting.scopes)
      if (scope.object !== undefined && setting.base !== undefined && scope.value !== setting.base) differing.add(scope.object);
  for (const local of setup.localOverrides)
    if (local.object !== undefined && (local.kind !== 'object' || !local.key)) differing.add(local.object);
  return differing.size;
}

// The two summary lines: layer height and walls, then infill and supports.
// The order is fixed; it never follows whichever setting changed last.
export function appliedSummary(setup: AppliedSetupInfo | null | undefined): string[] {
  if (!setup || setup.printableObjects === 0) return [];
  // A vase has one spiralling wall and no infill, whatever the keys say.
  if (setup.spiralMode) return ['Spiral vase mode'];
  const layer = settingOf(setup, 'layer_height');
  const walls = settingOf(setup, 'wall_loops');
  const density = settingOf(setup, 'sparse_infill_density');
  const pattern = settingOf(setup, 'sparse_infill_pattern');
  const enabled = settingOf(setup, 'enable_support');
  const type = settingOf(setup, 'support_type');
  const plateOnly = settingOf(setup, 'support_on_build_plate_only');
  const exactValue = (setting: Setting | undefined) => setting?.coverage === 'exact' ? setting.value : undefined;

  // Objects disagree: no value is the plate's, so the lines state the plate
  // default as a default and count the objects that depart from it.
  const disagree = [layer, walls, density, enabled].some((setting) => setting?.coverage === 'mixed');
  if (disagree && layer?.base && walls?.base && density?.base) {
    const height = setup.variableLayerHeight ? 'variable layer height' : `${millimetres(layer.base)} layers`;
    return [
      `Plate default: ${height} · ${wallsText(walls.base)}`,
      `${sentence(infillText(density.base, pattern?.base))} · ${
        plural(objectsThatDiffer(setup), 'object', 'objects')} with local overrides`,
    ];
  }

  const layerClause = setup.variableLayerHeight ? 'variable layer height' :
    clause(setup, layer, (value) => `${millimetres(value)} layers`, millimetres,
      'layer height varies by object', 'local layer heights — see details');
  const wallClause = clause(setup, walls, wallsText, (value) => value,
    'walls vary by object', 'local wall settings — see details');
  const infillClause = clause(setup, density, (value) => infillText(value, exactValue(pattern)), (value) => value,
    'infill varies by object', 'local infill settings — see details');
  let supportClause = '';
  if (enabled && enabled.coverage !== 'unavailable') {
    if (enabled.coverage === 'mixed') supportClause = 'supports vary by object';
    else {
      supportClause = supportsText(enabled.value, exactValue(type), exactValue(plateOnly));
      // Blockers, enforcers, paint, or a modifier's own support keys: the
      // configured mode is still true, and it is not the whole story.
      if (enabled.coverage === 'local') supportClause += '; local support edits';
    }
  }
  return [[layerClause, wallClause], [infillClause, supportClause]]
    .map((parts) => parts.filter(Boolean).join(' · ')).filter(Boolean).map(sentence);
}

// What the estimate area says. Time and material always belong to the same
// plate and the same slice; neither is shown as zero when it is missing.
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
  const view: EstimateView = { heading: '', metrics: [], cost: '', plate: plate?.name ?? '', description: '',
    status: 'none', sliced: !!plate?.sliced };
  if (!plate) return { ...view, description: 'Estimate unavailable' };
  const estimate = plate.estimate;
  const scoped = (text: string) => plate.name ? `${text} · ${plate.name}` : text;
  // A host that predates the two availability flags sent a zero for a
  // quantity it could not work out, so there a zero means absent.
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
  // A priced job so small it rounds to nothing reads exactly like the
  // unpriced case, so it takes the same exit: no clause rather than 0.00.
  const cost = estimate && estimate.materialCost !== null && estimate.materialCost >= 0.005
    ? formatCost(estimate.materialCost, context.currency) : '';
  if (plate.estimateStatus === 'stale')
    return { ...view, status: 'stale', heading: 'Previous estimates · not current', metrics, cost };
  return { ...view, status: 'current', metrics, cost };
}

// One deterministic finding. A check that has only its headline to offer is
// one row; one that compared two values can also show them.
export interface Finding { title: string; detail?: string; facts?: string[]; }

// Findings a deterministic comparison produced, most consequential first.
// The printer's side is what it last reported, with when: nothing here can
// prove what is loaded this second, and a mismatch nobody timed is not shown.
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

// The newest thing in the log, when it is a change the agent just completed:
// every entry it made since the same point in the conversation.
export function recentAgentChange(changes: ChangeInfo[] | undefined, now: number): ChangeInfo[] {
  const log = [...(changes ?? [])].sort((a, b) => a.seq - b.seq);
  const last = log[log.length - 1];
  if (!last || last.actor !== 'agent' || (last.kind !== 'setting' && last.kind !== 'step')) return [];
  const made = Date.parse(last.createdAt);
  if (!Number.isFinite(made) || now - made >= RECENT_CHANGE_MS || now < made) return [];
  let start = log.length - 1;
  while (start > 0 && log[start - 1].actor === 'agent' && log[start - 1].afterId === last.afterId &&
    (log[start - 1].kind === 'setting' || log[start - 1].kind === 'step')) start -= 1;
  return log.slice(start);
}

// The settings the person changed by hand since the agent last changed
// anything, net of their own later edits to the same setting. Restoring a
// saved project replays its settings and logs them under the person who asked
// for the restore; those are the restore's work, not hand edits, so the run
// starts after the newest restore.
export function handEdits(changes: ChangeInfo[] | undefined): ChangeInfo[] {
  const log = [...(changes ?? [])].sort((a, b) => a.seq - b.seq);
  let start = log.length;
  while (start > 0 && log[start - 1].actor === 'person' && log[start - 1].kind !== 'restore') start -= 1;
  const net = new Map<string, ChangeInfo>();
  for (const change of log.slice(start))
    if (change.kind === 'setting') net.set(change.label, change);
  return [...net.values()];
}

function changeText(change: ChangeInfo): string {
  if (change.kind !== 'setting') return change.label || 'Object settings changed';
  return `${change.label} ${change.from || 'none'} → ${change.to || 'none'}`;
}

function estimatePair(estimate: SliceEstimateInfo): string {
  return `${formatPrintTime(estimate.printTimeSeconds)} / ${formatGrams(estimate.materialGrams)}`;
}

function Row({ label, children }: { label: string; children: ReactNode }) {
  return <div className="current-setup-row"><dt>{label}</dt><dd>{children}</dd></div>;
}

function Identity({ identity }: { identity: SetupIdentityInfo | null | undefined }) {
  if (!identity) return <p className="current-setup-note">Printer and material details unavailable</p>;
  const several = identity.filaments.length > 1;
  return <dl className="current-setup-rows">
    {identity.printer && <Row label="Printer">{identity.printer}</Row>}
    {identity.nozzles.length > 0 && <Row label={identity.nozzles.length > 1 ? 'Nozzles' : 'Nozzle'}>
      {identity.nozzles.map((size) => `${size} mm`).join(', ')}</Row>}
    {identity.filaments.map((filament, index) => <Row key={index} label={several ? `Filament ${index + 1}` : 'Filament'}>
      {filament.preset || filament.material || 'Unavailable'}</Row>)}
    {identity.plateType && <Row label="Build plate">{identity.plateType}</Row>}
  </dl>;
}

// Objects disagree. The plate's own value is still a fact worth showing, as
// long as it is not shown alone.
function varies(setting: Setting | undefined, exact: (value: string) => string): string {
  return setting?.base ? `${exact(setting.base)} · varies by object` : 'Varies by object';
}

// One applied-settings row's value. Off, mixed, and unknown each read
// differently: they are three different facts.
function generic(setting: Setting | undefined, exact: (value: string) => string): string {
  if (!setting || setting.coverage === 'unavailable') return 'Unavailable';
  if (setting.coverage === 'mixed') return varies(setting, exact);
  if (setting.coverage === 'local') return 'Local settings';
  return exact(setting.value);
}

function appliedRows(setup: AppliedSetupInfo): { label: string; value: string }[] {
  const get = (key: string) => settingOf(setup, key);
  const exactValue = (key: string) => get(key)?.coverage === 'exact' ? get(key)!.value : undefined;
  const rows: { label: string; value: string }[] = [];
  rows.push({ label: 'Layer height', value: setup.variableLayerHeight ? 'Variable' :
    clause(setup, get('layer_height'), millimetres, millimetres, varies(get('layer_height'), millimetres), 'Local settings') || 'Unavailable' });
  if (setup.spiralMode) {
    rows.push({ label: 'Mode', value: 'Spiral vase' });
  } else {
    rows.push({ label: 'Wall loops', value:
      clause(setup, get('wall_loops'), (value) => value, (value) => value, varies(get('wall_loops'), (value) => value),
        'Local settings') || 'Unavailable' });
    const infill = (value: string) => {
      const percent = Number.parseFloat(value);
      if (percent === 0) return 'None';
      if (percent === 100) return '100%';
      const pattern = exactValue('sparse_infill_pattern');
      return pattern ? `${value} · ${enumWord(pattern)}` : value;
    };
    rows.push({ label: 'Infill', value: clause(setup, get('sparse_infill_density'), infill, (value) => value,
      varies(get('sparse_infill_density'), infill), 'Local settings') || 'Unavailable' });
    const top = get('top_shell_layers');
    const bottom = get('bottom_shell_layers');
    if (top || bottom) {
      const layers = top?.coverage === 'exact' && bottom?.coverage === 'exact'
        ? `${top.value} / ${bottom.value} layers` : generic(top?.coverage === 'exact' ? bottom : top, (value) => value);
      // A minimum thickness can add shells beyond the count, so when one is
      // set the count alone does not describe the shell.
      const minimum = [exactValue('top_shell_thickness'), exactValue('bottom_shell_thickness')];
      const constrained = layers.endsWith('layers') && minimum.every((value) => value !== undefined) &&
        minimum.some((value) => Number(value) > 0);
      rows.push({ label: 'Top / bottom', value: constrained
        ? `${layers} · min ${Number(minimum[0])} / ${Number(minimum[1])} mm` : layers });
    }
  }
  const enabled = get('enable_support');
  const supportMode = (value: string) => {
    if (!on(value)) return 'Off';
    const type = exactValue('support_type') ?? '';
    const kind = `${type.startsWith('tree') ? 'Tree' : 'Normal'}${type.endsWith('(manual)') ? ' (manual)' : ''}`;
    return on(exactValue('support_on_build_plate_only') ?? '') ? `${kind} · build plate only` : kind;
  };
  // Blockers, enforcers and paint leave the configured mode true and
  // incomplete, so the row says both.
  const supports = enabled?.coverage === 'local' ? `${supportMode(enabled.value)}; local support edits`
    : generic(enabled, supportMode);
  rows.push({ label: 'Supports', value: supports });
  const brim = get('brim_type');
  if (brim) rows.push({ label: 'Brim', value: generic(brim, (value) => {
    const width = exactValue('brim_width');
    const sized = (where: string) => width && Number(width) > 0 ? `${Number(width)} mm ${where}` : sentence(where);
    switch (value) {
      case 'no_brim': return 'None';
      case 'auto_brim': return 'Auto';
      case 'outer_only': return sized('outer brim');
      case 'inner_only': return sized('inner brim');
      case 'outer_and_inner': return sized('outer and inner brim');
      case 'brim_ears': return 'Mouse ears';
      default: return sentence(enumWord(value));
    }
  }) });
  return rows;
}

function overrideValue(local: LocalOverride, identity: SetupIdentityInfo | null | undefined): string {
  if (local.key === 'extruder') {
    const index = Number(local.value);
    const filament = Number.isInteger(index) && index > 0 ? identity?.filaments[index - 1] : undefined;
    return filament ? `${index} · ${filament.preset || filament.material}` : local.value;
  }
  if (local.key === 'layer_height' || local.key.endsWith('_thickness')) return local.value ? millimetres(local.value) : 'variable';
  if (local.key === 'enable_support' || local.key === 'support_on_build_plate_only') return on(local.value) ? 'on' : 'off';
  return enumWord(local.value);
}

// One object's customizations, its own values first: what each is on, and
// what it sets. A value is the one configured on that owner; where the owner
// reaches in the print is the slicer's business and is not claimed here.
function overrideEntries(setup: AppliedSetupInfo, locals: LocalOverride[], identity: SetupIdentityInfo | null | undefined,
  compare: boolean): { head: string; body: string }[] {
  const describe = (local: LocalOverride) => `${SETTING_NAMES[local.key].toLowerCase()} ${overrideValue(local, identity)}`;
  const own = locals.filter((local) => local.kind === 'object' && local.key);
  const entries = own.length > 0 ? [{ head: 'Object override', body: own.map(describe).join(', ') }] : [];
  const inside = new Map<string, LocalOverride[]>();
  for (const local of locals.filter((item) => item.kind !== 'object' || !item.key)) {
    const name = `${shortTarget(local.target)}\u0000${local.kind}`;
    inside.set(name, [...(inside.get(name) ?? []), local]);
  }
  for (const [name, group] of inside) {
    const [target, kind] = name.split('\u0000');
    const keyed = group.filter((local) => local.key && local.value);
    if (keyed.length === 0) { entries.push({ head: target, body: kind }); continue; }
    entries.push({ head: `${target} (${kind})`, body: keyed.map((local) => {
      // Beside a modifier's value, the object's own: the comparison a
      // reader is making.
      const rest = compare ? settingOf(setup, local.key)?.scopes.find((scope) => scope.object === local.object)?.value : undefined;
      return rest === undefined ? describe(local)
        : `${describe(local)} · rest of object: ${overrideValue({ ...local, value: rest }, identity)}`;
    }).join(', ') });
  }
  return entries;
}

function LocalOverrides({ setup, identity }: { setup: AppliedSetupInfo; identity: SetupIdentityInfo | null | undefined }) {
  // A setting the card has no name for is counted, never described: the
  // list says what was configured, and it does not pretend to be every key.
  const named = setup.localOverrides.filter((local) => !local.key || SETTING_NAMES[local.key]);
  const unnamed = setup.localOverrides.length - named.length;
  const names = setup.objects ?? [];
  const objectValue = (key: string, object: number) =>
    settingOf(setup, key)?.scopes.find((scope) => scope.object === object)?.value;
  const other = unnamed > 0 && <p className="current-setup-note">{plural(unnamed, 'other local setting', 'other local settings')}</p>;
  if (names.length > 1) {
    return <section className="current-setup-section">
      <h3>Local overrides · {names.length} objects</h3>
      {names.map((name, object) => {
        const entries = overrideEntries(setup, named.filter((local) => local.object === object), identity, false);
        const walls = objectValue('wall_loops', object);
        const density = objectValue('sparse_infill_density', object);
        const values = setup.spiralMode ? [] : [walls && wallsText(walls),
          density && infillText(density, objectValue('sparse_infill_pattern', object)).replace(/ infill$/, '')].filter(Boolean);
        return <div className="current-setup-object" key={object}>
          <p className="current-setup-object-name">{name}</p>
          {values.length > 0 && <p className="current-setup-object-values">{values.join(' · ')}</p>}
          <p className="current-setup-note">{entries.length > 0
            ? entries.map((entry) => `${entry.head}: ${entry.body}`).join(' · ') : 'No local overrides · uses plate settings'}</p>
        </div>;
      })}
      {other}
    </section>;
  }
  const entries = overrideEntries(setup, named, identity, true);
  return <section className="current-setup-section">
    <h3>{names.length === 1 ? `Local overrides · ${names[0]}` : 'Local overrides'}</h3>
    {entries.length === 0 && unnamed === 0 && <p className="current-setup-note">No local overrides found</p>}
    {entries.map((entry, index) => <div className="current-setup-override" key={index}>
      <p className="current-setup-override-target">{entry.head}</p>
      <p className="current-setup-note">{sentence(entry.body)}</p>
    </div>)}
    {other}
  </section>;
}

function PresetChanges({ deltas }: { deltas: PresetDeltaInfo[] }) {
  const [open, setOpen] = useState(false);
  // The count is the rows the list shows: one per setting, never a group.
  return <>
    <button type="button" className="current-setup-disclosure" aria-expanded={open} onClick={() => setOpen(!open)}>
      <span className={`jp-icon jp-icon-chevron-${open ? 'down' : 'right'}`} aria-hidden="true" />
      {plural(deltas.length, 'change', 'changes')} from preset
    </button>
    {open && <dl className="current-setup-rows" data-testid="setup-preset-changes">
      {deltas.map((delta) => <Row key={delta.key} label={delta.label || delta.key}>{delta.preset} → {delta.value}</Row>)}
    </dl>}
  </>;
}

function Details({ context }: { context: WorkspaceContext }) {
  const setup = context.appliedSetup;
  const plate = context.plates.find((item) => item.active);
  return <div className="current-setup-details" data-testid="current-setup-expansion">
    <hr className="current-setup-divider" />
    <section className="current-setup-section">
      <h3>Printer &amp; material</h3>
      <Identity identity={context.setupIdentity} />
    </section>
    <hr className="current-setup-divider" />
    <section className="current-setup-section">
      <h3>Applied settings{plate?.name ? ` · ${plate.name}` : ''}</h3>
      {setup ? <dl className="current-setup-rows">
        {appliedRows(setup).map((row) => <Row key={row.label} label={row.label}>{row.value}</Row>)}
      </dl> : <p className="current-setup-note">Applied setting details are unavailable for this setup.</p>}
    </section>
    <hr className="current-setup-divider" />
    {setup ? <LocalOverrides setup={setup} identity={context.setupIdentity} /> : <section className="current-setup-section">
      <h3>Local overrides</h3>
      <p className="current-setup-note">Local override details unavailable</p>
    </section>}
    <hr className="current-setup-divider" />
    <dl className="current-setup-rows"><Row label="Base preset">{context.printer.process || 'Unavailable'}</Row></dl>
    {context.presetDeltas.length > 0 && <>
      <hr className="current-setup-divider" />
      <PresetChanges deltas={context.presetDeltas} />
    </>}
  </div>;
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
  // More than one finding, or one with compared values, has something to open.
  const reviewable = findings.length > 1 || (first.facts ?? []).length > 0;
  const subject = findings.length > 1 ? plural(findings.length, 'finding', 'findings') : first.title.replace(/^(Last reported|Confirmed) | differs$/g, '');
  return <>
    <div className="current-setup-attention">
      <p className="current-setup-warning"><span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />{first.title}</p>
      {first.detail && <p className="current-setup-line">{first.detail}</p>}
      {/* Reviewing shows what was compared. It changes nothing on the
          printer or in the setup. */}
      {reviewable && <button type="button" className="current-setup-link" aria-expanded={open} onClick={() => setOpen(!open)}>
        Review {subject}</button>}
      {open && <ul className="current-setup-findings">
        {findings.map((finding, index) => <li key={index}>
          {findings.length > 1 && <strong>{finding.title}</strong>}
          {(finding.facts ?? []).map((fact) => <span key={fact}>{fact}</span>)}
        </li>)}
      </ul>}
    </div>
    <hr className="current-setup-divider" />
  </>;
}

export function SetupCard({ context, expanded, onToggle, working: busy = false, historical = false, savedAt,
  agentAvailable = true, changes, now = Date.now(), estimateBefore, sent, attention = [], onCompute, onUndo }: SetupCardProps) {
  if (!context) return null;
  // Conversation status outranks live activity: a saved card is never
  // working, whatever the chat that is active happens to be doing.
  const working = busy && !historical;
  const setup = context.appliedSetup;
  const plate = context.plates.find((item) => item.active);
  const preset = context.printer.process.trim();
  const intent = context.setupIntent.trim();
  const ariaLabel = historical ? 'Earlier setup — saved with this conversation' : 'Current setup';

  // Nothing vouches for this setup but the preset the person picked: no
  // agent is configured, or the host has no applied settings to report.
  const noAgent = !historical && !agentAvailable;
  if (noAgent || (!historical && !setup && !intent)) {
    const estimate = estimateView(context);
    return <section className="current-setup current-setup--live" data-testid="current-setup" aria-label={ariaLabel}>
      <div className="current-setup-header"><span className="current-setup-label">Preset fallback</span>
        {noAgent && <span className="current-setup-state">No agent</span>}</div>
      {preset && <h2 className="current-setup-title" title={preset}>{preset}</h2>}
      {noAgent ? <p className="current-setup-line">No agent configured · using the selected preset.</p>
        : estimate.metrics.length > 0 ? <Metrics estimate={estimate} />
        : <p className="current-setup-line">{estimate.description}</p>}
    </section>;
  }

  const estimate = estimateView(context);
  const summary = appliedSummary(setup);
  const justChanged = historical ? [] : recentAgentChange(changes, now);
  const edited = historical || working || justChanged.length > 0 ? [] : handEdits(changes);
  // "Edited by you" needs a setup the agent had a hand in; without one a
  // hand edit is simply how the project was made.
  const agentManaged = !!intent || (changes ?? []).some((change) => change.actor === 'agent');
  const editedByYou = edited.length > 0 && agentManaged;
  // Out of date stands the card down: it drops the accent, as a saved card
  // does, because it no longer vouches for the numbers under it.
  const outOfDate = estimate.status === 'stale' && !working && justChanged.length === 0 && !editedByYou;
  const live = !historical && !outOfDate;
  const findings = [...deterministicAttention(context), ...attention];

  // An out-of-date card is an earlier setup too: what it summarised has
  // been overtaken by the project, whichever chat it sits in.
  const label = sent ? 'Saved sliced snapshot' : historical || outOfDate ? 'Earlier setup' : 'Setup summary';
  const saved = formatMoment(savedAt);
  const state: ReactNode = historical
    ? sent ? 'Sliced & sent'
      : estimate.status === 'stale' ? 'Out of date'
      : saved ? <><span className="jp-icon jp-icon-clock" aria-hidden="true" />Saved {saved}</> : null
    : working ? 'Agent working'
    : justChanged.length > 0 ? 'Updated'
    : estimate.status === 'recomputing' ? 'Confirmed'
    : editedByYou ? 'Edited by you'
    : outOfDate ? 'Out of date'
    : estimate.status === 'none' && !estimate.sliced ? 'Not sliced'
    : 'Current';

  const title = intent || preset;
  // A saved summary from before the card read applied settings has only
  // the preset differences it was saved with. Those are shown as what they
  // are; today's settings never stand in for them.
  const savedDifferences = !setup && context.presetDeltas.length > 0
    ? `Saved preset differences: ${context.presetDeltas.slice(0, 2).map((delta) =>
      `${delta.label || delta.key} ${delta.value}`).join(' · ')}` : '';
  const lines = summary.length > 0 ? summary
    : setup?.printableObjects === 0 ? ['No printable objects on this plate']
    : savedDifferences ? [savedDifferences]
    : [historical ? 'Applied settings were not saved with this chat' : 'Applied settings unavailable'];

  // The estimate area. A hand edit that outdated the slice has no estimate
  // to offer for the settings now in force, and says so in one line.
  const withheld = editedByYou && estimate.status === 'stale';
  const heading = withheld ? ''
    : estimate.heading ? estimate.heading
    : estimate.metrics.length === 0 ? ''
    : sent ? 'Saved pre-slice estimates'
    : working ? 'Last confirmed estimates' : '';
  const description = withheld ? (estimate.plate ? `Estimate unavailable for these edits · ${estimate.plate}`
    : 'Estimate unavailable for these edits') : estimate.description;
  const canCompute = !historical && !working && !!onCompute && estimate.status === 'none';

  const step = justChanged[justChanged.length - 1];
  // Orca's undo takes the top of its own stack. Only a project step the
  // agent just made is known to be there; a preset edit is not on it at all.
  const undoable = !!onUndo && !working && step?.kind === 'step' && context.history.canUndo;
  const after = plate?.estimate;
  const estimateDelta = justChanged.length > 0 && estimateBefore && after && estimate.status === 'current' &&
    (estimateBefore.printTimeSeconds !== after.printTimeSeconds || estimateBefore.materialGrams !== after.materialGrams)
    ? `Estimate: ${estimatePair(estimateBefore)} → ${estimatePair(after)}` : '';
  const named = edited.slice(0, 2).map((change) => `${change.label} ${change.to || 'none'}`);
  const editedText = edited.length > 2 ? `${named.join(', ')} and ${plural(edited.length - 2, 'more setting', 'more settings')}`
    : named.join(' and ');

  return <section className={live ? 'current-setup current-setup--live' : 'current-setup'}
    data-testid="current-setup" aria-label={ariaLabel} aria-busy={working || undefined}>
    <div className="current-setup-header"><span className="current-setup-label">{label}</span>
      {state && <span className="current-setup-state">{state}</span>}</div>
    {title && <h2 className="current-setup-title" title={title}>{title}</h2>}
    {lines.map((line, index) => <p className="current-setup-line" key={index}>{line}</p>)}
    {heading && <p className="current-setup-heading">{heading}</p>}
    {!withheld && estimate.metrics.length > 0 && <Metrics estimate={estimate} />}
    {description && <p className="current-setup-line">{description}</p>}
    {sent && estimate.metrics.length > 0 && <p className="current-setup-line">Actual time &amp; material unavailable.</p>}
    {canCompute && <button type="button" className="current-setup-link" onClick={onCompute}>Compute estimates</button>}
    <hr className="current-setup-divider" />
    {working && <>
      <p className="current-setup-line">Agent is applying and evaluating changes…<br />
        Shown settings remain the last confirmed setup.</p>
      <hr className="current-setup-divider" />
    </>}
    {!working && justChanged.length > 0 && <>
      <p className="current-setup-line">{justChanged.map(changeText).join(' · ')}</p>
      {estimateDelta && <p className="current-setup-line">{estimateDelta}</p>}
      {undoable && <button type="button" className="current-setup-link" onClick={() => onUndo!(step.seq)}>Undo</button>}
      <hr className="current-setup-divider" />
    </>}
    {outOfDate && <>
      <div className="current-setup-attention">
        <p className="current-setup-warning"><span className="jp-icon jp-icon-triangle-alert" aria-hidden="true" />
          {/* A saved card is about the project as it was saved, not today's. */}
          {historical ? 'These estimates were already out of date when saved.'
            : 'Estimates no longer match the current project.'}</p>
        {plate?.invalidatedBy && <p className="current-setup-line">{plate.invalidatedBy}</p>}
        {!historical && onCompute && <button type="button" className="current-setup-link" onClick={onCompute}>
          Refresh setup &amp; recompute</button>}
      </div>
      <hr className="current-setup-divider" />
    </>}
    {editedByYou && <>
      <p className="current-setup-info"><span className="jp-icon jp-icon-info" aria-hidden="true" />
        <span>You set {editedText} outside the agent. Your current settings are preserved.</span></p>
      <hr className="current-setup-divider" />
    </>}
    <Attention findings={findings} />
    <button type="button" className="current-setup-disclosure" aria-expanded={expanded} onClick={onToggle}>
      <span className={`jp-icon jp-icon-chevron-${expanded ? 'down' : 'right'}`} aria-hidden="true" />Setup details
    </button>
    {expanded && <Details context={context} />}
    {historical && <>
      <hr className="current-setup-divider" />
      {sent && <p className="current-setup-sent">{[formatMoment(sent.at), sent.printer && `sent to ${sent.printer}`]
        .filter(Boolean).join(' · ')}</p>}
      <p className="current-setup-info"><span className="jp-icon jp-icon-info" aria-hidden="true" />
        <span>{sent ? 'Saved snapshot.' : 'Saved with this chat.'} The canvas shows your current project.</span></p>
    </>}
  </section>;
}
