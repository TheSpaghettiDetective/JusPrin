// The page in printer-panel mode: the same thread and composer as the docked
// panel, with the printer session's header, pinned card and chips around
// them. What it sends back is a typed printer action or an ordinary message.

import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, it } from 'vitest';
import { App } from './App';
import { PROTOCOL_NAME, PROTOCOL_VERSION } from './bridge/protocol';
import type { Envelope, PrinterSessionPayload, StatePayload, WorkspaceContext } from './bridge/protocol';

const context: WorkspaceContext = {
  sessionId: '1',
  revision: 1,
  projectName: 'Garage bracket',
  printer: { preset: 'Bambu Lab A1 mini 0.4 nozzle', process: 'Standard', plateType: 'Textured PEI Plate' },
  filaments: [],
  plates: [],
  presetDeltas: [],
} as unknown as WorkspaceContext;

class MockHost {
  received: Envelope[] = [];

  transport = {
    post: (json: string) => {
      this.received.push(JSON.parse(json));
    },
  };

  deliver(type: string, payload: unknown): void {
    act(() => {
      window.__jusprinBridge!.deliver({
        protocol: PROTOCOL_NAME,
        version: PROTOCOL_VERSION,
        id: `h-${this.received.length}-${type}`,
        type,
        payload,
      } as Envelope);
    });
  }

  lastOfType(type: string): Envelope | undefined {
    return [...this.received].reverse().find((envelope) => envelope.type === type);
  }
}

function session(overrides: Partial<PrinterSessionPayload> = {}): PrinterSessionPayload {
  return {
    mode: 'add',
    caption: 'NEW PRINTER',
    facts: {
      printer: { value: 'Bambu Lab A1 mini', provenance: 'settled' },
      nozzle: { value: '0.4 mm', provenance: 'assumed' },
      plate: { value: 'Textured PEI Plate', provenance: 'assumed' },
      filament: { value: 'PLA', provenance: 'assumed' },
    },
    blocks: [],
    chips: [],
    chipHint: '',
    placeholder: 'e.g. "I put a 0.6 nozzle on it"',
    browse: null,
    ...overrides,
  };
}

function state(overrides: Partial<StatePayload> = {}): StatePayload {
  return {
    agent: { status: 'ready' },
    appearance: 'light',
    conversations: [{ id: 'conv-1', title: 'Printer', createdAt: 't' }],
    activeConversationId: 'conv-1',
    conversation: [{ id: 'm-1', role: 'assistant', state: 'complete', text: 'What printer do you have?', seq: 1 }],
    streamingMessageId: null,
    toolActivities: [],
    builds: [],
    exportedCopies: [],
    physicalPrints: [],
    draft: '',
    context,
    session: session(),
    ...overrides,
  } as unknown as StatePayload;
}

function open(payload: StatePayload = state()): MockHost {
  const host = new MockHost();
  render(<App getTransport={() => host.transport} printerPanel />);
  host.deliver('hello_ack', { version: PROTOCOL_VERSION });
  host.deliver('state', payload);
  return host;
}

