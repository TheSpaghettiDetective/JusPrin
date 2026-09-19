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
    caption: 'NEW PRINTER',
    facts: {
      printer: { value: 'Bambu Lab A1 mini', provenance: 'settled' },
      nozzle: { value: '0.4 mm', provenance: 'assumed' },
      plate: { value: 'Textured PEI Plate', provenance: 'assumed' },
      filament: { value: 'PLA', provenance: 'assumed' },
    },
    blocks: [],
    chips: [{ id: 'add', label: 'Add this printer', style: 'primary', action: 'add' }],
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

  it('writes the model its instructions, and again only when the words change', () => {
    const host = open(state({ session: session({ context: { printers: [['BBL/Bambu Lab A1 mini', 'Bambu Lab A1 mini', '180 × 180 × 180 mm']], network: [] } }) }));
    const sent = () => host.received.filter((envelope) => envelope.type === 'printer_instructions');
    expect(sent()).toHaveLength(1);
    expect((sent()[0].payload as { text: string }).text).toContain('BBL/Bambu Lab A1 mini | Bambu Lab A1 mini | 180 × 180 × 180 mm');

    // A new card changes nothing the model is told.
    host.deliver('printer_session', session({ chips: [], context: { printers: [['BBL/Bambu Lab A1 mini', 'Bambu Lab A1 mini', '180 × 180 × 180 mm']], network: [] } }));
    expect(sent()).toHaveLength(1);

    // A changed printer does.
    host.deliver(
      'printer_session',
      session({
        mode: 'change',
        chips: [],
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

  it('asks a likely answer, and leads the composer with Photo', () => {
    open();
    expect(screen.getByPlaceholderText('e.g. "I put a 0.6 nozzle on it"')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Add a photo' })).toBeInTheDocument();
  });

  it('adds the printer and refuses it natively, never as the person speaking', async () => {
    const host = open(
      state({
        session: session({
          chips: [
            { id: 'add', label: 'Add this printer', style: 'primary', action: 'add' },
            { id: 'reject', label: 'Not this one', style: 'plain', action: 'reject' },
          ],
        }),
      }),
    );

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'add' });

    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'reject' });
    expect(host.lastOfType('user_message')).toBeUndefined();
  });

  it('sends an access code with Add, and never into the chat', async () => {
    const host = open(state({ session: session({ accessCode: true }) }));

    await userEvent.type(screen.getByRole('textbox', { name: 'Access code' }), '12345678');
    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'add', id: '', accessCode: '12345678' });
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
        session: session({ mode: 'change', caption: 'PRINTER', chips: [] }),
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
          chips: [],
          blocks: [{ id: 'b2', seq: 1, afterMessageId: 'm-1', kind: 'undo', text: 'Nozzle set to 0.6 mm' }],
        }),
      }),
    );
    await userEvent.click(screen.getByRole('button', { name: 'Undo' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'undo', id: 'b2' });
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
