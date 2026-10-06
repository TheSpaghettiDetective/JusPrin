// The setup card's rules, one behavior at a time. Every assertion is about
// what a state means and which facts it may claim; the frames themselves are
// compared with Figma through SetupCard.preview and SetupCard.acceptance.

import { describe, expect, it, vi } from 'vitest';
import { cleanup, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { AppliedSetupInfo, ChangeInfo, WorkspaceContext } from '../bridge/protocol';
import { SetupCard, SetupCardProps, appliedSummary, deterministicAttention, estimateView, formatCost, formatGrams,
  formatPrintTime, handEdits, recentAgentChange } from './SetupCard';

type Coverage = AppliedSetupInfo['settings'][number]['coverage'];
const setting = (key: string, value: string, coverage: Coverage = 'exact') =>
  ({ key, value, base: value, coverage, scopes: [{ object: 0, target: 'Bracket', kind: 'object', value }] });

function context(): WorkspaceContext {
  return {
    sessionId: 'project-1', revision: 4, projectName: 'Bracket', projectDirty: false,
    printer: { preset: 'Bambu A1', filament: 'PETG', process: '0.20 mm Standard' },
    plates: [{ id: 'plate-1', name: 'Plate 1', active: true, sliced: true,
      estimate: { printTimeSeconds: 8280, materialGrams: 18, materialCost: null, timeAvailable: true, materialAvailable: true },
      estimateStatus: 'current', invalidatedBy: '', objects: [] }],
    selection: { status: 'none', objectIds: [] },
    history: { canUndo: true, canRedo: false }, presetDeltas: [], currency: '', setupIntent: 'Quick fit check',
    appliedSetup: { version: 1, plateId: 'plate-1', printableObjects: 1, spiralMode: false,
      variableLayerHeight: false, objects: ['Bracket'], localOverrides: [], settings: [
        setting('layer_height', '0.2'), setting('wall_loops', '4'),
        setting('sparse_infill_density', '30%'), setting('sparse_infill_pattern', 'gyroid'),
        setting('enable_support', '1'), setting('support_type', 'tree(auto)'),
        setting('support_on_build_plate_only', '1'), setting('top_shell_layers', '5'),
        setting('bottom_shell_layers', '4'), setting('top_shell_thickness', '0'),
        setting('bottom_shell_thickness', '0'), setting('brim_type', 'auto_brim'), setting('brim_width', '0'),
      ] },
    setupIdentity: { printer: 'Bambu A1', nozzles: [0.4], plateType: 'Textured PEI',
      filaments: [{ preset: 'PETG Basic', material: 'PETG' }] },
  };
}

const at = (value: WorkspaceContext, key: string) => value.appliedSetup!.settings.find((item) => item.key === key)!;
const NOW = Date.parse('2026-10-05T20:00:30');
const change = (seq: number, patch: Partial<ChangeInfo>): ChangeInfo => ({ seq, createdAt: '2026-10-05T20:00:00',
  kind: 'setting', actor: 'agent', label: 'Wall loops', from: '2', to: '4', preset: 'Standard',
  conversationId: 'chat', afterId: 'm-1', ...patch });

function show(value: WorkspaceContext, props: Partial<SetupCardProps> = {}) {
  render(<SetupCard context={value} expanded={false} onToggle={() => {}} {...props} />);
  return screen.getByTestId('current-setup');
}

describe('formatting', () => {
  it('writes time without seconds and with both units past an hour', () => {
    expect(formatPrintTime(2520)).toBe('~42 min');
    expect(formatPrintTime(8280)).toBe('~2h 18m');
    expect(formatPrintTime(3660)).toBe('~1h 01m');
  });

  it('keeps a decimal only for a mass too small to round', () => {
    expect(formatGrams(18.4)).toBe('18 g');
    expect(formatGrams(4.26)).toBe('4.3 g');
  });

  it('denominates a cost only in a currency it was given', () => {
    expect(formatCost(1.12, 'GBP')).toContain('1.12');
    expect(formatCost(1.12, '')).toBe('cost 1.12');
  });
});

describe('applied-settings summary', () => {
  it('states inherited settings when the process preset is untouched', () => {
    const value = context();
    expect(value.presetDeltas).toEqual([]);
    expect(appliedSummary(value.appliedSetup)).toEqual([
      '0.20 mm layers · 4 walls', '30% gyroid infill · tree supports from build plate']);
  });

  it('is derived from stable keys, so a translated label changes nothing', () => {
    const value = context();
    const before = appliedSummary(value.appliedSetup);
    value.presetDeltas = [{ key: 'wall_loops', label: 'Wandschleifen', preset: '2', value: '4', origin: 'user' }];
    const card = show(value);
    expect(appliedSummary(value.appliedSetup)).toEqual(before);
    expect(card).toHaveTextContent('0.20 mm layers · 4 walls');
    expect(card).not.toHaveTextContent('Wandschleifen');
  });

  it('keeps one field order whatever changed last', () => {
    const value = context();
    value.appliedSetup!.settings.reverse();
    expect(appliedSummary(value.appliedSetup)[0]).toBe('0.20 mm layers · 4 walls');
  });

  it('names no sparse pattern at zero or full density', () => {
    const value = context();
    at(value, 'sparse_infill_density').value = '0%';
    expect(appliedSummary(value.appliedSetup)[1]).toBe('No sparse infill · tree supports from build plate');
    at(value, 'sparse_infill_density').value = '100%';
    expect(appliedSummary(value.appliedSetup)[1]).toContain('100% infill');
    expect(appliedSummary(value.appliedSetup).join(' ')).not.toContain('gyroid');
  });

  it('says spiral vase instead of walls and infill it does not print', () => {
    const value = context();
    value.appliedSetup!.spiralMode = true;
    expect(appliedSummary(value.appliedSetup)).toEqual(['Spiral vase mode']);
    const card = show(value, { expanded: true });
    expect(card).not.toHaveTextContent('4 walls');
    expect(within(card).getByTestId('current-setup-expansion')).toHaveTextContent('ModeSpiral vase');
  });

  it('does not quote the nominal height under a variable-height profile', () => {
    const value = context();
    value.appliedSetup!.variableLayerHeight = true;
    expect(appliedSummary(value.appliedSetup)[0]).toBe('Variable layer height · 4 walls');
    expect(appliedSummary(value.appliedSetup).join(' ')).not.toMatch(/\d\.\d\d mm/);
  });

  it('states the plate default as a default when objects disagree', () => {
    const value = context();
    const setup = value.appliedSetup!;
    setup.printableObjects = 3;
    setup.objects = ['Bracket', 'Cover', 'Spacer'];
    const walls = at(value, 'wall_loops');
    walls.coverage = 'mixed';
    walls.scopes = ['4', '4', '2'].map((wall, object) => ({ object, target: setup.objects![object], kind: 'object', value: wall }));
    const lines = appliedSummary(setup);
    expect(lines[0]).toBe('Plate default: 0.20 mm layers · 4 walls');
    expect(lines[1]).toBe('30% gyroid infill · 1 object with local overrides');
  });

  it('falls back to plain variation when a saved setup has no plate default', () => {
    const value = context();
    const walls = at(value, 'wall_loops');
    walls.coverage = 'mixed';
    delete walls.base;
    expect(appliedSummary(value.appliedSetup)[0]).toBe('0.20 mm layers · walls vary by object');
  });

  it('quotes one modifier by name and its configured value', () => {
    const value = context();
    at(value, 'wall_loops').coverage = 'local';
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'Bracket / Mounting tab', kind: 'modifier', key: 'wall_loops', value: '6' }];
    expect(appliedSummary(value.appliedSetup)[0]).toBe('0.20 mm layers · 4 walls; Mounting tab: 6');
  });

  it('points at the details instead of resolving overlapping modifiers', () => {
    const value = context();
    at(value, 'sparse_infill_density').coverage = 'local';
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'Bracket / Dense region', kind: 'modifier', key: 'sparse_infill_density', value: '50%' },
      { object: 0, target: 'Bracket / Strong tab', kind: 'modifier', key: 'sparse_infill_density', value: '75%' },
    ];
    const lines = appliedSummary(value.appliedSetup).join(' ');
    expect(lines).toContain('Local infill settings — see details');
    expect(lines).not.toContain('30%');
    expect(lines).not.toContain('75%');
    const details = within(show(value, { expanded: true })).getByTestId('current-setup-expansion');
    expect(details).toHaveTextContent('Dense region (modifier)');
    expect(details).toHaveTextContent('Strong tab (modifier)');
  });

  it('never resolves a height range to a part of the print', () => {
    const value = context();
    at(value, 'layer_height').coverage = 'local';
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'Bracket', kind: 'height range', key: 'layer_height', value: '0.12' }];
    expect(appliedSummary(value.appliedSetup)[0]).toBe('Local layer heights — see details · 4 walls');
    expect(within(show(value, { expanded: true })).getByTestId('current-setup-expansion'))
      .toHaveTextContent('Bracket (height range)Layer height 0.12 mm');
  });

  it('describes configured supports without promising what gets generated', () => {
    const value = context();
    at(value, 'enable_support').coverage = 'local';
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'Bracket / Hole', kind: 'support blocker', key: '', value: '' }];
    const card = show(value, { expanded: true });
    expect(card).toHaveTextContent('tree supports from build plate; local support edits');
    expect(card).toHaveTextContent('HoleSupport blocker');
    expect(card).not.toHaveTextContent(/no supports? in|will not be supported|Supports generated/i);
  });

  it('reports a plate with nothing printable instead of leftover values', () => {
    const value = context();
    value.appliedSetup!.printableObjects = 0;
    const card = show(value);
    expect(card).toHaveTextContent('No printable objects on this plate');
    expect(card).not.toHaveTextContent('4 walls');
  });
});

