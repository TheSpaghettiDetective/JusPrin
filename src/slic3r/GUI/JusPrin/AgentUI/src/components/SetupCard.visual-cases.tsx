// One state input per approved Figma frame on the "Current Setup Card" page,
// sections 5 to 7. The acceptance tests and the review page both render the
// production card from these, so a frame is covered only if its state can be
// produced from real card inputs. The wording on a frame is example data; the
// card's own wording is asserted where the two differ on purpose.

import { AppliedSetupInfo, ChangeInfo, WorkspaceContext } from '../bridge/protocol';
import { SetupCard, SetupCardProps } from './SetupCard';

type Setting = AppliedSetupInfo['settings'][number];
type CaseProps = Omit<SetupCardProps, 'context' | 'onToggle' | 'onCompute' | 'onUndo'> &
  { compute?: boolean; undo?: boolean };

export interface SetupVisualCase {
  id: string;
  section: '5' | '6' | '7';
  // The Figma frame this state reproduces.
  frame: string;
  name: string;
  context: WorkspaceContext;
  props: CaseProps;
}

const setting = (key: string, value: string, object = 'bracket v3'): Setting =>
  ({ key, value, base: value, coverage: 'exact', scopes: [{ object: 0, target: object, kind: 'object', value }] });

function workspace(): WorkspaceContext {
  return {
    sessionId: 'visual-project', revision: 4, projectName: 'Workshop bracket', projectDirty: false,
    printer: { preset: 'Bambu A1', filament: 'PETG', process: '0.20 mm Standard' },
    plates: [{ id: 'plate-1', name: 'Plate 1', active: true, sliced: true,
      estimate: { printTimeSeconds: 8280, materialGrams: 46, materialCost: null, timeAvailable: true, materialAvailable: true },
      estimateStatus: 'current', invalidatedBy: '', objects: [
        { id: 'bracket', name: 'bracket v3', instances: 1, selected: false },
      ] }],
    selection: { status: 'none', objectIds: [] }, history: { canUndo: true, canRedo: false },
    presetDeltas: [], currency: '', setupIntent: 'Strong bracket, clear screw holes',
    appliedSetup: { version: 1, plateId: 'plate-1', printableObjects: 1, spiralMode: false,
      variableLayerHeight: false, objects: ['bracket v3'], localOverrides: [], settings: [
        setting('layer_height', '0.2'), setting('wall_loops', '4'),
        setting('sparse_infill_density', '30%'), setting('sparse_infill_pattern', 'gyroid'),
        setting('enable_support', '1'), setting('support_type', 'normal(auto)'),
        setting('support_on_build_plate_only', '1'), setting('top_shell_layers', '5'),
        setting('bottom_shell_layers', '4'), setting('top_shell_thickness', '0'),
        setting('bottom_shell_thickness', '0'), setting('brim_type', 'outer_only'), setting('brim_width', '5'),
      ] },
    setupIdentity: { printer: 'Bambu A1', nozzles: [0.4], plateType: 'Textured PEI',
      filaments: [{ preset: 'PETG', material: 'PETG' }] },
    printerReview: { mismatches: [] },
  };
}

function variant(base: WorkspaceContext, change: (value: WorkspaceContext) => void): WorkspaceContext {
  const value = structuredClone(base);
  change(value);
  return value;
}

const at = (value: WorkspaceContext, key: string) => value.appliedSetup!.settings.find((item) => item.key === key)!;
const set = (value: WorkspaceContext, key: string, next: string) => {
  const item = at(value, key);
  item.value = next;
  item.base = next;
  item.scopes.forEach((scope) => { scope.value = next; });
};

// The bracket the agent strengthened: thicker walls with a modifier on the
// tabs, denser infill, supports from the plate, blockers in the screw holes.
const strong = variant(workspace(), (value) => {
  at(value, 'wall_loops').coverage = 'local';
  at(value, 'enable_support').coverage = 'local';
  value.appliedSetup!.localOverrides = [
    { object: 0, target: 'bracket v3 / Mounting tabs', kind: 'modifier', key: 'wall_loops', value: '6' },
    { object: 0, target: 'bracket v3 / Screw holes', kind: 'support blocker', key: '', value: '' },
  ];
});

