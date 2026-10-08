// One state input per frame of the setup card and the setup page. The
// acceptance tests and the review page both render the production components
// from these, so a state is covered only if it can be produced from real
// inputs. A1, A2 and B are the three frames of the "setup card shows
// differences from presets" design; the card's remaining states are the ones
// first drawn on the Figma page "Current Setup Card", sections 6 and 7, whose
// frame each names. Every setting value below is written the way the host's
// formatter writes it.

import { ChangeInfo, PresetDeltaInfo, WorkspaceContext } from '../bridge/protocol';
import { Finding, SetupCard, SetupCardProps } from './SetupCard';
import { SetupPage, SetupPageHeader } from './SetupPage';

type CaseProps = Omit<SetupCardProps, 'context' | 'onCompute' | 'onUndo' | 'onViewSetup'> &
  { compute?: boolean; undo?: boolean };

export interface SetupVisualCase {
  id: string;
  name: string;
  kind: 'card' | 'page';
  // The Figma frame the state was first drawn on, where there is one.
  frame?: string;
  context: WorkspaceContext;
  props: CaseProps;
}

// When the review page and the tests "are": a fixed clock, so the window a
// completed change stays new for does not depend on when the suite runs.
export const VISUAL_NOW = Date.parse('2026-10-08T12:00:30');

const PROCESS = '0.20mm Standard @MyKlipper';
const FILAMENT = 'Generic ABS @System';

const delta = (key: string, label: string, display: string, extra: Partial<PresetDeltaInfo> = {}): PresetDeltaInfo => ({
  key, label, display, preset: '', value: '', origin: 'agent', presetType: 'process', presetName: PROCESS,
  page: 'Others', group: 'Brim', ...extra });

export function setupWorkspace(): WorkspaceContext {
  return {
    sessionId: 'visual-project', revision: 4, projectName: 'Workshop bracket', projectDirty: false,
    printer: { preset: 'MyKlipper 0.4 nozzle', filament: FILAMENT, process: PROCESS },
    plates: [
      { id: 'plate-1', name: 'Plate 1', active: true, sliced: true,
        estimate: { printTimeSeconds: 8280, materialGrams: 41, materialCost: null, timeAvailable: true, materialAvailable: true },
        estimateStatus: 'current', invalidatedBy: '', objects: [
          { id: 'cali', name: '3x3_cali_RL.stl', instances: 1, selected: false },
          { id: 'bracket', name: 'bracket.stl', instances: 1, selected: false },
        ] },
      { id: 'plate-2', name: 'Plate 2', active: false, sliced: false, estimate: null,
        estimateStatus: 'current', invalidatedBy: '', objects: [] },
    ],
    selection: { status: 'none', objectIds: [] }, history: { canUndo: true, canRedo: false },
    // In the host's order, which is OrcaSlicer's own order of its settings.
    presetDeltas: [
      delta('initial_layer_print_height', 'First layer height', '0.2 → 0.28 mm',
        { preset: '0.2', value: '0.28', page: 'Quality', group: 'Layer height' }),
      delta('initial_layer_speed', 'First layer', '50 → 25 mm/s',
        { preset: '50', value: '25', page: 'Speed', group: 'First layer speed' }),
      delta('brim_width', 'Brim width', '0 → 8 mm', { preset: '0', value: '8' }),
      delta('brim_type', 'Brim type', 'Auto → Outer brim only', { preset: 'auto_brim', value: 'outer_only' }),
      delta('hot_plate_temp', 'Bed temperature', '100 → 105 ℃', { preset: '100', value: '105', origin: 'user',
        presetType: 'filament', presetName: FILAMENT, page: 'Filament', group: 'Bed temperature' }),
    ],
    appliedSetup: { version: 1, plateId: 'plate-1', printableObjects: 2, spiralMode: false,
      variableLayerHeight: false, objects: ['3x3_cali_RL.stl', 'bracket.stl'], localOverrides: [
        { object: 0, target: '3x3_cali_RL.stl', kind: 'object', key: 'sparse_infill_density', value: '30%',
          label: 'Sparse infill density', display: '30%' },
        { object: 0, target: '3x3_cali_RL.stl / Corner tab', kind: 'modifier', key: 'wall_loops', value: '5',
          label: 'Wall loops', display: '5' },
      ] },
    setupIdentity: { printer: 'MyKlipper 0.4 nozzle', nozzles: [0.4], plateType: 'Smooth High Temp Plate', filaments: [
      { preset: FILAMENT, material: 'ABS' }, { preset: 'Generic PLA @System', material: 'PLA' },
      { preset: 'Generic PLA @System', material: 'PLA' },
    ] },
    printerReview: { mismatches: [] }, currency: '', setupIntent: 'Stop the corners lifting',
  };
}