describe('time and material', () => {
  it('shows a current estimate with its plate', () => {
    expect(estimateView(context())).toMatchObject({ status: 'current', heading: '', plate: 'Plate 1',
      metrics: [{ icon: 'clock', text: '~2h 18m' }, { icon: 'package', text: '18 g' }] });
  });

  it('never prints a missing quantity as zero', () => {
    const value = context();
    value.plates[0].estimate = { printTimeSeconds: 8280, materialGrams: 0, materialCost: null,
      timeAvailable: true, materialAvailable: false };
    expect(estimateView(value).metrics).toEqual([{ icon: 'clock', text: '~2h 18m' }]);
    expect(show(value)).not.toHaveTextContent('0 g');
  });

  it('separates a plate that was never sliced from one with no usable estimate', () => {
    const value = context();
    value.plates[0].estimate = null;
    expect(estimateView(value).description).toBe('Estimate unavailable · Plate 1');
    value.plates[0].sliced = false;
    expect(estimateView(value).description).toBe('Not sliced yet · estimates unavailable · Plate 1');
  });

  it('labels stale figures as previous and keeps time and material together', () => {
    const value = context();
    value.plates[0].estimateStatus = 'stale';
    const view = estimateView(value);
    expect(view.heading).toBe('Previous estimates · not current');
    expect(view.metrics.map((metric) => metric.text)).toEqual(['~2h 18m', '18 g']);
  });

  it('shows no old figure as current while the plate recomputes', () => {
    const value = context();
    value.plates[0].estimateStatus = 'recomputing';
    const card = show(value);
    expect(card).toHaveTextContent('Confirmed');
    expect(card).toHaveTextContent('Time & material estimates recomputing…');
    expect(card).toHaveTextContent('Settings confirmed · Plate 1');
    expect(card).toHaveTextContent('0.20 mm layers · 4 walls');
    expect(card).not.toHaveTextContent('~2h 18m');
  });

  it('reads the displayed plate, not whichever plate is slicing', () => {
    const value = context();
    value.plates.push({ id: 'plate-2', name: 'Plate 2', active: false, sliced: true,
      estimate: { printTimeSeconds: 4000, materialGrams: 22, materialCost: null },
      estimateStatus: 'recomputing', invalidatedBy: '', objects: [] });
    expect(estimateView(value)).toMatchObject({ status: 'current', plate: 'Plate 1' });
  });

  it('adds a cost only when the slice supplied one, and leaves no gap otherwise', () => {
    const value = context();
    expect(estimateView(value).cost).toBe('');
    value.currency = 'GBP';
    value.plates[0].estimate!.materialCost = 1.12;
    expect(estimateView(value).cost).toContain('1.12');
    value.plates[0].estimate!.materialCost = 0.001;
    expect(estimateView(value).cost).toBe('');
  });

  it('offers to compute only where the host can, through the caller', async () => {
    const value = context();
    value.plates[0].estimate = null;
    value.plates[0].sliced = false;
    const onCompute = vi.fn();
    const card = show(value, { onCompute });
    await userEvent.click(within(card).getByRole('button', { name: 'Compute estimates' }));
    expect(onCompute).toHaveBeenCalledOnce();
  });

  it('offers refresh for an out-of-date estimate and says what outdated it', async () => {
    const value = context();
    value.plates[0].estimateStatus = 'stale';
    value.plates[0].invalidatedBy = 'Wall loops changed';
    const onCompute = vi.fn();
    const card = show(value, { onCompute });
    expect(card).toHaveTextContent('Earlier setup');
    expect(card).not.toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('Out of date');
    expect(card).toHaveTextContent('Wall loops changed');
    // Standing down: the card no longer carries the live edge.
    expect(card).not.toHaveClass('current-setup--live');
    await userEvent.click(within(card).getByRole('button', { name: 'Refresh setup & recompute' }));
    expect(onCompute).toHaveBeenCalledOnce();
  });
});

