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
    chips: [{ id: 'add', label: 'Add this printer', style: 'primary', action: 'add' }],
    chipHint: '',
    placeholder: 'e.g. "I put a 0.6 nozzle on it"',
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

  it('keeps the way out to OrcaSlicer’s own screens', async () => {
    const host = open();
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

  it('adds the printer natively and says an ordinary chip as the person', async () => {
    const host = open(
      state({
        session: session({
          chips: [
            { id: 'add', label: 'Add this printer', style: 'primary', action: 'add' },
            { id: 'no', label: 'Not this one', style: 'plain', say: 'Not this one' },
          ],
        }),
      }),
    );

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'add' });

    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'Not this one' });
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
              printers: [
                {
                  catalogId: 'BBL/Bambu Lab A1 mini',
                  deviceId: '',
                  name: 'Bambu Lab A1 mini',
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

  it('follows the session the host sends after the state', () => {
    const host = open();
    host.deliver('printer_session', session({ caption: 'PRINTER', mode: 'change', chips: [] }));
    expect(screen.getByText('PRINTER')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
  });
});