function variant(change: (value: WorkspaceContext) => void, base: WorkspaceContext = setupWorkspace()): WorkspaceContext {
  const value = structuredClone(base);
  change(value);
  return value;
}

const change = (seq: number, patch: Partial<ChangeInfo>): ChangeInfo => ({
  seq, createdAt: '2026-10-08T10:00:00', kind: 'setting', actor: 'agent', label: '', preset: PROCESS,
  conversationId: 'visual-chat', afterId: 'message-1', ...patch });

// The agent's latest turn: one patch of the two brim settings, logged in the
// host's order. They lead the card ahead of what it changed in earlier turns.
export const visualChanges: ChangeInfo[] = [
  change(1, { key: 'brim_width', label: 'Brim width', from: '0 mm', to: '8 mm' }),
  change(2, { key: 'brim_type', label: 'Brim type', from: 'Auto', to: 'Outer brim only' }),
];

const justNow = (entry: ChangeInfo): ChangeInfo => ({ ...entry, createdAt: '2026-10-08T12:00:00' });
const justChanged: ChangeInfo[] = [...visualChanges.map(justNow),
  justNow(change(3, { kind: 'step', label: 'Add modifier: Corner tab', preset: undefined }))];

const handChanges: ChangeInfo[] = [...visualChanges,
  change(3, { actor: 'person', key: 'hot_plate_temp', label: 'Bed temperature', from: '100 ℃', to: '105 ℃',
    preset: FILAMENT, afterId: 'message-2', createdAt: '2026-10-08T11:30:00' })];

// A new project nobody has touched: no purpose, nothing changed, never sliced.
const untouched = variant((value) => {
  value.setupIntent = '';
  value.presetDeltas = [];
  value.appliedSetup!.localOverrides = [];
  value.plates[0].sliced = false;
  value.plates[0].estimate = null;
});

const stale = variant((value) => {
  value.plates[0].estimateStatus = 'stale';
  value.plates[0].invalidatedBy = 'a setting changed';
});

const dense = variant((value) => {
  value.setupIntent = 'Keep the corners of this unusually long workshop fixture flat through the entire print';
  value.printer.process = 'Draft quality profile for the large enclosure panels with thick walls';
  value.printer.filament = 'Workshop PETG carbon fibre reinforced high temperature';
  value.setupIdentity!.filaments = Array.from({ length: 6 }, (_, index) =>
    ({ preset: index === 0 ? value.printer.filament : `Generic PLA @System`, material: 'PLA' }));
  value.presetDeltas = Array.from({ length: 20 }, (_, index) => delta(`setting_${index}`,
    index === 0 ? 'Stamping distance measured from the center of the cooling tube' : `Setting ${index + 1}`,
    `${index} → ${index + 1} mm`, { presetName: value.printer.process, page: `Page ${Math.floor(index / 5) + 1}`,
      group: `Group ${index % 2 + 1}`, origin: 'user' }));
});

const threeObjects = variant((value) => {
  const names = ['bracket.stl', 'cover.stl', 'spacer.stl'];
  value.plates[0].objects = names.map((name, index) => ({ id: String(index), name, instances: 1, selected: false }));
  value.appliedSetup!.printableObjects = 3;
  value.appliedSetup!.objects = names;
  value.appliedSetup!.spiralMode = true;
  value.appliedSetup!.localOverrides = [
    { object: 0, target: 'bracket.stl / Mounting tabs', kind: 'modifier', key: 'wall_loops', value: '6', label: 'Wall loops', display: '6' },
    { object: 0, target: 'bracket.stl / Screw holes', kind: 'support blocker', key: '', value: '' },
    { object: 0, target: 'bracket.stl', kind: 'support painting', key: '', value: '' },
    { object: 2, target: 'spacer.stl', kind: 'object', key: 'extruder', value: '2', label: 'Filament slot', display: '2' },
    { object: 2, target: 'spacer.stl', kind: 'object', key: 'enable_support', value: '0', label: 'Enable support', display: 'Off' },
    { object: 2, target: 'spacer.stl', kind: 'height range', key: 'layer_height', value: '0.12', label: 'Layer height', display: '0.12 mm' },
  ];
});

// What a chat saved before the host formatted values still carries: the raw
// pair for each difference and the raw key of each override.
const legacy = variant((value) => {
  value.presetDeltas = [
    { key: 'wall_loops', label: 'Wall loops', preset: '2', value: '4', origin: 'agent' },
    { key: 'brim_type', label: 'Brim type', preset: 'auto_brim', value: 'outer_only', origin: 'agent' },
  ];
  value.appliedSetup!.localOverrides = [
    { object: 0, target: '3x3_cali_RL.stl', kind: 'object', key: 'sparse_infill_density', value: '30%' }];
});

