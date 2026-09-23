// The page in printer-panel mode: the same thread and composer as the docked
// panel, under a header with Back and a menu. Everything the person does is
// a message, except the menu's two ways into OrcaSlicer's own screens and
// the credential card's decision.

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

  it('goes back without asking, whatever is on the page', async () => {
    const host = open();
    await userEvent.type(screen.getByLabelText('Message the Agent'), 'half a thought');
    await userEvent.click(screen.getByRole('button', { name: '‹ Back' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'close' });
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('keeps OrcaSlicer’s own screens in the menu, printer settings only once there is a printer', async () => {
    const host = open();
    await userEvent.click(screen.getByRole('button', { name: 'More' }));
    expect(screen.queryByRole('menuitem', { name: 'Open printer settings' })).toBeNull();
    await userEvent.click(screen.getByRole('menuitem', { name: 'Browse the full printer list' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'manual_setup' });

    host.deliver('printer_session', session({ printerName: 'Lab Printer' }));
    await userEvent.click(screen.getByRole('button', { name: 'More' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Open printer settings' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'open_printer_settings' });
  });

  it('keeps setup visible when the host adds the automatic printer greeting', async () => {
    const host = open(state({ agent: { status: 'unavailable' }, conversation: [] }));
    expect(host.lastOfType('printer_opening')).toBeDefined();
    host.deliver('message_added', { message: state().conversation[0] });
    expect(screen.getByTestId('agent-not-configured')).toBeVisible();
    expect(screen.queryByText('What printer do you have?')).not.toBeInTheDocument();
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();

    await userEvent.click(screen.getByRole('button', { name: 'Add your printer manually' }));
    expect(host.lastOfType('printer_action')!.payload).toEqual({ action: 'manual_setup' });
  });

  it('keeps the add-printer fallback out of an existing printer’s settings', () => {
    open(state({ agent: { status: 'unavailable' }, session: session({ mode: 'change', printerName: 'Lab Printer' }) }));
    expect(screen.queryByRole('button', { name: 'Add your printer manually' })).not.toBeInTheDocument();
  });

  it('navigates agent setup and resumes the printer chat when ready', async () => {
    const host = open(state({ agent: { status: 'unavailable' } }));
    await userEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
    await userEvent.click(screen.getByRole('button', { name: 'Use your own API key' }));
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'test-key');
    await userEvent.click(screen.getByRole('button', { name: 'Check key' }));
    host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 500 });
    host.deliver('agent_status', { status: 'ready' });
    expect(screen.getByText('What printer do you have?')).toBeVisible();
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

  it('asks for the access code on its card, and sends it only with Connect', async () => {
    const connect: ToolActivityInfo = {
      actionId: 't-connect', correlationId: 'm-1', server: 'jusprin', tool: 'printer_connect', title: 'Connect to Workshop',
      arguments: { printerName: 'Lab Printer', deviceId: '01P00A3B', provider: 'bambu' }, actionClass: 'mutation',
      requiresApproval: true, sessionId: '1', expectedRevision: 1, state: 'pending', progress: { current: 0, total: 1 },
    };
    const identify = { ...connect, actionId: 't-identify', tool: 'printer_identify', title: 'Show the printers you mean', requiresApproval: false, state: 'succeeded' as const };
    const host = open(state({ toolActivities: [identify, connect] }));
    // Only the card that asks for something is drawn.
    expect(screen.queryByText('Show the printers you mean')).toBeNull();
    expect(screen.getByText('Connect to Workshop')).toBeInTheDocument();

    await userEvent.type(screen.getByLabelText('Access code'), 'secretcode');
    expect(JSON.stringify(host.received)).not.toContain('secretcode');
    await userEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(host.lastOfType('tool_decision')!.payload).toEqual({ actionId: 't-connect', decision: 'approve', input: { credential: 'secretcode' } });
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

  // F3 review fix: the general chat's kind label ("Image") is dropped, but
  // the wireframe (3.1 item 4, 3.3 state E) keeps the file name beside the
  // thumbnail -- an earlier pass over-simplified this to a bare thumbnail.
  it('stages a photo as the picture with its name, not the general chip with a kind label', () => {
    const host = open();
    host.deliver('attachment_updated', { attachment: stagedPhoto });
    expect(screen.queryByText('Image')).toBeNull();
    expect(screen.getByText('nameplate.jpg')).toBeInTheDocument();
    expect(screen.getByAltText('nameplate.jpg')).toBeInTheDocument();
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
    expect(screen.getByText('Working on it…')).toBeInTheDocument();
    expect(document.querySelector('.agent-avatar')).toBeNull();
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
    expect(document.querySelector('.agent-avatar')).toBeNull();
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
