// The printer panel's fixed words: its opening line and the composer's
// placeholder. Everything else the panel says, the model says. The model's
// instructions are in printerInstructions.ts.

import type { PrinterSessionPayload } from './bridge/protocol';

// "0.4", "0.25", "1.0": two decimals, trailing zeros dropped down to one, as
// the printer profiles spell nozzle sizes.
export function numberText(value: number): string {
  let text = value.toFixed(2);
  while (text.length > 3 && text.endsWith('0')) text = text.slice(0, -1);
  return text;
}

// "0.2, 0.4, 0.6 and 0.8 mm"
export function sizesText(nozzles: number[]): string {
  const sizes = nozzles.map(numberText);
  return `${sizes.map((size, index) => (index === 0 ? '' : index + 1 === sizes.length ? ' and ' : ', ') + size).join('')} mm`;
}

// A likely answer, rather than a standing invitation.
export function placeholder(session: PrinterSessionPayload): string {
  if (session.mode === 'connect') return 'Ask anything about connecting it';
  return session.mode === 'add'
    ? 'e.g. "bambu a1 mini" or "not sure, the small one"'
    : 'e.g. "I put a 0.6 nozzle on it" or "loaded black PETG"';
}

// The panel's own first line, which the model is shown as having said. A
// trailing "Choices:" line is drawn as reply chips, as the model's are.
export function opening(session: PrinterSessionPayload): string {
  const printer = session.context.printer;
  const name = printer?.name || session.printerName || 'this printer';
  if (session.mode === 'connect')
    return printer?.provider === 'bambu'
      ? `Let’s connect ${name}. Is it turned on, in LAN mode, and on the same network as this computer?\nChoices: Yes, it is | How do I check?`
      : `Let’s connect ${name}. What address do you use to open it in a browser? It’s usually its IP address, like 192.168.1.20.`;
  if (session.mode === 'change') return `This is the ${name}. Tell me what changed on it, or ask anything about it.`;
  return (
    'Which printer do you have? Tell me the brand and model, or share a photo of its label. ' +
    "Not sure? Tell me what you know, and I'll help you find it."
  );
}
