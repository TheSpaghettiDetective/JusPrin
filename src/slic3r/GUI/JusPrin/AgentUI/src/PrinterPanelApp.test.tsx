// The page in printer-panel mode: the same thread and composer as the docked
// panel, under a header with Back and a menu. Everything the person does is
// a message, except the menu's two ways into OrcaSlicer's own screens and
// the credential card's decision.

import { act, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, it } from 'vitest';
import { App } from './App';
import { PROTOCOL_NAME, PROTOCOL_VERSION } from './bridge/protocol';
import type { Envelope, PrinterFormInfo, PrinterFormOptions, PrinterSessionPayload, StatePayload, ToolActivityInfo, WorkspaceContext } from './bridge/protocol';

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
  return { mode: 'add', printerName: '', blocks: [], context: { printers: [], network: [] }, ...overrides };
}

const labPrinter = { name: 'Lab Printer', model: 'Bambu Lab A1 mini', nozzle: 0.4, nozzles: [0.4, 0.6], spools: [], connected: false };

function state(overrides: Partial<StatePayload> = {}): StatePayload {
  return {
    agent: { status: 'ready' },
    appearance: 'light',
    conversations: [{ id: 'conv-1', title: 'Printer', createdAt: 't' }],
    activeConversationId: 'conv-1',
    conversation: [{ id: 'm-1', role: 'assistant', state: 'complete', text: 'What printer do you have?', seq: 1 }],
    streamingMessageId: null,
    toolActivities: [],
    gcodeMarkers: [],
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
    // The panel has its own assistant; the project's prompt is not sent here.
    expect(host.received.some((envelope) => envelope.type === 'project_instructions')).toBe(false);
    expect(host.received.some((envelope) => envelope.type === 'skills')).toBe(false);

    // A new card changes nothing the model is told.
    host.deliver('printer_session', session({ blocks: [{ id: 'b1', seq: 1, afterMessageId: 'm-1', kind: 'tip' }],
      context: { printers: [['BBL/Bambu Lab A1 mini', 'Bambu Lab A1 mini', '180 × 180 × 180 mm']], network: [] } }));
    expect(sent()).toHaveLength(1);

    // A printer to talk about does.
    host.deliver('printer_session', session({ mode: 'change', printerName: 'Lab Printer',
      context: { printer: { ...labPrinter, nozzle: 0.6, provider: 'bambu' } } }));
    expect(sent()).toHaveLength(2);
    expect((sent()[1].payload as { text: string }).text).toContain('Nozzle: 0.6 mm');
  });

  it('draws nothing but Back, its menu, the thread and the composer', () => {
    open();
    expect(document.querySelector('.app--printer-setup')).not.toBeNull();
    expect(screen.queryByRole('heading')).toBeNull();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
    expect(screen.getByText('What printer do you have?')).toBeInTheDocument();
    expect(screen.getByLabelText('Message the Agent')).toBeEnabled();
    // The project's own setup card belongs to the other panel.
    expect(screen.queryByTestId('current-setup')).toBeNull();
  });

  it('goes back without asking before the person has said anything', async () => {
    const host = open();
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'close' });
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  const said = state({ conversation: [
    { id: 'm-1', role: 'assistant', state: 'complete', text: 'What printer do you have?', attempt: 1 },
    { id: 'm-2', role: 'user', state: 'complete', text: 'creality k1', attempt: 1 },
  ] } as Partial<StatePayload>);

  it('asks before going back once the person has said something, and Cancel stays', async () => {
    const host = open(said);
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    const dialog = screen.getByRole('dialog', { name: 'Close this conversation?' });
    expect(dialog).toHaveTextContent("This conversation will be closed. You can't continue it at a later point. Are you sure?");
    expect(screen.getByRole('button', { name: 'Cancel' })).toHaveFocus();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(host.lastOfType('printer_action')).toBeUndefined();

    // Escape is Cancel.
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    await userEvent.keyboard('{Escape}');
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(host.lastOfType('printer_action')).toBeUndefined();
  });

  it('closes the conversation when the person confirms', async () => {
    const host = open(said);
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    await userEvent.click(screen.getByRole('button', { name: 'Close conversation' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'close' });
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('asks before going back over words not yet sent', async () => {
    const host = open();
    await userEvent.type(screen.getByLabelText('Message the Agent'), 'half a thought');
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(screen.getByRole('dialog')).toBeInTheDocument();
    expect(host.lastOfType('printer_action')).toBeUndefined();
  });

  it('sends a tapped Done as the person’s own message, and asks nothing', async () => {
    const host = open(state({ conversation: [
      { id: 'm-1', role: 'user', state: 'complete', text: 'Not now', attempt: 1 },
      { id: 'm-2', role: 'assistant', state: 'complete', text: 'It can still prepare prints.\nChoices: Done', attempt: 1 },
    ] } as Partial<StatePayload>));
    await userEvent.click(screen.getByRole('button', { name: 'Done' }));
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'Done' });
    expect(host.lastOfType('printer_action')).toBeUndefined();
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('keeps OrcaSlicer’s own screens visible, printer settings only once there is a printer', async () => {
    const host = open();
    expect(screen.queryByRole('button', { name: 'Open printer settings' })).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Browse the full printer list' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'manual_setup' });

    host.deliver('printer_session', session({ printerName: 'Lab Printer' }));
    await userEvent.click(screen.getByRole('button', { name: 'Open printer settings' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'open_printer_settings' });
  });

  it('keeps setup visible when the host adds the automatic printer greeting', async () => {
    const host = open(state({ agent: { status: 'unavailable' }, conversation: [] }));
    expect(host.lastOfType('printer_opening')).toBeDefined();
    host.deliver('message_added', { message: state().conversation[0] });
    expect(screen.getByTestId('agent-not-configured')).toBeVisible();
    expect(screen.queryByText('What printer do you have?')).not.toBeInTheDocument();
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();

    expect(screen.getAllByRole('button', { name: 'Browse the full printer list' })).toHaveLength(1);
    await userEvent.click(screen.getByRole('button', { name: 'Browse the full printer list' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'manual_setup' });
  });

  it('keeps the add-printer fallback out of an existing printer’s settings', () => {
    open(state({ agent: { status: 'unavailable' }, session: session({ mode: 'change', printerName: 'Lab Printer' }) }));
    expect(screen.queryByRole('button', { name: 'Browse the full printer list' })).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Open printer settings' })).toBeInTheDocument();
  });

  it('sends its offer to Preferences › Agent, keeps the panel, and continues the greeting once an agent is there', async () => {
    const host = open(state({ agent: { status: 'unavailable' } }));
    await userEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
    expect(host.lastOfType('shell_action')!.payload).toEqual({ action: 'open_preferences', section: 'agent' });
    // No setup is drawn in the panel, and the panel is not closed for the person.
    expect(screen.queryByTestId('setup-chooser')).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Your own API key' })).not.toBeInTheDocument();
    expect(host.received.filter((envelope) => envelope.type === 'printer_action')).toEqual([]);
    expect(screen.getByRole('button', { name: 'Back' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Browse the full printer list' })).toBeInTheDocument();

    // The window closes with an agent: the greeting is there and nothing was sent.
    host.deliver('agent_status', { status: 'ready' });
    expect(screen.getByText('What printer do you have?')).toBeVisible();
    expect(host.lastOfType('user_message')).toBeUndefined();
    await userEvent.type(screen.getByLabelText('Message the Agent'), 'Bambu A1 mini{Enter}');
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'Bambu A1 mini' });
  });

  it('writes the opening line for an empty thread, once, before the instructions', () => {
    const host = open(state({ conversation: [] }));
    const openings = host.received.filter((envelope) => envelope.type === 'printer_opening');
    expect(openings).toHaveLength(1);
    expect((openings[0].payload as { text: string }).text).toMatch(/^Which printer do you have\? /);
    const types = host.received.map((envelope) => envelope.type);
    expect(types.indexOf('printer_opening')).toBeLessThan(types.indexOf('printer_instructions'));

    host.deliver('printer_session', session({ printerName: 'Lab Printer' }));
    expect(host.received.filter((envelope) => envelope.type === 'printer_opening')).toHaveLength(1);
  });

  it('asks nothing of a thread that already opened', () => {
    const host = open();
    expect(host.lastOfType('printer_opening')).toBeUndefined();
  });

  it('asks a likely answer, and leads the composer with Photo', () => {
    open(state({ session: session({ mode: 'change', printerName: 'Lab Printer' }) }));
    expect(screen.getByPlaceholderText('e.g. "I put a 0.6 nozzle on it" or "loaded black PETG"')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Add a photo' })).toBeInTheDocument();
  });

  it('draws the cards the agent drew, under the message that drew them, with nothing to tap', () => {
    open(state({ session: session({ blocks: [{ id: 'b1', seq: 1, afterMessageId: 'm-1', kind: 'printers', printers: [
      { catalogId: 'BBL/Bambu Lab A1 mini', name: 'Bambu Lab A1 mini', buildVolume: '180 × 180 × 180 mm', picture: '' },
    ] }] }) }));
    const card = screen.getByText('180 × 180 × 180 mm').closest('.printer-card')!;
    expect(card.querySelector('button')).toBeNull();
    expect(screen.getByText('What printer do you have?').compareDocumentPosition(card) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
  });

  it('draws the added receipt between the turn that saved the printer and the reply after it', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'creality k1', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'complete', text: '', attempt: 1 },
          { id: 'm-3', role: 'assistant', state: 'complete', text: 'Want to connect it?\nChoices: Connect it | Not now', attempt: 1 },
        ],
        session: session({
          printerName: 'Creality K1',
          blocks: [{ id: 'b2', seq: 2, afterMessageId: 'm-2', kind: 'added', printer: { name: 'Creality K1', nozzle: 0.4 } }],
        }),
      }),
    );
    const receipt = screen.getByText('Added:', { exact: false }).closest('.printer-added')!;
    expect(screen.getByText('creality k1').compareDocumentPosition(receipt) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    expect(receipt.compareDocumentPosition(screen.getByText('Want to connect it?')) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    // The reply's own choices are still the reply's, under it.
    expect(screen.getByRole('button', { name: 'Connect it' })).toBeInTheDocument();
  });

  it('undoes an add from its receipt, which then says the printer is removed', async () => {
    const added = { id: 'b2', seq: 2, afterMessageId: 'm-1', kind: 'added' as const, printer: { name: 'Creality K1', nozzle: 0.4 } };
    const host = open(state({ session: session({ printerName: 'Creality K1', blocks: [added] }) }));
    await userEvent.click(screen.getByRole('button', { name: 'Undo' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'undo_add', blockId: 'b2' });

    host.deliver('printer_session', session({ blocks: [{ ...added, removed: true }] }));
    const receipt = screen.getByText('Removed:', { exact: false }).closest('.printer-added')!;
    expect(receipt).toHaveClass('printer-added-removed');
    expect(screen.queryByText('Added:', { exact: false })).toBeNull();
    expect(receipt.querySelector('button')).toBeNull();
  });

  it('asks for the access code locally, and sends it only with Connect', async () => {
    const connect: ToolActivityInfo = {
      actionId: 't-connect', correlationId: 'm-1', server: 'jusprin', tool: 'printer_connect', title: 'Connect to Workshop',
      arguments: { printerName: 'Lab Printer', deviceId: '01P00A3B', provider: 'bambu' }, actionClass: 'mutation',
      sessionId: '1', expectedRevision: 1, state: 'succeeded', progress: { current: 1, total: 1 },
    };
    const identify = { ...connect, actionId: 't-identify', tool: 'printer_identify', title: 'Show the printers you mean' };
    const host = open(state({ toolActivities: [identify, connect], session: session({
      credentialRequest: { actionId: 't-connect', target: 'Workshop', provider: 'bambu' },
    }) }));
    // Only the local form is drawn.
    expect(screen.queryByText('Show the printers you mean')).toBeNull();
    expect(screen.getByText('Connect to Workshop')).toBeInTheDocument();

    await userEvent.type(screen.getByLabelText('Access code'), 'secretcode');
    expect(JSON.stringify(host.received)).not.toContain('secretcode');
    await userEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'connect', actionId: 't-connect', credential: 'secretcode' });
  });
});

describe('a photo in the printer panel', () => {
  const stagedPhoto = {
    id: 'a-1',
    name: 'nameplate.jpg',
    kind: 'image' as const,
    mime: 'image/jpeg',
    sizeBytes: 900,
    source: 'picker' as const,
    state: 'staged' as const,
    previewDataUrl: 'data:image/jpeg;base64,AAA',
  };

  it('stages a photo in the shared attachment row with its name and thumbnail', () => {
    const host = open();
    host.deliver('attachment_updated', { attachment: stagedPhoto });
    expect(screen.queryByText('Image')).toBeNull();
    expect(screen.getByText('nameplate.jpg')).toBeInTheDocument();
    expect(screen.getByText('nameplate.jpg').closest('.attachment-chip')!.querySelector('img')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Remove nameplate.jpg' })).toBeInTheDocument();
  });

  it('draws a sent photo inline above its caption, not as a chip below the text', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'is this the right one', attempt: 1, attachments: ['a-1'] },
        ],
        attachments: [{ ...stagedPhoto, state: 'sent' }],
      }),
    );
    expect(screen.queryByText('nameplate.jpg')).toBeNull();
    const photo = screen.getByAltText('nameplate.jpg') as HTMLImageElement;
    expect(photo.tagName).toBe('IMG');
    expect(photo.className).toContain('message-photo');
    const caption = screen.getByText('is this the right one');
    // The image is the DOM sibling before the caption, so it draws above it.
    expect(photo.compareDocumentPosition(caption) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
  });
});