describe('activity and authorship', () => {
  it('keeps the confirmed setup and estimate while the agent works', () => {
    const card = show(context(), { working: true, onCompute: () => {} });
    expect(card).toHaveTextContent('Agent working');
    expect(card).toHaveTextContent('Last confirmed estimates');
    expect(card).toHaveTextContent('~2h 18m');
    expect(card).toHaveTextContent('Shown settings remain the last confirmed setup.');
    expect(card).toHaveAttribute('aria-busy', 'true');
  });

  it('finds the change the agent just completed and nothing older', () => {
    const log = [change(1, { afterId: 'm-0', createdAt: '2026-10-05T19:00:00' }), change(2, {}),
      change(3, { label: 'Sparse infill density', from: '5%', to: '30%' })];
    expect(recentAgentChange(log, NOW).map((item) => item.seq)).toEqual([2, 3]);
    expect(recentAgentChange(log, NOW + 60000)).toEqual([]);
    expect(recentAgentChange([...log, change(4, { actor: 'person' })], NOW)).toEqual([]);
  });

  it('shows a bounded delta for a completed change', () => {
    const card = show(context(), { changes: [change(1, {})], now: NOW });
    expect(card).toHaveTextContent('Updated');
    expect(card).toHaveTextContent('Wall loops 2 → 4');
  });

  it('pairs estimates only when both are current figures for the plate', () => {
    const before = { printTimeSeconds: 2520, materialGrams: 18, materialCost: null };
    const value = context();
    value.plates[0].estimate!.materialGrams = 46;
    expect(show(value, { changes: [change(1, {})], now: NOW, estimateBefore: before }))
      .toHaveTextContent('Estimate: ~42 min / 18 g → ~2h 18m / 46 g');
    cleanup();
    value.plates[0].estimateStatus = 'stale';
    expect(show(value, { changes: [change(1, {})], now: NOW, estimateBefore: before })).not.toHaveTextContent('Estimate:');
  });

  it('offers Undo only for a project step Orca can undo', async () => {
    const onUndo = vi.fn();
    // A preset edit is not on Orca's undo stack.
    const preset = show(context(), { changes: [change(1, {})], now: NOW, onUndo });
    expect(within(preset).queryByRole('button', { name: 'Undo' })).toBeNull();
    cleanup();
    const step = [change(1, {}), change(2, { kind: 'step', label: 'Add modifier' })];
    const blocked = context();
    blocked.history.canUndo = false;
    const none = show(blocked, { changes: step, now: NOW, onUndo });
    expect(within(none).queryByRole('button', { name: 'Undo' })).toBeNull();
    cleanup();
    const card = show(context(), { changes: step, now: NOW, onUndo });
    await userEvent.click(within(card).getByRole('button', { name: 'Undo' }));
    expect(onUndo).toHaveBeenCalledWith(2);
  });

  it('attributes a hand edit from the change log and keeps the intent', () => {
    const log = [change(1, { createdAt: '2026-10-05T19:00:00' }),
      change(2, { actor: 'person', label: 'Wall loops', from: '4', to: '3' }),
      change(3, { actor: 'person', label: 'Wall loops', from: '3', to: '5' }),
      change(4, { actor: 'person', label: 'Sparse infill density', from: '15%', to: '30%' })];
    expect(handEdits(log).map((item) => `${item.label} ${item.to}`)).toEqual(['Wall loops 5', 'Sparse infill density 30%']);
    const value = context();
    value.plates[0].estimateStatus = 'stale';
    const card = show(value, { changes: log, now: NOW });
    expect(card).toHaveTextContent('Edited by you');
    expect(card).toHaveTextContent('Quick fit check');
    expect(card).toHaveTextContent('You set Wall loops 5 and Sparse infill density 30% outside the agent.');
    expect(card).toHaveTextContent('Estimate unavailable for these edits · Plate 1');
    expect(card).not.toHaveTextContent('~2h 18m');
  });

  it('does not credit the person with settings a restore put back', () => {
    const log = [change(1, { createdAt: '2026-10-05T19:00:00' }),
      change(2, { actor: 'person', label: 'Sparse infill density', from: '15%', to: '5%' }),
      change(3, { actor: 'person', kind: 'restore', label: 'Restored this chat’s saved project setup' })];
    expect(handEdits(log)).toEqual([]);
    expect(show(context(), { changes: log, now: NOW })).not.toHaveTextContent('Edited by you');
    cleanup();
    // An edit made after the restore is the person's again.
    const later = [...log, change(4, { actor: 'person', label: 'Wall loops', from: '4', to: '3' })];
    expect(handEdits(later).map((item) => item.label)).toEqual(['Wall loops']);
    expect(show(context(), { changes: later, now: NOW })).toHaveTextContent('Edited by you');
  });

  it('says a plate was never sliced even after a hand edit', () => {
    const value = context();
    value.plates[0].estimate = null;
    value.plates[0].sliced = false;
    const card = show(value, { changes: [change(1, { createdAt: '2026-10-05T19:00:00' }),
      change(2, { actor: 'person', label: 'Wall loops', from: '4', to: '3' })], now: NOW, onCompute: () => {} });
    expect(card).toHaveTextContent('Edited by you');
    expect(card).toHaveTextContent('Not sliced yet · estimates unavailable · Plate 1');
    expect(card).not.toHaveTextContent('for these edits');
    expect(within(card).getByRole('button', { name: 'Compute estimates' })).toBeInTheDocument();
  });

  it('stays neutral when nothing says who changed the settings', () => {
    const card = show(context());
    expect(card).toHaveTextContent('Current');
    expect(card).not.toHaveTextContent('Edited by you');
    expect(card).not.toHaveTextContent('Updated');
  });

  it('does not call a setup the agent never touched edited', () => {
    const value = context();
    value.setupIntent = '';
    const card = show(value, { changes: [change(1, { actor: 'person' })], now: NOW });
    expect(card).not.toHaveTextContent('Edited by you');
  });
});

