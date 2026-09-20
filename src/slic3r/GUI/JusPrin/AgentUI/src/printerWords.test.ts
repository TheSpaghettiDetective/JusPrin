// The printer panel's words, made from the facts the app sends: what the
// person reads, and the notes the model reads after a tap.

import { describe, expect, it } from 'vitest';
import type { NetworkPrinterInfo, PrinterCardInfo, PrinterFacts, PrinterSessionPayload } from './bridge/protocol';
import {
  cardSubline,
  changeTitle,
  chosenNote,
  factTexts,
  networkNote,
  numberText,
  opening,
  rejectedNote,
  sizesText,
  spoolSummary,
  undoneNote,
  undoText,
  workingText,
} from './printerWords';

const facts: PrinterFacts = {
  printer: { name: '', provenance: 'settled' },
  nozzle: { size: 0, provenance: 'settled' },
  plate: { name: '', provenance: 'settled' },
  filament: { preset: '', ams: '', spools: [], provenance: 'settled' },
};

function session(overrides: Partial<PrinterSessionPayload>): PrinterSessionPayload {
  return { mode: 'add', facts, blocks: [], canAdd: false, ...overrides };
}

function card(overrides: Partial<PrinterCardInfo> = {}): PrinterCardInfo {
  return {
    catalogId: 'Prusa/Prusa MK3S',
    deviceId: '',
    name: 'Prusa MK3S',
    buildVolume: '250 × 210 × 210 mm',
    picture: '',
    action: 'add',
    assumed: { nozzle: 0.4, plate: '', filament: 'Prusa Generic PLA' },
    ...overrides,
  };
}

// The words that would make a note an order rather than a record.
function expectStatement(note: string) {
  for (const order of ['printer_', 'Propose', 'propose', 'Find ', 'call ', 'Call ', 'Say ', 'say that', 'Ask ']) {
    expect(note).not.toContain(order);
  }
}

describe('numbers', () => {
  it('spells sizes as the printer profiles do', () => {
    expect(numberText(0.4)).toBe('0.4');
    expect(numberText(0.25)).toBe('0.25');
    expect(numberText(1)).toBe('1.0');
    expect(sizesText([0.25, 0.4, 0.6, 0.8])).toBe('0.25, 0.4, 0.6 and 0.8 mm');
  });

  it('sums up what is loaded on one line', () => {
    const spools = [
      { name: 'PLA Matte', material: 'PLA' },
      { name: '', material: 'PETG' },
    ];
    expect(spoolSummary('AMS lite', spools)).toBe('AMS lite · PLA Matte + 1');
    expect(spoolSummary('', spools.slice(1))).toBe('PETG');
    expect(spoolSummary('AMS', [])).toBe('AMS');
  });
});

describe('the pinned card', () => {
  it('states nothing for a fact nobody has said', () => {
    const text = factTexts(facts);
    expect([text.printer.value, text.nozzle.value, text.plate.value, text.filament.value]).toEqual(['', '', '', '']);
  });

  it('states what is loaded, or else the filament the card assumes', () => {
    const loaded = factTexts({
      ...facts,
      nozzle: { size: 0.6, provenance: 'changed' },
      filament: {
        preset: 'Bambu PLA Basic @BBL A1M',
        ams: 'AMS lite',
        spools: [{ name: 'PLA Matte', material: 'PLA', colour: '#5f7d4f' }],
        provenance: 'settled',
      },
    });
    expect(loaded.nozzle).toEqual({ value: '0.6 mm', provenance: 'changed' });
    expect(loaded.filament).toEqual({ value: 'AMS lite · PLA Matte', provenance: 'settled', swatch: '#5f7d4f' });

    const assumed = factTexts({ ...facts, filament: { ...facts.filament, preset: 'Prusa Generic PLA', provenance: 'assumed' } });
    expect(assumed.filament.value).toBe('Prusa Generic PLA');
    expect(assumed.filament.swatch).toBeUndefined();
  });
});

describe('the opening', () => {
  // F1 review fix: a network-found printer connects *now*, with Add, not
  // "later" -- so the opener makes no promise about when, for either path.
  it('asks what printer it is when adding, after saying what adding does, promising nothing about connecting', () => {
    expect(opening(session({}))).toBe(
      "I'll add your printer so your projects slice for it. " +
        'What printer do you have? Say it any way: "bambu a1 mini", "the ender with the touchscreen", "not sure, the small one".',
    );
    expect(opening(session({}))).not.toContain('connect');
  });

  it('names the printer when changing one', () => {
    const change = session({
      mode: 'change',
      context: { printer: { name: 'Lab Printer', model: '', nozzle: 0.4, nozzles: [], spools: [], connected: false } },
    });
    expect(opening(change)).toMatch(/^This is the Lab Printer\. Tell me what changed on it/);
    expect(opening(session({ mode: 'change' }))).toMatch(/^This is the this printer\./);
  });
});

