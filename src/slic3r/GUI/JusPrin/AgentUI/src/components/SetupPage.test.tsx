// The setup page's rules: what each section shows, in what order, and what it
// leaves out when the host had nothing to say.

import { describe, expect, it, vi } from 'vitest';
import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PresetDeltaInfo, WorkspaceContext } from '../bridge/protocol';
import { SetupPage, SetupPageHeader } from './SetupPage';
import { setupWorkspace, visualChanges } from './SetupCard.visual-cases';

function show(context: WorkspaceContext, props: Partial<Parameters<typeof SetupPage>[0]> = {}) {
  render(<SetupPage context={context} {...props} />);
  return screen.getByTestId('current-setup-page');
}

const section = (page: HTMLElement, heading: string | RegExp) =>
  within(page).getByRole('heading', { name: heading }).closest('section') as HTMLElement;
const texts = (root: HTMLElement, selector: string) => [...root.querySelectorAll(selector)].map((node) => node.textContent);

describe('setup page', () => {
  it('carries the card\'s own label, state and title', () => {
    const page = show(setupWorkspace(), { changes: visualChanges });
    expect(page).toHaveTextContent('Setup summary');
    expect(page).toHaveTextContent('Current');
    expect(within(page).getByRole('heading', { name: 'Stop the corners lifting' })).toBeInTheDocument();
    expect(page.querySelector('.current-setup')).toHaveClass('current-setup--live');
  });

  it('is a saved chat\'s page when the chat is an earlier one', () => {
    const page = show(setupWorkspace(), { historical: true, savedAt: '2026-10-03T10:42:00', working: true });
    expect(screen.getByLabelText('Earlier setup — saved with this conversation')).toBeInTheDocument();
    expect(page).toHaveTextContent('Earlier setup');
    expect(page).toHaveTextContent('Saved Oct 3, 10:42');
    expect(page).not.toHaveTextContent('Agent working');
    expect(page.querySelector('.current-setup')).not.toHaveClass('current-setup--live');
  });

  it('lists every preset by its full name', () => {
    const presets = section(show(setupWorkspace()), 'Presets');
    expect(texts(presets, '.current-setup-row')).toEqual([
      'PrinterMyKlipper 0.4 nozzle', 'Process0.20mm Standard @MyKlipper', 'Filament 1Generic ABS @System',
      'Filament 2Generic PLA @System', 'Filament 3Generic PLA @System', 'Build plateSmooth High Temp Plate']);
    // A preset name is not cut to make room for its label.
    expect(presets.querySelector('.current-setup-row--change')).toBeNull();
  });

  it('does not number a single filament', () => {
    const context = setupWorkspace();
    context.setupIdentity!.filaments.length = 1;
    expect(section(show(context), 'Presets')).toHaveTextContent('FilamentGeneric ABS @System');
  });

  it('groups differences by preset, then by their place in the settings tabs', () => {
    const changed = section(show(setupWorkspace(), { changes: visualChanges }), 'Changed from presets · 5');
    expect(texts(changed, '.current-setup-object-name, .current-setup-group, .current-setup-row')).toEqual([
      '0.20mm Standard @MyKlipper',
      'Others · Brim', 'Brim width0 → 8 mm', 'Brim typeAuto → Outer brim only',
      'Quality · Layer height', 'First layer height0.2 → 0.28 mm',
      'Speed · First layer speed', 'First layer50 → 25 mm/s',
      'Generic ABS @System',
      'Filament · Bed temperature', 'Bed temperature100 → 105 ℃',
    ]);
  });

  it('keeps process, filament, printer in that order whichever was edited last', () => {
    const context = setupWorkspace();
    const printer: PresetDeltaInfo = { key: 'retraction_length', label: 'Retraction Length', preset: '0.8', value: '1.2',
      display: '0.8 → 1.2 mm', origin: 'agent', presetType: 'printer', presetName: 'MyKlipper 0.4 nozzle',
      page: 'Extruder 1', group: 'Retraction' };
    context.presetDeltas = [printer, ...context.presetDeltas.reverse()];
    const changed = section(show(context), /Changed from presets/);
    expect(texts(changed, '.current-setup-object-name')).toEqual([
      '0.20mm Standard @MyKlipper', 'Generic ABS @System', 'MyKlipper 0.4 nozzle']);
  });

  it('shows a setting the index could not place without a caption, never under another\'s', () => {
    const context = setupWorkspace();
    const of = (key: string) => context.presetDeltas.find((delta) => delta.key === key)!;
    context.presetDeltas = [{ ...of('brim_type'), page: undefined, group: undefined }, of('initial_layer_print_height')];
    const changed = section(show(context), /Changed from presets/);
    expect(texts(changed, '.current-setup-group, .current-setup-row')).toEqual([
      'Brim typeAuto → Outer brim only', 'Quality · Layer height', 'First layer height0.2 → 0.28 mm']);
  });

  it('says there are no changes, and counts none', () => {
    const context = setupWorkspace();
    context.presetDeltas = [];
    const changed = section(show(context), 'Changed from presets');
    expect(changed).toHaveTextContent('No changes from presets');
    expect(changed).not.toHaveTextContent('· 0');
  });

  it('opens the first object that has overrides and leaves the others closed', async () => {
    const context = setupWorkspace();
    context.appliedSetup!.localOverrides.push({ object: 1, target: 'bracket.stl', kind: 'object', key: 'brim_width',
      value: '5', label: 'Brim width', display: '5 mm' });
    const objects = section(show(context), 'Objects on Plate 1 · 2');
    const first = within(objects).getByRole('button', { name: '3x3_cali_RL.stl · 2 overrides' });
    const second = within(objects).getByRole('button', { name: 'bracket.stl · 1 override' });
    expect(first).toHaveAttribute('aria-expanded', 'true');
    expect(second).toHaveAttribute('aria-expanded', 'false');
    expect(texts(objects, '.current-setup-group, .current-setup-row')).toEqual([
      'Whole object', 'Sparse infill density30%', 'Corner tab · modifier', 'Wall loops5']);
    await userEvent.click(second);
    expect(objects).toHaveTextContent('Brim width5 mm');
    await userEvent.click(first);
    expect(objects).not.toHaveTextContent('Sparse infill density');
  });

  it('says an object without overrides uses the plate\'s settings', () => {
    const objects = section(show(setupWorkspace()), /Objects on Plate 1/);
    expect(objects).toHaveTextContent('bracket.stlUses plate settings');
    expect(within(objects).queryByRole('button', { name: /bracket\.stl/ })).toBeNull();
  });

  it('names paint and blockers by what they are, with no value to quote', () => {
    const context = setupWorkspace();
    context.appliedSetup!.localOverrides = [
      { object: 0, target: '3x3_cali_RL.stl / Screw holes', kind: 'support blocker', key: '', value: '' },
      { object: 0, target: '3x3_cali_RL.stl', kind: 'support painting', key: '', value: '' },
      { object: 0, target: '3x3_cali_RL.stl', kind: 'variable layer height', key: 'layer_height', value: '', label: 'Layer height' },
      { object: 0, target: '3x3_cali_RL.stl', kind: 'height range', key: 'wall_loops', value: '3', label: 'Wall loops', display: '3' },
    ];
    const objects = section(show(context), /Objects on Plate 1/);
    expect(texts(objects, '.current-setup-group, .current-setup-row')).toEqual([
      'Screw holes · support blocker', 'Support painting', 'Variable layer height', 'Height range', 'Wall loops3']);
  });

  it('names an object\'s filament by the slot\'s preset', () => {
    const context = setupWorkspace();
    context.appliedSetup!.localOverrides = [{ object: 0, target: '3x3_cali_RL.stl', kind: 'object', key: 'extruder',
      value: '2', label: 'Filament slot', display: '2' }];
    expect(section(show(context), /Objects on Plate 1/)).toHaveTextContent('Filament slot2 · Generic PLA @System');
  });

  it('reads a summary saved before the host named settings and formatted values', () => {
    const context = setupWorkspace();
    context.presetDeltas = [{ key: 'brim_type', label: 'Brim type', preset: 'auto_brim', value: 'outer_only', origin: 'agent' }];
    context.appliedSetup!.localOverrides = [{ object: 0, target: '3x3_cali_RL.stl', kind: 'object',
      key: 'sparse_infill_density', value: '30%' }];
    const page = show(context, { historical: true });
    expect(page).toHaveTextContent('Brim typeauto_brim → outer_only');
    expect(page).toHaveTextContent('Sparse infill density30%');
    expect(page.querySelector('.current-setup-group')).toHaveTextContent('Whole object');
  });

  it('leaves out the sections a summary has nothing for', () => {
    const context = setupWorkspace();
    context.appliedSetup = null;
    context.setupIdentity = null;
    context.printer.process = '';
    context.plates = [];
    const page = show(context, { historical: true });
    expect(texts(page, 'h3')).toEqual(['Changed from presets · 5']);
  });

  it('gives each plate its own estimate and marks the one on show', () => {
    const context = setupWorkspace();
    context.plates.push({ id: 'plate-3', name: 'Plate 3', active: false, sliced: true, estimate: null,
      estimateStatus: 'current', invalidatedBy: '', objects: [] });
    expect(texts(section(show(context), 'Plates'), '.current-setup-row')).toEqual([
      'Plate 1 · shown~2h 18m · 41 g', 'Plate 2Not sliced', 'Plate 3Estimate unavailable']);
  });

  it('does not mark the only plate as the one on show', () => {
    const context = setupWorkspace();
    context.plates.length = 1;
    expect(section(show(context), 'Plates')).toHaveTextContent(/^PlatesPlate 1~2h 18m · 41 g$/);
  });

  it('says a plate prints as a spiral vase', () => {
    const context = setupWorkspace();
    context.appliedSetup!.spiralMode = true;
    expect(section(show(context), 'Plates')).toHaveTextContent('Plate 1 · shown~2h 18m · 41 g · Spiral vase');
  });

  it('has no message box', () => {
    show(setupWorkspace());
    expect(screen.queryByRole('textbox')).toBeNull();
  });
});

describe('setup page header', () => {
  it('goes back to the chat, and takes the focus', async () => {
    const onBack = vi.fn();
    render(<SetupPageHeader onBack={onBack} onCollapse={() => {}} />);
    const back = screen.getByRole('button', { name: 'Back' });
    expect(back).toHaveFocus();
    await userEvent.click(back);
    expect(onBack).toHaveBeenCalledOnce();
  });

  it('can put the whole panel away, as the chat header can', async () => {
    const onCollapse = vi.fn();
    render(<SetupPageHeader onBack={() => {}} onCollapse={onCollapse} />);
    await userEvent.click(screen.getByRole('button', { name: 'Hide the Agent panel' }));
    expect(onCollapse).toHaveBeenCalledOnce();
  });
});
