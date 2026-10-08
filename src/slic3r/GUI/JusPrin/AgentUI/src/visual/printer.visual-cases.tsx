// The printer chat matrix (Figma 1572:2336): the panel's own cards, each from
// the block or connection record the host sends, and the close confirmation
// in the real printer panel.

import { screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PrinterBlock, StatePayload } from '../bridge/protocol';
import { PrinterBlockView, PrinterCredentialForm } from '../components/PrinterPanel';
import { mounted, mountedApp, VisualCase } from '../test/visual';
import { chatState, turn } from './states';

const noop = () => {};

const block = (overrides: Partial<PrinterBlock>): PrinterBlock =>
  ({ id: 'b-1', seq: 1, afterMessageId: 'm-1', kind: 'tip', ...overrides });

const card = (id: string, name: string, expects: string[], info: PrinterBlock): VisualCase => ({
  node: '1572:2336', matrix: 'Printer chat', id: `printer-${id}`, name, frame: 'card', expects,
  build: () => mounted(<PrinterBlockView block={info} onUndoAdd={noop} onInstallPlugin={noop} />),
});

const session = (): StatePayload => chatState({
  conversation: [turn('m-0', 'assistant', 'Which printer do you have?'), turn('m-1', 'user', 'bambu a1')],
  session: { mode: 'add', printerName: '', blocks: [], context: { printers: [], network: [] } },
});

export const printerCases: VisualCase[] = [
  card('added', 'Added receipt with Undo', ['Added:', 'Studio A1', '0.4 mm nozzle', 'Undo'],
    block({ kind: 'added', printer: { name: 'Studio A1', nozzle: 0.4 } })),
  card('removed', 'Removed receipt', ['Removed:', 'Workshop M4', 'Removed from your printers.'],
    block({ kind: 'added', removed: true, printer: { name: 'Workshop M4', nozzle: 0.4 } })),
  card('plugin', 'Plug-in required', ['Bambu plug-in needed', 'Install plug-in'], block({ kind: 'plugin' })),
  card('plugin-installed', 'Plug-in installed', ['Bambu plug-in installed'], block({ kind: 'plugin', installed: true })),
  card('discovery', 'Discovery · Results · online and offline', ['Found on your network', 'Online · DEMO-A1-2048', 'printer-row-offline', 'Offline'],
    block({ kind: 'network', printers: [
      { name: 'Studio A1', serial: 'DEMO-A1-2048', online: true },
      { name: 'Workshop M4', serial: 'DEMO-M4-0031', online: false },
    ] })),
  card('missing-image', 'Missing image candidate', ['printer-card-picture-empty', 'Bambu Lab A1 mini', '180 × 180 × 180 mm'],
    block({ kind: 'printers', printers: [{ catalogId: 'a1-mini', name: 'Bambu Lab A1 mini', buildVolume: '180 × 180 × 180 mm', picture: '' }] })),
  { node: '1572:2336', matrix: 'Printer chat', id: 'printer-credential', name: 'Local credential form', frame: 'card',
    expects: ['Connect to Studio A1', 'Access code', 'Stays on this computer.', 'Connect'],
    build: () => mounted(<PrinterCredentialForm request={{ actionId: 'c-1', target: 'Studio A1', provider: 'bambu' }} onConnect={noop} />) },
  // The same cards with the details the app does not send yet (model, brand,
  // percentage): made-up values in the optional slots, as Figma draws them.
  card('added-model', 'Added receipt · with model', ['Bambu A1 · 0.4 mm nozzle'],
    block({ kind: 'added', printer: { name: 'Studio A1', nozzle: 0.4, model: 'Bambu A1' } })),
  card('discovery-model', 'Discovery · Results · with model', ['Online · Bambu A1 · DEMO-A1-2048'],
    block({ kind: 'network', printers: [
      { name: 'Studio A1', serial: 'DEMO-A1-2048', online: true, model: 'Bambu A1' },
      { name: 'Workshop M4', serial: 'DEMO-M4-0031', online: false, model: 'Qidi M4' },
    ] })),
  card('candidate-brand', 'Candidate · brand above model', ['printer-card-brand', 'Bambu Lab', 'A1 mini'],
    block({ kind: 'printers', printers: [{ catalogId: 'a1-mini', name: 'Bambu Lab A1 mini', brand: 'Bambu Lab', model: 'A1 mini',
      buildVolume: '180 × 180 × 180 mm', picture: '' }] })),
  { node: '1572:2336', matrix: 'Printer chat', id: 'printer-close', name: 'Close conversation confirmation', frame: 'pane',
    expects: ['Close this conversation?', 'Close conversation', 'role="dialog"'],
    build: () => mountedApp(session(), { props: { printerPanel: true }, after: async () => {
      await userEvent.click(screen.getByRole('button', { name: /Back/ }));
    } }) },
];
