// What the printer panel tells the model, written from the facts the app
// sends in the session's context.

import { describe, expect, it } from 'vitest';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import type { PrinterSessionPayload } from './bridge/protocol';
import { printerInstructions } from './printerInstructions';

const empty = { value: '', provenance: 'settled' as const };

function session(overrides: Partial<PrinterSessionPayload>): PrinterSessionPayload {
  return {
    mode: 'add',
    caption: 'NEW PRINTER',
    facts: { printer: empty, nozzle: empty, plate: empty, filament: empty },
    blocks: [],
    chips: [],
    placeholder: '',
    ...overrides,
  };
}

const add = session({
  context: {
    printers: [
      ['BBL/Bambu Lab A1 mini', 'Bambu Lab A1 mini', '180 × 180 × 180 mm'],
      ['Prusa/Prusa MK3S', 'Prusa MK3S', '250 × 210 × 210 mm'],
      ['Vendor/Unmeasured', 'Vendor Unmeasured', ''],
    ],
    network: [],
  },
});

const change = session({
  mode: 'change',
  caption: 'PRINTER',
  context: {
    printer: {
      name: 'Lab Printer',
      model: 'Bambu Lab A1 mini',
      nozzle: 0.4,
      nozzles: [0.2, 0.4, 0.6, 0.8],
      spools: [
        { name: 'PLA Matte', material: 'PLA', colour: '#5f7d4f' },
        { name: 'PETG', material: 'PETG' },
      ],
      connected: false,
    },
  },
});

describe('the Add instructions', () => {
  it('carry the rules and every printer, one line each', () => {
    const text = printerInstructions(add);
    expect(text).toContain('BBL/Bambu Lab A1 mini | Bambu Lab A1 mini | 180 × 180 × 180 mm\n');
    expect(text).toContain('Prusa/Prusa MK3S | Prusa MK3S | 250 × 210 × 210 mm\n');
    // A size nobody measured is a question mark, not a blank.
    expect(text).toContain('Vendor/Unmeasured | Vendor Unmeasured | ?\n');
    expect(text).toContain('More than three fit: do not call it');
    expect(text).toContain('Set it up myself');
    expect(text).toContain('never say a printer has been added');
    expect(text).not.toContain('printer_suggest');
    expect(text).not.toContain('The printer as it is now');
  });

  it('name what is on the network, which the panel already lists', () => {
    const text = printerInstructions(
      session({ context: { printers: [], network: [{ name: 'Bambu Lab A1 mini', serial: '01P00A3B' }] } }),
    );
    expect(text).toContain('with their own "Use this" buttons: Bambu Lab A1 mini (01P00A3B) \n');
  });
});

describe('the Change instructions', () => {
  it('state the printer as it is now', () => {
    const text = printerInstructions(change);
    expect(text).toContain('Name: Lab Printer\n');
    expect(text).toContain('Brand and model: Bambu Lab A1 mini\n');
    expect(text).toContain('Nozzle: 0.4 mm (this model ships 0.2, 0.4, 0.6 and 0.8 mm)');
    expect(text).toContain('\n- PLA Matte (PLA, #5f7d4f)\n- PETG (PETG)');
    expect(text).toContain('Connected to this app: no\n');
    expect(text).toContain('The plate belongs to each project');
    expect(text).toContain('Nozzle material');
    expect(text).not.toContain('Printer list');
  });

  it('spell sizes as the profiles do, and say when nothing is loaded', () => {
    const text = printerInstructions(
      session({
        mode: 'change',
        context: {
          printer: { name: 'Big', model: '', nozzle: 1, nozzles: [0.25, 1], spools: [], connected: true },
        },
      }),
    );
    expect(text).toContain('Nozzle: 1.0 mm (this model ships 0.25 and 1.0 mm)');
    expect(text).toContain('Spools loaded: none recorded');
    expect(text).toContain('Connected to this app: yes');
    expect(text).not.toContain('Brand and model');
  });
});

// Not a check: fills in the instructions of the requests the app wrote for
// the evaluation (agent_bridge_tests "[.printer-eval]"), so it replays
// exactly what this page sends. Run with PRINTER_EVAL_OUT=<dir>.
describe('printer evaluation writer', () => {
  it('writes the instructions into the evaluation requests', () => {
    const out = process.env.PRINTER_EVAL_OUT;
    if (!out) return;
    for (const mode of ['add', 'change']) {
      const state = JSON.parse(readFileSync(join(out, `${mode}_session.json`), 'utf8')) as PrinterSessionPayload;
      const request = JSON.parse(readFileSync(join(out, `${mode}_request.json`), 'utf8'));
      request.instructions = printerInstructions(state);
      writeFileSync(join(out, `${mode}_request.json`), JSON.stringify(request, null, 2));
    }
  });
});
