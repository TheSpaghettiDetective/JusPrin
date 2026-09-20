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

  // F4 review fix on printer-panel-review-fixes-handoff.md: the receipt
  // used to survive every later `state`, so a nozzle changed through its
  // own Change link kept reading the printer's old, now-wrong facts. The
  // host sends `state` (leading the column with the added printer, for
  // that one push) then `printer_added` right after, so the receipt
  // arrives one envelope behind the reorder it describes -- and lives no
  // longer than that reorder does.
  it('shows the receipt after its state and printer_added arrive, in order', () => {
    let s = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [printer] }),
    });
    s = reduce(s, {
      kind: 'envelope',
      envelope: envelope('printer_added', {
        name: 'Bambu Lab A1 mini',
        nozzleText: '0.4 mm',
        nozzleAssumed: false,
        plateText: 'Textured PEI Plate',
        plateAssumed: true,
        filamentText: 'Bambu PLA Basic',
        filamentAssumed: true,
      }),
    });
    expect(s.addedPrinter?.name).toBe('Bambu Lab A1 mini');
  });

  it('clears the receipt on the state push after the one it rode in on', () => {
    let s = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [printer] }),
    });
    s = reduce(s, {
      kind: 'envelope',
      envelope: envelope('printer_added', {
        name: 'Bambu Lab A1 mini',
        nozzleText: '0.4 mm',
        nozzleAssumed: false,
        plateText: 'Textured PEI Plate',
        plateAssumed: true,
        filamentText: 'Bambu PLA Basic',
        filamentAssumed: true,
      }),
    });
    // Any reason at all -- a rename, a refusal, a plain state_request -- the
    // reorder is gone by this same push, so the receipt cannot outlive it.
    s = reduce(s, {
      kind: 'envelope',
      envelope: envelope('state', { appearance: 'light', projects: [], printers: [printer] }),
    });
    expect(s.addedPrinter).toBeUndefined();
  });

  it('lets the person dismiss the receipt early, before any refresh', () => {
    const withReceipt = reduce(initialState, {
      kind: 'envelope',
      envelope: envelope('printer_added', {
        name: 'X1 Carbon',
        nozzleText: '',
        nozzleAssumed: false,
        plateText: '',
        plateAssumed: false,
        filamentText: '',
        filamentAssumed: false,
      }),
    });
    expect(withReceipt.addedPrinter).toBeDefined();
    expect(reduce(withReceipt, { kind: 'dismiss_added_printer' }).addedPrinter).toBeUndefined();
  });
});
