import { describe, expect, it } from 'vitest';
import { initialState, reduce } from './store';
import type { Envelope, PrinterInfo, ProjectInfo } from '../bridge/protocol';

function envelope(type: string, payload: unknown): Envelope {
  return { protocol: 'jusprin-home-bridge', version: 1, id: 'h-1', type, payload };
}

const project: ProjectInfo = {
  id: 'p1',
  name: 'Garage bracket',
  path: 'C:/projects/garage.3mf',
  status: { kind: 'unknown', text: 'Edited yesterday' },
};

const printer: PrinterInfo = {
  id: 'x1',
  name: 'X1 Carbon',
  kind: 'named',
  canOpenSettings: true,
  canRename: true,
  canRemove: true,
  state: 'printing',
  spools: [],
  canLaunchMonitor: true,
  justAdded: false,
};

describe('the Home reducer', () => {
  it('is not loaded until the host sends state', () => {
    expect(initialState.loaded).toBe(false);
    const connected = reduce(initialState, { kind: 'connection', state: 'connected' });
    expect(connected.loaded).toBe(false);
    expect(connected.connection).toBe('connected');
  });

  // An empty gallery and "the host has not answered yet" are different
  // screens, so state must set loaded even when both lists are empty.
  it('loads an empty screen as loaded', () => {
    const state = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [] }),
    });
    expect(state.loaded).toBe(true);
    expect(state.projects).toEqual([]);
  });

  it('takes projects, printers, and appearance from state', () => {
    const state = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'dark', projects: [project], printers: [printer] }),
    });
    expect(state.appearance).toBe('dark');
    expect(state.projects).toEqual([project]);
    expect(state.printers).toEqual([printer]);
  });

  it('replaces each list on its own update', () => {
    const loaded = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [project], printers: [printer] }),
    });
    const noPrinters = reduce(loaded, { kind: 'envelope', envelope: envelope('printers', { printers: [] }) });
    expect(noPrinters.printers).toEqual([]);
    expect(noPrinters.projects).toEqual([project]);
  });

  it('follows a later appearance change', () => {
    const dark = reduce(initialState, { kind: 'envelope', envelope: envelope('appearance', { appearance: 'dark' }) });
    expect(dark.appearance).toBe('dark');
  });

  it('keeps what it has when the host sends a type it does not know', () => {
    const loaded = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [project], printers: [] }),
    });
    expect(reduce(loaded, { kind: 'envelope', envelope: envelope('invented_later', {}) })).toBe(loaded);
  });

  it('records a bridge error and clears it on the next state', () => {
    const failed = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('bridge_error', { message: 'The host could not read the recent list.' }),
    });
    expect(failed.error).toBe('The host could not read the recent list.');
    const recovered = reduce(failed, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [] }),
    });
    expect(recovered.error).toBeUndefined();
  });

  // The host sends a fresh state right after a refusal, so a state must not
  // wipe the reason before the person has read it.
  it('keeps a printer refusal through the following state until the next printer action', () => {
    const refused = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('printer_error', { id: 'named:Garage', message: 'That name is reserved.' }),
    });
    const refreshed = reduce(refused, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [printer] }),
    });
    expect(refreshed.printerError).toEqual({ id: 'named:Garage', message: 'That name is reserved.' });
    expect(reduce(refreshed, { kind: 'printer_action' }).printerError).toBeUndefined();
  });

  // WP6: the strip stays up through every state the host sends after the
  // one that carried it, until the person dismisses it themselves.
  it('keeps a printer receipt through later states until dismissed', () => {
    const receipt = { name: 'Bambu Lab A1 mini', nozzle: '0.4 mm', plate: 'Textured PEI Plate', filament: 'PLA', assumed: true };
    const added = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [printer], printerReceipt: receipt }),
    });
    expect(added.printerReceipt).toEqual(receipt);

    const refreshed = reduce(added, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [printer] }),
    });
    expect(refreshed.printerReceipt).toEqual(receipt);
    expect(reduce(refreshed, { kind: 'dismiss_printer_receipt' }).printerReceipt).toBeUndefined();
  });

  // Adding a second printer while the first's strip is still up replaces it
  // rather than stacking a second one.
  it('replaces an undismissed receipt with a later one', () => {
    const first = { name: 'Bambu Lab A1 mini', nozzle: '0.4 mm', plate: 'Textured PEI Plate', filament: 'PLA', assumed: true };
    const second = { name: 'Ender-3 V2', nozzle: '0.4 mm', plate: 'Cool Plate', filament: 'PLA', assumed: true };
    const added = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [], printerReceipt: first }),
    });
    const addedAgain = reduce(added, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [], printerReceipt: second }),
    });
    expect(addedAgain.printerReceipt).toEqual(second);
  });
});