// F8 review fix: this used to be hard-coded in MessageList.tsx to Add's own
// tool (printer_identify looks through a list); Change's only tool
// (printer_change) does not, so it needs its own words.
describe('the tool-only turn', () => {
  it('names Add\'s own tool, and Change\'s own, differently', () => {
    expect(workingText('add')).toBe('Looking through the printer list…');
    expect(workingText('change')).not.toBe(workingText('add'));
    expect(workingText('change')).not.toContain('printer list');
  });
});

describe('the cards', () => {
  it('state the size, or what a network printer reported', () => {
    expect(cardSubline(card())).toBe('250 × 210 × 210 mm');
    expect(
      cardSubline(card({ device: { nozzle: 0.4, ams: 'AMS lite', spools: [{ name: 'PLA Matte', material: 'PLA' }], reported: true } })),
    ).toBe('0.4 mm nozzle · AMS lite · PLA Matte · read from the printer just now');
    expect(cardSubline(card({ device: { nozzle: 0.4, ams: '', spools: [], reported: false } }))).toBe('0.4 mm nozzle');
  });

  it('name the change on the undo row and the change card', () => {
    expect(undoText({ id: 'u', seq: 1, afterMessageId: '', kind: 'undo', changed: { nozzle: { before: 0.4, after: 0.6 } } })).toBe(
      'Nozzle set to 0.6 mm',
    );
    expect(
      undoText({
        id: 'u',
        seq: 1,
        afterMessageId: '',
        kind: 'undo',
        changed: { nozzle: { before: 0.4, after: 0.6 }, spools: { before: [], after: [] } },
      }),
    ).toBe('Nozzle set to 0.6 mm, spools updated');
    expect(changeTitle(true, true)).toBe('Change nozzle and spools');
    expect(changeTitle(false, true)).toBe('Change spools');
  });
});

describe('the notes the model reads after a tap', () => {
  it('records a refusal', () => {
    const note = rejectedNote('Bambu Lab A1 mini');
    expect(note).toBe('The person said Bambu Lab A1 mini is not their printer.');
    expectStatement(note);
  });

  it('records the printer chosen and what its card assumes, including what the profile leaves out', () => {
    const noPlate = chosenNote(card());
    expect(noPlate).toBe(
      'The person chose Prusa MK3S. Its card offers Add, assuming a 0.4 mm nozzle and Prusa Generic PLA; the profile names no plate.',
    );
    expectStatement(noPlate);
    expect(
      chosenNote(
        card({ name: 'Bambu Lab A1 mini', assumed: { nozzle: 0.6, plate: 'Textured PEI Plate', filament: 'Bambu PLA Basic @BBL A1M' } }),
      ),
    ).toBe(
      'The person chose Bambu Lab A1 mini. Its card offers Add, assuming a 0.6 mm nozzle, the Textured PEI Plate and Bambu PLA Basic @BBL A1M.',
    );
    expect(chosenNote(card({ assumed: { nozzle: 0.8, plate: '', filament: '' } }))).toBe(
      'The person chose Prusa MK3S. Its card offers Add, assuming a 0.8 mm nozzle; the profile names no plate; the profile names no filament.',
    );
  });

  it('records a network printer as the app matched it, and nothing when it did not', () => {
    const found: NetworkPrinterInfo = { deviceId: '01P00A3B', name: 'A1', serial: '01P00A3B', online: true };
    const reported = networkNote({ ...found, match: { name: 'Bambu Lab A1 mini', nozzle: 0.4, reported: true } });
    expect(reported).toBe('The person chose the network printer 01P00A3B, a Bambu Lab A1 mini that reports a 0.4 mm nozzle.');
    expectStatement(reported);
    expect(networkNote({ ...found, match: { name: 'Bambu Lab A1 mini', nozzle: 0.4, reported: false } })).toBe(
      'The person chose the network printer 01P00A3B, a Bambu Lab A1 mini; it did not report its nozzle, so its card assumes 0.4 mm.',
    );
    expect(networkNote(found)).toBe('');
  });

  it('records what an undo put back', () => {
    const block = {
      id: 'u',
      seq: 1,
      afterMessageId: '',
      kind: 'undo' as const,
      changed: {
        nozzle: { before: 0.4, after: 0.6 },
        spools: { before: [{ name: 'PLA Matte', material: 'PLA' }, { name: '', material: 'PETG' }], after: [] },
      },
    };
    const note = undoneNote(block);
    expect(note).toBe('The person undid the change: the nozzle is 0.4 mm again and the spools are PLA Matte and PETG again.');
    expectStatement(note);
    expect(undoneNote({ ...block, changed: { spools: { before: [], after: block.changed.spools.before } } })).toBe(
      'The person undid the change: the spools are none again.',
    );
  });
});
