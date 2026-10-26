// The setup card's rules, one behavior at a time. Every assertion is about
// what a state means and which facts it may claim; the frames themselves are
// rendered through SetupCard.preview and SetupCard.acceptance.

import { describe, expect, it, vi } from 'vitest';
import { cleanup, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PresetDeltaInfo, WorkspaceContext } from '../bridge/protocol';
import { SetupCard, SetupCardProps, changedSettingCount, estimateView, formatCost, formatGrams, formatPrintTime,
  planNote, presetsLine, presetsNote } from './SetupCard';

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

const HEADLINE = 'Four walls and gyroid infill for strength';
const REASON = 'Import Object: Print_Bed_Level_Test.stl';

function show(value: WorkspaceContext, props: Partial<SetupCardProps> = {}) {
  render(<SetupCard context={value} onViewSetup={() => {}} {...props} />);
  return screen.getByTestId('current-setup');
}

const annotations = (card: HTMLElement) =>
  [...card.querySelectorAll('.current-setup-annotation-text')].map((note) => note.textContent);

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

describe('above the rule: the words someone wrote', () => {
  it('shows the purpose, then the plan, then the rule', () => {
    const value = context();
    value.planHeadline = HEADLINE;
    const card = show(value);
    const purpose = within(card).getByRole('heading', { name: 'Quick fit check' });
    const plan = card.querySelector('.current-setup-strategy')!;
    const rule = card.querySelector('.current-setup-divider')!;
    expect(plan).toHaveTextContent(HEADLINE);
    expect(purpose.compareDocumentPosition(plan) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(plan.compareDocumentPosition(rule) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(card.querySelectorAll('.current-setup-divider')).toHaveLength(1);
  });

  it('is absent, rule and all, when nobody has written anything, and nothing stands in for it', () => {
    for (const blank of ['', '   ']) {
      const value = context();
      value.setupIntent = blank;
      value.planHeadline = blank;
      const card = show(value);
      expect(within(card).queryByRole('heading')).toBeNull();
      expect(card.querySelector('.current-setup-strategy')).toBeNull();
      expect(card.querySelector('.current-setup-divider')).toBeNull();
      // Not the project's name, and the process only once, on its fact line.
      expect(card).not.toHaveTextContent('Bracket');
      expect(card.textContent!.match(/0\.20 mm Standard/g)).toHaveLength(1);
      cleanup();
    }
  });

  it('shows a plan recorded before any purpose', () => {
    const value = context();
    value.setupIntent = '';
    value.planHeadline = HEADLINE;
    const card = show(value);
    expect(within(card).queryByRole('heading')).toBeNull();
    expect(card.querySelector('.current-setup-strategy')).toHaveTextContent(HEADLINE);
    expect(card.querySelector('.current-setup-divider')).not.toBeNull();
  });

  it('shows a purpose with no plan yet', () => {
    const card = show(context());
    expect(within(card).getByRole('heading')).toHaveTextContent('Quick fit check');
    expect(card.querySelector('.current-setup-strategy')).toBeNull();
  });

  it('wraps the purpose instead of cutting it on one line, whole in the tooltip', () => {
    const value = context();
    value.setupIntent = 'Kid’s Halloween arrow headband that flexes without snapping';
    const heading = within(show(value)).getByRole('heading');
    expect(heading).toHaveTextContent(value.setupIntent);
    expect(heading).toHaveAttribute('title', value.setupIntent);
    expect(heading).toHaveClass('current-setup-title');
  });

  it('wraps a long or localized headline whole instead of cutting it', () => {
    const value = context();
    value.planHeadline = 'Vier Wände, Gyroid-Füllung mit 25 % und ein breiter Rand gegen das Ablösen der Ecken auf dem Druckbett';
    const headline = show(value).querySelector('.current-setup-strategy')!;
    expect(headline).toHaveTextContent(value.planHeadline);
    expect(headline).not.toHaveAttribute('title');
  });
});

describe('the plan\'s mark', () => {
  const marked = () => ({ ...context(), planHeadline: HEADLINE, planValidity: 'needs_reassessment' as const,
    planInvalidatedBy: REASON });

  it('annotates the plan with the edit that put it in doubt, and leaves the purpose alone', () => {
    const card = show(marked());
    expect(annotations(card)).toEqual([`Plan needs another look · ${REASON}`]);
    expect(within(card).getByRole('heading')).toHaveTextContent('Quick fit check');
    const note = card.querySelector('.current-setup-annotation')!;
    expect(card.querySelector('.current-setup-strategy')!.compareDocumentPosition(note) &
      Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(note.compareDocumentPosition(card.querySelector('.current-setup-divider')!) &
      Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(note.querySelector('.jp-icon-triangle-alert')).not.toBeNull();
  });

  it('says only that the plan needs another look when the app gave no reason', () => {
    expect(planNote({ ...marked(), planInvalidatedBy: '' })).toBe('Plan needs another look');
  });

  it('goes when the plan is recorded again, and is never drawn without a plan', () => {
    expect(planNote({ ...marked(), planValidity: 'current' })).toBe('');
    expect(planNote({ ...marked(), planHeadline: '' })).toBe('');
    // An older host sends no validity at all.
    expect(planNote({ ...context(), planHeadline: HEADLINE })).toBe('');
  });

  it('holds the place of a calibration test whose steps the file lost', () => {
    const tower = { startedFrom: 'Backpack hook', filament: 'Gray PETG', printer: 'Prusa MK4', symptom: '', test: 'temperature',
      method: '', start: 230, end: 190, step: 5, generatedAt: '2026-10-21T10:00:00Z' };
    const value = { ...context(), calibration: tower };
    expect(annotations(show(value))).toEqual(['Temperature steps not kept in the file']);
    // One annotation per line: the app's mark first.
    expect(planNote({ ...marked(), calibration: tower })).toBe(`Plan needs another look · ${REASON}`);
    const holding = { ...value, plates: value.plates.map((plate) => ({ ...plate,
      calibration: { test: 'temperature', start: 230, end: 190, step: 5 } })) };
    expect(planNote(holding)).toBe('');
    expect(planNote({ ...value, calibration: { ...tower, test: 'pressure_advance', method: 'pattern' } })).toBe('');
    expect(planNote({ ...value, calibration: { ...tower, test: 'pressure_advance', method: 'tower' } }))
      .toBe('Pressure advance steps not kept in the file');
  });
});

describe('below the rule: the presets in force', () => {
  it('names the process and the first filament, each without its printer suffix', () => {
    const value = context();
    value.printer.process = '0.20mm Standard @BBL A1';
    expect(presetsLine(value)).toBe('0.20mm Standard · PETG Basic');
    expect(show(value)).toHaveTextContent('0.20mm Standard · PETG Basic');
  });

  it('counts the filament slots it does not name', () => {
    const value = context();
    value.setupIdentity!.filaments.push({ preset: 'PLA Basic @BBL A1', material: 'PLA' }, { preset: 'PLA Basic @BBL A1', material: 'PLA' });
    expect(presetsLine(value)).toBe('0.20 mm Standard · PETG Basic +2');
  });

  it('counts the settings that differ from the presets, the objects\' own included, and names none', () => {
    const value = context();
    value.presetDeltas = [delta('a', 'Wall loops', '2 → 4'), delta('b', 'Brim type', 'Auto → Outer brim only')];
    value.appliedSetup!.localOverrides = [
      { object: 0, target: 'a', kind: 'object', key: 'wall_loops', value: '5' },
      { object: 0, target: 'a / tab', kind: 'modifier', key: 'wall_loops', value: '7' },
      // Painting sets no setting.
      { object: 2, target: 'c', kind: 'support painting', key: '', value: '' }];
    expect(changedSettingCount(value)).toBe(4);
    const card = show(value);
    expect(card).toHaveTextContent('0.20 mm Standard · PETG Basic · 4 settings changed');
    expect(card).not.toHaveTextContent(/Wall loops|Brim type|→|with overrides/);
    cleanup();
    value.presetDeltas = [];
    value.appliedSetup!.localOverrides.length = 1;
    expect(presetsLine(value)).toBe('0.20 mm Standard · PETG Basic · 1 setting changed');
    value.appliedSetup = null;
    expect(presetsLine(value)).toBe('0.20 mm Standard · PETG Basic');
  });

  it('keeps long preset names on one line, whole in the tooltip', () => {
    const value = context();
    value.printer.process = 'Draft quality profile for the large enclosure panels with thick walls';
    const line = show(value).querySelector('.current-setup-facts-text')!;
    expect(line).toHaveTextContent('Draft quality profile for the large enclosure panels with thick walls · PETG Basic');
    expect(line).toHaveAttribute('title', line.textContent);
  });

  it('says it read no settings for a printer with no process preset', () => {
    const value = context();
    value.printer.process = '';
    expect(presetsLine(value)).toBe('');
    expect(show(value).querySelector('.current-setup-facts-text')).toHaveTextContent('No settings read from the project');
  });

  it('opens the All settings panel from the link beside the presets', async () => {
    const onViewSetup = vi.fn();
    const card = show(context(), { onViewSetup });
    const link = within(card).getByRole('button', { name: 'All settings' });
    expect(link.closest('.current-setup-facts')).toBe(card.querySelector('.current-setup-facts'));
    await userEvent.click(link);
    expect(onViewSetup).toHaveBeenCalledOnce();
  });

  it('annotates the presets when the printer last reported another material, and only then', () => {
    const value = context();
    value.printerReview = { observed: { connection: 'offline', observedAt: '2026-10-05T20:00:00' },
      mismatches: [{ what: 'filament', configured: 'PETG', observed: 'PLA', source: 'device' }] };
    const card = show(value);
    expect(annotations(card)).toEqual(['Printer last reported PLA']);
    const note = card.querySelector('.current-setup-annotation')!;
    expect(card.querySelector('.current-setup-facts')!.compareDocumentPosition(note) &
      Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    // A report nobody timed is not a report.
    expect(presetsNote({ ...value, printerReview: { mismatches: value.printerReview.mismatches } })).toBe('');
    expect(presetsNote({ ...value, printerReview: { observed: value.printerReview.observed, mismatches: [] } })).toBe('');
  });

  it('leaves the nozzle, the printer model and the plate to the header\'s chip and the chat', () => {
    const value = context();
    value.printerReview = { observed: { observedAt: '2026-10-05T20:00:00' }, mismatches: [
      { what: 'model', configured: 'A1', observed: 'P1', source: 'device' },
      { what: 'nozzle', configured: '0.4', observed: '0.6', source: 'device' },
      { what: 'plate', configured: 'Textured PEI', observed: 'Smooth PEI', source: 'user_confirmed' }] };
    const card = show(value);
    expect(annotations(card)).toEqual([]);
    expect(card).not.toHaveTextContent(/differs|P1|0\.6|Smooth PEI/);
  });
});

describe('below the rule: time and material', () => {
  it('shows a current estimate with its plate', () => {
    expect(estimateView(context())).toMatchObject({ status: 'current', line: '', plate: 'Plate 1',
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
    expect(estimateView(value).line).toBe('Estimate unavailable');
    value.plates[0].sliced = false;
    expect(estimateView(value).line).toBe('Not sliced yet');
  });

  it('offers to compute a plate never sliced, through the caller', async () => {
    const value = context();
    value.plates[0].estimate = null;
    value.plates[0].sliced = false;
    const onCompute = vi.fn();
    const card = show(value, { onCompute });
    expect(card).toHaveTextContent('Not sliced yet');
    await userEvent.click(within(card).getByRole('button', { name: 'Compute estimates' }));
    expect(onCompute).toHaveBeenCalledOnce();
  });

  it('shows no old figure while the plate recomputes, and no "Confirmed"', () => {
    const value = context();
    value.plates[0].estimateStatus = 'recomputing';
    const card = show(value);
    expect(card).toHaveTextContent('Time & material estimates recomputing…');
    expect(card).not.toHaveTextContent(/Confirmed|~2h 18m/);
    expect(card.querySelector('.current-setup-state')).toBeNull();
  });

  it('annotates figures that no longer match the project and offers to recompute them', async () => {
    const value = context();
    value.plates[0].estimateStatus = 'stale';
    value.plates[0].invalidatedBy = 'Wall loops changed';
    const onCompute = vi.fn();
    const card = show(value, { onCompute });
    expect(card).toHaveTextContent('~2h 18m');
    expect(annotations(card)).toEqual(['Previous estimates · not current']);
    // The card stays the live one: the annotation is the whole warning.
    expect(card).toHaveTextContent('Setup summary');
    expect(card).toHaveClass('current-setup--live');
    expect(card).not.toHaveTextContent(/Out of date|Earlier setup|Wall loops changed/);
    await userEvent.click(within(card).getByRole('button', { name: 'Recompute' }));
    expect(onCompute).toHaveBeenCalledOnce();
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
});

describe('one card-wide state', () => {
  it('says "Agent working" and nothing else while the agent works, and offers no action', () => {
    const value = context();
    value.plates[0].estimateStatus = 'stale';
    const card = show(value, { working: true, onCompute: () => {} });
    expect(card.querySelector('.current-setup-state')).toHaveTextContent(/^Agent working$/);
    expect(card).toHaveTextContent('~2h 18m');
    expect(card).not.toHaveTextContent(/Agent is applying|Last confirmed/);
    expect(card).toHaveAttribute('aria-busy', 'true');
    expect(within(card).queryByRole('button', { name: /Recompute|Compute/ })).toBeNull();
  });

  it('has no state at all otherwise, and no all-clear', () => {
    const card = show({ ...context(), planHeadline: HEADLINE });
    expect(card.querySelector('.current-setup-state')).toBeNull();
    expect(card).not.toHaveTextContent(/Current|Updated|Edited by|Not sliced$|safe to print|no issues|all clear/i);
  });

  it('makes no warning out of a question or a time in the purpose, or out of the plan', () => {
    const value = context();
    value.setupIntent = 'Could this finish under 1 hour?';
    value.planHeadline = 'Fast draft layers to finish within the hour';
    const card = show(value);
    expect(annotations(card)).toEqual([]);
    expect(card).not.toHaveTextContent(/exceeds|target|reassess/i);
  });
});

describe('saved and no-agent cards', () => {
  it('draws a saved card the same way, quiet, with when it was saved and no live action', () => {
    const value = { ...context(), planHeadline: HEADLINE };
    value.plates[0].estimateStatus = 'stale';
    const card = show(value, { historical: true, savedAt: '2026-10-03T10:42:00', onCompute: () => {}, working: true });
    expect(card).toHaveAccessibleName('Earlier setup — saved with this conversation');
    expect(card).not.toHaveClass('current-setup--live');
    expect(card.querySelector('.current-setup-label')).toHaveTextContent('Earlier setup');
    expect(card.querySelector('.current-setup-state')).toHaveTextContent('Saved Oct 3, 10:42');
    expect(card.querySelector('.current-setup-strategy')).toHaveTextContent(HEADLINE);
    expect(annotations(card)).toEqual(['Previous estimates · not current']);
    expect(card).toHaveTextContent('~2h 18m');
    expect(card).toHaveTextContent('Saved with this chat. The canvas shows your current project.');
    expect(card).not.toHaveTextContent(/Agent working/);
    expect(card).not.toHaveAttribute('aria-busy');
    expect(within(card).queryByRole('button', { name: /Recompute|Compute/ })).toBeNull();
  });

  it('says where and when a saved sliced snapshot was sent', () => {
    const card = show(context(), { historical: true, sent: { at: '2026-10-03T10:42:00', printer: 'Bambu A1' } });
    expect(card.querySelector('.current-setup-label')).toHaveTextContent('Saved sliced snapshot');
    expect(card.querySelector('.current-setup-state')).toHaveTextContent('Sliced & sent');
    expect(card).toHaveTextContent('Sent to Bambu A1 · Oct 3, 10:42. Saved snapshot: the canvas shows your current project.');
    expect(card).not.toHaveTextContent(/pre-slice|Actual time/);
  });

  it('is the same card with no agent, saying so under the facts', () => {
    const value = context();
    value.setupIntent = '';
    value.presetDeltas = [delta('a', 'Wall loops', '2 → 3')];
    const card = show(value, { agentAvailable: false, working: true });
    expect(card.querySelector('.current-setup-label')).toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('0.20 mm Standard · PETG Basic · 1 setting changed');
    // Someone changed a setting, so the preset alone is not what is in force.
    expect(card).toHaveTextContent('No agent configured.');
    expect(card).not.toHaveTextContent(/using the selected preset|Preset fallback|Edited by|Updated|Agent working/);
    expect(within(card).queryByRole('button')).toBeNull();
  });

  it('makes no claim it did not read', () => {
    const value = context();
    value.setupIntent = '';
    value.appliedSetup = null;
    value.setupIdentity = null;
    value.printer.process = '';
    value.plates[0].estimate = null;
    const card = show(value);
    expect(card).toHaveTextContent('No settings read from the project');
    expect(card).toHaveTextContent('Estimate unavailable');
    expect(card).not.toHaveTextContent(/settings changed|0 g/);
    expect(within(card).queryByRole('button')).toBeNull();
  });

  it('renders nothing without a context', () => {
    const { container } = render(<SetupCard context={null} onViewSetup={() => {}} />);
    expect(container).toBeEmptyDOMElement();
  });
});
