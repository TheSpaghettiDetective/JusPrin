// What the Import from OrcaSlicer window says on each of its screens, and
// which shell_action each of its buttons asks for.

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
  return { kind: 'orca_import', screen: 'found', folder, error: '', ...overrides };
}

describe('the Import from OrcaSlicer window', () => {
  it('offers the printers JusPrin does not have and marks the ones it does', async () => {
    const act = actions();
    render(<OrcaImport actions={act} state={state({
      printers: [{ name: 'Bambu Lab P1S 0.4 nozzle', already: false }, { name: 'Prusa MK4 0.4 nozzle', already: false },
        { name: 'Bambu Lab A1 mini 0.4 nozzle', already: true }],
      newPrinters: 2, newFilaments: 14,
    })} />);
    expect(screen.getByText(/JusPrin found 2 printers in OrcaSlicer that it doesn’t have yet/)).toBeInTheDocument();
    const rows = within(screen.getByRole('list', { name: 'OrcaSlicer printers' })).getAllByRole('listitem');
    expect(rows.map((row) => row.textContent)).toEqual([
      'Bambu Lab P1S 0.4 nozzle', 'Prusa MK4 0.4 nozzle', 'Bambu Lab A1 mini 0.4 nozzleAlready in JusPrin',
    ]);
    expect(screen.getByText(folder)).toBeInTheDocument();

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
    expect(screen.getByText(/JusPrin found 1 printer in OrcaSlicer that it doesn’t have yet\. JusPrin copies it with its filaments/))
      .toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Import 1 printer' })).toBeInTheDocument();
  });

  it('offers new filaments and settings when every printer is already in JusPrin', () => {
    render(<OrcaImport actions={actions()} state={state({ printers: [{ name: 'Voron', already: true }], newPrinters: 0, newFilaments: 2 })} />);
    expect(screen.getByText(/JusPrin found filaments or settings in OrcaSlicer/)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Import' })).toBeInTheDocument();
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

  it('lists what became of each, with the filaments as a count, and keeps the project printer', async () => {
    const act = actions();
    render(<OrcaImport actions={act} state={state({
      screen: 'imported',
      result: [
        { kind: 'printer', name: 'Bambu Lab P1S 0.4 nozzle', renamedTo: '', imported: true, reason: '' },
        { kind: 'printer', name: 'Shop X1C', renamedTo: 'Shop X1C (Orca copy)', imported: true, reason: '' },
        { kind: 'process', name: 'Old', renamedTo: '', imported: false, reason: 'Not imported. The file has no version.' },
        { kind: 'filament', name: 'PLA', renamedTo: '', imported: true, reason: '' },
        { kind: 'filament', name: 'PETG', renamedTo: '', imported: true, reason: '' },
      ],
    })} />);
    expect(screen.getByText('They are in the printer menu. The printer this project uses didn’t change.')).toBeInTheDocument();
    const rows = within(screen.getByRole('list', { name: 'Imported' })).getAllByRole('listitem');
    expect(rows.map((row) => row.textContent)).toEqual([
      'Bambu Lab P1S 0.4 nozzleImported', 'Shop X1CImported as ‘Shop X1C (Orca copy)’', 'OldNot imported. The file has no version.',
    ]);
    expect(screen.getByText('2 filaments imported: PLA, PETG.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Done' }));
    expect(act.close).toHaveBeenCalledTimes(1);
  });

  it('says how to try again on the line itself, with Done as the only button', () => {
    render(<OrcaImport actions={actions()} state={state({ screen: 'imported', result: [
      { kind: 'printer', name: 'Old', renamedTo: '', imported: false, reason: 'JusPrin couldn’t read this file. To try again, choose File › Import › Import from OrcaSlicer…' },
    ] })} />);
    expect(screen.getByText('Old').closest('li')).toHaveTextContent('JusPrin couldn’t read this file. To try again, choose File › Import › Import from OrcaSlicer…');
    expect(screen.getAllByRole('button').map((button) => button.textContent)).toEqual(['Done']);
  });
});