// The quick fit check: coarse layers, two walls, almost no infill.
const quick = variant(workspace(), (value) => {
  value.setupIntent = 'Quick fit check, under 1 hour';
  set(value, 'layer_height', '0.28');
  set(value, 'wall_loops', '2');
  set(value, 'sparse_infill_density', '5%');
  set(value, 'enable_support', '0');
  value.plates[0].estimate = { printTimeSeconds: 2520, materialGrams: 18, materialCost: null,
    timeAvailable: true, materialAvailable: true };
});

const three = variant(workspace(), (value) => {
  const names = ['bracket.stl', 'cover.stl', 'spacer.stl'];
  value.setupIntent = '3 objects, different settings';
  value.plates[0].sliced = false;
  value.plates[0].estimate = null;
  value.plates[0].objects = names.map((name, index) => ({ id: String(index), name, instances: 1, selected: false }));
  const setup = value.appliedSetup!;
  setup.printableObjects = 3;
  setup.objects = names;
  const perObject: Record<string, string[]> = {
    wall_loops: ['4', '4', '2'], sparse_infill_density: ['30%', '30%', '5%'], enable_support: ['1', '1', '0'],
  };
  for (const item of setup.settings) {
    const values = perObject[item.key] ?? names.map(() => item.value);
    item.scopes = names.map((name, object) => ({ object, target: name, kind: 'object', value: values[object] }));
    if (perObject[item.key]) item.coverage = 'mixed';
  }
  setup.localOverrides = [
    { object: 0, target: 'bracket.stl / Mounting tabs', kind: 'modifier', key: 'wall_loops', value: '6' },
    { object: 0, target: 'bracket.stl / Screw holes', kind: 'support blocker', key: '', value: '' },
    { object: 2, target: 'spacer.stl', kind: 'object', key: 'wall_loops', value: '2' },
    { object: 2, target: 'spacer.stl', kind: 'object', key: 'sparse_infill_density', value: '5%' },
    { object: 2, target: 'spacer.stl', kind: 'object', key: 'enable_support', value: '0' },
  ];
});

// When the review page and the tests "are": a fixed clock, so the window a
// completed change stays new for does not depend on when the suite runs.
export const VISUAL_NOW = Date.parse('2026-10-03T10:42:30');

const change = (seq: number, patch: Partial<ChangeInfo>): ChangeInfo => ({
  seq, createdAt: '2026-10-03T10:42:00', kind: 'setting', actor: 'agent', label: '', preset: '0.20 mm Standard',
  conversationId: 'visual-chat', afterId: 'message-1', ...patch });

const agentChange: ChangeInfo[] = [
  change(1, { label: 'Layer height', from: '0.28', to: '0.2' }),
  change(2, { label: 'Wall loops', from: '2', to: '4' }),
  change(3, { label: 'Sparse infill density', from: '5%', to: '30%' }),
  change(4, { kind: 'step', label: 'Add modifier: Mounting tabs', preset: undefined }),
];

const handChange: ChangeInfo[] = [
  change(1, { label: 'Wall loops', from: '3', to: '2' }),
  change(2, { actor: 'person', label: 'Wall loops', from: '2', to: '4', afterId: 'message-2', createdAt: '2026-10-03T10:30:00' }),
  change(3, { actor: 'person', label: 'Sparse infill density', from: '15%', to: '30%', afterId: 'message-2',
    createdAt: '2026-10-03T10:31:00' }),
];

const saved = '2026-10-03T10:42:00';

