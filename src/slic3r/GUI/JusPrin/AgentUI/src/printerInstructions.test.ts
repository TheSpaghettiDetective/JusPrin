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
      'set up the connection manually in the app',
      'Do not claim to open settings for them',
      'browse the full list manually in the app',
      'If the person asks to see the full printer list',
      'do not recite the list in chat or claim to open it for them',
      'leave it for now, as the printer can prepare prints without a connection',
      'nozzle mismatch reported by the printer',
      'when someone says what they loaded there is nothing to save',
      'call the tool without asking again',
      'ask first and stop: that reply calls no tool',
      // Measured: without the exact words, the offer came in 27-77% of replies
      // and its choices were mostly a bare "Yes | No".
      'end the reply to a successful printer_add with exactly "Want to connect it so you can send prints straight to it?" and then "Choices: Connect it | Not now"',
      'Do not add a Choices line',
      'it is safe to close this chat now',
      'after the person turns down connecting, after the app says the connection is verified, after they leave connecting for now, and after a successful printer_change',
      'they can close this chat now',
      'now slices for 0.6 mm.") and say they can close this chat now',
      'Never after printer_connect comes back cancelled: nothing failed and nothing was declined',
    ])
      expect(text.toLowerCase()).toContain(rule.toLowerCase());
  });

  it('offer no dialog or finish tool in any printer conversation', () => {
    for (const mode of ['add', 'change', 'connect'] as const) {
      const text = printerInstructions(session({ ...change, mode }));
      for (const removed of ['printer_manual_setup', 'printer_manual_connection', 'printer_setup_finish', 'Choices: Done'])
        expect(text).not.toContain(removed);
    }
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

  it('say which settings the tools change and where the rest are, for a printer the person has', () => {
    const connect = session({ ...change, mode: 'connect' });
    for (const text of [printerInstructions(change), printerInstructions(connect)]) {
      expect(text).toContain('read or change its other settings, such as start g-code');
      expect(text).toContain('is in its printer settings on this computer');
      expect(text).toContain('they can change it manually in the app; do not claim to open them');
      expect(text).toContain('Never give a reason you cannot help that the tools and the facts below do not state');
      expect(text).toContain('Their printer is already set up so they can prepare prints for it');
      // Measured: without it, "add my other printer too" got "Yes, I can add
      // it", which the app then refuses.
      expect(text).toContain('Another printer is not added here: say it is added with + Add printer on Home.');
    }
  });

  it('send the settings rules only to a session about a printer, and say a shipped profile is saved as a copy', () => {
    const connect = session({ ...change, mode: 'connect' });
    for (const text of [printerInstructions(change), printerInstructions(connect)]) {
      expect(text).toContain('Rules for changing its settings');
      expect(text).toContain('scope "printer" and target {"preset": the printer\'s name below}');
      expect(text).toContain('pass persistAs set to the printer\'s Name below, exactly as written there, not its brand and model');
      expect(text).toContain('when the facts below give a copy to save as, pass that name instead');
      expect(text).toContain('When a preview says read_only_preset');
      expect(text).not.toContain('Kind: the settings OrcaSlicer comes with');
    }
    expect(printerInstructions(add)).not.toContain('Rules for changing its settings');
    const stock = session({ ...change, context: { printer: { ...change.context.printer!, stock: true, copyName: 'Lab Printer - Copy' } } });
    expect(printerInstructions(stock)).toContain(
      'Kind: the settings OrcaSlicer comes with for this model, selected in the project, not a printer the person added.');
    expect(printerInstructions(stock)).toContain('Copy to save as: Lab Printer - Copy\n');
    expect(printerInstructions(change)).not.toContain('Copy to save as');
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
