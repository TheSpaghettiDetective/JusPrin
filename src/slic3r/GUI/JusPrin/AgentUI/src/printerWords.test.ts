// The printer panel's fixed words: its opening and the composer's hint.

import { describe, expect, it } from 'vitest';
import type { PrinterSessionPayload } from './bridge/protocol';
import { numberText, opening, placeholder, sizesText } from './printerWords';
import { splitChoices } from './replyChoices';

function session(overrides: Partial<PrinterSessionPayload>): PrinterSessionPayload {
  return { mode: 'add', printerName: '', blocks: [], context: {}, ...overrides };
}

const printer = { name: 'Workshop', model: 'Bambu Lab P1S', nozzle: 0.4, nozzles: [0.4], spools: [], connected: false };

describe('numbers', () => {
  it('spells sizes as the printer profiles do', () => {
    expect(numberText(0.4)).toBe('0.4');
    expect(numberText(0.25)).toBe('0.25');
    expect(numberText(1)).toBe('1.0');
    expect(sizesText([0.2, 0.4, 0.6])).toBe('0.2, 0.4 and 0.6 mm');
  });
});

describe('the opening', () => {
  it('starts with only the model and helps someone who is unsure', () => {
    const text = opening(session({}));
    expect(text).toMatch(/^Which printer do you have\? /);
    expect(text).not.toMatch(/nozzle|plate|filament/i);
  });

  it('names the printer when changing one', () => {
    expect(opening(session({ mode: 'change', printerName: 'Lab Printer' }))).toMatch(/^This is the Lab Printer\. /);
  });

  it('asks a Bambu Lab printer about LAN mode, with answers to tap', () => {
    const { body, choices } = splitChoices(opening(session({ mode: 'connect', context: { printer: { ...printer, provider: 'bambu' } } })));
    expect(body).toMatch(/^Let’s connect Workshop\. .*LAN mode/);
    expect(choices).toEqual(['Yes, it is', 'How do I check?']);
  });

  it('asks a Moonraker or OctoPrint printer for its address, with nothing to tap', () => {
    const text = opening(session({ mode: 'connect', context: { printer: { ...printer, provider: 'host' } } }));
    expect(text).toContain('What address do you use to open it in a browser?');
    expect(splitChoices(text).choices).toEqual([]);
  });

  it('never advertises a form or a button', () => {
    for (const mode of ['add', 'change', 'connect'] as const) {
      const text = opening(session({ mode, context: { printer: { ...printer, provider: 'bambu' } } }));
      expect(text).not.toMatch(/form|button|tap /i);
    }
  });
});

describe('the placeholder', () => {
  it('suggests a likely answer for each question', () => {
    expect(placeholder(session({}))).toContain('bambu a1 mini');
    expect(placeholder(session({ mode: 'change' }))).toContain('0.6 nozzle');
    expect(placeholder(session({ mode: 'connect' }))).toBe('Ask anything about connecting it');
  });
});
