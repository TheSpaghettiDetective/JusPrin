// The All settings panel's rules, one behaviour at a time: what Showing
// lists and follows, what the filter and the search leave, how the Filament
// group draws one, three and six slots, and what an object's or a plate's
// view says. The frames themselves render through SetupCard.preview and
// SetupCard.acceptance.

import { describe, expect, it, vi } from 'vitest';
import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { CatalogFilamentInfo, CatalogSettingInfo, SettingsActionPayload, SettingsCatalogPayload, WorkspaceContext } from '../bridge/protocol';
import { AllSettings, catalogFromContext, selectedObject } from './AllSettings';

function context(): WorkspaceContext {
  return {
    sessionId: 'project-1', revision: 4, projectName: 'Bracket', projectDirty: false,
    printer: { preset: 'Bambu Lab A1', filament: 'Generic PETG @BBL A1', process: '0.20mm Standard @BBL A1' },
    plates: [{ id: 'p1', name: 'Plate 1', active: true, sliced: false, estimate: null, estimateStatus: 'current', invalidatedBy: '',
      objects: [{ id: 'o1', name: 'bracket v3', instances: 1, selected: false }, { id: 'o2', name: 'spacer', instances: 1, selected: false }] }],
    selection: { status: 'none', objectIds: [] }, history: { canUndo: false, canRedo: false },
    presetDeltas: [], appliedSetup: null, printerReview: null, currency: '', setupIntent: '', planHeadline: '',
    setupIdentity: { printer: 'Bambu Lab A1', nozzles: [0.4], plateType: 'Textured PEI Plate',
      filaments: [{ preset: 'Generic PETG @BBL A1', material: 'PETG' }] },
  };
}

const row = (key: string, label: string, page: string, group: string, display: string,
  extra: Partial<CatalogSettingInfo> = {}): CatalogSettingInfo => ({ key, label, page, group, value: display, display, ...extra });

function filament(slot: number, preset: string, material: string, settings: CatalogSettingInfo[]): CatalogFilamentInfo {
  return { slot, preset, material, colour: slot === 1 ? '#FF8800' : '', settings };
}

function catalog(): SettingsCatalogPayload {
  return {
    available: true,
    process: { name: '0.20mm Standard @BBL A1', settings: [
      row('k_layer', 'Layer height', 'Quality', 'Layer height', '0.2 mm'),
      row('k_walls', 'Wall loops', 'Strength', 'Walls', '4', { preset: '2', presetDisplay: '2' }),
      row('k_infill', 'Sparse infill density', 'Strength', 'Infill', '30%', { preset: '15%', presetDisplay: '15%' }),
      row('k_speed', 'Outer wall speed', 'Speed', 'Other layers speed', '200 mm/s'),
      row('k_odd', 'Unindexed setting', '', '', 'On'),
    ] },
    printer: { name: 'Bambu Lab A1 0.4 nozzle', settings: [row('k_retract', 'Retraction length', 'Extruder', 'Retraction', '0.8 mm')] },
    filaments: [filament(1, 'Generic PETG @BBL A1', 'PETG', [
      row('k_temp', 'Nozzle temperature', 'Filament', 'Temperatures', '255 °C'),
      row('k_bed_textured', 'Bed temperature', 'Filament', 'Temperatures', '70 °C'),
      row('filament_k_retract', 'Retraction length', 'Setting Overrides', 'Retraction', 'Not set', { inherited: true, inheritedDisplay: '0.8 mm' }),
    ])],
    plates: [{ id: 'p1', name: 'Plate 1', active: true, bedType: 'Textured PEI Plate', ownBedType: true,
      bedTempKey: 'k_bed_textured', bedTempInitialKey: 'k_bed_textured_first', own: [row('k_bed_type', 'Bed type', 'Plate', '', 'Textured PEI Plate')] }],
    objects: [
      { id: 'o1', name: 'bracket v3', plateId: 'p1', overrides: [
        { target: 'bracket v3 / Mounting tabs', kind: 'modifier', key: 'k_walls', value: '6', label: 'Wall loops', display: '6',
          action: 'use_project_value' },
        { target: 'bracket v3 / Mounting tabs', kind: 'modifier', key: 'k_walls', value: '6', label: 'Wall loops', display: '6',
          action: 'use_project_value' },
        { target: 'bracket v3 / Screw holes', kind: 'support blocker', key: '', value: '', action: 'remove' },
        { target: 'bracket v3 / Screw holes', kind: 'support blocker', key: '', value: '', action: 'remove' },
        { target: 'bracket v3', kind: 'object', key: 'extruder', value: '2', label: 'Filament slot', display: '2',
          action: 'use_project_value' },
      ] },
      { id: 'o2', name: 'spacer', plateId: 'p1', overrides: [] },
    ],
  };
}

