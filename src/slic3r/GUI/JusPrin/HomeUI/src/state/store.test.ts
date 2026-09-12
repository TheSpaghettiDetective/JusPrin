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
});
