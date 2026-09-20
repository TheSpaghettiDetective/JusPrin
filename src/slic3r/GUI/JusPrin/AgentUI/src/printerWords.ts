// Every word the printer panel shows the person, and the notes it records for
// the model after a tap. The app sends facts -- sizes, spools, which printer,
// what changed -- and the words are made here. The model's instructions are
// in printerInstructions.ts.

import type {
  NetworkPrinterInfo,
  PrinterBlock,
  PrinterCardInfo,
  PrinterFacts,
  PrinterSessionPayload,
  PrinterSpoolInfo,
} from './bridge/protocol';

// -- Numbers and lists --------------------------------------------------------

// "0.4", "0.25", "1.0": two decimals, trailing zeros dropped down to one, as
// the printer profiles spell nozzle sizes.
export function numberText(value: number): string {
  let text = value.toFixed(2);
  while (text.length > 3 && text.endsWith('0')) text = text.slice(0, -1);
  return text;
}

export function mm(value: number): string {
  return `${numberText(value)} mm`;
}

// "a, b and c"
export function listed(items: string[]): string {
  return items.map((item, index) => (index === 0 ? '' : index + 1 === items.length ? ' and ' : ', ') + item).join('');
}

// "0.2, 0.4, 0.6 and 0.8 mm"
export function sizesText(nozzles: number[]): string {
  return `${listed(nozzles.map(numberText))} mm`;
}

function spoolName(spool: PrinterSpoolInfo): string {
  return spool.name || spool.material;
}

// "PLA Matte and PETG", or "none"
function spoolsText(spools: PrinterSpoolInfo[]): string {
  return spools.length === 0 ? 'none' : listed(spools.map(spoolName));
}

// What a printer has loaded, as one line: "AMS lite · PLA Matte + 3".
export function spoolSummary(ams: string, spools: PrinterSpoolInfo[]): string {
  if (spools.length === 0) return ams;
  const first = spoolName(spools[0]) + (spools.length > 1 ? ` + ${spools.length - 1}` : '');
  return ams ? `${ams} · ${first}` : first;
}

// -- The panel's fixed words --------------------------------------------------

export function caption(session: PrinterSessionPayload): string {
  return session.mode === 'add' ? 'NEW PRINTER' : 'PRINTER';
}

// A likely answer, rather than a standing invitation.
export function placeholder(session: PrinterSessionPayload): string {
  return session.mode === 'add'
    ? 'e.g. "bambu a1 mini" or "not sure, the small one"'
    : 'e.g. "I put a 0.6 nozzle on it" or "loaded black PETG"';
}

export const ADD_LABEL = 'Add this printer';
export const REJECT_LABEL = 'Not this one';

// What stands in for a tool-only turn while it still has no words: which
// tool the mode's own instructions let the model call. Add's only tool
// looks through the printer list; Change's only tool works out and applies
// a change, so it does not name a list that mode never shows.
export function workingText(mode: PrinterSessionPayload['mode']): string {
  return mode === 'add' ? 'Looking through the printer list…' : 'Working on it…';
}

// The panel's own first line, which the model is shown as having said: the
// same promise every time the panel opens.
export function opening(session: PrinterSessionPayload): string {
  if (session.mode === 'change') {
    const name = session.context?.printer?.name || session.facts.printer.name || 'this printer';
    return `This is the ${name}. Tell me what changed on it, or ask anything about it: nozzle, plate, spools. A photo of the part works too.`;
  }
  return (
    "I'll add your printer so your projects slice for it. " +
    'What printer do you have? Say it any way: "bambu a1 mini", "the ender with the touchscreen", "not sure, the small one".'
  );
}

// -- The pinned card ----------------------------------------------------------

export interface FactText {
  value: string; // empty: nobody has said yet
  provenance: PrinterFacts['printer']['provenance'];
  swatch?: string;
}

export function factTexts(facts: PrinterFacts): { printer: FactText; nozzle: FactText; plate: FactText; filament: FactText } {
  const loaded = facts.filament.spools;
  return {
    printer: { value: facts.printer.name, provenance: facts.printer.provenance },
    nozzle: { value: facts.nozzle.size > 0 ? mm(facts.nozzle.size) : '', provenance: facts.nozzle.provenance },
    plate: { value: facts.plate.name, provenance: facts.plate.provenance },
    filament: {
      value: loaded.length > 0 || facts.filament.ams ? spoolSummary(facts.filament.ams, loaded) : facts.filament.preset,
      provenance: facts.filament.provenance,
      swatch: loaded[0]?.colour || undefined,
    },
  };
}

// -- The cards in the thread --------------------------------------------------

// Under a printer's name: what the printer reported, or its size.
export function cardSubline(card: PrinterCardInfo): string {
  const device = card.device;
  if (!device) return card.buildVolume;
  let text = `${mm(device.nozzle)} nozzle`;
  if (device.spools.length > 0) text += ` · ${spoolSummary(device.ams, device.spools)}`;
  if (device.reported) text += ' · read from the printer just now';
  return text;
}

// The undo row: "Nozzle set to 0.6 mm", "Spools updated", or both.
export function undoText(block: PrinterBlock): string {
  const parts: string[] = [];
  if (block.changed?.nozzle) parts.push(`Nozzle set to ${mm(block.changed.nozzle.after)}`);
  if (block.changed?.spools) parts.push(parts.length > 0 ? 'spools updated' : 'Spools updated');
  return parts.join(', ');
}

// The change card's title.
export function changeTitle(nozzle: boolean, spools: boolean): string {
  return nozzle && spools ? 'Change nozzle and spools' : nozzle ? 'Change nozzle' : 'Change spools';
}

// -- What the model is told after a tap ---------------------------------------
// Statements of what the person did, never instructions. The app posts them
// only when the tap goes through as described.

export function rejectedNote(name: string): string {
  return `The person said ${name} is not their printer.`;
}

export function chosenNote(card: PrinterCardInfo): string {
  const assumed = [`a ${mm(card.assumed.nozzle)} nozzle`];
  if (card.assumed.plate) assumed.push(`the ${card.assumed.plate}`);
  if (card.assumed.filament) assumed.push(card.assumed.filament);
  let text = `The person chose ${card.name}. Its card offers Add, assuming ${listed(assumed)}`;
  if (!card.assumed.plate) text += '; the profile names no plate';
  if (!card.assumed.filament) text += '; the profile names no filament';
  return `${text}.`;
}

// For a network printer the app matched to a model in the list.
export function networkNote(printer: NetworkPrinterInfo): string {
  const match = printer.match;
  if (!match) return '';
  const start = `The person chose the network printer ${printer.serial}, a ${match.name}`;
  return match.reported
    ? `${start} that reports a ${mm(match.nozzle)} nozzle.`
    : `${start}; it did not report its nozzle, so its card assumes ${mm(match.nozzle)}.`;
}

export function undoneNote(block: PrinterBlock): string {
  const back: string[] = [];
  if (block.changed?.nozzle) back.push(`the nozzle is ${mm(block.changed.nozzle.before)} again`);
  if (block.changed?.spools) back.push(`the spools are ${spoolsText(block.changed.spools.before)} again`);
  return `The person undid the change: ${listed(back)}.`;
}

export const ACCESS_CODE_NOTE = 'An access code was entered.';
