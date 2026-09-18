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

  // WP10, F10: Kenneth decided -- confirm before discarding a conversation
  // that holds anything the person typed or sent. The opening line alone
  // (every fixture above closes with no prompt) is not "anything".
  const withContent = () =>
    state({
      conversation: [
        { id: 'm-1', role: 'assistant', state: 'complete', text: 'What printer do you have?', seq: 1 },
        { id: 'm-2', role: 'user', state: 'complete', text: 'bambu a1 mini', attempt: 1 },
      ] as StatePayload['conversation'],
    });

  it('asks before leaving a conversation that holds something, and stays on Stay', async () => {
    const host = open(withContent());
    await userEvent.click(screen.getByRole('button', { name: 'Back to printers' }));
    expect(screen.getByRole('dialog', { name: 'Leave this conversation?' })).toBeInTheDocument();
    expect(screen.getByText('What you’ve told me about this printer will be lost.')).toBeInTheDocument();
    expect(host.lastOfType('printer_action')).toBeUndefined();

    await userEvent.click(screen.getByRole('button', { name: 'Stay' }));
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(host.lastOfType('printer_action')).toBeUndefined();
  });

  it('leaves for real once the person taps Leave', async () => {
    const host = open(withContent());
    await userEvent.click(screen.getByRole('button', { name: 'Back to printers' }));
    await userEvent.click(screen.getByRole('button', { name: 'Leave' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  });

  it('asks before Add mode’s "Set it up myself" leaves for the wizard, with content', async () => {
    const host = open(
      state({
        conversation: withContent().conversation,
        session: session({
          browse: { level: 'models', vendorId: 'BBL', vendorName: 'Bambu Lab', models: [] },
        }),
      }),
    );
    await userEvent.click(screen.getByRole('button', { name: /Set it up myself/ }));
    expect(host.lastOfType('printer_action')).toBeUndefined();
    expect(screen.getByRole('dialog', { name: 'Leave this conversation?' })).toBeInTheDocument();

    await userEvent.click(screen.getByRole('button', { name: 'Leave' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'manual_setup' });
  });

  it('never asks for a Change session’s own "Set it up myself", which stays in this session', async () => {
    const host = open({ ...withContent(), session: session({ mode: 'change', caption: 'PRINTER' }) });
    await userEvent.click(screen.getByRole('button', { name: 'Set it up myself' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'manual_setup' });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  });

  it('never asks to browse, which stays in this session', async () => {
    const host = open(withContent());
    await userEvent.click(screen.getByRole('button', { name: 'Browse the full list' }));
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'browse' });
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

  // WP8, F4: the panel always seeds an opening line (PrinterPanel::open
  // posts it before the page can render anything), so "no messages yet"
  // can never gate this the way it does in the docked chat panel.
  it('offers to set up the agent instead of a disabled composer, with no agent configured', () => {
    open(
      state({
        agent: { status: 'unavailable' },
        session: session({
          blocks: [
            { id: 'b1', seq: 1, afterMessageId: '', kind: 'tip' },
            {
              id: 'b2',
              seq: 2,
              afterMessageId: '',
              kind: 'network',
              printers: [{ deviceId: '01P00A3B', name: 'Bambu Lab A1 mini', serial: '01P00A3B', online: true }],
            },
          ],
        }),
      }),
    );

    expect(screen.getByText('No agent connected')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Set up the agent' })).toBeInTheDocument();
    expect(screen.queryByPlaceholderText('e.g. "I put a 0.6 nozzle on it"')).not.toBeInTheDocument();
    // What needs no agent stays live beside the offer.
    expect(screen.getByText('FOUND ON YOUR NETWORK')).toBeInTheDocument();
    expect(screen.getByText('Bambu Lab A1 mini')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Browse the full list' })).toBeInTheDocument();
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

  // A turn that only called a tool stays empty once the stream ends too --
  // the model's own words about what it found land in the NEXT turn, after
  // it sees the tool's result. "Looking through…" is gone the moment the
  // stream settles (it names in-progress work, not a finished empty turn),
  // so this turn draws nothing at all rather than a bare avatar disc.
  it('draws nothing for a tool-only turn once its stream ends, still empty', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'x1c', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'complete', text: '', attempt: 1 },
        ],
        streamingMessageId: null,
      }),
    );

    expect(screen.queryByText('Looking through the printer list…')).not.toBeInTheDocument();
    expect(document.querySelector('.agent-avatar')).toBeNull();
  });

  // An empty-text turn is only ever hidden when it truly has nothing to
  // show -- a failed request still needs its "Retry" (WP11's "Try again").
  it('still shows a failed empty turn, with its Retry', async () => {
    const host = open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'x1c', attempt: 1 },
          {
            id: 'm-2',
            role: 'assistant',
            state: 'failed',
            text: '',
            attempt: 1,
            error: { code: 'agent_unavailable', message: 'The Agent service could not start this request.', retryable: true },
          },
        ],
        streamingMessageId: null,
      }),
    );

    expect(screen.getByText('The Agent service could not start this request.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Retry' }));
    expect(host.lastOfType('retry_message')!.payload).toEqual({ messageId: 'm-2' });
  });

  it('follows the session the host sends after the state', () => {
    const host = open();
    host.deliver('printer_session', session({ caption: 'PRINTER', mode: 'change', chips: [] }));
    expect(screen.getByText('PRINTER')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
  });
});