describe('reply chips in the printer panel', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  const offered = { id: 'm-2', role: 'assistant', state: 'complete', text: 'That’s the Kobra 3. Is that right?\nChoices: A | B', attempt: 1 } as const;

  it('draws a finished reply’s choices as chips instead of text', () => {
    open(state({ conversation: [{ id: 'm-1', role: 'user', state: 'complete', text: 'kobra 3', attempt: 1 }, offered] }));
    expect(screen.getByText('That’s the Kobra 3. Is that right?')).toBeVisible();
    expect(screen.getByRole('button', { name: 'A' })).toHaveClass('reply-chip');
    expect(screen.getByRole('button', { name: 'B' })).toHaveClass('reply-chip');
    expect(screen.queryByText(/Choices:/)).not.toBeInTheDocument();
  });

  it('sends a tapped chip as the person’s own message', async () => {
    const host = open(state({ conversation: [offered] }));
    await userEvent.click(screen.getByRole('button', { name: 'A' }));
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'A' });
  });

  it('reaches a chip with Tab and sends it with Enter', async () => {
    const host = open(state({ conversation: [offered] }));
    const chip = screen.getByRole('button', { name: 'A' });
    while (document.activeElement !== chip) await userEvent.tab();
    await userEvent.keyboard('{Enter}');
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'A' });
  });

  it('shows neither chips nor a partial choices line while the reply streams', () => {
    open(state({ conversation: [{ ...offered, text: 'That’s the Kobra 3. Is that right?\nChoices: A | ' }], streamingMessageId: 'm-2' }));
    expect(screen.getByText('That’s the Kobra 3. Is that right?')).toBeVisible();
    expect(screen.queryByText(/Choices/)).not.toBeInTheDocument();
    expect(document.querySelector('.reply-chip')).toBeNull();
  });

  it('offers no chips on an older message', () => {
    open(state({ conversation: [offered, { id: 'm-3', role: 'user', state: 'complete', text: 'A', attempt: 1 }] }));
    expect(document.querySelector('.reply-chip')).toBeNull();
    expect(screen.queryByText(/Choices:/)).not.toBeInTheDocument();
  });
});

