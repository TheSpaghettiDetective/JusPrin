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
    // canAdd: false -- this test is about which action the tap sends, not
    // about F7's leave-confirmation, so it uses the nothing-to-lose case.
    const host = open(state({ session: session({ canAdd: false }) }));
    expect(screen.getByRole('heading', { name: 'Let’s add your printer' })).toBeInTheDocument();
    expect(document.querySelector('.app--printer-setup')).not.toBeNull();

    await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
  });

  it('keeps the way out to OrcaSlicer’s own screens', async () => {
    const host = open(state({ session: session({ canAdd: false }) }));
    await userEvent.click(screen.getByRole('button', { name: /Choose printer manually|Set it up myself/ }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'manual_setup' });
  });

  it('shows a saved receipt and connection controls without an AI service', async () => {
    const host = open(state({ agent: { status: 'unavailable' }, session: session({
      canAdd: false, added: [{ name: 'Garage', model: 'A1 mini' }],
    }) }));
    expect(screen.getByRole('heading', { name: 'Your printer has been added' })).toBeVisible();
    expect(screen.queryByTestId('agent-not-configured')).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Connect printer' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'connect', id: 'Garage' });
  });

  it('keeps setup visible when the host adds the automatic printer greeting', async () => {
    const host = open(state({ agent: { status: 'unavailable' }, conversation: [], session: session({ canAdd: false }) }));
    expect(host.lastOfType('printer_opening')).toBeDefined();
    expect(screen.getByTestId('agent-not-configured')).toBeVisible();

    host.deliver('message_added', { message: state().conversation[0] });
    expect(screen.getByTestId('agent-not-configured')).toBeVisible();
    expect(screen.queryByText('What printer do you have?')).not.toBeInTheDocument();
    expect(screen.queryByText('NEW PRINTER')).not.toBeInTheDocument();
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();

    expect(screen.getByText('Not ready to set up the agent?')).toBeVisible();
    expect(screen.queryByRole('button', { name: /Choose printer manually|Set it up myself/ })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Add your printer manually' }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'manual_setup' });
    await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
    expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
  });

  it('navigates agent setup with a greeting already present and resumes the printer chat when ready', async () => {
    const host = open(state({ agent: { status: 'unavailable' }, session: session({ canAdd: false }) }));
    await userEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
    await userEvent.click(screen.getByRole('button', { name: 'Connect an AI tool you already use' }));
    expect(screen.getByTestId('setup-local-tools')).toBeVisible();
    await userEvent.click(screen.getByRole('button', { name: 'Back to setup options' }));
    await userEvent.click(screen.getByRole('button', { name: 'Close setup' }));
    expect(screen.getByTestId('agent-not-configured')).toBeVisible();

    await userEvent.click(screen.getByRole('button', { name: 'Set up the agent' }));
    await userEvent.click(screen.getByRole('button', { name: 'Use your own API key' }));
    await userEvent.type(screen.getByLabelText('OpenAI API key'), 'test-key');
    await userEvent.click(screen.getByRole('button', { name: 'Check key' }));
    expect(host.lastOfType('setup_check_key')!.payload).toEqual({ provider: 'openai', apiKey: 'test-key' });
    host.deliver('setup_status', { phase: 'verified', provider: 'openai', elapsedMs: 500 });
    expect(screen.getByTestId('setup-verified')).toBeVisible();
    expect(screen.queryByLabelText('Message the Agent')).not.toBeInTheDocument();

    host.deliver('agent_status', { status: 'ready' });
    expect(screen.queryByTestId('setup-api-key')).not.toBeInTheDocument();
    expect(screen.getByText('What printer do you have?')).toBeVisible();
    expect(document.querySelector('.printer-pinned')).toBeNull();
    expect(screen.getByLabelText('Message the Agent')).toBeEnabled();
    expect(screen.getByRole('button', { name: 'Add a photo' })).toBeEnabled();
    await userEvent.type(screen.getByLabelText('Message the Agent'), 'Bambu A1 mini{Enter}');
    expect(host.lastOfType('user_message')!.payload).toMatchObject({ text: 'Bambu A1 mini' });
  });

  it('keeps the add-printer fallback out of an existing printer’s settings', () => {
    open(state({ agent: { status: 'unavailable' }, session: session({ mode: 'change', canAdd: false }) }));
    expect(screen.queryByRole('button', { name: 'Add your printer manually' })).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Choose printer manually|Set it up myself/ })).toBeVisible();
  });

  // WP10 (F10), corrected by F7 on printer-panel-review-fixes-handoff.md:
  // message count alone caught a saved, applied Change too -- the panel
  // stays open after a printer_change succeeds, and nothing said there was
  // actually going to be lost. Gate on unsaved work instead: an Add
  // proposal still on its card, a Change still waiting on its own approval
  // card, a staged photo, or a draft.
  describe('leaving a conversation that holds unsaved work', () => {
    const withProposal = () =>
      open(
        state({
          conversation: [
            { id: 'm-1', role: 'assistant', state: 'complete', text: 'What printer do you have?', attempt: 1 },
            { id: 'm-2', role: 'user', state: 'complete', text: 'bambu a1 mini', attempt: 1 },
          ],
          session: session({ canAdd: true }),
        }),
      );

    it('asks before "‹ Printers" discards an Add proposal, and does nothing until answered', async () => {
      const host = withProposal();
      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      expect(screen.getByRole('dialog', { name: 'Leave this conversation?' })).toBeInTheDocument();
      expect(host.received.some((envelope) => envelope.type === 'printer_action')).toBe(false);
    });

    it('leaves on confirmation, and does nothing on cancel', async () => {
      const host = withProposal();
      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
      expect(screen.queryByRole('dialog')).toBeNull();
      expect(host.received.some((envelope) => envelope.type === 'printer_action')).toBe(false);

      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      await userEvent.click(screen.getByRole('button', { name: 'Leave' }));
      expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
    });

    it('asks before "Set it up myself" discards an Add proposal too', async () => {
      const host = withProposal();
      await userEvent.click(screen.getByRole('button', { name: /Choose printer manually|Set it up myself/ }));
      expect(screen.getByRole('dialog', { name: 'Leave this conversation?' })).toBeInTheDocument();
      await userEvent.click(screen.getByRole('button', { name: 'Leave' }));
      expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'manual_setup' });
    });

    it('asks before discarding a Change still waiting on its own approval card', async () => {
      const host = open(
        state({
          session: session({ mode: 'change', canAdd: false }),
          toolActivities: [
            {
              actionId: 't-1',
              correlationId: 'm-2',
              server: 'jusprin-native',
              tool: 'printer_change',
              title: 'Change nozzle',
              arguments: { nozzle: 0.6 },
              actionClass: 'mutation',
              requiresApproval: true,
              sessionId: '1',
              expectedRevision: 1,
              state: 'pending',
              progress: { current: 0, total: 1 },
            },
          ],
        }),
      );
      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      expect(screen.getByRole('dialog', { name: 'Leave this conversation?' })).toBeInTheDocument();
      await userEvent.click(screen.getByRole('button', { name: 'Leave' }));
      expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
    });

    it('closes at once after a Change already applied -- there is nothing left to lose', async () => {
      const host = open(
        state({
          conversation: [
            { id: 'm-1', role: 'assistant', state: 'complete', text: 'This is the Bambu Lab A1 mini.', attempt: 1 },
            { id: 'm-2', role: 'user', state: 'complete', text: 'I put a 0.6 nozzle on it', attempt: 1 },
            { id: 'm-3', role: 'note', state: 'complete', text: 'Nozzle set to 0.6 mm', attempt: 1 },
          ],
          session: session({ mode: 'change', canAdd: false }),
          toolActivities: [
            {
              actionId: 't-1',
              correlationId: 'm-2',
              server: 'jusprin-native',
              tool: 'printer_change',
              title: 'Change nozzle',
              arguments: { nozzle: 0.6 },
              actionClass: 'mutation',
              requiresApproval: true,
              sessionId: '1',
              expectedRevision: 1,
              state: 'succeeded',
              progress: { current: 1, total: 1 },
            },
          ],
        }),
      );
      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      expect(screen.queryByRole('dialog')).toBeNull();
      expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
    });

    // F7 follow-up: state.draft is the main chat's own recovery draft --
    // the backend only ever sets it from a draft_update that panel sends,
    // and the printer panel's composer never did, so state.draft could
    // never answer this check for the printer panel. Past the composer's
    // own debounce, an untouched but typed-into field must still gate.
    it('asks before "‹ Printers" discards an unsent draft, never mind state.draft', async () => {
      const host = open(state({ session: session({ canAdd: false }) }));
      await userEvent.type(screen.getByPlaceholderText(/bambu a1 mini/), 'I put a 0.6 nozzle on it');
      await act(async () => {
        await new Promise((resolve) => setTimeout(resolve, 350));
      });
      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      expect(screen.getByRole('dialog', { name: 'Leave this conversation?' })).toBeInTheDocument();
      expect(host.received.some((envelope) => envelope.type === 'printer_action')).toBe(false);
    });

    it('asks nothing of a thread that only holds the opening', async () => {
      // The default session() fixture carries canAdd: true (many other
      // tests in this file need a proposed card); this one wants the
      // genuinely-empty case, so it says so explicitly.
      const host = open(state({ session: session({ canAdd: false }) }));
      await userEvent.click(screen.getByRole('button', { name: /Back to (Home|printers)/ }));
      expect(screen.queryByRole('dialog')).toBeNull();
      expect(host.lastOfType('printer_action')!.payload).toMatchObject({ action: 'close' });
    });
  });

  it('keeps the conversation and Add action without a pinned hardware summary', () => {
    open();
    expect(document.querySelector('.printer-pinned')).toBeNull();
    expect(document.querySelector('.pinned-setup')).toBeNull();
    expect(screen.queryByText('Nozzle')).toBeNull();
    expect(screen.queryByText('Plate')).toBeNull();
    expect(screen.queryByText('Filament')).toBeNull();
    expect(screen.getByRole('button', { name: 'Add this printer' })).toBeEnabled();
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
    expect((openings[0].payload as { text: string }).text).toMatch(
      /^Which printer do you have\? Tell me the brand and model/,
    );
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

  it('draws the card a network printer brings under the note that records it', () => {
    open(
      state({
        conversation: [
          { id: 'm-1', role: 'assistant', state: 'complete', text: 'What printer do you have?', attempt: 1 },
          { id: 'n-2', role: 'note', state: 'complete', text: 'The person chose the network printer 01P00A3B.', attempt: 1 },
        ],
        session: session({
          blocks: [
            {
              id: 'b3',
              seq: 3,
              afterMessageId: 'n-2',
              kind: 'printers',
              printers: [
                {
                  catalogId: 'BBL/Bambu Lab A1 mini',
                  deviceId: '01P00A3B',
                  name: 'Bambu Lab A1 mini',
                  buildVolume: '180 × 180 × 180 mm',
                  picture: '',
                  action: 'add',
                  assumed: { nozzle: 0.4, plate: 'Textured PEI Plate', filament: 'Bambu PLA Basic @BBL A1M' },
                  device: { nozzle: 0.4, ams: '', spools: [], reported: true },
                },
              ],
            },
          ],
        }),
      }),
    );
    expect(screen.getByText('0.4 mm nozzle · read from the printer just now')).toBeInTheDocument();
  });

  it('follows the session the host sends after the state', () => {
    const host = open();
    host.deliver('printer_session', session({ mode: 'change', canAdd: false }));
    expect(document.querySelector('.app--printer-setup')).toBeNull();
    expect(document.querySelector('.printer-pinned')).toBeNull();
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
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
    expect(screen.getByText('Looking through the printer list…')).toBeInTheDocument();
    expect(document.querySelector('.agent-avatar')).toBeNull();
  });

  // F8 review fix: the words used to be hard-coded to Add's own tool
  // (printer_identify looks through a list); Change's only tool
  // (printer_change) does not, so it says something else.
  it('names the work in Change mode with Change mode\'s own words', () => {
    open(
      state({
        session: session({ mode: 'change', canAdd: false }),
        conversation: [
          { id: 'm-1', role: 'user', state: 'complete', text: 'I put a 0.6 nozzle on it', attempt: 1 },
          { id: 'm-2', role: 'assistant', state: 'complete', text: '', attempt: 1 },
        ],
        streamingMessageId: 'm-2',
      }),
    );
    expect(screen.getByText('Working on it…')).toBeInTheDocument();
    expect(screen.queryByText('Looking through the printer list…')).not.toBeInTheDocument();
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
    expect(screen.queryByText('Looking through the printer list…')).not.toBeInTheDocument();
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
