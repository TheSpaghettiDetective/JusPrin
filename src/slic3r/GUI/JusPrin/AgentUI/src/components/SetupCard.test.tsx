// The setup card's rules, one behavior at a time. Every assertion is about
// what a state means and which facts it may claim; the frames themselves are
// rendered through SetupCard.preview and SetupCard.acceptance.

import { describe, expect, it, vi } from 'vitest';
import { cleanup, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { ChangeInfo, PresetDeltaInfo, WorkspaceContext } from '../bridge/protocol';
import { SetupCard, SetupCardProps, deterministicAttention, estimateView, formatCost, formatGrams, formatPrintTime,
  handEdits, overrideObjectCount, presetsLine, recentAgentChange } from './SetupCard';

function context(): WorkspaceContext {
  return {
    sessionId: 'project-1', revision: 4, projectName: 'Bracket', projectDirty: false,
    printer: { preset: 'Bambu A1', filament: 'PETG Basic @BBL A1', process: '0.20 mm Standard' },
    plates: [{ id: 'plate-1', name: 'Plate 1', active: true, sliced: true,
      estimate: { printTimeSeconds: 8280, materialGrams: 18, materialCost: null, timeAvailable: true, materialAvailable: true },
      estimateStatus: 'current', invalidatedBy: '', objects: [] }],
    selection: { status: 'none', objectIds: [] },
    history: { canUndo: true, canRedo: false }, presetDeltas: [], currency: '', setupIntent: 'Quick fit check',
    planHeadline: '', printerReview: null,
    appliedSetup: { version: 1, plateId: 'plate-1', printableObjects: 1, spiralMode: false,
      variableLayerHeight: false, objects: ['Bracket'], localOverrides: [] },
    setupIdentity: { printer: 'Bambu A1', nozzles: [0.4], plateType: 'Textured PEI',
      filaments: [{ preset: 'PETG Basic @BBL A1', material: 'PETG' }] },
  };
}

const delta = (key: string, label: string, display: string, extra: Partial<PresetDeltaInfo> = {}): PresetDeltaInfo => ({
  key, label, display, preset: '', value: '', presetType: 'process', presetName: '0.20 mm Standard',
  page: 'Strength', group: 'Walls', ...extra });

const NOW = Date.parse('2026-10-05T20:00:30');
const change = (seq: number, patch: Partial<ChangeInfo>): ChangeInfo => ({ seq, createdAt: '2026-10-05T20:00:00',
  kind: 'setting', actor: 'agent', label: 'Wall loops', from: '2', to: '4', preset: 'Standard',
  conversationId: 'chat', afterId: 'm-1', ...patch });

function show(value: WorkspaceContext, props: Partial<SetupCardProps> = {}) {
  render(<SetupCard context={value} onViewSetup={() => {}} {...props} />);
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

describe('presets and differences', () => {
  it('names the process and the first filament, each without its printer suffix', () => {
    const value = context();
    value.printer.process = '0.20mm Standard @BBL A1';
    expect(presetsLine(value, 'Quick fit check')).toBe('0.20mm Standard · PETG Basic');
    expect(show(value)).toHaveTextContent('0.20mm Standard · PETG Basic');
  });

  it('keeps long preset names on one line, whole in the tooltip', () => {
    const value = context();
    value.printer.process = 'Draft quality profile for the large enclosure panels with thick walls';
    const line = show(value).querySelector('.current-setup-line--single')!;
    expect(line).toHaveTextContent('Draft quality profile for the large enclosure panels with thick walls · PETG Basic');
    expect(line).toHaveAttribute('title', line.textContent);
  });

  it('counts the filament slots it does not name', () => {
    const value = context();
    value.setupIdentity!.filaments.push({ preset: 'PLA Basic @BBL A1', material: 'PLA' }, { preset: 'PLA Basic @BBL A1', material: 'PLA' });
    expect(presetsLine(value, 'Quick fit check')).toBe('0.20 mm Standard · PETG Basic +2');
  });

  it('shows a name that follows no convention whole', () => {
    const value = context();
    value.printer.process = 'My draft profile';
    expect(presetsLine(value, 'Quick fit check')).toBe('My draft profile · PETG Basic');
  });

  it('does not say the process twice when the process is the title', () => {
    const value = context();
    value.setupIntent = '';
    const card = show(value);
    expect(within(card).getByRole('heading')).toHaveTextContent('0.20 mm Standard');
    expect(presetsLine(value, '0.20 mm Standard')).toBe('PETG Basic');
    expect(card.textContent!.match(/0\.20 mm Standard/g)).toHaveLength(1);
  });

  it('has no presets line for a printer with no process preset', () => {
    const value = context();
    value.printer.process = '';
    expect(presetsLine(value, 'Quick fit check')).toBe('');
  });

  it('shows the plan headline under the presets and none of the differences', () => {
    const value = context();
    value.planHeadline = 'Four walls and gyroid infill for strength';
    value.presetDeltas = [delta('a', 'Wall loops', '2 → 4'), delta('b', 'Brim type', 'Auto → Outer brim only'),
      delta('c', 'G-code thumbnails', '48x48/PNG,300x300/PNG → 48x48/PNG, 300x300/PNG')];
    const card = show(value);
    const headline = card.querySelector('.current-setup-strategy')!;
    expect(headline).toHaveTextContent('Four walls and gyroid infill for strength');
    expect(card.querySelector('.current-setup-line--single')!.compareDocumentPosition(headline) &
      Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(card.querySelector('.current-setup-row--change')).toBeNull();
    expect(card).not.toHaveTextContent(/Wall loops|thumbnails|→|more change|No changes from presets/);
  });

  it('has no strategy line at all without a headline', () => {
    for (const headline of ['', '   ']) {
      const value = context();
      value.planHeadline = headline;
      value.presetDeltas = [delta('a', 'Wall loops', '2 → 4')];
      const card = show(value);
      expect(card.querySelector('.current-setup-strategy')).toBeNull();
      expect(card).not.toHaveTextContent(/Wall loops|No changes from presets|Quick fit check.*Quick fit check/);
      cleanup();
    }
  });

  it('wraps a long or localized headline whole instead of cutting it', () => {
    const value = context();
    value.planHeadline = 'Vier Wände, Gyroid-Füllung mit 25 % und ein breiter Rand gegen das Ablösen der Ecken auf dem Druckbett';
    const headline = show(value).querySelector('.current-setup-strategy')!;
    expect(headline).toHaveTextContent(value.planHeadline);
    expect(headline).not.toHaveClass('current-setup-line--single');
    expect(headline).not.toHaveAttribute('title');
  });

  it('counts the objects that carry overrides, not the overrides', () => {
    const value = context();
    value.appliedSetup!.objects = ['a', 'b', 'c'];
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'a', kind: 'object', key: 'wall_loops', value: '5' },
      { object: 0, target: 'a / tab', kind: 'modifier', key: 'wall_loops', value: '7' },
      { object: 2, target: 'c', kind: 'support painting', key: '', value: '' }];
    expect(overrideObjectCount(value)).toBe(2);
    expect(show(value).querySelector('.current-setup-note')).toHaveTextContent(/^2 objects with overrides$/);
    cleanup();
    value.appliedSetup!.localOverrides.length = 1;
    value.planHeadline = 'Four walls and gyroid infill for strength';
    expect(show(value).querySelector('.current-setup-note')).toHaveTextContent(/^1 object with overrides$/);
    cleanup();
    value.appliedSetup!.localOverrides.length = 0;
    expect(show(value).querySelector('.current-setup-note')).toBeNull();
  });

  it('opens the All settings panel from one link and never grows itself', async () => {
    const onViewSetup = vi.fn();
    const value = context();
    value.presetDeltas = [delta('a', 'A', '1 → 2')];
    const card = show(value, { onViewSetup });
    expect(within(card).queryByRole('button', { name: /details/i })).toBeNull();
    await userEvent.click(within(card).getByRole('button', { name: 'All settings' }));
    expect(onViewSetup).toHaveBeenCalledOnce();
    expect(card.querySelectorAll('.current-setup-row--change')).toHaveLength(0);
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
    // Standing down: the card's header is no longer in the live colour.
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

  it('marks a completed change as new without listing it', () => {
    const value = context();
    value.presetDeltas = [delta('wall_loops', 'Wall loops', '2 → 4')];
    const card = show(value, { changes: [change(1, { key: 'wall_loops' })], now: NOW });
    expect(card).toHaveTextContent('Updated');
    // The thread's change rows name the setting under the agent's turn.
    expect(card).not.toHaveTextContent('Wall loops');
    // With no step and no estimate to compare, the minute adds no block.
    expect(card.querySelectorAll('.current-setup-divider')).toHaveLength(1);
  });

  it('does not grow with the number of settings the agent just changed', () => {
    const value = context();
    const keys = Array.from({ length: 19 }, (_, index) => `setting_${index}`);
    value.presetDeltas = keys.map((key) => delta(key, `Setting ${key}`, '1 → 2'));
    const card = show(value, { changes: keys.map((key, index) => change(index + 1, { key, label: `Setting ${key}` })), now: NOW });
    expect(card).toHaveTextContent('Updated');
    expect(card).not.toHaveTextContent(/Setting setting_|more change/);
  });

  it('names a project step the agent just made, which no row shows', () => {
    const card = show(context(), { changes: [change(1, {}), change(2, { kind: 'step', label: 'Add modifier: Corner tab' })], now: NOW });
    expect(card).toHaveTextContent('Add modifier: Corner tab');
    expect(card).not.toHaveTextContent('Wall loops 2');
    cleanup();
    // Orca names some steps with nothing at all.
    expect(show(context(), { changes: [change(1, { kind: 'step', label: '' })], now: NOW })).toHaveTextContent('Object settings changed');
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

describe('the strategy headline in each state', () => {
  const HEADLINE = 'Four walls and gyroid infill for strength';
  const withHeadline = () => ({ ...context(), planHeadline: HEADLINE });
  const strategy = (card: HTMLElement) => card.querySelector('.current-setup-strategy')?.textContent;
  const edits = [change(1, { createdAt: '2026-10-05T19:00:00' }),
    change(2, { actor: 'person', label: 'Wall loops', from: '4', to: '3' })];

  it.each([
    ['Agent working', () => show(withHeadline(), { working: true })],
    ['Updated', () => show(withHeadline(), { changes: [change(1, {})], now: NOW })],
    ['Confirmed', () => {
      const value = withHeadline();
      value.plates[0].estimateStatus = 'recomputing';
      return show(value);
    }],
    ['Current', () => show(withHeadline())],
    ['Not sliced', () => {
      const value = withHeadline();
      value.plates[0].estimate = null;
      value.plates[0].sliced = false;
      return show(value);
    }],
    ['Edited by you', () => show(withHeadline(), { changes: edits, now: NOW })],
    ['Out of date', () => {
      const value = withHeadline();
      value.plates[0].estimateStatus = 'stale';
      return show(value);
    }],
    ['Saved', () => show(withHeadline(), { historical: true, savedAt: '2026-10-03T10:42:00' })],
    ['Sliced & sent', () => show(withHeadline(), { historical: true, sent: { at: '2026-10-03T10:42:00', printer: 'A1' } })],
  ])('shows it under %s', (state, render) => {
    const card = render();
    expect(card.querySelector('.current-setup-state')).toHaveTextContent(state);
    expect(strategy(card)).toBe(HEADLINE);
  });

  it('keeps it beside the notice a hand edit raises', () => {
    const card = show(withHeadline(), { changes: edits, now: NOW });
    expect(card).toHaveTextContent('You set Wall loops 3 outside the agent.');
    expect(strategy(card)).toBe(HEADLINE);
  });

  it('keeps it on an out-of-date card that offers a refresh', () => {
    const value = withHeadline();
    value.plates[0].estimateStatus = 'stale';
    const card = show(value, { onCompute: () => {} });
    expect(card).toHaveTextContent('Earlier setup');
    expect(within(card).getByRole('button', { name: 'Refresh setup & recompute' })).toBeInTheDocument();
    expect(strategy(card)).toBe(HEADLINE);
  });

  it('has none on the preset fallback', () => {
    expect(strategy(show(withHeadline(), { agentAvailable: false }))).toBeUndefined();
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
    const { rerender } = render(<SetupCard context={value} onViewSetup={() => {}} />);
    expect(screen.getByTestId('current-setup')).toHaveTextContent('Last reported material differs');
    rerender(<SetupCard context={{ ...value, printerReview: { mismatches: [] } }} onViewSetup={() => {}} />);
    expect(screen.getByTestId('current-setup')).not.toHaveTextContent('differs');
  });

  it('makes no finding out of a question or a time in the intent, or out of the plan', () => {
    const value = context();
    value.setupIntent = 'Could this finish under 1 hour?';
    value.planHeadline = 'Fast draft layers to finish within the hour';
    const card = show(value);
    expect(deterministicAttention(value)).toEqual([]);
    expect(card).toHaveTextContent('Could this finish under 1 hour?');
    expect(card.querySelector('.current-setup-attention')).toBeNull();
    expect(card).not.toHaveTextContent(/exceeds|target|reassess/i);
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

  it('is a plain preset label when no agent is configured', () => {
    const value = context();
    value.printerReview = { observed: { observedAt: '2026-10-05T20:00:00' },
      mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' }] };
    // A step that changed no setting leaves the preset in force.
    const card = show(value, { agentAvailable: false, working: true, changes: [change(1, { kind: 'step', label: 'Rotate' })], now: NOW });
    expect(card).toHaveTextContent('Preset fallback');
    expect(card).toHaveTextContent('No agent');
    expect(within(card).getByRole('heading', { name: '0.20 mm Standard' })).toBeInTheDocument();
    expect(card).toHaveTextContent('No agent configured · using the selected preset.');
    expect(card).not.toHaveTextContent(/Quick fit check|review|optimi|Updated|Agent working/i);
    expect(within(card).queryByRole('button')).toBeNull();
  });

  it('names an external tool that changed settings with no agent configured', () => {
    const value = context();
    value.setupIntent = '';
    value.appliedSetup = null;
    const log = [change(1, { external: true, client: 'Claude Code', label: 'Wall loops', from: '2', to: '3' }),
      change(2, { kind: 'step', external: true, client: 'Claude Code', label: 'Rotate' })];
    const card = show(value, { agentAvailable: false, changes: log, now: NOW });
    expect(card).toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('Edited by Claude Code');
    expect(card).toHaveTextContent('Claude Code set Wall loops 3.');
    expect(card).not.toHaveTextContent(/Preset fallback|using the selected preset|outside the agent|Updated/);
    cleanup();
    const unnamed = show(value, { agentAvailable: false, changes: [change(1, { external: true })], now: NOW });
    expect(unnamed).toHaveTextContent('Edited by an external tool');
    expect(unnamed).toHaveTextContent('An external tool set Wall loops 4.');
  });

  it('names each author of the settings in force with no agent configured', () => {
    const log = [change(1, { external: true, client: 'Codex', key: 'wall_loops', label: 'Wall loops', to: '3' }),
      change(2, { actor: 'person', key: 'sparse_infill_density', label: 'Sparse infill density', to: '30%' }),
      // A setting changed again counts for its latest author only.
      change(3, { actor: 'person', key: 'wall_loops', label: 'Wall loops', to: '5' }),
      change(4, { external: true, client: 'Codex', key: 'brim_width', label: 'Brim width', to: '5 mm' })];
    const card = show(context(), { agentAvailable: false, changes: log, now: NOW });
    expect(card).toHaveTextContent('Edited by Codex');
    expect(card).toHaveTextContent('You and Codex set Wall loops 5, Sparse infill density 30% and 1 more setting.');
    cleanup();
    // A restore puts the preset back: nothing before it counts.
    const restored = [...log, change(5, { actor: 'person', kind: 'restore', label: 'Restored' })];
    expect(show(context(), { agentAvailable: false, changes: restored, now: NOW })).toHaveTextContent('Preset fallback');
  });

  it('uses the preset as the heading when the chat has no intent', () => {
    const value = context();
    value.setupIntent = '   ';
    const card = show(value);
    expect(within(card).getByRole('heading', { name: '0.20 mm Standard' })).toBeInTheDocument();
    expect(card).not.toHaveTextContent('No changes from presets');
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
    value.planHeadline = 'Four walls and gyroid infill for strength';
    const card = show(value);
    expect(card).toHaveTextContent('Preset fallback');
    expect(card).not.toHaveTextContent('Four walls');
    expect(card).toHaveTextContent('Estimate unavailable');
    expect(card).not.toHaveTextContent(/No changes from presets|with overrides|Current|0 g/);
    expect(within(card).queryByRole('button')).toBeNull();
  });

  it('renders nothing without a context', () => {
    const { container } = render(<SetupCard context={null} onViewSetup={() => {}} />);
    expect(container).toBeEmptyDOMElement();
  });
});

// Board 7: a calibration test reopened later. OrcaSlicer kept its steps only
// in memory, so the card reads Out of date and warns before anyone slices.
describe('a reopened calibration test', () => {
  const tower = { startedFrom: 'Backpack hook', filament: 'Gray PETG', printer: 'Prusa MK4', symptom: '', test: 'temperature',
    method: '', start: 230, end: 190, step: 5, generatedAt: '2026-10-21T10:00:00Z', result: '215 °C' };

  it('is out of date and says the steps are not in the file', () => {
    const card = show({ ...context(), calibration: tower });
    expect(within(card).getByText('Out of date')).toBeInTheDocument();
    expect(within(card).getByText('Temperature steps not kept in the file')).toBeInTheDocument();
    cleanup();
  });

  it('says nothing while the plate still holds the steps, or for a test that keeps its own', () => {
    const value = context();
    const holding = { ...value, calibration: tower,
      plates: value.plates.map((plate) => ({ ...plate, calibration: { test: 'temperature', start: 230, end: 190, step: 5 } })) };
    expect(deterministicAttention(holding)).toEqual([]);
    expect(deterministicAttention({ ...value, calibration: { ...tower, test: 'pressure_advance', method: 'pattern' } })).toEqual([]);
    expect(deterministicAttention({ ...value, calibration: { ...tower, test: 'pressure_advance', method: 'tower' } }))
      .toEqual([{ title: 'Pressure advance steps not kept in the file' }]);
    // A calibration project whose test is not made yet has nothing to lose.
    expect(deterministicAttention({ ...value, calibration: { ...tower, test: undefined } })).toEqual([]);
  });
});
