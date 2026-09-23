// What the printer panel tells the model, written from the facts the app
// sends in the session's context.

import { describe, expect, it } from 'vitest';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import type { PrinterSessionPayload } from './bridge/protocol';
import { printerInstructions } from './printerInstructions';
import { splitChoices } from './replyChoices';

function session(overrides: Partial<PrinterSessionPayload>): PrinterSessionPayload {
  return { mode: 'add', printerName: '', blocks: [], context: {}, ...overrides };
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
  printerName: 'Lab Printer',
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
      provider: 'bambu',
    },
  },
});

describe('the instructions', () => {
  it('carry every printer when adding, one line each', () => {
    const text = printerInstructions(add);
    expect(text).toContain('The person is adding a printer.');
    expect(text).toContain('BBL/Bambu Lab A1 mini | Bambu Lab A1 mini | 180 × 180 × 180 mm\n');
    // A size nobody measured is a question mark, not a blank.
    expect(text).toContain('Vendor/Unmeasured | Vendor Unmeasured | ?\n');
    expect(text).not.toContain('The printer this is about');
  });

  it('state the printer the conversation is about, as it is now', () => {
    const text = printerInstructions(change);
    expect(text).toContain('Name: Lab Printer\n');
    expect(text).toContain('Brand and model: Bambu Lab A1 mini\n');
    expect(text).toContain('Nozzle: 0.4 mm (this model ships 0.2, 0.4, 0.6 and 0.8 mm)');
    expect(text).toContain('\n- PLA Matte (PLA, #5f7d4f)\n- PETG (PETG)');
    expect(text).toContain('Connected to this app: no\n');
    expect(text).toContain('Connects through: Bambu Lab LAN mode');
    expect(text).not.toContain('Printer list');
  });

  it('name what is on the network', () => {
    const text = printerInstructions(session({ context: { printers: [], network: [{ name: 'Workshop', serial: '01P00A3B' }] } }));
    expect(text).toContain('On the network now: Workshop (01P00A3B)\n');
  });

  it('keep the rules that encode real constraints', () => {
    const text = printerInstructions(add);
    for (const rule of [
      'Start with just the printer model',
      'More than three fit',
      'is not one model',
      'never substitute a size',
      'alreadyYours means',
      'Never ask for a password, access code or API key in chat',
      '"connecting" means the app is still waiting',
      'a timeout means no response, not a wrong code',
      'nozzle mismatch reported by the printer',
      'call printer_setup_finish only when the person says they are done',
      'call the tool without asking again',
      'ask first and stop: that reply calls no tool',
    ])
      expect(text.toLowerCase()).toContain(rule.toLowerCase());
  });

  it('name no button, screen, form or phase of the panel', () => {
    for (const text of [printerInstructions(add), printerInstructions(change)])
      expect(text).not.toMatch(/button|connection form|the form above|phase|Add this printer|This one|Use this|Not this one/);
  });

  it('offer reply choices whose examples parse as the page parses them', () => {
    const examples = printerInstructions(add)
      .split('\n')
      .filter((line) => line.startsWith('Choices: '));
    expect(examples.map((line) => splitChoices(`Question?\n${line}`).choices)).toEqual([
      ['Yes, that is it', 'Different printer'],
      ['Prusa MK4', 'Prusa MK4S', 'Prusa MK4S HF'],
    ]);
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
