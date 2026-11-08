// What the Import from OrcaSlicer window says on each of its screens, and
// which shell_action each of its buttons asks for; Import from JusPrin 1 is
// the same window with JusPrin 1's name.

import { describe, expect, it, vi } from 'vitest';
import { render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { OrcaImportPayload } from '../bridge/protocol';
import { OrcaImport } from './OrcaImport';

const folder = '~/Library/Application Support/OrcaSlicer';

function actions() {
  return { importPrinters: vi.fn(), chooseFolder: vi.fn(), close: vi.fn() };
}

function state(overrides: Partial<OrcaImportPayload>): OrcaImportPayload {
  return { kind: 'orca_import', app: 'OrcaSlicer', screen: 'found', folder, error: '', ...overrides };
}

describe('the Import from OrcaSlicer window', () => {
  it('offers the printers JusPrin does not have and marks the ones it does', async () => {
    const act = actions();
    render(<OrcaImport actions={act} state={state({
      printers: [{ name: 'Bambu Lab P1S 0.4 nozzle', already: false }, { name: 'Prusa MK4 0.4 nozzle', already: false },
        { name: 'Bambu Lab A1 mini 0.4 nozzle', already: true }],
      newPrinters: 2, filaments: ['Generic PLA @Qidi', 'Elegoo PETG'],
    })} />);
    expect(screen.getByText(/JusPrin found 2 printers in OrcaSlicer that it doesn’t have yet/)).toBeInTheDocument();
    const rows = within(screen.getByRole('list', { name: 'OrcaSlicer printers' })).getAllByRole('listitem');
    expect(rows.map((row) => row.textContent)).toEqual([
      'Bambu Lab P1S 0.4 nozzle', 'Prusa MK4 0.4 nozzle', 'Bambu Lab A1 mini 0.4 nozzleAlready in JusPrin',
    ]);
    expect(screen.getByText(`OrcaSlicer keeps these in ${folder}`)).toBeInTheDocument();
    // The new filaments come along, so the window names them.
    expect(within(screen.getByRole('list', { name: 'New filaments and print settings' })).getAllByRole('listitem')
      .map((row) => row.textContent)).toEqual(['Generic PLA @QidiNew filament', 'Elegoo PETGNew filament']);

    // Each new printer has a tick box, ticked; the button counts the ticked ones.
    expect(screen.getAllByRole('checkbox')).toHaveLength(2);
    await userEvent.click(screen.getByRole('checkbox', { name: 'Prusa MK4 0.4 nozzle' }));
    expect(screen.getByRole('button', { name: 'Import 1 printer' })).toBeEnabled();
    await userEvent.click(screen.getByRole('checkbox', { name: 'Bambu Lab P1S 0.4 nozzle' }));
    expect(screen.getByRole('button', { name: 'Import' })).toBeDisabled();
    await userEvent.click(screen.getByRole('checkbox', { name: 'Bambu Lab P1S 0.4 nozzle' }));

    await userEvent.click(screen.getByRole('button', { name: 'Choose another folder…' }));
    expect(act.chooseFolder).toHaveBeenCalledTimes(1);
    await userEvent.click(screen.getByRole('button', { name: 'Import 1 printer' }));
    // The import holds the app, so the window says it is working on it, and
    // asks only once that has been painted.
    expect(screen.getByRole('button', { name: 'Importing…' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Cancel' })).toBeDisabled();
    expect(screen.queryByRole('button', { name: 'Choose another folder…' })).toBeNull();
    expect(act.importPrinters).not.toHaveBeenCalled();
    await waitFor(() => expect(act.importPrinters).toHaveBeenCalledTimes(1));
    expect(act.importPrinters).toHaveBeenCalledWith(['Bambu Lab P1S 0.4 nozzle']);
  });

  it('says one printer in the singular', () => {
    render(<OrcaImport actions={actions()} state={state({ printers: [{ name: 'Voron', already: false }], newPrinters: 1 })} />);
    expect(screen.getByText(/JusPrin found 1 printer in OrcaSlicer that it doesn’t have yet\. When you import, JusPrin copies it with its filaments and print settings\. /))
      .toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Import 1 printer' })).toBeInTheDocument();
  });

  it('names the new filaments and print settings when every printer is already in JusPrin, and what it can’t import', () => {
    render(<OrcaImport actions={actions()} state={state({
      printers: [{ name: 'Voron', already: true }], newPrinters: 0,
      filaments: ['Generic PLA @Qidi', 'Elegoo PETG'], processes: ['0.16mm Fine @Voron'],
      skipped: [{ kind: 'filament', name: 'My Silk PLA', renamedTo: '', imported: false,
        reason: 'Not imported, because it’s based on Kingroon PLA Silk, which JusPrin doesn’t have. It stays in OrcaSlicer. When you prepare a print, choose a similar filament instead.' }],
    })} />);
    expect(screen.getByText('JusPrin found filaments and print settings in OrcaSlicer that it doesn’t have yet. When you import, JusPrin '
      + 'copies them and skips the item with the warning sign. OrcaSlicer keeps its own copy.')).toBeInTheDocument();
    expect(screen.getByText(`OrcaSlicer keeps these in ${folder}`)).toBeInTheDocument();
    expect(within(screen.getByRole('list', { name: 'New filaments and print settings' })).getAllByRole('listitem')
      .map((row) => row.textContent)).toEqual(['Generic PLA @QidiNew filament', 'Elegoo PETGNew filament', '0.16mm Fine @VoronNew print settings']);
    const skipped = within(screen.getByRole('list', { name: 'Can’t import' })).getAllByRole('listitem');
    expect(skipped.map((row) => row.textContent))
      .toEqual(['My Silk PLANot imported, because it’s based on Kingroon PLA Silk, which JusPrin doesn’t have. It stays in OrcaSlicer. When you prepare a print, choose a similar filament instead.']);
    expect(screen.getByRole('button', { name: 'Import' })).toBeEnabled();
  });

  it('says one new filament in the singular', () => {
    render(<OrcaImport actions={actions()} state={state({
      printers: [{ name: 'Voron', already: true }], newPrinters: 0, filaments: ['Elegoo PETG'],
    })} />);
    expect(screen.getByText('JusPrin found a filament in OrcaSlicer that it doesn’t have yet. When you import, JusPrin copies it. OrcaSlicer keeps its own copy.'))
      .toBeInTheDocument();
  });

  // Reported 2026-11-08: JusPrin 1's only new file was settings built on a
  // profile JusPrin doesn't have, and the window said it found something new.
  it('says there is nothing to import when what JusPrin doesn’t have can’t be imported, and says why', () => {
    render(<OrcaImport actions={actions()} state={state({
      app: 'JusPrin 1', screen: 'nothing_new', folder: '~/Library/Application Support/JusPrin',
      printers: [{ name: 'MyKlipper 0.4 nozzle - Copy', already: true }, { name: 'Qidi X-Plus 4 0.4 nozzle - Copy', already: true }],
      skipped: [{ kind: 'process', name: '0.20mm Detail @MK3S 0.6 - Copy', renamedTo: '', imported: false,
        reason: 'Not imported, because they’re based on 0.20mm Detail @MK3S 0.6, which JusPrin doesn’t have. They stay in JusPrin 1. When you prepare a print, choose similar print settings instead.' }],
    })} />);
    expect(screen.getByText('There is nothing to import. JusPrin already has everything in this JusPrin 1 folder except the item '
      + 'with the warning sign, which it can’t import.')).toBeInTheDocument();
    // JusPrin 1's folder is called JusPrin, so the line says whose it is.
    expect(screen.getByText('JusPrin 1 keeps these in ~/Library/Application Support/JusPrin')).toBeInTheDocument();
    expect(screen.queryByText(/doesn’t have yet/)).toBeNull();
    const skipped = within(screen.getByRole('list', { name: 'Can’t import' })).getAllByRole('listitem');
    expect(skipped.map((row) => row.textContent)).toEqual([
      '0.20mm Detail @MK3S 0.6 - CopyNot imported, because they’re based on 0.20mm Detail @MK3S 0.6, which JusPrin doesn’t have. They stay in JusPrin 1. When you prepare a print, choose similar print settings instead.',
    ]);
    expect(screen.queryByRole('checkbox')).toBeNull();
    expect(screen.getAllByRole('button').map((button) => button.textContent)).toEqual(['Choose another folder…', 'Close']);
  });

  it('counts the items it can’t import when there are several', () => {
    const line = (name: string) => ({ kind: 'filament' as const, name, renamedTo: '', imported: false, reason: 'Not imported. The file has no version.' });
    render(<OrcaImport actions={actions()} state={state({
      screen: 'nothing_new', printers: [{ name: 'Voron', already: true }], skipped: [line('Old PLA'), line('Old PETG')],
    })} />);
    expect(screen.getByText(/except the 2 items with the warning sign, which it can’t import\.$/)).toBeInTheDocument();
  });

  it('says there is nothing new, with only a way to another folder and Close', async () => {
    const act = actions();
    render(<OrcaImport actions={act} state={state({ screen: 'nothing_new', printers: [{ name: 'Voron', already: true }] })} />);
    expect(screen.getByText('Every printer in this OrcaSlicer folder is already in JusPrin, so there is nothing to import.')).toBeInTheDocument();
    expect(screen.getByText('Already in JusPrin')).toBeInTheDocument();
    expect(screen.getAllByRole('button').map((button) => button.textContent)).toEqual(['Choose another folder…', 'Close']);
    await userEvent.click(screen.getByRole('button', { name: 'Close' }));
    expect(act.close).toHaveBeenCalledTimes(1);
  });

  it('says where it looked when there is no folder, and asks for one', async () => {
    const act = actions();
    render(<OrcaImport actions={act} state={state({ screen: 'no_folder' })} />);
    expect(screen.getByText(/JusPrin didn’t find OrcaSlicer’s folder on this computer/)).toBeInTheDocument();
    expect(screen.getByText(`Looked in ${folder}`)).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Choose the folder…' }));
    expect(act.chooseFolder).toHaveBeenCalledTimes(1);
  });

  it('says a folder had no printers, and shows a refused pick', () => {
    render(<OrcaImport actions={actions()}
      state={state({ screen: 'no_printers', error: 'JusPrin found no OrcaSlicer printers in that folder.' })} />);
    expect(screen.getByText(/JusPrin found no OrcaSlicer printers in this folder/)).toBeInTheDocument();
    expect(screen.getByRole('alert')).toHaveTextContent('JusPrin found no OrcaSlicer printers in that folder.');
  });

  it('lists the printers imported, counts the rest, and keeps what wasn’t imported behind Show details', async () => {
    const act = actions();
    render(<OrcaImport actions={act} state={state({
      screen: 'imported',
      result: [
        { kind: 'printer', name: 'Bambu Lab P1S 0.4 nozzle', renamedTo: '', imported: true, reason: '' },
        { kind: 'printer', name: 'Shop X1C', renamedTo: 'Shop X1C (Orca copy)', imported: true, reason: '' },
        { kind: 'process', name: '0.20mm Detail @MK3S 0.6 - Copy', renamedTo: '', imported: false,
          reason: 'It’s based on 0.20mm Detail @MK3S 0.6, which JusPrin doesn’t have. When you prepare a print, choose similar print settings instead.' },
        { kind: 'filament', name: 'PLA', renamedTo: '', imported: true, reason: '' },
        { kind: 'filament', name: 'PETG', renamedTo: '', imported: true, reason: '' },
        { kind: 'process', name: '0.12mm Fine', renamedTo: '', imported: true, reason: '' },
      ],
    })} />);
    expect(screen.getByText('The 2 imported printers are now in the printer menu at the top of the window. '
      + 'The open project still uses the same printer as before. OrcaSlicer’s own files were not changed.')).toBeInTheDocument();
    // Only what was imported is in the box.
    const rows = within(screen.getByRole('list', { name: 'Imported' })).getAllByRole('listitem');
    expect(rows.map((row) => row.textContent)).toEqual([
      'Bambu Lab P1S 0.4 nozzleImported', 'Shop X1CImported as ‘Shop X1C (Orca copy)’',
    ]);
    expect(screen.getByText('Also imported: 2 filaments (PLA, PETG) and 1 print setting (0.12mm Fine).')).toBeInTheDocument();
    expect(screen.getByText('1 print setting wasn’t imported. It stays in OrcaSlicer.')).toBeInTheDocument();
    // The details are closed until asked for.
    const toggle = screen.getByRole('button', { name: 'Show details' });
    expect(toggle).toHaveAttribute('aria-expanded', 'false');
    expect(screen.queryByRole('list', { name: 'Not imported' })).toBeNull();
    expect(screen.queryByText('0.20mm Detail @MK3S 0.6 - Copy')).toBeNull();
    await userEvent.click(toggle);
    expect(toggle).toHaveTextContent('Hide details');
    expect(toggle).toHaveAttribute('aria-expanded', 'true');
    const details = screen.getByRole('list', { name: 'Not imported' });
    expect(toggle).toHaveAttribute('aria-controls', details.id);
    expect(within(details).getAllByRole('listitem').map((row) => row.textContent)).toEqual([
      '0.20mm Detail @MK3S 0.6 - CopyIt’s based on 0.20mm Detail @MK3S 0.6, which JusPrin doesn’t have. When you prepare a print, choose similar print settings instead.',
    ]);
    await userEvent.click(toggle);
    expect(toggle).toHaveTextContent('Show details');
    expect(screen.queryByRole('list', { name: 'Not imported' })).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Done' }));
    expect(act.close).toHaveBeenCalledTimes(1);
  });

  it('counts what wasn’t imported by kind, in one line', () => {
    const skipped = (kind: 'printer' | 'filament' | 'process', name: string) =>
      ({ kind, name, renamedTo: '', imported: false, reason: 'Not imported. The file has no version.' });
    const cases: [ReturnType<typeof skipped>[], string][] = [
      [[skipped('filament', 'A')], '1 filament wasn’t imported. It stays in OrcaSlicer.'],
      [[skipped('filament', 'A'), skipped('filament', 'B')], '2 filaments weren’t imported. They stay in OrcaSlicer.'],
      [[skipped('process', 'C'), skipped('filament', 'A')], '1 filament and 1 print setting weren’t imported. They stay in OrcaSlicer.'],
      [[skipped('process', 'C'), skipped('process', 'D'), skipped('filament', 'A'), skipped('printer', 'E')],
        '1 printer, 1 filament and 2 print settings weren’t imported. They stay in OrcaSlicer.'],
    ];
    for (const [result, line] of cases) {
      const { unmount } = render(<OrcaImport actions={actions()} state={state({ screen: 'imported', result })} />);
      expect(screen.getByText(line)).toBeInTheDocument();
      unmount();
    }
    // Everything imported: no line, nothing to show.
    render(<OrcaImport actions={actions()} state={state({ screen: 'imported', result: [
      { kind: 'printer', name: 'Voron', renamedTo: '', imported: true, reason: '' }] })} />);
    expect(screen.queryByText(/imported\. (It|They) stay/)).toBeNull();
    expect(screen.queryByRole('button', { name: 'Show details' })).toBeNull();
  });

  it('names a single imported printer by the name the menu shows', () => {
    render(<OrcaImport actions={actions()} state={state({ screen: 'imported', result: [
      { kind: 'printer', name: 'Voron 2.4 350', renamedTo: 'Voron 2.4 350 (Orca copy)', imported: true, reason: '' },
      { kind: 'printer', name: 'Old', renamedTo: '', imported: false, reason: 'Not imported. The file has no version.' },
    ] })} />);
    expect(screen.getByText('Voron 2.4 350 (Orca copy) is now in the printer menu at the top of the window. '
      + 'The open project still uses the same printer as before. OrcaSlicer’s own files were not changed.')).toBeInTheDocument();
  });

  it('says how to try again in the details, with Done as the only other button', async () => {
    render(<OrcaImport actions={actions()} state={state({ screen: 'imported', result: [
      { kind: 'printer', name: 'Old', renamedTo: '', imported: false, reason: 'JusPrin couldn’t read this file. To try again, choose File › Import › Import from OrcaSlicer…' },
    ] })} />);
    expect(screen.queryByRole('list', { name: 'Imported' })).toBeNull();
    expect(screen.getByText('1 printer wasn’t imported. It stays in OrcaSlicer.')).toBeInTheDocument();
    expect(screen.getAllByRole('button').map((button) => button.textContent)).toEqual(['Show details', 'Done']);
    await userEvent.click(screen.getByRole('button', { name: 'Show details' }));
    expect(screen.getByText('Old').closest('li')).toHaveTextContent('JusPrin couldn’t read this file. To try again, choose File › Import › Import from OrcaSlicer…');
  });

  it('names JusPrin 1 wherever it says OrcaSlicer, when it imports from JusPrin 1', () => {
    const { unmount } = render(<OrcaImport actions={actions()} state={state({ app: 'JusPrin 1',
      folder: '~/Library/Application Support/JusPrin', printers: [{ name: 'Voron', already: false }], newPrinters: 1 })} />);
    expect(screen.getByText(/JusPrin found 1 printer in JusPrin 1 that it doesn’t have yet\..* JusPrin 1 keeps its own copy\./))
      .toBeInTheDocument();
    expect(screen.getByRole('list', { name: 'JusPrin 1 printers' })).toBeInTheDocument();
    unmount();
    render(<OrcaImport actions={actions()} state={state({ app: 'JusPrin 1', screen: 'no_folder',
      folder: '~/Library/Application Support/JusPrin' })} />);
    expect(screen.getByText('JusPrin didn’t find JusPrin 1’s folder on this computer. If JusPrin 1 keeps its settings somewhere else, choose that folder.'))
      .toBeInTheDocument();
    expect(screen.queryByText(/OrcaSlicer/)).toBeNull();
  });
});
