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
      connected: true,
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
    expect(text).toContain('Loaded, as the printer reports it:\n- PLA Matte (PLA, #5f7d4f)\n- PETG (PETG)');
    expect(text).toContain('Connected to this app: yes\n');
    expect(text).toContain('Connects through: Bambu Lab LAN mode');
    expect(text).not.toContain('Printer list');
    expect(text).not.toContain('network plug-in');
  });

  it('state that the printer cannot be reached without the plug-in, when the app says so', () => {
    const printer = { ...change.context.printer!, needsNetworkPlugin: true };
    const text = printerInstructions({ ...change, context: { ...change.context, printer } });
    expect(text).toContain("Bambu's network plug-in: not installed, so nothing can reach this printer yet.");
    expect(text).not.toMatch(/button/);
  });

  it('claim nothing is loaded on a printer that is not connected', () => {
    const offline = session({ ...change, context: { printer: { ...change.context.printer!, spools: [], connected: false } } });
    const text = printerInstructions(offline);
    expect(text).toContain('Loaded, as the printer reports it: not known, as it is not connected\n');
    expect(text).toContain('Connected to this app: no\n');
  });

  it('say so when a connected printer reports nothing loaded', () => {
    const empty = session({ ...change, context: { printer: { ...change.context.printer!, spools: [] } } });
    expect(printerInstructions(empty)).toContain('Loaded, as the printer reports it: nothing\n');
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
      'undid adding a printer, that printer is removed',
      'Do not add the same model again unless they ask for it',
      'Never ask for a password, access code or API key in chat',
      '"connecting" means the app is still waiting',
      'a timeout means no response, not a wrong code',
      'that you are checking and how long it can take',
      'answer anything the person says meanwhile',
      'name the three ways forward:',
      'try again with the same address',
      '(printer_manual_connection)',
      'leave it for now, as the printer can prepare prints without a connection',
      'nozzle mismatch reported by the printer',
      'when someone says what they loaded there is nothing to save',
      'call printer_setup_finish only when the person says they are done',
      'call the tool without asking again',
      'ask first and stop: that reply calls no tool',
      // Measured: without the exact words, the offer came in 27-77% of replies
      // and its choices were mostly a bare "Yes | No".
      'end the reply to a successful printer_add with exactly "Want to connect it so you can send prints straight to it?" and then "Choices: Connect it | Not now"',
      // Measured: as a general rule alone, Done came in as few as 11 of 20
      // replies after leaving connecting and 15 of 20 after a change, whose
      // own rules said how those replies end; pinned there too, 20 of 20.
      'Done, below, is the only one offered alone',
      'end the reply with the line "Choices: Done", the one choice offered alone',
      'after the person turns down connecting, after the app says the connection is verified, after they leave connecting for now, and after a successful printer_change',
      'is there for later, and end with "Choices: Done"',
      'now slices for 0.6 mm.") and end with "Choices: Done"',
      // Measured: without it, the reply to a card cancelled by writing
      // offered Done in 11 of 20; with it, 1 of 40.
      'Never after printer_connect comes back cancelled: nothing failed and nothing was declined',
    ])
      expect(text.toLowerCase()).toContain(rule.toLowerCase());
  });

  it('send the rules for finding and adding a printer only while adding one', () => {
    const connect = session({ ...change, mode: 'connect' });
    for (const text of [printerInstructions(change), printerInstructions(connect)]) {
      expect(text).not.toContain('Rules for finding the printer');
      expect(text).not.toContain('printer_add');
      expect(text).toContain('Rules for changing a printer');
      expect(text).toContain('Rules for connecting a printer');
      expect(text).toContain('Rules for finishing');
    }
    expect(printerInstructions(add)).toContain('Rules for finding the printer');
  });

  it('keep the rule for a refused nozzle size in every session, since printer_change refuses one too', () => {
    const connect = session({ ...change, mode: 'connect' });
    for (const text of [printerInstructions(add), printerInstructions(change), printerInstructions(connect)])
      expect(text.match(/After unknown_nozzle, name the sizes supported/g)).toHaveLength(1);
  });

  it('say that the printer a change or a connection is about is already set up', () => {
    // Left to "Connected to this app: no", a change session offered to add
    // the printer it was about (2026-09-28).
    const connect = session({ ...change, mode: 'connect' });
    for (const text of [printerInstructions(change), printerInstructions(connect)])
      expect(text).toContain('This printer is already set up in this app: it can prepare prints now, connected or not.');
    expect(printerInstructions(add)).not.toContain('already set up in this app');
  });

  it('say where the settings no tool reaches are, for a printer the person has', () => {
    const connect = session({ ...change, mode: 'connect' });
    for (const text of [printerInstructions(change), printerInstructions(connect)]) {
      expect(text).toContain('is in its printer settings on this computer, which printer_manual_connection opens');
      expect(text).toContain('Never give a reason you cannot help that the tools and the facts below do not state');
      expect(text).toContain('Their printer is already set up so they can prepare prints for it');
    }
  });

  it('send adding a printer the goal it was measured with, and none of the other sessions\' words', () => {
    // Measured: the scope rule, or a goal naming all three sessions, made the
    // model ask about a printer named plainly instead of adding it.
    const text = printerInstructions(add);
    expect(text).toContain('The goal is that their printer is set up so they can prepare prints for it');
    expect(text).not.toContain('What you can do here');
    expect(text).not.toContain('already set up');
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
      ['Done'],
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