describe('the printer panel page', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  it('names where back leads, not the printer it is about', async () => {
    const host = open();
    expect(screen.getByRole('heading', { name: 'Printers' })).toBeInTheDocument();

    await userEvent.click(screen.getByRole('button', { name: 'Back to printers' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
  });

  it('opens the in-panel browse list from the header, in Add mode', async () => {
    const host = open();
    await userEvent.click(screen.getByRole('button', { name: 'Browse the full list' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'browse' });
  });

  it('keeps the way out to OrcaSlicer’s own screens, in Change mode', async () => {
    const host = open(state({ session: session({ mode: 'change', caption: 'PRINTER' }) }));
    await userEvent.click(screen.getByRole('button', { name: 'Set it up myself' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'manual_setup' });
  });

  it('pins the printer’s four facts above the thread', () => {
    open();
    expect(screen.getByText('NEW PRINTER')).toBeInTheDocument();
    expect(screen.getByText(/Bambu Lab A1 mini/)).toBeInTheDocument();
    expect(screen.getByText(/0\.4 mm · assumed/)).toBeInTheDocument();
    // The project's own setup card belongs to the other panel.
    expect(screen.queryByTestId('current-setup')).toBeNull();
  });

  it('shows the agent’s opening in the thread', () => {
    open();
    expect(screen.getByText('What printer do you have?')).toBeInTheDocument();
  });

  it('asks a likely answer, and leads the composer with Photo', () => {
    open();
    expect(screen.getByPlaceholderText('e.g. "I put a 0.6 nozzle on it"')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Add a photo' })).toBeInTheDocument();
  });

  // WP7: a chip is the person's own tap, which stands in for the approval
  // card the same change would otherwise need. Free typing carries no such
  // claim, so the host must be able to tell the two apart on the wire.
  it('marks a chip\'s message preApproved, and a typed one not', async () => {
    const host = open(
      state({
        session: session({
          chips: [{ id: 's1', label: 'Use 0.6 mm nozzle', style: 'suggested', say: 'Use 0.6 mm nozzle', opensPhotoPicker: false }],
        }),
      }),
    );

    await userEvent.click(screen.getByRole('button', { name: 'Use 0.6 mm nozzle' }));
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'Use 0.6 mm nozzle', preApproved: true });

    await userEvent.type(screen.getByPlaceholderText('e.g. "I put a 0.6 nozzle on it"'), 'never mind, keep 0.4{enter}');
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ preApproved: false });
  });

  it('adds the printer from its own card, and rejects it as a typed action, not a chat message', async () => {
    const host = open(
      state({
        session: session({
          blocks: [
            {
              id: 'b1',
              seq: 1,
              afterMessageId: 'm-1',
              kind: 'printers',
              live: true,
              printers: [
                {
                  catalogId: 'BBL/Bambu Lab A1 mini',
                  deviceId: '',
                  vendor: 'Bambu Lab',
                  model: 'A1 mini',
                  subline: '180 × 180 × 180 mm',
                  picture: '',
                  action: 'add',
                },
              ],
            },
          ],
        }),
      }),
    );

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'add' });

    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'reject', id: 'BBL/Bambu Lab A1 mini' });
  });

  it('draws the cards the agent drew, under the message that drew them', () => {
    open(
      state({
        session: session({
          blocks: [
            {
              id: 'b1',
              seq: 1,
              afterMessageId: 'm-1',
              kind: 'printers',
              live: true,
              printers: [
                {
                  catalogId: 'BBL/Bambu Lab A1 mini',
                  deviceId: '',
                  vendor: 'Bambu Lab',
                  model: 'A1 mini',
                  subline: '180 × 180 × 180 mm',
                  picture: '',
                  action: 'add',
                },
              ],
            },
          ],
        }),
      }),
    );

    expect(screen.getByText('180 × 180 × 180 mm')).toBeInTheDocument();
  });

  it('stages a photo as a small thumbnail inside the composer, not a wide chip', () => {
    const host = open();
    host.deliver('attachment_updated', {
      attachment: {
        id: 'a-1',
        name: 'printer.jpg',
        kind: 'image',
        mime: 'image/jpeg',
        sizeBytes: 4,
        source: 'picker',
        state: 'staged',
        previewDataUrl: 'data:image/jpeg;base64,AAAA',
      },
    });

    expect(screen.getByText('printer.jpg')).toBeInTheDocument();
    expect(screen.getByText('add a note, or just send')).toBeInTheDocument();
    expect(screen.getByAltText('')).toHaveClass('composer-staged-thumb');
  });

  it('sends a photo as the message itself, with a caption from any words added', () => {
    open(
      state({
        conversation: [
          { id: 'm-2', role: 'user', state: 'complete', text: "it's this one", attempt: 1, attachments: ['a-1'] },
        ],
        attachments: [
          {
            id: 'a-1',
            name: 'printer.jpg',
            kind: 'image',
            mime: 'image/jpeg',
            sizeBytes: 4,
            source: 'picker',
            state: 'sent',
            previewDataUrl: 'data:image/jpeg;base64,AAAA',
          },
        ],
      }),
    );

    const photo = screen.getByAltText('printer.jpg');
    expect(photo).toHaveClass('message-photo');
    expect(screen.getByText("it's this one")).toHaveClass('message-photo-caption');
  });

  it('draws the brand list when browsing, and filters it by typing', async () => {
    open(
      state({
        session: session({
          browse: { level: 'vendors', vendors: [
            { id: 'BBL', name: 'Bambu Lab', count: 5 },
            { id: 'Creality', name: 'Creality', count: 41 },
          ] },
        }),
      }),
    );

    expect(screen.getByText('Bambu Lab')).toBeInTheDocument();
    expect(screen.getByText('Creality')).toBeInTheDocument();

    await userEvent.type(screen.getByPlaceholderText('Filter brands'), 'bambu');
    expect(screen.getByText('Bambu Lab')).toBeInTheDocument();
    expect(screen.queryByText('Creality')).toBeNull();
  });

  it('opens a brand, picks a model, and asks the agent to propose it', async () => {
    const host = open(
      state({
        session: session({
          browse: {
            level: 'models',
            vendorId: 'BBL',
            vendorName: 'Bambu Lab',
            models: [{ catalogId: 'BBL/Bambu Lab A1 mini', model: 'A1 mini', subline: '180 × 180 × 180 mm', picture: '' }],
          },
        }),
      }),
    );

    await userEvent.click(screen.getByRole('button', { name: /A1 mini/ }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({
      action: 'browse_pick',
      id: 'BBL/Bambu Lab A1 mini',
    });

    // The way out for a printer that is not on the list at all.
    expect(screen.getByText('Set it up myself')).toBeInTheDocument();
  });

  it('names the work instead of an empty bubble while a tool runs', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'x1c', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'streaming', text: '', attempt: 1 },
        ],
        streamingMessageId: 'm-2',
      }),
    );

    expect(screen.getByText('Looking through the printer list…')).toBeInTheDocument();
    expect(document.querySelector('.agent-avatar')).toBeNull();
  });

  it('follows the session the host sends after the state', () => {
    const host = open();
    host.deliver('printer_session', session({ caption: 'PRINTER', mode: 'change', chips: [] }));
    expect(screen.getByText('PRINTER')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
  });
});