const panel = () => screen.getByTestId('all-settings');
const showing = (name: string) => screen.getByRole('button', { name: `Showing: ${name}` });

describe('Showing', () => {
  it('lists the whole project and each object, and plates only when there are two or more', async () => {
    const { rerender } = render(<AllSettings context={context()} catalog={catalog()} />);
    await userEvent.click(showing('Whole project'));
    expect(within(screen.getByRole('menu')).getAllByRole('menuitemradio').map((item) => item.textContent)).toEqual([
      'Whole project', 'bracket v3', 'spacer']);
    await userEvent.keyboard('{Escape}');
    const two = context();
    two.plates.push({ ...two.plates[0], id: 'p2', name: 'Plate 2', active: false, objects: [] });
    const table = catalog();
    table.plates.push({ ...table.plates[0], id: 'p2', name: 'Plate 2', active: false, ownBedType: false, own: [] });
    rerender(<AllSettings context={two} catalog={table} />);
    await userEvent.click(showing('Whole project'));
    expect(within(screen.getByRole('menu')).getAllByRole('menuitemradio').map((item) => item.textContent)).toEqual([
      'Whole project', 'Plate 1', 'Plate 2', 'bracket v3', 'spacer']);
  });

  it('opens on the whole project whatever the 3D view has selected', () => {
    render(<AllSettings context={{ ...context(), selection: { status: 'objects', objectIds: ['o1'] } }} catalog={catalog()} />);
    expect(panel()).toHaveAttribute('data-scope', 'project');
    expect(panel()).toHaveTextContent('Follows the selection in the 3D view.');
  });

  it('follows a change of selection in the 3D view, and a choice made here holds until the next', async () => {
    const value = context();
    const { rerender } = render(<AllSettings context={value} catalog={catalog()} />);
    expect(panel()).toHaveAttribute('data-scope', 'project');
    const selected = { ...value, selection: { status: 'objects' as const, objectIds: ['o2'] } };
    rerender(<AllSettings context={selected} catalog={catalog()} />);
    expect(panel()).toHaveAttribute('data-scope', 'object:o2');
    expect(panel()).toHaveTextContent('Shown: only what spacer sets itself.');
    await userEvent.click(showing('spacer'));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Whole project' }));
    expect(panel()).toHaveAttribute('data-scope', 'project');
    rerender(<AllSettings context={{ ...selected, revision: 5 }} catalog={catalog()} />);
    expect(panel()).toHaveAttribute('data-scope', 'project');
    rerender(<AllSettings context={{ ...value, selection: { status: 'objects', objectIds: ['o1'] } }} catalog={catalog()} />);
    expect(panel()).toHaveAttribute('data-scope', 'object:o1');
    // Two selected objects are nothing to follow.
    rerender(<AllSettings context={{ ...value, selection: { status: 'objects', objectIds: ['o1', 'o2'] } }} catalog={catalog()} />);
    expect(panel()).toHaveAttribute('data-scope', 'project');
    expect(selectedObject({ ...value, selection: { status: 'unsupported', objectIds: ['o1'] } })).toBeNull();
  });
});

