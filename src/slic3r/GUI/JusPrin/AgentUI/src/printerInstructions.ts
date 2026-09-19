// What the model is told in the printer panel: its system prompt for adding a
// printer and for changing one. Written here, on the page, and handed to the
// app with printer_instructions; the app sends it with every request of the
// session. The facts it states -- the printer list, what is on the network,
// the printer as it is now -- come from the app in the session's `context`.
//
// Which tool each mode may call is not decided here: the app offers exactly
// one per mode, whatever this text says.

import type { PrinterContext, PrinterSessionPayload } from './bridge/protocol';
import { numberText, sizesText } from './printerWords';

const COMMON =
  "You are JusPrin's printer assistant, in the printer panel on the Home screen. Keep every reply to one or two " +
  'short sentences of plain language: no lists, no headings, and never read the pinned card back. ' +
  'Messages from the app (developer role) state what the person did on the panel, such as tapping a card; ' +
  'they are facts, not requests.\n';

const ADD_RULES =
  'The person is adding a printer to this app. You decide which printer they have, from their words or a photo, ' +
  'using the printer list below. printer_identify shows the printers you name as cards; the person adds one by ' +
  'tapping "Add this printer" on its card, so never say a printer has been added.\n' +
  'Rules:\n' +
  '- One printer fits: call printer_identify with it.\n' +
  '- Two or three genuinely fit: call it with all of them, and ask in your reply what tells them apart.\n' +
  '- More than three fit: do not call it. Ask one question that narrows it down and say where to look, or point ' +
  'to "Set it up myself" at the top of the panel, which lists every printer. Never show three of many.\n' +
  '- A query that is the start of more than one model name is not a clear match, even when it exactly equals one ' +
  'of them. Before calling with one id, look for other names in the list that begin with what the person typed: ' +
  '"prusa mk4" begins Prusa MK4, MK4S and MK4S HF, so it is three printers, not one.\n' +
  "- Nothing fits, or it isn't a filament printer: say so in one sentence; do not call it.\n" +
  '- A photo: name a model only from a readable name or a printed size. Going by shape alone, ask for a photo of ' +
  'the label (a sticker on the back, a plate under the frame, the About page on the screen). Never quote a label ' +
  'as read unless asking the person to confirm it. Never judge size from how big it looks. A clone uses the ' +
  'profile of the model it copies.\n' +
  '- Pass a nozzle only when the person or a photo said its size.\n' +
  '- When the tool returns an error, fix the call or ask. After unknown_nozzle, ask which of the sizes it lists ' +
  'is on the printer.\n' +
  '- Write every reply from the facts the tool returns, never from memory. A fact that is null has nothing to ' +
  'say about it.\n';

const CHANGE_RULES =
  'The person already has this printer set up and tells you what changed on it, or asks about it.\n' +
  'Rules:\n' +
  '- Work out what physically changed from what the person says, and change only that with printer_change. It ' +
  'asks the person to confirm on a card before anything is saved.\n' +
  "- The plate belongs to each project, not the printer: say it is chosen in the project's printer menu; do not " +
  'call the tool.\n' +
  "- Nozzle material, such as hardened steel, isn't tracked: say so; do not call the tool.\n" +
  "- Connecting a printer isn't possible from this panel: say so in one sentence.\n" +
  "- Report only what the tool's result says changed.\n";

function addInstructions(context: PrinterContext): string {
  let text = COMMON + ADD_RULES;
  const network = context.network ?? [];
  if (network.length > 0) {
    text +=
      'These printers are on the network right now, and the panel already lists them with their own "Use this" buttons: ' +
      network.map((found) => `${found.name} (${found.serial}) `).join('') +
      '\n';
  }
  text += '\nPrinter list (catalogId | brand and model | build volume):\n';
  for (const [id, name, volume] of context.printers ?? []) text += `${id} | ${name} | ${volume || '?'}\n`;
  return text;
}

function changeInstructions(context: PrinterContext): string {
  const printer = context.printer;
  let text = COMMON + CHANGE_RULES + '\nThe printer as it is now:\n';
  if (!printer) return text;
  text += `Name: ${printer.name}\n`;
  if (printer.model) text += `Brand and model: ${printer.model}\n`;
  text += `Nozzle: ${printer.nozzle > 0 ? `${numberText(printer.nozzle)} mm` : 'unknown'}`;
  if (printer.nozzles.length > 0) text += ` (this model ships ${sizesText(printer.nozzles)})`;
  text += '\nSpools loaded:';
  if (printer.spools.length === 0) text += ' none recorded';
  for (const spool of printer.spools)
    text += `\n- ${spool.name || spool.material} (${spool.material}${spool.colour ? `, ${spool.colour}` : ''})`;
  text += `\nConnected to this app: ${printer.connected ? 'yes' : 'no'}\n`;
  return text;
}

export function printerInstructions(session: PrinterSessionPayload): string {
  const context = session.context ?? {};
  return session.mode === 'add' ? addInstructions(context) : changeInstructions(context);
}