describe('needs attention', () => {
  it('shows nothing, and no all-clear, when no check produced a finding', () => {
    const card = show(context());
    expect(deterministicAttention(context())).toEqual([]);
    expect(card).not.toHaveTextContent(/safe to print|no issues|all clear/i);
  });

  it('reports a material mismatch as what the printer last reported, with when', async () => {
    const value = context();
    value.printerReview = { observed: { connection: 'offline', observedAt: '2026-10-05T20:00:00' },
      mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA, ABS', source: 'device' }] };
    const card = show(value);
    expect(card).toHaveTextContent('Last reported material differs');
    expect(card).toHaveTextContent('Setup uses PETG. Printer last reported PLA, ABS.');
    expect(card).not.toHaveTextContent(/slot|is loaded/i);
    await userEvent.click(within(card).getByRole('button', { name: 'Review material' }));
    expect(card).toHaveTextContent('Setup: PETG');
    expect(card).toHaveTextContent('Printer last reported: PLA, ABS');
    expect(card).toHaveTextContent('Reported Oct 5, 20:00');
    expect(card).toHaveTextContent('Printer is offline now');
  });

  it('does not act on a device mismatch nobody timed', () => {
    const value = context();
    value.printerReview = { mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' }] };
    expect(deterministicAttention(value)).toEqual([]);
  });

  it('orders several findings and keeps every one inspectable', async () => {
    const value = context();
    value.printerReview = { observed: { observedAt: '2026-10-05T20:00:00' }, mismatches: [
      { what: 'model', configured: 'A1', observed: 'P1', source: 'device' },
      { what: 'plate', configured: 'Textured PEI', observed: 'Smooth PEI', source: 'user_confirmed' },
      { what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' },
    ] };
    expect(deterministicAttention(value).map((finding) => finding.title)).toEqual([
      'Last reported material differs', 'Last reported printer differs', 'Confirmed build plate differs']);
    const card = show(value);
    await userEvent.click(within(card).getByRole('button', { name: 'Review 3 findings' }));
    expect(card).toHaveTextContent('Printer last reported: P1');
    expect(card).toHaveTextContent('You confirmed: Smooth PEI');
  });

  it('clears with the mismatch, without a chat turn', () => {
    const value = context();
    value.printerReview = { observed: { observedAt: '2026-10-05T20:00:00' },
      mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' }] };
    const { rerender } = render(<SetupCard context={value} expanded={false} onToggle={() => {}} />);
    expect(screen.getByTestId('current-setup')).toHaveTextContent('Last reported material differs');
    rerender(<SetupCard context={{ ...value, printerReview: { mismatches: [] } }} expanded={false} onToggle={() => {}} />);
    expect(screen.getByTestId('current-setup')).not.toHaveTextContent('differs');
  });

  it('makes no finding out of a question, a plan, or a time in the intent', () => {
    const value = context();
    value.setupIntent = 'Could this finish under 1 hour?';
    value.planValidity = 'needs_reassessment';
    value.planInvalidatedBy = 'Is PETG right for this?';
    const card = show(value);
    expect(deterministicAttention(value)).toEqual([]);
    expect(card).toHaveTextContent('Could this finish under 1 hour?');
    expect(card).not.toHaveTextContent(/within|exceeds|target|reassess|PETG right/i);
  });
});

describe('expanded details', () => {
  it('lists every configured nozzle and filament, not the first slot', () => {
    const value = context();
    value.setupIdentity!.nozzles = [0.4, 0.6];
    value.setupIdentity!.filaments.push({ preset: 'PLA Matte', material: 'PLA' });
    const details = within(show(value, { expanded: true })).getByTestId('current-setup-expansion');
    expect(details).toHaveTextContent('Nozzles0.4 mm, 0.6 mm');
    expect(details).toHaveTextContent('Filament 1PETG Basic');
    expect(details).toHaveTextContent('Filament 2PLA Matte');
  });

  it('gives off, auto, mixed and unknown each their own words', () => {
    const value = context();
    at(value, 'enable_support').value = '0';
    at(value, 'top_shell_layers').coverage = 'unavailable';
    at(value, 'bottom_shell_layers').coverage = 'unavailable';
    const details = within(show(value, { expanded: true })).getByTestId('current-setup-expansion');
    expect(details).toHaveTextContent('SupportsOff');
    expect(details).toHaveTextContent('BrimAuto');
    expect(details).toHaveTextContent('Top / bottomUnavailable');
  });

  it('adds a minimum shell thickness when one constrains the count', () => {
    const value = context();
    at(value, 'top_shell_thickness').value = '1';
    at(value, 'bottom_shell_thickness').value = '0.8';
    expect(within(show(value, { expanded: true })).getByTestId('current-setup-expansion'))
      .toHaveTextContent('Top / bottom5 / 4 layers · min 1 / 0.8 mm');
  });

  it('says there are no local overrides only for a setup it actually read', () => {
    const read = within(show(context(), { expanded: true })).getByTestId('current-setup-expansion');
    expect(read).toHaveTextContent('No local overrides found');
  });

  it('counts a local setting it has no name for instead of describing it', () => {
    const value = context();
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'Bracket', kind: 'object', key: 'ironing_type', value: 'top' }];
    const details = within(show(value, { expanded: true })).getByTestId('current-setup-expansion');
    expect(details).toHaveTextContent('1 other local setting');
    expect(details).not.toHaveTextContent('No local overrides found');
    expect(details).not.toHaveTextContent('ironing');
  });

  it('names a filament assignment without claiming a physical slot', () => {
    const value = context();
    value.setupIdentity!.filaments.push({ preset: 'PLA Matte', material: 'PLA' });
    value.appliedSetup!.localOverrides = [{ object: 0, target: 'Bracket', kind: 'object', key: 'extruder', value: '2' }];
    expect(within(show(value, { expanded: true })).getByTestId('current-setup-expansion'))
      .toHaveTextContent('Object overrideFilament 2 · PLA Matte');
  });

  it('keeps preset differences apart from applied settings and counts its own rows', async () => {
    const value = context();
    value.presetDeltas = [
      { key: 'top_shell_layers', label: 'Top shell layers', preset: '4', value: '5', origin: 'agent' },
      { key: 'bottom_shell_layers', label: 'Bottom shell layers', preset: '3', value: '4', origin: 'agent' },
      { key: 'wall_loops', label: 'Wall loops', preset: '2', value: '4', origin: 'user' },
    ];
    const card = show(value, { expanded: true });
    expect(within(card).queryByTestId('setup-preset-changes')).toBeNull();
    await userEvent.click(within(card).getByRole('button', { name: '3 changes from preset' }));
    const rows = within(within(card).getByTestId('setup-preset-changes')).getAllByRole('term');
    expect(rows).toHaveLength(3);
    expect(within(card).getByTestId('setup-preset-changes')).toHaveTextContent('Wall loops2 → 4');
  });

  it('has no comparison to open when the preset is untouched', () => {
    const card = show(context(), { expanded: true });
    expect(within(card).queryByRole('button', { name: /from preset/ })).toBeNull();
    expect(card).toHaveTextContent('Base preset0.20 mm Standard');
  });

  it('opens from the keyboard through one labelled control', async () => {
    const onToggle = vi.fn();
    const card = show(context(), { onToggle });
    const control = within(card).getByRole('button', { name: 'Setup details' });
    expect(control).toHaveAttribute('aria-expanded', 'false');
    control.focus();
    await userEvent.keyboard('{Enter}');
    await userEvent.keyboard(' ');
    expect(onToggle).toHaveBeenCalledTimes(2);
  });
});

describe('saved and fallback cards', () => {
  it('uses the same estimate row for a saved card and offers it no live action', () => {
    const value = context();
    value.plates[0].estimateStatus = 'stale';
    const card = show(value, { historical: true, savedAt: '2026-10-03T10:42:00', onCompute: () => {},
      changes: [change(1, {})], now: NOW, working: true });
    expect(card).toHaveAccessibleName('Earlier setup — saved with this conversation');
    expect(card).toHaveTextContent('Earlier setup');
    expect(card).toHaveTextContent('Out of date');
    expect(card).toHaveTextContent('Previous estimates · not current');
    expect(card).toHaveTextContent('These estimates were already out of date when saved.');
    expect(card).not.toHaveTextContent('current project.Refresh');
    expect(card).toHaveTextContent('~2h 18m');
    expect(card).toHaveTextContent('Saved with this chat. The canvas shows your current project.');
    // The saved card is not the live one: no activity, no change, no action.
    expect(card).not.toHaveTextContent(/Agent working|Agent is applying|Updated|Wall loops 2/);
    expect(card).not.toHaveAttribute('aria-busy');
    expect(within(card).queryByRole('button', { name: /recompute|Compute|Undo/ })).toBeNull();
  });

  it('shows when a saved card was saved', () => {
    expect(show(context(), { historical: true, savedAt: '2026-10-03T10:42:00' })).toHaveTextContent('Saved Oct 3, 10:42');
  });

  it('keeps a saved pre-slice estimate apart from measured results', () => {
    const card = show(context(), { historical: true, sent: { at: '2026-10-03T10:42:00', printer: 'Bambu A1' } });
    expect(card).toHaveTextContent('Saved sliced snapshot');
    expect(card).toHaveTextContent('Sliced & sent');
    expect(card).toHaveTextContent('Saved pre-slice estimates');
    expect(card).toHaveTextContent('Actual time & material unavailable.');
    expect(card).toHaveTextContent('Oct 3, 10:42 · sent to Bambu A1');
  });

  it('fills a summary saved before the read model with nothing from today', () => {
    const value = context();
    delete value.appliedSetup;
    delete value.setupIdentity;
    value.presetDeltas = [{ key: 'sparse_infill_density', label: 'Sparse infill density', preset: '15%', value: '5%', origin: 'agent' }];
    const card = show(value, { historical: true, expanded: true });
    expect(card).toHaveTextContent('Saved preset differences: Sparse infill density 5%');
    expect(card).not.toHaveTextContent('4 walls');
    const details = within(card).getByTestId('current-setup-expansion');
    expect(details).toHaveTextContent('Printer and material details unavailable');
    expect(details).toHaveTextContent('Applied setting details are unavailable for this setup.');
    expect(details).toHaveTextContent('Local override details unavailable');
    expect(details).not.toHaveTextContent('No local overrides found');
  });

  it('is a plain preset label when no agent is configured', () => {
    const value = context();
    value.printerReview = { observed: { observedAt: '2026-10-05T20:00:00' },
      mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' }] };
    const card = show(value, { agentAvailable: false, working: true, changes: [change(1, {})], now: NOW });
    expect(card).toHaveTextContent('Preset fallback');
    expect(card).toHaveTextContent('No agent');
    expect(within(card).getByRole('heading', { name: '0.20 mm Standard' })).toBeInTheDocument();
    expect(card).toHaveTextContent('No agent configured · using the selected preset.');
    expect(card).not.toHaveTextContent(/Quick fit check|review|optimi|Updated|Agent working/i);
    expect(within(card).queryByRole('button')).toBeNull();
  });

  it('uses the preset as the heading when the chat has no intent', () => {
    const value = context();
    value.setupIntent = '   ';
    const card = show(value);
    expect(within(card).getByRole('heading', { name: '0.20 mm Standard' })).toBeInTheDocument();
    expect(card).toHaveTextContent('0.20 mm layers · 4 walls');
  });

  it('renders no empty heading when there is neither intent nor preset', () => {
    const value = context();
    value.setupIntent = '';
    value.printer.process = '';
    expect(within(show(value)).queryByRole('heading')).toBeNull();
  });

  it('truncates a long intent on one line and keeps the whole of it reachable', () => {
    const value = context();
    value.setupIntent = 'Durable workshop bracket with clean mounting holes that stay strong under repeated use';
    const heading = within(show(value)).getByRole('heading');
    expect(heading).toHaveTextContent(value.setupIntent);
    expect(heading).toHaveAttribute('title', value.setupIntent);
  });

  it('makes no claim at all when nothing was read', () => {
    const value = context();
    value.setupIntent = '';
    value.appliedSetup = null;
    value.setupIdentity = null;
    value.plates[0].estimate = null;
    const card = show(value);
    expect(card).toHaveTextContent('Preset fallback');
    expect(card).toHaveTextContent('Estimate unavailable');
    expect(card).not.toHaveTextContent(/No local overrides|Current|walls|0 g/);
  });

  it('renders nothing without a context', () => {
    const { container } = render(<SetupCard context={null} expanded={false} onToggle={() => {}} />);
    expect(container).toBeEmptyDOMElement();
  });
});