describe('the whole project', () => {
  it('names the presets in use, with the plate type set on the plate and the objects with their own settings', () => {
    render(<AllSettings context={context()} catalog={catalog()} />);
    const presets = screen.getByTestId('all-settings-presets');
    expect(presets).toHaveTextContent('PrinterBambu Lab A1 · 0.4 mm nozzle');
    expect(presets).toHaveTextContent('FilamentSlot 1 · Generic PETG');
    expect(within(presets).getByRole('img', { name: 'PETG' })).toHaveStyle({ background: '#FF8800' });
    expect(presets).toHaveTextContent('Process0.20mm Standard2 changes from the preset');
    expect(presets).toHaveTextContent('Plate 1Textured PEI PlateBed type set on this plate');
    expect(panel()).toHaveTextContent('bracket v3 has settings of its own in 4 regions: 2 Mounting tabs and 2 Screw holes.');
    expect(panel()).toHaveTextContent('Open it in the 3D view, or choose it above, to see them.');
  });

  it('groups the process settings under OrcaSlicer\'s pages, files an unindexed one under Others, and opens the changed groups', async () => {
    render(<AllSettings context={context()} catalog={catalog()} />);
    const names = () => [...panel().querySelectorAll('.all-settings-group-name')].map((name) => name.textContent);
    expect(names()).toEqual(['Quality', 'Strength', 'Speed', 'Support', 'Multimaterial', 'Others', 'Filament', 'Printer']);
    expect(panel()).toHaveTextContent('Strength2 changed');
    expect(panel()).toHaveTextContent('Speedfrom the preset');
    // Quality (the first) and Strength (changed) start open; Speed is closed.
    expect(panel()).toHaveTextContent('Layer height0.2 mm');
    expect(panel()).toHaveTextContent('Wall loops4Preset: 2Mounting tabs: 6Mounting tabs: 6');
    expect(panel()).not.toHaveTextContent('Outer wall speed');
    await userEvent.click(screen.getByRole('button', { name: /^Speed/ }));
    expect(panel()).toHaveTextContent('Outer wall speed200 mm/s');
    await userEvent.click(screen.getByRole('button', { name: /^Others/ }));
    expect(panel()).toHaveTextContent('Unindexed settingOn');
    expect(panel().querySelectorAll('.all-settings-row--changed')).toHaveLength(2);
  });

  it('keeps only the changed rows and their groups under the Changed filter', async () => {
    render(<AllSettings context={context()} catalog={catalog()} />);
    await userEvent.click(screen.getByRole('radio', { name: 'Changed' }));
    expect([...panel().querySelectorAll('.all-settings-group-name')].map((name) => name.textContent)).toEqual(['Strength']);
    expect(panel()).toHaveTextContent('Wall loops4Preset: 2');
    expect(panel()).toHaveTextContent('Sparse infill density30%Preset: 15%');
    expect(panel()).not.toHaveTextContent('Layer height');
    await userEvent.click(screen.getByRole('radio', { name: 'All' }));
    expect(panel()).toHaveTextContent('Layer height');
  });

  it('searches every group by name or key, and opens the groups with matches', async () => {
    render(<AllSettings context={context()} catalog={catalog()} />);
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search settings' }), 'retract');
    expect([...panel().querySelectorAll('.all-settings-group-name')].map((name) => name.textContent)).toEqual(['Filament', 'Printer']);
    expect(panel()).toHaveTextContent('Setting Overrides · RetractionRetraction lengthprinterFrom the printer: 0.8 mm');
    expect(panel()).toHaveTextContent('Extruder · RetractionRetraction length0.8 mm');
    await userEvent.clear(screen.getByRole('searchbox'));
    await userEvent.type(screen.getByRole('searchbox'), 'k_odd');
    expect(panel()).toHaveTextContent('Othersfrom the presetUnindexed settingOn');
  });

  it('names the bed temperature for the plate type the shown plate prints with', async () => {
    render(<AllSettings context={context()} catalog={catalog()} />);
    await userEvent.click(screen.getByRole('button', { name: /^Filament/ }));
    expect(panel()).toHaveTextContent('Filament · TemperaturesNozzle temperature255 °CBed temperature · Textured PEI Plate70 °C');
  });
});