const saved = '2026-10-08T10:42:00';
const risk: Finding[] = [{ title: 'Coarse layers may leave visible surface lines.' }];

export const setupVisualCases: SetupVisualCase[] = [
  { id: 'A1', name: 'Card · changes', kind: 'card', context: setupWorkspace(), props: { changes: visualChanges } },
  { id: 'A2', name: 'Card · nothing changed', kind: 'card', context: untouched, props: { compute: true } },
  { id: 'B', name: 'Setup page', kind: 'page', context: setupWorkspace(), props: { changes: visualChanges } },

  { id: 'earlier', name: 'Earlier · saved', kind: 'card', frame: '1291:1731', context: setupWorkspace(),
    props: { historical: true, savedAt: saved } },
  { id: 'just-changed', name: 'Just changed', kind: 'card', frame: '1312:5525', context: setupWorkspace(),
    props: { changes: justChanged, undo: true, estimateBefore: { printTimeSeconds: 7200, materialGrams: 38, materialCost: null } } },
  { id: 'working', name: 'Agent working', kind: 'card', frame: '1312:5565', context: setupWorkspace(),
    props: { changes: visualChanges, working: true } },
  { id: 'recomputing', name: 'Estimate recomputing', kind: 'card', frame: '1312:5603',
    context: variant((value) => { value.plates[0].estimateStatus = 'recomputing'; }), props: { changes: visualChanges } },
  { id: 'stale', name: 'Out of date', kind: 'card', frame: '1312:5627', context: stale, props: { compute: true } },
  { id: 'manual', name: 'Edited by hand', kind: 'card', frame: '1312:5671', context: stale, props: { changes: handChanges } },
  { id: 'risk', name: 'One-line risk', kind: 'card', frame: '1312:5699', context: setupWorkspace(),
    props: { changes: visualChanges, attention: risk } },
  { id: 'no-agent', name: 'No agent configured', kind: 'card', frame: '1312:5739',
    context: variant((value) => { value.setupIntent = ''; }), props: { agentAvailable: false } },
  { id: 'sent', name: 'Sliced and sent', kind: 'card', frame: '1312:5745', context: setupWorkspace(),
    props: { historical: true, savedAt: saved, sent: { at: saved, printer: 'MyKlipper' } } },
  { id: 'with-currency', name: 'With currency', kind: 'card', frame: '1312:5897',
    context: variant((value) => { value.currency = 'GBP'; value.plates[0].estimate!.materialCost = 1.12; }),
    props: { changes: visualChanges } },
  { id: 'missing', name: 'Nothing read', kind: 'card', frame: '1312:6081',
    context: variant((value) => { value.setupIntent = ''; value.appliedSetup = null; value.setupIdentity = null;
      value.printerReview = null; value.presetDeltas = []; value.plates[0].estimate = null; }), props: {} },
  { id: 'legacy', name: 'Saved before values were formatted', kind: 'card', context: legacy,
    props: { historical: true, savedAt: saved } },
  { id: 'dense', name: 'Card · long and dense', kind: 'card', frame: '1312:5791', context: dense, props: {} },

  { id: 'page-untouched', name: 'Page · nothing changed', kind: 'page', context: untouched, props: {} },
  { id: 'page-objects', name: 'Page · three objects', kind: 'page', frame: '1312:5982', context: threeObjects,
    props: { changes: visualChanges } },
  { id: 'page-legacy', name: 'Page · saved before values were formatted', kind: 'page', context: legacy,
    props: { historical: true, savedAt: saved } },
  { id: 'page-dense', name: 'Page · long and dense', kind: 'page', context: dense, props: {} },
];

// The production component for one case. The handlers exist exactly when the
// case says the host could carry the action out. A page comes with the header
// the panel gives it.
export function renderVisualCase(entry: SetupVisualCase, handlers: {
  onViewSetup?: () => void; onCompute?: () => void; onUndo?: (changeSeq: number) => void; onBack?: () => void;
  onCollapse?: () => void } = {}) {
  const { compute, undo, ...props } = entry.props;
  if (entry.kind === 'page')
    return <><SetupPageHeader onBack={handlers.onBack ?? (() => {})} onCollapse={handlers.onCollapse ?? (() => {})} />
      <SetupPage context={entry.context} now={VISUAL_NOW} {...props} /></>;
  return <SetupCard context={entry.context} now={VISUAL_NOW} {...props}
    onViewSetup={handlers.onViewSetup ?? (() => {})}
    onCompute={compute ? handlers.onCompute ?? (() => {}) : undefined}
    onUndo={undo ? handlers.onUndo ?? (() => {}) : undefined} />;
}
