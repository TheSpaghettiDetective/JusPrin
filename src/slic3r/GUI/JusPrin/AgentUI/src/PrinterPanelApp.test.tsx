// The page in printer-panel mode: the same thread and composer as the docked
// panel, with the printer session's header, pinned card and chips around
// them. What it sends back is a typed printer action, a tool decision, or
// what the person typed -- never a tap dressed up as the person's words.

import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, it } from 'vitest';
import { App } from './App';
import { PROTOCOL_NAME, PROTOCOL_VERSION } from './bridge/protocol';
import type { Envelope, PrinterSessionPayload, StatePayload, ToolActivityInfo, WorkspaceContext } from './bridge/protocol';

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
    facts: {
      printer: { name: 'Bambu Lab A1 mini', provenance: 'settled' },
      nozzle: { size: 0.4, provenance: 'assumed' },
      plate: { name: 'Textured PEI Plate', provenance: 'assumed' },
      filament: { preset: 'Bambu PLA Basic @BBL A1M', ams: '', spools: [], provenance: 'assumed' },
    },
    blocks: [],
    canAdd: true,
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

  it('writes the model its instructions, and again only when the words change', () => {
    const host = open(state({ session: session({ context: { printers: [['BBL/Bambu Lab A1 mini', 'Bambu Lab A1 mini', '180 × 180 × 180 mm']], network: [] } }) }));
    const sent = () => host.received.filter((envelope) => envelope.type === 'printer_instructions');
    expect(sent()).toHaveLength(1);
    expect((sent()[0].payload as { text: string }).text).toContain('BBL/Bambu Lab A1 mini | Bambu Lab A1 mini | 180 × 180 × 180 mm');

    // A new card changes nothing the model is told.
    host.deliver('printer_session', session({ canAdd: false, context: { printers: [['BBL/Bambu Lab A1 mini', 'Bambu Lab A1 mini', '180 × 180 × 180 mm']], network: [] } }));
    expect(sent()).toHaveLength(1);

    // A changed printer does.
    host.deliver(
      'printer_session',
      session({
        mode: 'change',
        canAdd: false,
        context: {
          printer: { name: 'Lab Printer', model: '', nozzle: 0.6, nozzles: [0.4, 0.6], spools: [], connected: false },
        },
      }),
    );
    expect(sent()).toHaveLength(2);
    expect((sent()[1].payload as { text: string }).text).toContain('Nozzle: 0.6 mm');
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

  it('writes the opening line for an empty thread, once, before the instructions', () => {
    const host = open(state({ conversation: [], session: session({ context: { printers: [], network: [] } }) }));
    const openings = host.received.filter((envelope) => envelope.type === 'printer_opening');
    expect(openings).toHaveLength(1);
    expect((openings[0].payload as { text: string }).text).toMatch(/^What printer do you have\? Say it any way/);
    const types = host.received.map((envelope) => envelope.type);
    expect(types.indexOf('printer_opening')).toBeLessThan(types.indexOf('printer_instructions'));

    // A later session state does not ask again.
    host.deliver('printer_session', session({ canAdd: false }));
    expect(host.received.filter((envelope) => envelope.type === 'printer_opening')).toHaveLength(1);
  });

  it('names the printer in a Change session’s opening', () => {
    const host = open(
      state({
        conversation: [],
        session: session({
          mode: 'change',
          context: { printer: { name: 'Lab Printer', model: '', nozzle: 0.4, nozzles: [0.4], spools: [], connected: false } },
        }),
      }),
    );
    expect((host.lastOfType('printer_opening')!.payload as { text: string }).text).toMatch(/^This is the Lab Printer\. /);
  });

  it('asks nothing of a thread that already opened', () => {
    const host = open();
    expect(host.lastOfType('printer_opening')).toBeUndefined();
  });

  it('asks a likely answer, and leads the composer with Photo', () => {
    open(state({ session: session({ mode: 'change', canAdd: false }) }));
    expect(screen.getByPlaceholderText('e.g. "I put a 0.6 nozzle on it" or "loaded black PETG"')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Add a photo' })).toBeInTheDocument();
  });

  it('adds the printer and refuses it natively, never as the person speaking', async () => {
    const host = open();

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'add', id: '' });

    // The note the model reads is written here, from the printer on the card.
    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({
      action: 'reject',
      id: '',
      note: 'The person said Bambu Lab A1 mini is not their printer.',
    });
    expect(host.lastOfType('user_message')).toBeUndefined();
  });

  it('sends an access code with Add, and never into the chat', async () => {
    const host = open(state({ session: session({ accessCode: true }) }));

    await userEvent.type(screen.getByRole('textbox', { name: 'Access code' }), '12345678');
    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({
      action: 'add',
      id: '',
      accessCode: '12345678',
      note: 'An access code was entered.',
    });
    expect(host.received.some((envelope) => envelope.type === 'user_message')).toBe(false);
    expect(JSON.stringify(host.received.filter((envelope) => envelope.type !== 'printer_action'))).not.toContain('12345678');
  });

  it('offers no access code for a printer that was not found on the network', () => {
    open();
    expect(screen.queryByRole('textbox', { name: 'Access code' })).toBeNull();
  });

  it('confirms a change on its own card and hides the tools that only show printers', async () => {
    const activity = (tool: string, extra: Partial<ToolActivityInfo>): ToolActivityInfo => ({
      actionId: `t-${tool}`,
      correlationId: 'm-1',
      server: 'jusprin',
      tool,
      title: tool === 'printer_change' ? 'Change nozzle' : 'Show the printers you mean',
      arguments: {},
      actionClass: tool === 'printer_change' ? 'mutation' : 'read_only',
      requiresApproval: tool === 'printer_change',
      sessionId: '1',
      expectedRevision: 1,
      state: 'pending',
      progress: { current: 0, total: 1 },
      ...extra,
    });
    const host = open(
      state({
        session: session({ mode: 'change', canAdd: false }),
        toolActivities: [
          activity('printer_change', {
            arguments: { nozzle: 0.6, confirm: { printer: 'Bambu Lab A1 mini', before: { nozzle: 0.4 } } },
          }),
          activity('printer_identify', { state: 'failed', error: { code: 'unknown_printer', message: 'not on the list' } }),
        ],
      }),
    );

    expect(screen.queryByText('not on the list')).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Set 0.6 mm' }));
    expect(host.lastOfType('tool_decision')!.payload).toMatchObject({ actionId: 't-printer_change', decision: 'approve' });
  });

  it('undoes the last change natively', async () => {
    const host = open(
      state({
        session: session({
          mode: 'change',
          canAdd: false,
          blocks: [
            {
              id: 'b2',
              seq: 1,
              afterMessageId: 'm-1',
              kind: 'undo',
              changed: { spools: { before: [{ name: 'PETG', material: 'PETG' }], after: [] } },
            },
          ],
        }),
      }),
    );
    expect(screen.getByText('Spools updated')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Undo' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({
      action: 'undo',
      id: 'b2',
      note: 'The person undid the change: the spools are PETG again.',
    });
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
                  buildVolume: '180 × 180 × 180 mm',
                  picture: '',
                  action: 'add',
                  assumed: { nozzle: 0.4, plate: 'Textured PEI Plate', filament: 'Bambu PLA Basic @BBL A1M' },
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
    host.deliver('printer_session', session({ mode: 'change', canAdd: false }));
    expect(screen.getByText('PRINTER')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
  });
});