export const setupVisualCases: SetupVisualCase[] = [
  { id: '5-current', section: '5', frame: '1291:1402', name: 'Current · collapsed', context: quick, props: { expanded: false } },
  { id: '5-expanded', section: '5', frame: '1291:1463', name: 'Current · expanded',
    context: variant(strong, (value) => {
      value.printerReview = { observed: { connection: 'idle', observedAt: '2026-10-03T10:41:00' },
        mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' }] };
      value.presetDeltas = [
        { key: 'wall_loops', label: 'Wall loops', preset: '2', value: '4', origin: 'agent' },
        { key: 'sparse_infill_density', label: 'Sparse infill density', preset: '15%', value: '30%', origin: 'agent' },
        { key: 'enable_support', label: 'Enable support', preset: '0', value: '1', origin: 'agent' },
        { key: 'brim_width', label: 'Brim width', preset: '0', value: '5', origin: 'agent' },
      ];
    }), props: { expanded: true } },
  { id: '5-earlier', section: '5', frame: '1291:1731', name: 'Earlier · saved', context: quick,
    props: { expanded: false, historical: true, savedAt: saved } },

  { id: '6-just-changed', section: '6', frame: '1312:5525', name: 'Just changed', context: strong,
    props: { expanded: false, changes: agentChange, now: VISUAL_NOW, undo: true,
      estimateBefore: { printTimeSeconds: 2520, materialGrams: 18, materialCost: null } } },
  { id: '6-working', section: '6', frame: '1312:5565', name: 'Agent working', context: quick,
    props: { expanded: false, working: true } },
  { id: '6-recomputing', section: '6', frame: '1312:5603', name: 'Estimate recomputing',
    context: variant(strong, (value) => { value.plates[0].estimateStatus = 'recomputing'; }),
    props: { expanded: false } },
  { id: '6-stale', section: '6', frame: '1312:5627', name: 'Out of date',
    context: variant(quick, (value) => { value.plates[0].estimateStatus = 'stale';
      value.plates[0].invalidatedBy = 'Project settings changed after this estimate.'; }),
    props: { expanded: false, compute: true } },
  { id: '6-manual', section: '6', frame: '1312:5671', name: 'Edited by hand',
    context: variant(strong, (value) => { value.plates[0].estimateStatus = 'stale';
      value.plates[0].invalidatedBy = 'Wall loops changed'; }),
    props: { expanded: false, changes: handChange, now: VISUAL_NOW } },
  { id: '6-risk', section: '6', frame: '1312:5699', name: 'One-line risk', context: quick,
    props: { expanded: false, attention: [{ title: 'Coarse layers may leave visible surface lines.' }] } },
  { id: '6-no-agent', section: '6', frame: '1312:5739', name: 'No agent configured',
    context: variant(workspace(), (value) => { value.setupIntent = ''; }),
    props: { expanded: false, agentAvailable: false } },
  { id: '6-sent', section: '6', frame: '1312:5745', name: 'Sliced and sent', context: quick,
    props: { expanded: false, historical: true, savedAt: saved, sent: { at: saved, printer: 'Bambu A1' } } },

  { id: '7-long-title', section: '7', frame: '1312:5791', name: 'Long title',
    context: variant(strong, (value) => {
      value.setupIntent = 'Durable workshop bracket with clean mounting holes that stay strong under repeated use'; }),
    props: { expanded: false } },
  { id: '7-no-title', section: '7', frame: '1312:5826', name: 'No title',
    context: variant(strong, (value) => { value.setupIntent = ''; }), props: { expanded: false } },
  { id: '7-no-currency', section: '7', frame: '1312:5861', name: 'No currency', context: quick, props: { expanded: false } },
  { id: '7-with-currency', section: '7', frame: '1312:5897', name: 'With currency',
    context: variant(quick, (value) => { value.currency = 'GBP'; value.plates[0].estimate!.materialCost = 1.12; }),
    props: { expanded: false } },
  { id: '7-never-sliced', section: '7', frame: '1312:5934', name: 'Never sliced',
    context: variant(quick, (value) => { value.plates[0].sliced = false; value.plates[0].estimate = null; }),
    props: { expanded: false, compute: true } },
  { id: '7-three-collapsed', section: '7', frame: '1312:5959', name: 'Three objects · collapsed', context: three,
    props: { expanded: false } },
  { id: '7-three-expanded', section: '7', frame: '1312:5982', name: 'Three objects · expanded',
    context: variant(three, (value) => {
      value.presetDeltas = [{ key: 'wall_loops', label: 'Wall loops', preset: '2', value: '4', origin: 'user' }]; }),
    props: { expanded: true } },
  { id: '7-missing', section: '7', frame: '1312:6081', name: 'Everything missing',
    context: variant(workspace(), (value) => { value.setupIntent = ''; value.appliedSetup = null;
      value.setupIdentity = null; value.printerReview = null; value.plates[0].estimate = null; }),
    props: { expanded: false } },
];

// The production card for one case. The handlers exist exactly when the case
// says the host could carry the action out.
export function renderVisualCase(entry: SetupVisualCase, handlers: {
  onToggle?: () => void; onCompute?: () => void; onUndo?: (changeSeq: number) => void } = {}) {
  const { compute, undo, ...props } = entry.props;
  return <SetupCard context={entry.context} {...props} onToggle={handlers.onToggle ?? (() => {})}
    onCompute={compute ? handlers.onCompute ?? (() => {}) : undefined}
    onUndo={undo ? handlers.onUndo ?? (() => {}) : undefined} />;
}