describe('a tool-only turn in the printer panel', () => {
  // The model sometimes calls printer_identify with no words yet -- its
  // actual reply lands in a later, separate message once it sees the tool's
  // result. A bare, empty message bubble either mid-stream or once it
  // settles reads as a rendering bug, not a pause.
  it('names the work while an empty turn still streams', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'bambu a1 mini', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'complete', text: '', attempt: 1 },
        ],
        streamingMessageId: 'm-2',
      }),
    );
    // A status line (Figma "Working on it"), never an empty reply.
    const line = screen.getByText('Working on it…');
    expect(line).toHaveAttribute('role', 'status');
    expect(document.querySelector('.markdown-content')).toBeNull();
  });

  it('draws nothing for a tool-only turn once its stream ends, still empty', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'bambu a1 mini', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'complete', text: '', attempt: 1 },
        ],
        streamingMessageId: null,
      }),
    );
    expect(screen.queryByText('Working on it…')).not.toBeInTheDocument();
  });

  it('still shows a failed empty turn, with its Retry', async () => {
    const host = open(
      state({
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'bambu a1 mini', attempt: 1 },
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
});

// The printer form, which "Browse the full printer list" opens: the app sends
// what it offers in the session, and the page sends back what was chosen.
describe('the printer form', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  const catalog: PrinterFormInfo['catalog'] = [
    { id: 'BBL/Bambu Lab P1S', vendorId: 'BBL', brand: 'Bambu Lab', model: 'P1S', nozzles: [0.2, 0.4, 0.6, 0.8] },
    { id: 'Creality/Creality Ender-3 V2', vendorId: 'Creality', brand: 'Creality', model: 'Ender-3 V2', nozzles: [0.4] },
    { id: 'Prusa/Prusa MK4', vendorId: 'Prusa', brand: 'Prusa', model: 'MK4', nozzles: [0.25, 0.4, 0.6, 0.8] },
    // One vendor file whose model names spell the brand two ways.
    { id: 'SecKit/SecKit SK-Tank', vendorId: 'SecKit', brand: 'SecKit', model: 'SK-Tank', nozzles: [0.4] },
    { id: 'SecKit/Seckit Go3', vendorId: 'SecKit', brand: 'Seckit', model: 'Go3', nozzles: [0.4] },
  ];
  const p1sOptions: PrinterFormOptions = {
    catalogId: 'BBL/Bambu Lab P1S', nozzle: 0.4,
    plates: [{ type: 1, label: 'Smooth Cool Plate' }, { type: 3, label: 'Smooth High Temp Plate' }, { type: 4, label: 'Textured PEI Plate' }],
    defaultPlate: 3,
    filaments: [
      { name: 'Bambu PLA Basic @BBL X1C', label: 'Bambu PLA Basic', vendor: 'Bambu Lab', material: 'PLA' },
      { name: 'Bambu PETG HF @BBL X1C', label: 'Bambu PETG HF', vendor: 'Bambu Lab', material: 'PETG' },
      { name: 'eSUN PLA+ @System', label: 'eSUN PLA+', vendor: 'eSUN', material: 'PLA' },
    ],
    suggested: ['Bambu PLA Basic @BBL X1C', 'Bambu PETG HF @BBL X1C'],
    defaultFilament: 'Bambu PLA Basic @BBL X1C',
    error: '',
    others: [{ name: 'Bambu PLA Basic @BBL H2D', label: 'Bambu PLA Galaxy', vendor: 'Bambu Lab', material: 'PLA', madeFor: 'the Bambu Lab H2D' }],
    own: [],
  };
  const mk4Options: PrinterFormOptions = {
    catalogId: 'Prusa/Prusa MK4', nozzle: 0.4, plates: [], defaultPlate: 3,
    filaments: [{ name: 'Prusa Generic PLA @MK4', label: 'Prusa Generic PLA @MK4', vendor: 'Generic', material: 'PLA' }],
    suggested: ['Prusa Generic PLA @MK4'], defaultFilament: 'Prusa Generic PLA @MK4', error: '',
    others: [
      { name: 'Prusament PLA @CORE One', label: 'Prusament PLA', vendor: 'Prusament', material: 'PLA', madeFor: 'the Prusa CORE One and Prusa XL' },
      { name: 'Prusament PETG @CORE One', label: 'Prusament PETG', vendor: 'Prusament', material: 'PETG', madeFor: 'the Prusa CORE One and Prusa XL' },
    ],
    own: [
      { name: 'PETG fast walls', label: 'PETG fast walls', vendor: 'Generic', material: 'PETG', base: 'Prusa Generic PETG' },
      { name: 'Matte PLA tuned', label: 'Matte PLA tuned', vendor: 'Bambu Lab', material: 'PLA', madeFor: 'the Bambu Lab X1 Carbon',
        base: 'Bambu PLA Matte' },
    ],
  };

  function form(overrides: Partial<PrinterFormInfo> = {}): PrinterFormInfo {
    return { catalog, nearby: [], error: '', loading: false, ...overrides };
  }
  const withForm = (info: PrinterFormInfo) => session({ form: info });
  const sentActions = (host: MockHost) =>
    host.received.filter((envelope) => envelope.type === 'printer_action').map((envelope) => envelope.payload);

  async function pick(menu: string, choice: string) {
    await userEvent.click(screen.getByRole('button', { name: new RegExp(`^${menu}:`) }));
    await userEvent.click(screen.getByRole('menuitemradio', { name: choice }));
  }

  it('asks which printer, from one brand list per vendor and each model’s own nozzle sizes', async () => {
    const host = open(state({ session: withForm(form()) }));
    expect(screen.getByRole('dialog', { name: 'Which printer?' })).toBeInTheDocument();
    expect(screen.getByText('Step 1 of 3')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Next' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Model: Choose a model' })).toBeDisabled();

    await userEvent.click(screen.getByRole('button', { name: 'Brand: Choose a brand' }));
    expect(screen.getAllByRole('menuitemradio').map((row) => row.textContent)).toEqual(['Bambu Lab', 'Creality', 'Prusa', 'SecKit']);
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'Prusa' }));
    await pick('Model', 'MK4');
    // 0.4 to start, as the chat assumes; the sizes are the MK4's own.
    expect(screen.getAllByRole('radio').map((button) => button.textContent)).toEqual(['0.25', '0.4', '0.6', '0.8']);
    expect(screen.getByRole('radio', { name: '0.4' })).toHaveAttribute('aria-checked', 'true');
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_options', catalogId: 'Prusa/Prusa MK4', nozzle: 0.4 });
    await userEvent.click(screen.getByRole('radio', { name: '0.6' }));
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_options', catalogId: 'Prusa/Prusa MK4', nozzle: 0.6 });

    // A model with one size says so instead of offering a choice.
    await pick('Brand', 'Creality');
    await pick('Model', 'Ender-3 V2');
    expect(screen.queryByRole('radiogroup', { name: 'Nozzle' })).toBeNull();
    expect(screen.getByText('0.4 mm, the only size it comes with')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Next' })).toBeEnabled();
  });

  it('sets up a printer found on the network as the model it is, linked to it', async () => {
    const host = open(state({ session: withForm(form({ nearby: [{ deviceId: '01P00A3B', name: 'Garage P1S', catalogId: 'BBL/Bambu Lab P1S' }] })) }));
    expect(screen.getByText('Found on your network')).toBeInTheDocument();
    expect(screen.getByText('You can connect it later.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: /Bambu Lab P1S · 01P00A3B/ }));
    expect(screen.getByRole('button', { name: /Bambu Lab P1S · 01P00A3B/ })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Brand: Bambu Lab' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Model: P1S' })).toBeInTheDocument();
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_options', catalogId: 'BBL/Bambu Lab P1S', nozzle: 0.4 });

    host.deliver('printer_session', withForm(form({ nearby: [{ deviceId: '01P00A3B', name: 'Garage P1S', catalogId: 'BBL/Bambu Lab P1S' }],
      options: p1sOptions })));
    await userEvent.click(screen.getByRole('button', { name: 'Next' }));

    // The plate OrcaSlicer starts this printer on is chosen, and named so.
    expect(screen.getByRole('dialog', { name: 'Which plate is on it?' })).toBeInTheDocument();
    expect(screen.getByText('Step 2 of 3')).toBeInTheDocument();
    expect(screen.getByRole('radio', { name: /Smooth High Temp Plate/ })).toHaveAttribute('aria-checked', 'true');
    expect(screen.getByText('Default for the P1S')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('radio', { name: 'Textured PEI Plate' }));

    expect(screen.getByRole('dialog', { name: 'Which filament do you want to start with?' })).toBeInTheDocument();
    expect(screen.getByText('Step 3 of 3')).toBeInTheDocument();
    expect(screen.getByText('Suggested for the P1S')).toBeInTheDocument();
    // Every filament that fits follows the suggestions, without a search.
    expect(screen.getByText('Every filament for the P1S')).toBeInTheDocument();
    expect(screen.getByText('eSUN PLA+')).toBeInTheDocument();
    // Search reads brand, material and name.
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search brand or material' }), 'esun');
    expect(screen.getByText('eSUN PLA+')).toBeInTheDocument();
    expect(screen.queryByText('Bambu PLA Basic')).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: /eSUN PLA\+/ }));
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_finish', catalogId: 'BBL/Bambu Lab P1S', nozzle: 0.4, plate: 4,
      filament: 'eSUN PLA+ @System', deviceId: '01P00A3B' });
  });

  it('skips the plate where OrcaSlicer offers none, and Skip keeps the profile’s filament', async () => {
    const host = open(state({ session: withForm(form()) }));
    await pick('Brand', 'Prusa');
    await pick('Model', 'MK4');
    // Moving on before the profiles are read waits on the next step.
    await userEvent.click(screen.getByRole('button', { name: 'Next' }));
    expect(screen.getByText('Reading this printer’s profiles…')).toBeInTheDocument();
    host.deliver('printer_session', withForm(form({ options: mk4Options })));
    expect(screen.getByRole('dialog', { name: 'Which filament do you want to start with?' })).toBeInTheDocument();
    expect(screen.getByText('Step 2 of 2')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Skip — start with Prusa Generic PLA @MK4' }));
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_finish', catalogId: 'Prusa/Prusa MK4', nozzle: 0.4 });
    // Back returns past the plate step that was never asked.
    await userEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Back' }));
    expect(screen.getByRole('dialog', { name: 'Which printer?' })).toBeInTheDocument();
  });

  it('shows why a finish saved nothing, and lets the person try again', async () => {
    const host = open(state({ session: withForm(form()) }));
    await pick('Brand', 'Prusa');
    await pick('Model', 'MK4');
    host.deliver('printer_session', withForm(form({ options: mk4Options })));
    await userEvent.click(screen.getByRole('button', { name: 'Next' }));
    // The default is both suggested and in the full list; the suggestion is first.
    await userEvent.click(screen.getAllByRole('button', { name: /^Prusa Generic PLA @MK4/ })[0]);
    expect(screen.getAllByRole('button', { name: /^Prusa Generic PLA @MK4/ })[0]).toBeDisabled();
    host.deliver('printer_session', withForm(form({ options: mk4Options, error: 'This printer could not be installed.' })));
    expect(screen.getByRole('alert')).toHaveTextContent('This printer could not be installed.');
    expect(screen.getAllByRole('button', { name: /^Prusa Generic PLA @MK4/ })[0]).toBeEnabled();
    expect(sentActions(host).filter((payload) => (payload as { action: string }).action === 'form_finish')).toHaveLength(1);
  });

  it('closes with Cancel or Escape, and Escape in an open list closes only the list', async () => {
    const host = open(state({ session: withForm(form()) }));
    await userEvent.click(screen.getByRole('button', { name: 'Brand: Choose a brand' }));
    expect(screen.getByRole('menu')).toBeInTheDocument();
    await userEvent.keyboard('{Escape}');
    expect(screen.queryByRole('menu')).toBeNull();
    expect(sentActions(host)).toEqual([]);

    await userEvent.keyboard('{Escape}');
    expect(sentActions(host)).toEqual([{ action: 'form_close' }]);
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(sentActions(host)).toEqual([{ action: 'form_close' }, { action: 'form_close' }]);
  });

  async function openOnMk4(info: Partial<PrinterFormInfo> = {}) {
    const host = open(state({ session: withForm(form(info)) }));
    await pick('Brand', 'Prusa');
    await pick('Model', 'MK4');
    host.deliver('printer_session', withForm(form({ ...info, options: mk4Options })));
    await userEvent.click(screen.getByRole('button', { name: 'Next' }));
    return host;
  }
  const headings = () => Array.from(document.querySelectorAll('.printer-form-section')).map((node) => node.textContent);

  it('lists the person’s own presets first, then the suggestions, then every filament that fits', async () => {
    const host = await openOnMk4();
    expect(headings()).toEqual(['Your filaments', 'Suggested for the MK4', 'Every filament for the MK4 · 1']);
    expect(screen.getByText('Your preset, based on Prusa Generic PETG')).toBeInTheDocument();
    expect(screen.getByText(/Your preset, made for the Bambu Lab X1 Carbon\. Picking it saves a copy for the MK4\./)).toBeInTheDocument();
    expect(screen.getByText('The MK4 profile’s default')).toBeInTheDocument();
    // Filaments made for other printers wait behind the tick box at the end.
    expect(screen.queryByText('Prusament PLA')).toBeNull();
    expect(screen.getByText(/Not in the list\?/)).toBeInTheDocument();
    const toggle = screen.getByRole('checkbox', { name: /Show filaments made for other printers/ });
    expect(toggle).toHaveAttribute('aria-checked', 'false');
    expect(toggle).toHaveTextContent('2 more. Picking one saves a copy for the MK4.');
    await userEvent.click(toggle);
    expect(headings()).toContain('Made for other printers · 2');
    expect(screen.getAllByText(/Made for the Prusa CORE One and Prusa XL\. Picking it saves a copy for the MK4\./)).toHaveLength(2);
    await userEvent.click(screen.getByRole('button', { name: /^Prusament PLA/ }));
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_finish', catalogId: 'Prusa/Prusa MK4', nozzle: 0.4,
      filament: 'Prusament PLA @CORE One' });
  });

  it('says when only filaments made for other printers match a search, and offers them', async () => {
    await openOnMk4();
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search brand or material' }), 'prusament');
    expect(screen.getByText(/No filament for the MK4 matches “prusament”\./)).toBeInTheDocument();
    expect(screen.getByText(/2 filaments made for other printers do\./)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Create a filament preset/ })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Show them' }));
    expect(headings()).toEqual(['Made for other printers · 2']);

    // A search that matches here counts the near misses it leaves out.
    await userEvent.clear(screen.getByRole('searchbox', { name: 'Search brand or material' }));
    await userEvent.click(screen.getByRole('checkbox', { name: /Show filaments made for other printers/ }));
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search brand or material' }), 'pla');
    expect(headings()).toEqual(['Your filaments', 'For the MK4 · 1']);
    // The tick box under the list says what it would add for this search.
    expect(screen.getByRole('checkbox', { name: /Show filaments made for other printers/ }))
      .toHaveTextContent('1 of them match your search. Picking one saves a copy for the MK4.');
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search brand or material' }), 'zzz');
    expect(screen.getByRole('checkbox', { name: /Show filaments made for other printers/ }))
      .toHaveTextContent('None of the 2 match your search either.');
  });

  it('offers to make a preset when nothing matches anywhere', async () => {
    const host = await openOnMk4();
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search brand or material' }), 'zzz');
    expect(screen.getByText(/No filament matches “zzz”,/).closest('p')).toHaveTextContent('any other printer JusPrin has profiles for.');
    // The ways out stay under the list, outside it.
    expect(document.querySelector('.printer-form-filaments')!.textContent).not.toContain('Not in the list?');
    expect(document.querySelector('.printer-form-end')!.textContent).toContain('Create a filament preset…');
    await userEvent.click(screen.getByRole('button', { name: 'Start with Prusa Generic PLA @MK4 instead' }));
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_finish', catalogId: 'Prusa/Prusa MK4', nozzle: 0.4 });
  });

  it('says what creating a preset does before it adds the printer', async () => {
    const host = await openOnMk4();
    await userEvent.click(screen.getByRole('button', { name: /Create a filament preset/ }));
    expect(screen.getByRole('dialog', { name: 'Create a filament preset' })).toBeInTheDocument();
    expect(screen.getByText(/so the MK4 is added first/)).toBeInTheDocument();
    // The form speaks of JusPrin, never of OrcaSlicer.
    expect(screen.getByRole('dialog').textContent).not.toContain('OrcaSlicer');
    expect(sentActions(host).some((payload) => (payload as { action: string }).action === 'form_finish')).toBe(false);
    // Back returns to the list with nothing done.
    await userEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Back' }));
    expect(screen.getByRole('dialog', { name: 'Which filament do you want to start with?' })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: /Create a filament preset/ }));
    await userEvent.click(screen.getByRole('button', { name: 'Add the MK4 and continue' }));
    expect(sentActions(host).at(-1)).toEqual({ action: 'form_finish', catalogId: 'Prusa/Prusa MK4', nozzle: 0.4, create: true });
  });
});
