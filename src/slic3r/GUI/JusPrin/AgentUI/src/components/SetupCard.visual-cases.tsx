// One state input per frame of the setup card and the All settings panel. The
// acceptance tests and the review page both render the production components
// from these, so a state is covered only if it can be produced from real
// inputs. A1, A2 and B are the three frames of the "setup card shows
// differences from presets" design; the card's remaining states are the
// boards of the two-part card on the canvas page "Current Setup Card"
// (2026-10-26). Every setting value below is written the way the host's
// formatter writes it.

import { CatalogSettingInfo, PresetDeltaInfo, SettingsCatalogPayload, WorkspaceContext } from '../bridge/protocol';
import { SetupCard, SetupCardProps } from './SetupCard';
import { AllSettings, AllSettingsHeader, catalogFromContext } from './AllSettings';

type CaseProps = Omit<SetupCardProps, 'context' | 'onCompute' | 'onViewSetup' | 'onCheckPlan'> & {
  compute?: boolean; checkPlan?: boolean };

export interface SetupVisualCase {
  id: string;
  name: string;
  kind: 'card' | 'page';
  context: WorkspaceContext;
  props: CaseProps;
}

const PROCESS = '0.20mm Standard @MyKlipper';
const FILAMENT = 'Generic ABS @System';

const delta = (key: string, label: string, display: string, extra: Partial<PresetDeltaInfo> = {}): PresetDeltaInfo => ({
  key, label, display, preset: '', value: '', presetType: 'process', presetName: PROCESS,
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
      delta('hot_plate_temp', 'Bed temperature', '100 → 105 ℃', { preset: '100', value: '105',
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
    planHeadline: 'Wide brim and a slow first layer for adhesion',
  };
}

function variant(change: (value: WorkspaceContext) => void, base: WorkspaceContext = setupWorkspace()): WorkspaceContext {
  const value = structuredClone(base);
  change(value);
  return value;
}

// A new project nobody has touched: no purpose, nothing changed, never sliced.
const untouched = variant((value) => {
  value.setupIntent = '';
  value.planHeadline = '';
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
  value.planHeadline = 'Extra-wide brim, a slow first layer and a cooler bed so the long fixture stays flat on the plate';
  value.printer.process = 'Draft quality profile for the large enclosure panels with thick walls';
  value.printer.filament = 'Workshop PETG carbon fibre reinforced high temperature';
  value.setupIdentity!.filaments = Array.from({ length: 6 }, (_, index) =>
    ({ preset: index === 0 ? value.printer.filament : `Generic PLA @System`, material: 'PLA' }));
  value.presetDeltas = Array.from({ length: 20 }, (_, index) => delta(`setting_${index}`,
    index === 0 ? 'Stamping distance measured from the center of the cooling tube' : `Setting ${index + 1}`,
    `${index} → ${index + 1} mm`, { presetName: value.printer.process, page: `Page ${Math.floor(index / 5) + 1}`,
      group: `Group ${index % 2 + 1}` }));
});

const threeObjects = variant((value) => {
  const names = ['bracket.stl', 'cover.stl', 'spacer.stl'];
  value.plates[0].objects = names.map((name, index) => ({ id: String(index), name, instances: 1, selected: index === 0 }));
  value.selection = { status: 'objects', objectIds: ['0'] };
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

const saved = '2026-10-08T10:42:00';

export const setupVisualCases: SetupVisualCase[] = [
  { id: 'A1', name: 'Card · current', kind: 'card', context: setupWorkspace(), props: {} },
  { id: 'A2', name: 'Card · no words yet, never sliced', kind: 'card', context: untouched, props: { compute: true } },
  { id: 'B', name: 'All settings · whole project', kind: 'page', context: setupWorkspace(), props: {} },

  { id: 'earlier', name: 'Earlier · saved', kind: 'card', context: setupWorkspace(),
    props: { historical: true, savedAt: saved } },
  { id: 'working', name: 'Agent working', kind: 'card', context: setupWorkspace(), props: { working: true } },
  { id: 'recomputing', name: 'Estimate recomputing', kind: 'card',
    context: variant((value) => { value.plates[0].estimateStatus = 'recomputing'; }), props: {} },
  { id: 'stale', name: 'Estimates not current', kind: 'card', context: stale, props: { compute: true } },
  { id: 'plan-mark', name: 'Plan needs another look', kind: 'card',
    context: variant((value) => { value.planValidity = 'needs_reassessment';
      value.planInvalidatedBy = 'Filament changed to Generic ABS by hand';
      value.plates[0].estimateStatus = 'recomputing'; }), props: { compute: true, checkPlan: true } },
  { id: 'material', name: 'Printer reports another material', kind: 'card',
    context: variant((value) => { value.printerReview = { observed: { observedAt: saved, connection: 'online' },
      mismatches: [{ what: 'filament', configured: 'ABS', observed: 'PLA', source: 'device' }] }; }), props: {} },
  { id: 'no-agent', name: 'No agent configured', kind: 'card', context: untouched, props: { agentAvailable: false } },
  { id: 'sent', name: 'Sliced and sent', kind: 'card', context: setupWorkspace(),
    props: { historical: true, savedAt: saved, sent: { at: saved, printer: 'MyKlipper' } } },
  { id: 'with-currency', name: 'With currency', kind: 'card',
    context: variant((value) => { value.currency = 'GBP'; value.plates[0].estimate!.materialCost = 1.12; }), props: {} },
  { id: 'missing', name: 'Nothing read', kind: 'card',
    context: variant((value) => { value.setupIntent = ''; value.planHeadline = ''; value.appliedSetup = null;
      value.setupIdentity = null; value.printerReview = null; value.presetDeltas = []; value.printer.process = '';
      value.plates[0].estimate = null; }), props: {} },
  { id: 'dense', name: 'Card · long and dense', kind: 'card', context: dense, props: {} },
  { id: 'no-plan', name: 'Card · purpose, no plan yet', kind: 'card',
    context: variant((value) => { value.planHeadline = ''; }), props: {} },
  { id: 'plan-only', name: 'Card · plan before a purpose', kind: 'card',
    context: variant((value) => { value.setupIntent = ''; }), props: {} },

  { id: 'page-untouched', name: 'All settings · nothing changed', kind: 'page', context: untouched, props: {} },
  { id: 'page-objects', name: 'All settings · one object', kind: 'page', context: threeObjects, props: {} },
  { id: 'page-dense', name: 'All settings · six slots', kind: 'page', context: dense, props: {} },
];

// The host's table for a panel case: the changed settings the context
// counts, in the company of a few unchanged ones so every group reads as a
// table, each filament slot with a colour and the rows the Filament group
// draws, and the shown plate with a plate type of its own.
export function visualCatalog(context: WorkspaceContext): SettingsCatalogPayload {
  const base = catalogFromContext(context);
  const row = (key: string, label: string, page: string, group: string, display: string): CatalogSettingInfo =>
    ({ key, label, page, group, value: display, display });
  const stock: CatalogSettingInfo[] = [
    row('fixture_layer', 'Layer height', 'Quality', 'Layer height', '0.2 mm'),
    row('fixture_seam', 'Seam position', 'Quality', 'Seam', 'Aligned'),
    row('fixture_walls', 'Wall loops', 'Strength', 'Walls', '2'),
    row('fixture_top', 'Top shell layers', 'Strength', 'Top/bottom shells', '5'),
    row('fixture_infill', 'Sparse infill density', 'Strength', 'Infill', '15%'),
    row('fixture_outer', 'Outer wall speed', 'Speed', 'Other layers speed', '200 mm/s'),
    row('fixture_support', 'Enable support', 'Support', 'Support', 'Off'),
    row('fixture_skirt', 'Skirt loops', 'Others', 'Skirt', '1'),
  ];
  const filamentRows = (slot: number): CatalogSettingInfo[] => [
    row('fixture_nozzle', 'Nozzle temperature', 'Filament', 'Temperatures', slot === 1 ? '255 °C' : '220 °C'),
    row('fixture_bed', 'Bed temperature', 'Filament', 'Temperatures', slot === 1 ? '70 °C' : '65 °C'),
    row('fixture_fan', 'Fan speed', 'Cooling', 'Cooling', slot === 1 ? '40%' : '100%'),
    { ...row('filament_fixture_retraction', 'Retraction length', 'Setting Overrides', 'Retraction', slot === 3 ? '1.2 mm' : 'Not set'),
      ...(slot === 3 ? {} : { inherited: true, inheritedDisplay: '0.8 mm' }) },
    ...(slot === 1 ? base.filaments[0]?.settings ?? [] : []),
  ];
  const colours = ['#D96A1F', '#2E6FB6', '#8BC34A', '#F2C94C', '#9B51E0', '#333333'];
  // The project's value of every setting an object sets itself, so the
  // object's view can say what it overrides.
  const overridden: CatalogSettingInfo[] = base.objects.flatMap((object) => object.overrides)
    .filter((local, index, all) => local.key && local.key !== 'extruder' && all.findIndex((other) => other.key === local.key) === index)
    .map((local) => row(local.key, local.label ?? local.key, 'Strength', 'Walls', '2'));
  return {
    ...base,
    process: { ...base.process, settings: [...stock, ...overridden, ...base.process.settings] },
    printer: { ...base.printer, settings: [row('fixture_retract', 'Retraction length', 'Extruder', 'Retraction', '0.8 mm'),
      ...base.printer.settings] },
    filaments: base.filaments.map((filament) => ({ ...filament, colour: colours[filament.slot - 1] ?? '',
      settings: filamentRows(filament.slot) })),
    plates: context.plates.map((plate) => ({ id: plate.id, name: plate.name, active: plate.active,
      bedType: plate.active ? 'Textured PEI Plate' : '', ownBedType: plate.active, bedTempKey: 'fixture_bed', bedTempInitialKey: '',
      own: plate.active ? [row('fixture_plate_type', 'Bed type', 'Plate', '', 'Textured PEI Plate')] : [] })),
  };
}

// The production component for one case. The handlers exist exactly when the
// case says the host could carry the action out. A panel comes with the
// header it has in the pane.
export function renderVisualCase(entry: SetupVisualCase, handlers: {
  onViewSetup?: () => void; onCompute?: () => void; onCheckPlan?: () => void; onBack?: () => void;
  onCollapse?: () => void } = {}) {
  const { compute, checkPlan, ...props } = entry.props;
  // The one-object case is drawn on the object the 3D view has.
  if (entry.kind === 'page')
    return <><AllSettingsHeader onBack={handlers.onBack ?? (() => {})} onCollapse={handlers.onCollapse ?? (() => {})} />
      <AllSettings context={entry.context} catalog={visualCatalog(entry.context)}
        initialScope={entry.id === 'page-objects' ? { kind: 'object', id: '0' } : undefined} /></>;
  return <SetupCard context={entry.context} {...props}
    onViewSetup={handlers.onViewSetup ?? (() => {})}
    onCompute={compute ? handlers.onCompute ?? (() => {}) : undefined}
    onCheckPlan={checkPlan ? handlers.onCheckPlan ?? (() => {}) : undefined} />;
}