describe('the Filament group', () => {
  const three = (): SettingsCatalogPayload => {
    const table = catalog();
    table.filaments = [
      filament(1, 'Generic PETG @BBL A1', 'PETG', [row('k_temp', 'Nozzle temperature', 'Filament', 'Temperatures', '255 °C'),
        row('filament_k_retract', 'Retraction length', 'Setting Overrides', 'Retraction', 'Not set', { inherited: true, inheritedDisplay: '0.8 mm' })]),
      filament(2, 'Bambu PLA Basic @BBL A1', 'PLA', [row('k_temp', 'Nozzle temperature', 'Filament', 'Temperatures', '220 °C', { preset: '215', presetDisplay: '215 °C' }),
        row('filament_k_retract', 'Retraction length', 'Setting Overrides', 'Retraction', 'Not set', { inherited: true, inheritedDisplay: '0.8 mm' })]),
      filament(3, 'Bambu PLA Matte @BBL A1', 'PLA', [row('k_temp', 'Nozzle temperature', 'Filament', 'Temperatures', '220 °C'),
        row('filament_k_retract', 'Retraction length', 'Setting Overrides', 'Retraction', '1.2 mm')]),
    ];
    return table;
  };

  it('draws two or three slots side by side, with "printer" where a slot leaves a setting to the printer', async () => {
    render(<AllSettings context={context()} catalog={three()} />);
    expect(panel()).toHaveTextContent('Filament3 slots, side by side');
    // Slot 2 changed a value, so the group starts open.
    const columns = panel().querySelector('.all-settings-columns')!;
    expect(columns).toHaveTextContent('Slot 1PETGSlot 2PLASlot 3PLA');
    expect(columns).toHaveTextContent('Nozzle temperature255 °C220 °C220 °CSlot 2 preset: 215 °C');
    expect(columns).toHaveTextContent('Retraction lengthprinterprinter1.2 mmSlot 3 overrides the printer’s 0.8 mm.');
    const cells = [...columns.querySelectorAll('.all-settings-row-value')].map((cell) => cell.className);
    expect(cells[0]).not.toContain('bold');
    expect(cells[1]).toContain('bold');
    expect(cells[3]).toContain('muted');
    expect(cells[5]).toContain('bold');
    await userEvent.click(screen.getByRole('radio', { name: 'Changed' }));
    expect(panel().querySelectorAll('.all-settings-columns .all-settings-row')).toHaveLength(2);
  });

  it('offers a slot picker, one slot at a time, above three slots', async () => {
    const table = three();
    table.filaments.push(filament(4, 'Bambu TPU @BBL A1', 'TPU', [row('k_temp', 'Nozzle temperature', 'Filament', 'Temperatures', '230 °C')]));
    render(<AllSettings context={context()} catalog={table} />);
    expect(panel()).toHaveTextContent('Filament4 slots');
    expect(panel().querySelector('.all-settings-columns')).toBeNull();
    expect(panel()).toHaveTextContent('Nozzle temperature255 °C');
    await userEvent.click(screen.getByRole('button', { name: 'Slot: Slot 1 · Generic PETG' }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Slot 4 · TPU' }));
    expect(panel()).toHaveTextContent('Nozzle temperature230 °C');
    expect(panel()).not.toHaveTextContent('255 °C');
  });
});

describe('one object', () => {
  it('lists only what the object sets itself, with what each overrides, and says the rest is the project\'s', async () => {
    render(<AllSettings context={context()} catalog={catalog()} initialScope={{ kind: 'object', id: 'o1' }} />);
    expect(panel()).toHaveTextContent('bracket v3 uses the project’s settings, except in 4 regions: 2 Mounting tabs and 2 Screw holes.');
    expect(panel()).toHaveTextContent('Strength1 own setting');
    expect(panel()).toHaveTextContent('Wall loops6RegionMounting tabs · 2 regions · project: 4');
    expect(panel()).toHaveTextContent('Support1 own setting');
    expect(panel()).toHaveTextContent('Support blockersOnRegionScrew holes, Screw holes · 2 regions');
    // The object's filament is a slot number in OrcaSlicer; the slot's preset
    // says more, where the project has that slot.
    expect(panel()).toHaveTextContent('Filament slot2ObjectWhole object · project: 1 · Generic PETG');
    expect(panel()).toHaveTextContent('Qualitysame as project');
    expect(panel()).toHaveTextContent('Speedsame as project');
    // Without a host that takes the presses, nothing to press on a row.
    expect(within(panel()).queryByRole('button', { name: /Use project value|Remove/ })).toBeNull();
    await userEvent.type(screen.getByRole('searchbox'), 'blocker');
    expect(panel()).not.toHaveTextContent('Wall loops');
    expect(panel()).toHaveTextContent('Support blockers');
  });

  it('offers "Use project value" and "Remove" on the rows the table marks, and sends each as the row names it', async () => {
    const sent: SettingsActionPayload[] = [];
    const value = { ...context(), selection: { status: 'objects' as const, objectIds: ['o1'] } };
    const table = catalog();
    // A height range's own layer height defines the range, and paint has no
    // value to give back: the table marks neither, so neither has a link.
    table.objects[0].overrides.push(
      { target: 'bracket v3', kind: 'height range', key: 'k_layer', value: '0.1', label: 'Layer height', display: '0.1 mm' },
      { target: 'bracket v3', kind: 'seam painting', key: '', value: '' });
    render(<AllSettings context={value} catalog={table} initialScope={{ kind: 'object', id: 'o1' }}
      onAction={(action) => sent.push(action)} />);
    expect(panel()).toHaveTextContent('Layer height0.1 mmHeight rangeHeight range · project: 0.2 mm');
    expect(panel()).toHaveTextContent('Seam paintingOnObjectbracket v3');
    const links = () => within(panel()).queryAllByRole('button', { name: /Use project value|Remove/ }).map((link) =>
      link.getAttribute('aria-label'));
    // The two Mounting tabs are one row, so one link; the height range's
    // layer height and the seam painting have none.
    expect(links()).toEqual(['Use project value for Wall loops', 'Remove support blockers', 'Use project value for Filament slot']);
    // Under the row's note, Button/Link text.
    const walls = screen.getByRole('button', { name: 'Use project value for Wall loops' });
    expect(walls).toHaveTextContent(/^Use project value$/);
    expect(walls).toHaveClass('link');
    expect(walls.parentElement).toHaveTextContent('Mounting tabs · 2 regions · project: 4Use project value');
    expect(screen.getByRole('button', { name: 'Remove support blockers' })).toHaveTextContent(/^Remove$/);

    await userEvent.click(walls);
    await userEvent.click(screen.getByRole('button', { name: 'Remove support blockers' }));
    await userEvent.click(screen.getByRole('button', { name: 'Use project value for Filament slot' }));
    expect(sent).toEqual([
      { sessionId: 'project-1', objectId: 'o1', action: 'use_project_value', kind: 'modifier', target: 'bracket v3 / Mounting tabs',
        key: 'k_walls', value: '6' },
      { sessionId: 'project-1', objectId: 'o1', action: 'remove', kind: 'support blocker', target: '', key: '', value: '' },
      { sessionId: 'project-1', objectId: 'o1', action: 'use_project_value', kind: 'object', target: 'bracket v3',
        key: 'extruder', value: '2' },
    ]);
  });

  it('offers nothing to press outside one object\'s view, or in a saved chat', async () => {
    const onAction = vi.fn();
    const { rerender } = render(<AllSettings context={context()} catalog={catalog()} onAction={onAction} />);
    expect(panel()).toHaveAttribute('data-scope', 'project');
    expect(within(panel()).queryByRole('button', { name: /Use project value|Remove/ })).toBeNull();
    const two = context();
    two.plates.push({ ...two.plates[0], id: 'p2', name: 'Plate 2', active: false, objects: [] });
    const table = catalog();
    table.plates.push({ ...table.plates[0], id: 'p2', name: 'Plate 2', active: false, ownBedType: false, own: [] });
    rerender(<AllSettings context={two} catalog={table} onAction={onAction} />);
    await userEvent.click(showing('Whole project'));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Plate 1' }));
    expect(panel()).toHaveAttribute('data-scope', 'plate:p1');
    expect(within(panel()).queryByRole('button', { name: /Use project value|Remove/ })).toBeNull();
    rerender(<AllSettings context={{ ...context(), selection: { status: 'objects', objectIds: ['o1'] } }} catalog={catalog()}
      onAction={onAction} saved />);
    expect(panel()).toHaveAttribute('data-scope', 'object:o1');
    expect(within(panel()).queryByRole('button', { name: /Use project value|Remove/ })).toBeNull();
    expect(onAction).not.toHaveBeenCalled();
  });

  it('says when an object sets nothing of its own', () => {
    render(<AllSettings context={context()} catalog={catalog()} initialScope={{ kind: 'object', id: 'o2' }} />);
    expect(panel()).toHaveTextContent('spacer uses the project’s settings and sets nothing of its own.');
    expect(panel().querySelectorAll('.all-settings-group-header--empty')).toHaveLength(8);
  });
});

describe('one plate', () => {
  it('lists the plate\'s own settings and the objects on it, each opening its own view', async () => {
    const two = context();
    two.plates.push({ ...two.plates[0], id: 'p2', name: 'Plate 2', active: false, objects: [] });
    const table = catalog();
    table.plates.push({ ...table.plates[0], id: 'p2', name: 'Plate 2', active: false, ownBedType: false, own: [] });
    render(<AllSettings context={two} catalog={table} />);
    await userEvent.click(showing('Whole project'));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Plate 1' }));
    expect(panel()).toHaveTextContent('Plate 1 sets 1 setting of its own; the rest is the project’s.');
    expect(panel()).toHaveTextContent('Bed typeTextured PEI PlateSet on this plate');
    expect(panel()).toHaveTextContent('Objects2 objects');
    expect(panel()).toHaveTextContent('bracket v35 own settings');
    expect(panel()).toHaveTextContent('spacersame as project');
    await userEvent.click(screen.getByRole('button', { name: 'spacer' }));
    expect(panel()).toHaveAttribute('data-scope', 'object:o2');
    await userEvent.click(showing('spacer'));
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Plate 2' }));
    expect(panel()).toHaveTextContent('Plate 2 prints with the project’s settings and sets nothing of its own.');
  });
});

describe('what the host has not read', () => {
  it('says it is reading, cannot list, or has no FFF printer', () => {
    const { rerender } = render(<AllSettings context={context()} catalog={null} />);
    expect(screen.getByRole('status')).toHaveTextContent('Reading the settings…');
    rerender(<AllSettings context={context()} catalog={null} unsupported />);
    expect(panel()).toHaveTextContent('This JusPrin build cannot list the settings.');
    rerender(<AllSettings context={context()} catalog={{ ...catalog(), available: false }} />);
    expect(panel()).toHaveTextContent('not an FFF printer');
  });

  it('shows a saved chat the settings its summary says differed, changed only', () => {
    const value = context();
    value.presetDeltas = [{ key: 'brim_width', label: 'Brim width', preset: '0', value: '8', display: '0 → 8 mm',
      presetType: 'process', presetName: value.printer.process, page: 'Others', group: 'Brim' },
    { key: 'k_bed', label: 'Bed temperature', preset: '65', value: '70', display: '65 → 70 ℃',
      presetType: 'filament', presetName: value.printer.filament, page: 'Filament', group: 'Temperatures' }];
    value.appliedSetup = { version: 1, plateId: 'p1', printableObjects: 2, spiralMode: false, variableLayerHeight: false,
      objects: ['bracket v3', 'spacer'], localOverrides: [{ object: 0, target: 'bracket v3 / Rim', kind: 'modifier',
        key: 'k_walls', value: '6', label: 'Wall loops', display: '6' }] };
    const saved = catalogFromContext(value);
    expect(saved.process.settings).toEqual([expect.objectContaining({ key: 'brim_width', display: '8 mm', presetDisplay: '0 mm' })]);
    expect(saved.filaments[0].settings).toEqual([expect.objectContaining({ key: 'k_bed', display: '70 ℃', presetDisplay: '65 ℃' })]);
    expect(saved.objects[0]).toMatchObject({ id: 'o1', name: 'bracket v3', overrides: [{ key: 'k_walls', target: 'bracket v3 / Rim' }] });
    render(<AllSettings context={value} catalog={saved} saved />);
    expect(panel()).toHaveTextContent('A saved chat’s setup');
    expect(screen.getByRole('radio', { name: 'Changed' })).toBeChecked();
    expect(screen.getByRole('radio', { name: 'All' })).toBeDisabled();
    expect(panel()).toHaveTextContent('Brim width8 mmPreset: 0 mm');
    expect(panel()).toHaveTextContent('Bed temperature70 ℃Preset: 65 ℃');
    expect(panel()).not.toHaveTextContent('has settings of its own');
  });
});
