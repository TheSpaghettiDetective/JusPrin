// Deterministic interaction tests: a scripted mock host plays the native side
// of the bridge while the real page runs in jsdom.

import { describe, expect, it } from 'vitest';
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { App } from './App';
import { Envelope, HistoryEntry, OnboardingState, PrinterInfo, ProjectInfo, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from './bridge/protocol';

class MockHost {
  received: Envelope[] = [];

  transport = {
    post: (json: string) => {
      this.received.push(JSON.parse(json));
    },
  };

  deliver(type: string, payload: unknown): void {
    const envelope: Envelope = {
      protocol: PROTOCOL_NAME,
      version: PROTOCOL_VERSION,
      id: `h-${this.received.length}-${type}`,
      type,
      payload,
    };
    act(() => {
      window.__jusprinBridge!.deliver(envelope);
    });
  }

  lastOfType(type: string): Envelope | undefined {
    return [...this.received].reverse().find((e) => e.type === type);
  }

  ofType(type: string): Envelope[] {
    return this.received.filter((e) => e.type === type);
  }

  messages(): Envelope[] {
    return this.received;
  }

  clear(): void {
    this.received = [];
  }
}

function project(overrides: Partial<ProjectInfo> = {}): ProjectInfo {
  return {
    id: 'p1',
    name: 'Garage bracket',
    path: 'C:/projects/garage.3mf',
    status: { kind: 'sent', text: 'Sent Oct 20 · X1 Carbon · PLA' },
    ...overrides,
  };
}

function printer(overrides: Partial<PrinterInfo> = {}): PrinterInfo {
  return {
    id: 'x1',
    name: 'X1 Carbon',
    kind: 'named',
    canOpenSettings: true,
    canRename: true,
    canRemove: true,
    status: 'printing',
    job: 'Cable clips',
    progressPercent: 43,
    secondsLeft: 7200,
    connectionLabel: 'LAN',
    modelText: 'X1 Carbon · 0.4 mm',
    spools: [
      { material: 'PLA', colour: '#9A9A9A' },
      { material: 'PLA', colour: '#C8202D' },
      { material: 'PLA', colour: '#FFFFFF' },
      { material: 'PLA', colour: '#000000' },
    ],
    action: 'monitor',
    ...overrides,
  };
}

function onboarding(overrides: Partial<OnboardingState> = {}): OnboardingState {
  return {
    visible: false,
    step: 'hidden',
    printersScreen: 'add_printer',
    signIn: 'idle',
    email: null,
    v1Imported: false,
    signInError: '',
    accountEmail: '',
    agentConfigured: true,
    agentKeyProvider: '',
    orca: { source: '', printers: [], selected: '', filamentCount: 0, found: false, importResult: null },
    printerForm: null,
    agentCard: 'none',
    account: null,
    ...overrides,
  };
}

function emailStep(overrides: Partial<NonNullable<OnboardingState['email']>> = {}): OnboardingState['email'] {
  return { step: 'code', email: 'alex@example.com', busy: false, refusal: '', codeResent: false, ...overrides };
}

// An account fresh from signing in: the first month's credits, untouched.
function account(credits: number): OnboardingState['account'] {
  return {
    credits: { total: credits, lots: [{ kind: 'signup', credits, granted: credits, expires_at: '2099-12-02T12:00:00Z' }] },
    plan: null,
    v1_subscription: null,
  } as unknown as OnboardingState['account'];
}

function state(overrides: Partial<StatePayload> = {}): StatePayload {
  return { appearance: 'light', projects: [project()], printers: [printer()], onboarding: onboarding(), ...overrides };
}

function start(): MockHost {
  const host = new MockHost();
  render(<App getTransport={() => host.transport} />);
  host.deliver('hello_ack', { protocolVersion: PROTOCOL_VERSION });
  return host;
}

describe('Home', () => {
  it('opens with a hello carrying the page protocol version', () => {
    const host = new MockHost();
    render(<App getTransport={() => host.transport} />);
    const hello = host.lastOfType('hello');
    expect(hello).toBeDefined();
    expect((hello!.payload as { protocolVersions: number[] }).protocolVersions).toEqual([PROTOCOL_VERSION]);
  });

  it('says the projects could not be read rather than showing an empty gallery', () => {
    const host = start();
    host.deliver('state', state({ projects: [], projectsProblem: "Your projects couldn't be loaded." }));
    expect(screen.getByRole('alert')).toHaveTextContent("Your projects couldn't be loaded.");
    expect(screen.queryByText('Drop a model or project to start')).not.toBeInTheDocument();
    host.deliver('state', state());
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
    expect(screen.getByText('Garage bracket')).toBeInTheDocument();
  });

  it('waits for the host rather than showing an empty gallery', () => {
    const host = new MockHost();
    render(<App getTransport={() => host.transport} />);
    expect(screen.getByText('Connecting…')).toBeInTheDocument();
    expect(screen.queryByText('Drop a model or project to start')).not.toBeInTheDocument();
    host.deliver('hello_ack', {});
    host.deliver('state', state({ projects: [], printers: [] }));
    expect(screen.getByText('Drop a model or project to start')).toBeInTheDocument();
  });

  it('renders a card per project and opens the one that is clicked', async () => {
    const host = start();
    host.deliver('state', state({ projects: [project(), project({ id: 'p2', name: 'Phone stand' })] }));
    expect(screen.getByText('Garage bracket')).toBeInTheDocument();
    await userEvent.click(screen.getByText('Phone stand'));
    expect(host.lastOfType('open_project')!.payload).toEqual({ id: 'p2' });
  });

  // Only a printing project is tinted and dotted; an idle one takes the plain
  // line, or the two states become indistinguishable.
  it('marks only a printing project', () => {
    const host = start();
    host.deliver(
      'state',
      state({
        projects: [
          project(),
          project({
            id: 'p2',
            name: 'Vent grille',
            path: 'C:/projects/vent.3mf',
            status: { kind: 'printing', text: 'Printing on X1 Carbon' },
          }),
        ],
      }),
    );
    const idle = screen.getByText('Garage bracket').closest<HTMLElement>('.project-card')!;
    const printing = screen.getByText('Vent grille').closest<HTMLElement>('.project-card')!;
    expect(idle.className).not.toContain('printing');
    expect(idle).not.toHaveAttribute('title');
    expect(printing.className).toContain('printing');
    expect(within(printing).getByText('Printing on X1 Carbon')).toBeInTheDocument();
    expect(printing.querySelectorAll('.status-dot.printing')).toHaveLength(1);
    expect(idle.querySelectorAll('.status-dot')).toHaveLength(0);
  });

  it('fits a thumbnail whole and keeps the frame when there is none', () => {
    const host = start();
    host.deliver(
      'state',
      state({
        projects: [
          project({ thumbnailUrl: 'file:///thumbs/garage.png' }),
          project({ id: 'p2', name: 'No render' }),
        ],
      }),
    );
    // The image is decorative: the card is titled by its name, so the thumbnail
    // carries an empty alt and is found by selector rather than by role.
    expect(document.querySelector('.project-thumbnail img')).toHaveAttribute('src', 'file:///thumbs/garage.png');
    const bare = screen.getByText('No render').closest('button')!;
    expect(bare.querySelectorAll('.project-thumbnail')).toHaveLength(1);
  });

  it('shows a printing printer with its job and a never-connected one with Connect…', async () => {
    const host = start();
    const never = printer({
      id: 'mk4',
      name: 'Prusa MK4',
      status: 'not_connected',
      job: undefined,
      progressPercent: undefined,
      secondsLeft: undefined,
      connectionLabel: undefined,
      message: undefined,
      noConnection: true,
      modelText: 'MK4 · 0.4 mm',
      spools: [],
      action: 'connect',
    });
    host.deliver('state', state({ printers: [printer(), never] }));
    const printing = screen.getByText('X1 Carbon').closest('.printer-card') as HTMLElement;
    expect(within(printing).getByText('· 43% · 2h 0m left', { exact: false })).toBeInTheDocument();
    expect(within(printing).getByText('Cable clips')).toBeInTheDocument();
    expect(within(printing).getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    expect(within(printing).getByText('LAN · X1 Carbon · 0.4 mm')).toBeInTheDocument();
    const idle = screen.getByText('Prusa MK4').closest('.printer-card') as HTMLElement;
    // No status word for a printer never connected: what connecting is for, beside Connect.
    expect(within(idle).queryByText('Not connected')).toBeNull();
    expect(idle.querySelector('.printer-status')).toBeNull();
    expect(within(idle).getByText('Connect it to send prints straight from JusPrin')).toBeInTheDocument();
    await userEvent.click(within(idle).getByRole('button', { name: 'Connect…' }));
    expect(host.lastOfType('connect_printer')!.payload).toEqual({ id: 'mk4' });
  });

  it('draws one bordered swatch per spool', () => {
    const host = start();
    host.deliver('state', state());
    const swatches = document.querySelectorAll('.spool-swatch');
    expect(swatches).toHaveLength(4);
    expect((swatches[3] as HTMLElement).style.background).toBe('rgb(0, 0, 0)');
  });

  it('sends the shell the actions it owns', async () => {
    const host = start();
    host.deliver('state', state());
    await userEvent.click(screen.getByText('Open…'));
    await userEvent.click(screen.getByText('New'));
    await userEvent.click(screen.getByText('Launch monitor'));
    await userEvent.click(screen.getByText('+ Add printer'));
    expect(host.lastOfType('open_file')).toBeDefined();
    expect(host.lastOfType('new_project')).toBeDefined();
    expect(host.lastOfType('launch_monitor')!.payload).toEqual({ id: 'x1' });
    expect(host.lastOfType('add_printer')).toBeDefined();
  });

  // Open… is the one primary action; a blank project is the compact outlined
  // button beside it, so it never competes with Open.
  it('offers Open as the primary and New as the compact secondary', () => {
    const host = start();
    host.deliver('state', state());
    expect(screen.getByRole('button', { name: 'Open…' })).toHaveClass('button-primary');
    expect(screen.getByRole('button', { name: 'New' })).toHaveClass('button-secondary', 'button-secondary-small');
  });

  it('sends the files dropped anywhere on the page to be opened', async () => {
    const host = start();
    host.deliver('state', state({ projects: [] }));
    const model = new File(['solid cube'], 'cube.stl', { type: 'model/stl' });
    const page = document.querySelector('.app')!;
    fireEvent.dragOver(page, { dataTransfer: { files: [model] } });
    fireEvent.drop(screen.getByText('Drop a model or project to start'), { dataTransfer: { files: [model] } });
    await waitFor(() => expect(host.lastOfType('open_dropped_files')).toBeDefined());
    const payload = host.lastOfType('open_dropped_files')!.payload as { files: { name: string; dataBase64: string }[] };
    expect(payload.files).toHaveLength(1);
    expect(payload.files[0].name).toBe('cube.stl');
    expect(atob(payload.files[0].dataBase64.split(';base64,')[1])).toBe('solid cube');
  });

  it('opens Preferences from the gear and from Ctrl+,', async () => {
    const host = start();
    host.deliver('state', state());
    await userEvent.click(screen.getByRole('button', { name: 'Preferences' }));
    expect(host.received.filter((envelope) => envelope.type === 'open_preferences')).toHaveLength(1);
    await userEvent.keyboard('{Control>},{/Control}');
    expect(host.received.filter((envelope) => envelope.type === 'open_preferences')).toHaveLength(2);
  });

  it('offers connection and printer actions from each named card header', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer(), printer({ id: 'mk4', name: 'Prusa MK4', status: 'ready', job: undefined, progressPercent: undefined })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Prusa MK4' }));
    const menu = screen.getByRole('menu');
      expect(within(menu).getAllByRole('menuitem').map((item) => item.textContent)).toEqual([
        'Connect…',
      'Printer settings…',
      'Ask AI about printer settings…',
      'Calibrate…',
      'Rename',
      'Remove printer…',
    ]);
    await userEvent.click(within(menu).getByText('Printer settings…'));
    expect(host.lastOfType('open_printer_settings')!.payload).toEqual({ id: 'mk4' });
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Prusa MK4' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Ask AI about printer settings…' }));
    expect(host.lastOfType('ask_about_printer_settings')!.payload).toEqual({ id: 'mk4' });
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
  });

  it('starts a calibration on a named printer from its card header', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ id: 'mk4', name: 'Prusa MK4', status: 'ready', job: undefined, progressPercent: undefined })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Prusa MK4' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Calibrate…' }));
    expect(host.lastOfType('calibrate_printer')!.payload).toEqual({ id: 'mk4' });
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
  });

  // The question the host then asks on Home (board 3): the Confirmation
  // dialog, answered once with its request id.
  it("asks the host's question over Home and sends the answer back", async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ id: 'mk4', name: 'Prusa MK4', status: 'ready', job: undefined, progressPercent: undefined })] }));
    host.deliver('confirm', {
      requestId: 'calibrate-2',
      title: 'Start a calibration test?',
      body: 'Backpack hook will be saved and closed. A new project opens with the test on Prusa MK4.',
      confirm: 'Start test',
      cancel: 'Cancel',
    });
    const dialog = screen.getByRole('dialog', { name: 'Start a calibration test?' });
    expect(dialog).toHaveTextContent('with the test on Prusa MK4');
    expect(within(dialog).getAllByRole('button').map((button) => button.textContent)).toEqual(['Cancel', 'Start test']);
    await userEvent.click(within(dialog).getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('confirm_answer')!.payload).toEqual({ requestId: 'calibrate-2', confirmed: false });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  });

  it('connects an existing saved printer without asking to add it again', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ status: 'not_connected', action: 'connect' })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Connect…' }));
    expect(host.lastOfType('connect_printer')!.payload).toEqual({ id: 'x1' });
    expect(host.lastOfType('add_printer')).toBeUndefined();
  });

  it('renames a named printer after checking the new name', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer(), printer({ id: 'mk4', name: 'Prusa MK4', status: 'ready', job: undefined, progressPercent: undefined })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));
    const dialog = screen.getByRole('dialog', { name: 'Rename printer' });
    const input = within(dialog).getByLabelText('Printer name');
    expect(input).toHaveValue('X1 Carbon');

    await userEvent.clear(input);
    await userEvent.type(input, 'prusa mk4');
    expect(within(dialog).getByRole('alert')).toHaveTextContent('Another printer is already named');
    expect(within(dialog).getByRole('button', { name: 'Save' })).toBeDisabled();

    await userEvent.clear(input);
    await userEvent.type(input, 'Garage/X1');
    expect(within(dialog).getByRole('alert')).toHaveTextContent('can’t contain');

    await userEvent.clear(input);
    await userEvent.type(input, '  Garage X1C  {Enter}');
    expect(host.lastOfType('rename_printer')!.payload).toEqual({ id: 'x1', name: 'Garage X1C' });
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
  });

  it('removes a named printer only once the person confirms', async () => {
    const host = start();
    host.deliver('state', state());
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Remove printer…' }));
    const dialog = screen.getByRole('dialog', { name: 'Remove printer?' });
    await userEvent.click(within(dialog).getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('remove_printer')).toBeUndefined();

    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Remove printer…' }));
    await userEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Remove' }));
    expect(host.lastOfType('remove_printer')!.payload).toEqual({ id: 'x1' });
  });

  // A device's name and binding are Orca's to change, in Orca's dialogs, so
  // the page asks nothing itself and never sends a name.
  it('hands a device rename and removal to the host, and disables what it cannot do', async () => {
    const host = start();
    const device = printer({ id: 'device:FAKE001', name: 'Lab P1S', kind: 'device', canOpenSettings: false });
    host.deliver('state', state({ printers: [device] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Lab P1S' }));
    expect(screen.getByRole('menuitem', { name: 'Printer settings…' })).toBeDisabled();
    expect(screen.getByRole('menuitem', { name: 'Ask AI about printer settings…' })).toBeDisabled();
    await userEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(host.lastOfType('rename_printer')!.payload).toEqual({ id: 'device:FAKE001' });

    await userEvent.click(screen.getByRole('button', { name: 'Actions for Lab P1S' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Remove printer…' }));
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    expect(host.lastOfType('remove_printer')!.payload).toEqual({ id: 'device:FAKE001' });
  });

  it('gives a card with no available actions no menu button', () => {
    const host = start();
    const lan = printer({
      id: 'device:LAN001',
      name: 'Bench A1 mini',
      kind: 'device',
      canOpenSettings: false,
      canRename: false,
      canRemove: false,
    });
    host.deliver('state', state({ printers: [lan] }));
    expect(screen.getByText('Bench A1 mini')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Actions for Bench A1 mini' })).not.toBeInTheDocument();
  });

  it('highlights a printer the conversation just added, for that one state', () => {
    const host = start();
    host.deliver('state', state({ highlightPrinter: 'X1 Carbon' }));
    expect(document.querySelectorAll('.printer-card')[0]).toHaveClass('printer-card-added');

    host.deliver('state', state());
    expect(document.querySelector('.printer-card-added')).toBeNull();
  });

  it('shows a refused printer action until the next one', async () => {
    const host = start();
    host.deliver('state', state());
    host.deliver('printer_error', { id: 'x1', message: 'That name is reserved. Choose another.' });
    host.deliver('state', state());
    expect(screen.getByRole('alert')).toHaveTextContent('That name is reserved. Choose another.');
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Printer settings…' }));
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
  });

  // The host pushes a fresh state while Home is on screen whenever a card
  // changes. Whatever the person has open on a card has to survive it.
  it('keeps an open card menu open while its printer changes state', async () => {
    const host = start();
    const idle = printer({ status: 'ready', job: undefined, progressPercent: undefined });
    host.deliver('state', state({ printers: [idle] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    expect(screen.getByRole('menu')).toBeInTheDocument();

    host.deliver('state', state({ printers: [printer()] }));
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    expect(screen.getByRole('menu')).toBeInTheDocument();

    host.deliver('state', state({ printers: [printer({ status: 'not_connected', message: 'The printer stopped sending updates.' })] }));
    expect(screen.getByText('The printer stopped sending updates.')).toBeInTheDocument();
    expect(screen.getByRole('menu')).toBeInTheDocument();
  });

  it('keeps a rename in progress while its printer changes state', async () => {
    const host = start();
    host.deliver('state', state());
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));
    const input = within(screen.getByRole('dialog', { name: 'Rename printer' })).getByLabelText('Printer name');
    await userEvent.clear(input);
    await userEvent.type(input, 'Garage');

    host.deliver('state', state({ printers: [printer({ progressPercent: 44 })] }));
    expect(screen.getByRole('dialog', { name: 'Rename printer' })).toBeInTheDocument();
    expect(input).toHaveValue('Garage');
  });

  it('follows the host into dark mode', () => {
    const host = start();
    host.deliver('state', state({ appearance: 'dark' }));
    expect(document.documentElement.dataset.appearance).toBe('dark');
    host.deliver('appearance', { appearance: 'light' });
    expect(document.documentElement.dataset.appearance).toBe('light');
  });
});

describe('first launch', () => {
  const welcome = (overrides: Partial<OnboardingState> = {}) =>
    state({ onboarding: onboarding({ visible: true, step: 'welcome', agentConfigured: false, ...overrides }) });

  it('offers the four doors on the welcome and sends each one', async () => {
    const host = start();
    host.deliver('state', welcome());

    expect(screen.getByRole('heading', { name: 'The AI slicer that fixes failed prints.' })).toBeInTheDocument();
    expect(screen.getByText('Sign up to use AI, or skip it and just slice.')).toBeInTheDocument();
    expect(screen.getByText('Sign up and get 500 free credits')).toBeInTheDocument();
    expect(screen.getByText('Good for your first month. That’s about 100 messages to AI.')).toBeInTheDocument();
    expect(screen.getByText('After that, 50 free credits a month. No card needed.')).toBeInTheDocument();
    // No plan is named before sign-up.
    expect(screen.queryByText(/Expert|Standard/)).toBeNull();
    expect(screen.queryByText(/Free · MCP server/)).toBeNull();
    // The own-account door is a card that says what it is and what it costs.
    const own = screen.getByRole('button', { name: /^Use your own AI account/ });
    expect(own).toHaveTextContent('Use your own AI accountYour API key, or Claude Code or Claude DesktopFree');
    // The v1 line shows only after a v1 import.
    expect(screen.queryByText(/Subscribed to JusPrin v1/)).toBeNull();
    expect(screen.queryByText(/trial|\$/i)).toBeNull();
    // No Home header on the welcome: it fills the window.
    expect(screen.queryByRole('button', { name: 'Preferences' })).toBeNull();

    await userEvent.click(screen.getByRole('button', { name: 'Continue with Google' }));
    expect(host.lastOfType('onboarding_sign_in')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: /^Use your own AI account/ }));
    expect(host.lastOfType('onboarding_open_preferences')!.payload).toEqual({ door: 'key' });
    await userEvent.click(screen.getByRole('button', { name: 'Just slice for now' }));
    expect(host.lastOfType('onboarding_leave_welcome')).toBeDefined();
  });

  it('waits on the browser with a way to open it again, to use email, or to cancel', async () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'browser' }));
    expect(screen.getByText('Finish in your browser.')).toBeInTheDocument();
    expect(screen.getByText('The app continues on its own once the browser is done.')).toBeInTheDocument();
    // Nothing can ask the server whether a loopback sign-in finished, so there is no "Check sign-in".
    expect(screen.queryByRole('button', { name: 'Check sign-in' })).toBeNull();
    // The other doors stay open, as boards 12b to 13b draw them.
    expect(screen.getByRole('button', { name: /^Use your own AI account/ })).toBeEnabled();
    await userEvent.click(screen.getByRole('button', { name: 'Open the browser again' }));
    expect(host.lastOfType('onboarding_sign_in_reopen')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('onboarding_sign_in_cancel')).toBeDefined();

    host.clear();
    await userEvent.click(screen.getByRole('button', { name: 'Use an email code instead' }));
    expect(host.lastOfType('onboarding_sign_in_cancel')).toBeDefined();
    host.deliver('state', welcome());
    expect(screen.getByRole('textbox', { name: 'Email' })).toHaveFocus();

    host.deliver('state', welcome({ signInError: 'That sign-in expired. Try again.' }));
    expect(screen.getByRole('alert')).toHaveTextContent('That sign-in expired. Try again.');
  });

  it('closes the other doors only while the account is read', () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'checking' }));
    expect(screen.getByText('Signing you in…')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /^Use your own AI account/ })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Just slice for now' })).toBeDisabled();
  });

  it('sends the typed address, trimmed; Sign up free asks for the code with it, Sign in does not', async () => {
    const host = start();
    host.deliver('state', welcome());
    const send = screen.getByRole('button', { name: 'Sign up free' });
    expect(send).toBeDisabled();
    // Sign in with no address yet asks for one.
    await userEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    expect(host.lastOfType('onboarding_sign_in_email')).toBeUndefined();
    expect(screen.getByRole('textbox', { name: 'Email' })).toHaveFocus();
    await userEvent.type(screen.getByRole('textbox', { name: 'Email' }), ' alex@example.com ');
    host.clear();
    await userEvent.click(send);
    expect(host.messages().map((message) => [message.type, message.payload])).toEqual([
      ['onboarding_sign_in_email', { email: 'alex@example.com' }], ['onboarding_sign_in_send_code', {}]]);
    host.clear();
    await userEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    expect(host.messages().map((message) => message.type)).toEqual(['onboarding_sign_in_email']);
    expect(host.lastOfType('onboarding_sign_in_email')!.payload).toEqual({ email: 'alex@example.com' });
  });

  it('shows the code on its way after Sign up free, never the step that asks how', async () => {
    const host = start();
    host.deliver('state', welcome());
    await userEvent.type(screen.getByRole('textbox', { name: 'Email' }), 'alex@example.com{Enter}');
    // The host answers the address before it answers the code request.
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose' }) }));
    expect(screen.getByText('Sending a code to alex@example.com…')).toBeInTheDocument();
    expect(screen.queryByText('We’ll email you a code.')).toBeNull();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose', busy: true }) }));
    expect(screen.getByText('Sending a code to alex@example.com…')).toBeInTheDocument();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    expect(screen.getByText('Check your email.')).toBeInTheDocument();
    // A server that refuses to send one: the step that asks how, with why.
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose', refusal: 'too_many_codes' }) }));
    expect(screen.getByRole('alert')).toHaveTextContent('Too many codes were sent to this address.');
    expect(screen.getByRole('button', { name: 'Use a password instead' })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('onboarding_sign_in_cancel')).toBeDefined();
  });

  it('draws the door at rest as board 13a: Google, or, the address, the Terms', async () => {
    const host = start();
    host.deliver('state', welcome());
    expect(screen.getByRole('textbox', { name: 'Email' })).toHaveAttribute('placeholder', 'you@example.com');
    expect(screen.getByText('or')).toBeInTheDocument();
    expect(screen.queryByText(/same door/)).toBeNull();
    expect(screen.getByText(/Already have an account\?/)).toHaveTextContent('Already have an account? Sign in');
    expect(screen.queryByText(/Google opens your browser/)).toBeNull();
    expect(screen.getByText(/By continuing you agree to the/)).toHaveTextContent('By continuing you agree to the Terms and Privacy Policy.');
    await userEvent.click(screen.getByRole('button', { name: 'Terms' }));
    expect(host.lastOfType('onboarding_open_link')!.payload).toEqual({ link: 'terms' });
    await userEvent.click(screen.getByRole('button', { name: 'Privacy Policy' }));
    expect(host.lastOfType('onboarding_open_link')!.payload).toEqual({ link: 'privacy' });
  });

  it('tells a v1 subscriber they keep their price', () => {
    const host = start();
    host.deliver('state', welcome({ v1Imported: true }));
    expect(screen.getByText('Subscribed to JusPrin v1? You get these 500 credits and keep your original price.')).toBeInTheDocument();
  });

  it('takes the emailed code in six cells and sends it on the sixth digit', async () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    expect(screen.getByText('Check your email.')).toBeInTheDocument();
    expect(screen.getByText('We sent a 6-digit code to alex@example.com.')).toBeInTheDocument();
    expect(screen.getByText('Type the code from the email. You can open it on your phone.')).toBeInTheDocument();
    const field = screen.getByRole('textbox', { name: 'Sign-in code' });
    expect(field).toHaveFocus();
    expect(document.querySelectorAll('.code-cell')).toHaveLength(6);

    await userEvent.type(field, '41a8-2');
    expect(field).toHaveValue('4182');
    expect([...document.querySelectorAll('.code-cell')].map((cell) => cell.textContent)).toEqual(['4', '1', '8', '2', '', '']);
    expect(host.lastOfType('onboarding_sign_in_code')).toBeUndefined();
    await userEvent.type(field, '03');
    expect(host.lastOfType('onboarding_sign_in_code')!.payload).toEqual({ code: '418203' });
    // A seventh digit is not taken, and nothing is sent twice.
    await userEvent.type(field, '9');
    expect(host.ofType('onboarding_sign_in_code')).toHaveLength(1);
  });

  it('fills every cell from a pasted code and sends it', async () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    const field = screen.getByRole('textbox', { name: 'Sign-in code' });
    await userEvent.click(field);
    await userEvent.paste('Your code: 418 203');
    expect(field).toHaveValue('418203');
    expect(host.lastOfType('onboarding_sign_in_code')!.payload).toEqual({ code: '418203' });
  });

  it('says a wrong code is wrong and empties the cells', () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    const field = screen.getByRole('textbox', { name: 'Sign-in code' });
    fireEvent.change(field, { target: { value: '000000' } });
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ busy: true }) }));
    expect(field).toBeDisabled();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ refusal: 'wrong_code' }) }));
    expect(screen.getByRole('alert')).toHaveTextContent('That code is wrong.');
    expect(field).toHaveValue('');
    expect(field).toHaveAttribute('aria-invalid', 'true');
    expect(document.querySelector('.code-field.refused')).not.toBeNull();
  });

  it('offers a new code for one that no longer works, as board 12c draws it', async () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    fireEvent.change(screen.getByRole('textbox', { name: 'Sign-in code' }), { target: { value: '418203' } });
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ refusal: 'expired_code' }) }));
    expect(screen.getByRole('alert')).toHaveTextContent('That code no longer works. We can send a new one.');
    // The spent digits stay in their cells.
    expect(screen.getByRole('textbox', { name: 'Sign-in code' })).toHaveValue('418203');
    expect(screen.getAllByRole('button', { name: 'Send a new code' })).toHaveLength(1);
    await userEvent.click(screen.getByRole('button', { name: 'Send a new code' }));
    expect(host.lastOfType('onboarding_sign_in_send_code')).toBeDefined();

    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ codeResent: true }) }));
    expect(screen.getByText('We sent a new code to alex@example.com.')).toBeInTheDocument();
    expect(screen.getByRole('textbox', { name: 'Sign-in code' })).toHaveValue('');
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('gives the code step its links: a new code, change email, the browser, cancel', async () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    await userEvent.click(screen.getByRole('button', { name: 'Send a new code' }));
    expect(host.lastOfType('onboarding_sign_in_send_code')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('onboarding_sign_in_cancel')).toBeDefined();

    host.clear();
    await userEvent.click(screen.getByRole('button', { name: 'Sign in with Google instead' }));
    expect(host.messages().map((message) => message.type)).toEqual(['onboarding_sign_in_cancel', 'onboarding_sign_in']);
  });

  it('keeps the typed address for Change email', async () => {
    const host = start();
    host.deliver('state', welcome());
    await userEvent.type(screen.getByRole('textbox', { name: 'Email' }), 'alex@example.com');
    await userEvent.click(screen.getByRole('button', { name: 'Sign up free' }));
    host.deliver('state', welcome({ signIn: 'email', email: emailStep() }));
    await userEvent.click(screen.getByRole('button', { name: 'Change email' }));
    expect(host.lastOfType('onboarding_sign_in_cancel')).toBeDefined();
    host.deliver('state', welcome());
    const address = screen.getByRole('textbox', { name: 'Email' });
    expect(address).toHaveValue('alex@example.com');
    expect(address).toHaveFocus();
  });

  it('lets someone who signs in pick an emailed code or the password, sending nothing first', async () => {
    const host = start();
    host.deliver('state', welcome());
    await userEvent.type(screen.getByRole('textbox', { name: 'Email' }), 'alex@example.com');
    await userEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    expect(host.lastOfType('onboarding_sign_in_email')!.payload).toEqual({ email: 'alex@example.com' });
    expect(host.lastOfType('onboarding_sign_in_send_code')).toBeUndefined();

    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose' }) }));
    expect(screen.getByText('We’ll email you a code.')).toBeInTheDocument();
    expect(screen.getByText(/^alex@example\.com\./)).toHaveTextContent('alex@example.com. Change email');
    expect(screen.queryByText(/New here\?/)).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Email me a 6-digit code' }));
    expect(host.lastOfType('onboarding_sign_in_send_code')).toBeDefined();

    // The password stays out of the way until asked for.
    expect(screen.queryByLabelText('Password')).toBeNull();
    expect(screen.queryByRole('button', { name: 'Forgot password' })).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Use a password instead' }));
    const signIn = screen.getByRole('button', { name: 'Sign in' });
    expect(signIn).toBeDisabled();
    await userEvent.type(screen.getByLabelText('Password'), 'correct horse{Enter}');
    expect(host.lastOfType('onboarding_sign_in_password')!.payload).toEqual({ password: 'correct horse' });
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose', busy: true }) }));
    expect(screen.getByRole('button', { name: 'Email me a 6-digit code' })).toBeDisabled();
    expect(screen.getByLabelText('Password')).toBeDisabled();

    await userEvent.click(screen.getByRole('button', { name: 'Forgot password' }));
    expect(host.lastOfType('onboarding_open_link')!.payload).toEqual({ link: 'reset_password' });
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.lastOfType('onboarding_sign_in_cancel')).toBeDefined();
  });

  it('puts each refusal of the choice next to what was refused', () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose', refusal: 'wrong_password' }) }));
    expect(screen.getByRole('alert')).toHaveTextContent('That password is wrong.');
    expect(screen.getByLabelText('Password')).toBeInTheDocument();
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose', refusal: 'too_many_tries' }) }));
    expect(screen.getByRole('alert')).toHaveTextContent('Too many tries. Wait a few minutes, or email me a code.');
    host.deliver('state', welcome({ signIn: 'email', email: emailStep({ step: 'choose', refusal: 'too_many_codes' }) }));
    // Under the code button.
    expect(screen.getByRole('alert')).toHaveTextContent('Too many codes were sent to this address. Try again in an hour.');
  });

  it('says who signed in, then moves on by itself', async () => {
    const host = start();
    host.deliver('state', welcome({ signIn: 'checking' }));
    host.deliver('state', welcome({ accountEmail: 'kim@example.com', account: account(500) }));
    expect(screen.getByRole('status')).toHaveTextContent(
      'You got 500 free credits for signing up, good until 2 December.Signed in as kim@example.com');
    expect(host.lastOfType('onboarding_leave_welcome')).toBeUndefined();
    await waitFor(() => expect(host.lastOfType('onboarding_leave_welcome')).toBeDefined(), { timeout: 3000 });
  });

  it('waits for Continue from someone who comes back already signed in', async () => {
    const host = start();
    host.deliver('state', welcome({ accountEmail: 'kim@example.com' }));
    expect(screen.getByRole('status')).toHaveTextContent('Signed in as kim@example.com');
    await new Promise((resolve) => setTimeout(resolve, 1700));
    expect(host.lastOfType('onboarding_leave_welcome')).toBeUndefined();
    expect(screen.getByRole('button', { name: /^Use your own AI account/ })).toBeEnabled();
    await userEvent.click(screen.getByRole('button', { name: 'Continue' }));
    expect(host.lastOfType('onboarding_leave_welcome')).toBeDefined();
  });

  it('offers to bring the ticked OrcaSlicer printers, with Import, Choose my printer instead and Back', async () => {
    const host = start();
    const screenState = () => state({ onboarding: onboarding({ visible: true, step: 'printers', printersScreen: 'orca_import',
      orca: { source: '~/Library/Application Support/OrcaSlicer', printers: ['Bambu Lab P1S 0.4 nozzle', 'Prusa MK4'],
        selected: 'Bambu Lab P1S 0.4 nozzle', filamentCount: 14, found: true, importResult: null } }) });
    host.deliver('state', screenState());
    expect(screen.getByRole('heading', { name: 'Bring your printers from OrcaSlicer?' })).toBeInTheDocument();
    const p1s = screen.getByRole('checkbox', { name: 'Bambu Lab P1S 0.4 nozzle' });
    const mk4 = screen.getByRole('checkbox', { name: 'Prusa MK4' });
    expect(p1s).toBeChecked();
    expect(mk4).toBeChecked();
    expect(screen.getByText('~/Library/Application Support/OrcaSlicer')).toBeInTheDocument();
    expect(screen.getByText('Also brings 14 filaments.')).toBeInTheDocument();
    expect(screen.getByText('JusPrin starts with Bambu Lab P1S 0.4 nozzle, the printer OrcaSlicer had selected.')).toBeInTheDocument();
    // Unticking OrcaSlicer's printer names the one JusPrin will start with.
    await userEvent.click(p1s);
    expect(screen.getByText('JusPrin starts with Prusa MK4.')).toBeInTheDocument();
    // With nothing ticked there is nothing to import.
    await userEvent.click(mk4);
    expect(screen.getByRole('button', { name: 'Import' })).toBeDisabled();
    await userEvent.click(mk4);

    // The import holds the app, so the step says so first and asks once
    // that has been painted; the host's next state ends it.
    await userEvent.click(screen.getByRole('button', { name: 'Import' }));
    expect(screen.getByRole('button', { name: 'Importing…' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Choose my printer instead' })).toBeDisabled();
    expect(host.lastOfType('onboarding_import')).toBeUndefined();
    await waitFor(() => expect(host.lastOfType('onboarding_import')).toBeDefined());
    expect(host.lastOfType('onboarding_import')!.payload).toEqual({ printers: ['Prusa MK4'] });
    host.deliver('state', screenState());
    expect(screen.getByRole('button', { name: 'Import' })).toBeEnabled();
    // The same list pushed again keeps the ticks.
    expect(screen.getByRole('checkbox', { name: 'Bambu Lab P1S 0.4 nozzle' })).not.toBeChecked();
    await userEvent.click(screen.getByRole('button', { name: 'Choose my printer instead' }));
    expect(host.lastOfType('onboarding_start_fresh')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(host.lastOfType('onboarding_back')).toBeDefined();

    host.deliver('onboarding_error', { message: 'JusPrin couldn’t bring these printers. OrcaSlicer was not changed.' });
    expect(screen.getByRole('alert')).toHaveTextContent('OrcaSlicer was not changed');
  });

  it('lists what the OrcaSlicer import brought, each failure saying what to do, with Continue as its only button', async () => {
    const host = start();
    const orca = (importResult: OnboardingState['orca']['importResult']) => state({ onboarding: onboarding({
      visible: true, step: 'printers', printersScreen: 'import_result',
      orca: { source: '', printers: [], selected: '', filamentCount: 0, found: true, importResult } }) });
    const missingBase = { kind: 'filament' as const, name: 'Silk Red', renamedTo: '', imported: false,
      reason: 'This filament is built on Kingroon PLA Silk, which JusPrin doesn’t have. Pick a similar filament from the filament menu instead.' };
    host.deliver('state', orca([
      { kind: 'printer', name: 'Bambu Lab P1S 0.4 nozzle', renamedTo: '', imported: true, reason: '' },
      { kind: 'printer', name: 'Prusa MK4', renamedTo: 'Prusa MK4 (Orca copy)', imported: true, reason: '' },
      { kind: 'filament', name: 'My PLA', renamedTo: '', imported: true, reason: '' },
      missingBase,
      { kind: 'filament', name: 'Broken PLA', renamedTo: '', imported: false,
        reason: 'JusPrin couldn’t read this file. To try again, choose File › Import › Import from OrcaSlicer…' },
      { kind: 'process', name: '0.20mm Tuned', renamedTo: '', imported: true, reason: '' },
    ]));
    // The title stays, whatever failed; each failure has its own row.
    expect(screen.getByRole('heading', { name: 'Imported from OrcaSlicer' })).toBeInTheDocument();
    expect(screen.queryByText(/OrcaSlicer’s own files/)).toBeNull();
    const line = (name: string) => screen.getByText(name).closest('li')!;
    expect(line('Bambu Lab P1S 0.4 nozzle')).toHaveTextContent(/Imported$/);
    expect(line('Prusa MK4')).toHaveTextContent('Imported as ‘Prusa MK4 (Orca copy)’');
    expect(line('Silk Red')).toHaveTextContent(
      'This filament is built on Kingroon PLA Silk, which JusPrin doesn’t have. Pick a similar filament from the filament menu instead.');
    expect(line('0.20mm Tuned')).toHaveTextContent(/Imported$/);
    // Filaments are counted and the imported ones named; only the ones not imported have a line.
    expect(screen.queryByText('My PLA')).toBeNull();
    expect(line('Broken PLA')).toHaveTextContent('JusPrin couldn’t read this file. To try again, choose File › Import › Import from OrcaSlicer…');
    expect(screen.getByText('1 of 3 filaments imported: My PLA.')).toBeInTheDocument();
    // Trying again is File › Import, as the line says: Continue is the only button.
    expect(screen.getAllByRole('button').map((button) => button.textContent)).toEqual(['Continue']);
    await userEvent.click(screen.getByRole('button', { name: 'Continue' }));
    expect(host.lastOfType('onboarding_import_continue')).toBeDefined();

    // Everything imported: nothing to retry.
    host.deliver('state', orca([
      { kind: 'printer', name: 'Prusa MK4', renamedTo: '', imported: true, reason: '' },
      { kind: 'filament', name: 'My PLA', renamedTo: '', imported: true, reason: '' },
    ]));
    expect(screen.getByRole('heading', { name: 'Imported from OrcaSlicer' })).toBeInTheDocument();
    expect(screen.getByText('1 filament imported: My PLA.')).toBeInTheDocument();
  });

  it('offers no OrcaSlicer folder on the printer screen: File › Import does', () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'printers', printersScreen: 'add_printer',
      printerForm: null, orca: { source: '', printers: [], selected: '', filamentCount: 0, found: false, importResult: null } }) }));
    expect(screen.queryByText(/OrcaSlicer printers in another folder/)).toBeNull();
    expect(screen.queryByRole('button', { name: 'Choose the folder…' })).toBeNull();
  });

  it('asks which printer, then adds it with its profile’s plate and filament', async () => {
    const host = start();
    const form = (options?: object) => ({ catalog: [
      { id: 'c1', vendorId: 'v1', brand: 'Bambu Lab', model: 'A1 mini', nozzles: [0.2, 0.4, 0.6] },
      { id: 'c2', vendorId: 'v2', brand: 'Creality', model: 'Ender 3 V2', nozzles: [0.4] },
    ], nearby: [{ deviceId: 'SN1', name: 'A1 mini', catalogId: 'c1' }], error: '', loading: false, options,
    } as unknown as OnboardingState['printerForm']);
    const screenState = (printerForm: OnboardingState['printerForm'], overrides: Partial<OnboardingState> = {}) =>
      state({ onboarding: onboarding({ visible: true, step: 'printers', printersScreen: 'add_printer', printerForm,
        accountEmail: 'k+test@obico.io', account: { credits: { total: 120, lots: [] }, plan: null, v1_subscription: null,
          usage_30d: { credits: 0, days: [], activity: [] } },
        ...overrides }) });
    host.deliver('state', screenState(null));
    expect(screen.getByText('Reading the printer list…')).toBeInTheDocument();
    host.deliver('state', screenState(form()));
    expect(screen.getByRole('heading', { name: 'Which printer do you have?' })).toBeInTheDocument();
    // What the welcome finished, above the headline.
    expect(screen.getByText('You have 120 credits.')).toBeInTheDocument();
    expect(screen.getByText('Signed in as k+test@obico.io')).toBeInTheDocument();
    expect(screen.queryByText(/Step 1 of/)).toBeNull();

    // The fields are the Agent page's, drawn in their own shadow root.
    const picker = document.querySelector('.onboarding-printer-picker')!;
    await waitFor(() => expect(picker.shadowRoot!.textContent).toContain('Brand'));
    const shadow = within(picker.shadowRoot as unknown as HTMLElement);
    expect(shadow.getByText('Can’t find your printer?')).toBeInTheDocument();
    const continueButton = screen.getByRole('button', { name: 'Continue' });
    expect(continueButton).toBeDisabled();
    // Until a printer is chosen, what skipping does.
    const skipNote = 'If you skip, files are sliced for a 250 mm Klipper printer until you add yours, and may not print on other printers.';
    expect(screen.getByText(skipNote)).toBeInTheDocument();

    // The printer found on the network picks its model and the usual nozzle.
    await userEvent.click(shadow.getByRole('button', { name: /A1 mini · SN1/ }));
    await waitFor(() => expect(shadow.getByRole('radio', { name: '0.4 mm' })).toHaveAttribute('aria-checked', 'true'));
    expect(shadow.getByText('Most printers ship with 0.4 mm.')).toBeInTheDocument();
    expect(host.lastOfType('onboarding_printer_form')?.payload).toEqual({ action: 'form_options', catalogId: 'c1', nozzle: 0.4 });
    expect(continueButton).toBeDisabled();
    // While the app reads that printer's settings, the note under the nozzles says so.
    host.deliver('state', screenState({ ...form(), loading: true } as OnboardingState['printerForm']));
    await waitFor(() => expect(shadow.getByRole('status')).toHaveTextContent('Reading this printer’s settings…'));
    expect(shadow.queryByText('Most printers ship with 0.4 mm.')).toBeNull();

    host.deliver('state', screenState(form({ catalogId: 'c1', nozzle: 0.4, plates: [{ type: 1, label: 'Cool' }, { type: 2, label: 'Textured' }],
      defaultPlate: 2, filaments: [], others: [], own: [], suggested: [], defaultFilament: '', error: '' })));
    await waitFor(() => expect(screen.getByRole('button', { name: 'Continue' })).toBeEnabled());
    await userEvent.click(screen.getByRole('button', { name: 'Continue' }));
    // No filament: the profile's own is the default.
    expect(host.lastOfType('onboarding_printer_form')?.payload).toEqual(
      { action: 'form_finish', catalogId: 'c1', nozzle: 0.4, plate: 2, deviceId: 'SN1' });

    await userEvent.click(shadow.getByRole('button', { name: 'Ask AI' }));
    expect(host.lastOfType('onboarding_ask_agent')).toBeDefined();
    expect(screen.queryByText(skipNote)).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Skip for now' }));
    expect(host.lastOfType('onboarding_assume_default')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(host.lastOfType('onboarding_back')).toBeDefined();

    // After Just slice for now: nothing finished to say, and no agent to ask.
    host.deliver('state', screenState(form(), { accountEmail: '', account: null }));
    await waitFor(() => expect(shadow.queryByText('Can’t find your printer?')).toBeNull());
    expect(screen.queryByText(/Signed in as/)).toBeNull();
    // A saved key: the agent can help, and the line names its provider.
    host.deliver('state', screenState(form(), { accountEmail: '', account: null, agentKeyProvider: 'Anthropic' }));
    expect(screen.getByText('AI uses your Anthropic key')).toBeInTheDocument();
    await waitFor(() => expect(shadow.getByText('Can’t find your printer?')).toBeInTheDocument());
    // After the OrcaSlicer screen's Start fresh, that screen said it.
    host.deliver('state', screenState(form(), { orca: { source: '', printers: [], selected: '', filamentCount: 0, found: true,
      importResult: null } }));
    expect(screen.queryByText(/Signed in as/)).toBeNull();
  });

  it('opens the OrcaSlicer screen with what the welcome finished', () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'printers', printersScreen: 'orca_import',
      accountEmail: 'k+test@obico.io', account: { credits: { total: 1, lots: [] }, plan: null, v1_subscription: null,
        usage_30d: { credits: 0, days: [], activity: [] } },
      orca: { source: '/o', printers: ['Bambu Lab A1 mini 0.4 nozzle'], selected: '', filamentCount: 0, found: true,
        importResult: null } }) }));
    expect(screen.getByText('You have 1 credit.')).toBeInTheDocument();
    expect(screen.getByText('Signed in as k+test@obico.io')).toBeInTheDocument();
    expect(screen.getByText('JusPrin starts with Bambu Lab A1 mini 0.4 nozzle.')).toBeInTheDocument();
    // The import's result comes after it and does not say it again.
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'printers', printersScreen: 'import_result',
      accountEmail: 'k+test@obico.io', orca: { source: '', printers: [], selected: '', filamentCount: 0, found: true,
        importResult: [{ kind: 'printer', name: 'Bambu Lab A1 mini 0.4 nozzle', renamedTo: '', imported: true, reason: '' }] } }) }));
    expect(screen.queryByText(/Signed in as/)).toBeNull();
  });

  it('puts the three doors at the top of Home’s right column after Just slice', async () => {
    const host = start();
    host.deliver('state', state({ projects: [], printers: [printer({ name: 'Generic printer', assumed: true,
      status: 'not_connected', spools: [] })],
    onboarding: onboarding({ agentConfigured: false, agentCard: 'doors' }) }));
    const group = screen.getByRole('region', { name: 'AI' });
    expect(within(group).getByText('Set up AI')).toBeInTheDocument();
    expect(within(group).getByText('Get 500 free credits and ask AI to fix failed prints.')).toBeInTheDocument();
    // The card keeps its own why-line: the welcome's offer is not repeated here.
    expect(within(group).queryByText(/After that, 50 free credits a month/)).toBeNull();
    expect(within(group).getByRole('button', { name: 'Sign up free' })).toBeInTheDocument();
    expect(within(group).getByRole('button', { name: 'Sign in' })).toBeInTheDocument();
    await userEvent.click(within(group).getByRole('button', { name: 'Continue with Google' }));
    expect(host.lastOfType('onboarding_sign_in')).toBeDefined();
    await userEvent.click(within(group).getByRole('button', { name: /^Use your own AI account/ }));
    expect(host.lastOfType('onboarding_open_preferences')!.payload).toEqual({ door: 'key' });
    // The generic printer is no printer of theirs: the drop target and the
    // card say what files slice for until they add theirs.
    expect(within(document.querySelector('.first-run') as HTMLElement)
      .getByText(/files are sliced for a 250 mm Klipper printer and may not print on other printers\./)).toBeInTheDocument();
    expect(screen.queryByText('Generic printer')).toBeNull();
    const card = screen.getByText('No printer yet').closest('.printer-card')! as HTMLElement;
    expect(within(card).getByText('Until you add yours, files are sliced for a 250 mm Klipper printer and may not print on other printers.'))
      .toBeInTheDocument();
    expect(within(card).queryByRole('button', { name: /Connect|More/ })).toBeNull();
    expect(screen.queryByRole('button', { name: '+ Add printer' })).toBeNull();
    await userEvent.click(within(card).getByRole('button', { name: 'Add your printer' }));
    expect(host.lastOfType('add_printer')).toBeDefined();
  });

  it('shows the account and its credits once after signing in', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ agentCard: 'account', accountEmail: 'kim@example.com',
      account: account(500) }) }));
    const group = screen.getByRole('region', { name: 'AI' });
    expect(within(group).getByText('Signed in as kim@example.com')).toBeInTheDocument();
    expect(within(group).queryByText('Connected')).toBeNull();
    expect(within(group).getByText('500 credits left')).toBeInTheDocument();
    expect(within(group).getByText('Sign-up credits')).toBeInTheDocument();
    expect(within(group).getByText('500 of 500 left')).toBeInTheDocument();
    expect(within(group).queryByRole('button', { name: 'Dismiss' })).toBeNull();
    await userEvent.click(within(group).getByRole('button', { name: 'Hide' }));
    expect(host.lastOfType('agent_card_dismiss')).toBeDefined();
  });
});

  it('offers Reconnect only on a printer that was connected before', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ status: 'not_connected', action: 'reconnect',
      message: 'The printer stopped sending updates.' })] }));
    expect(screen.getByText('Not connected')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Reconnect' }));
    expect(host.lastOfType('connect_printer')!.payload).toEqual({ id: 'x1' });
  });

// The Main Page boards on the JusPrin v2 canvas: main-home-light and
// main-home-first-run-light.
describe('Home gallery and rail', () => {
  const many = (count: number) =>
    Array.from({ length: count }, (_, index) => project({ id: `p${index}`, name: `Part ${String.fromCharCode(74 - index)}` }));

  // The shell hides its project header on Home, so the page draws its own.
  it('heads the page with JusPrin › Home and Preferences', () => {
    const host = start();
    host.deliver('state', state());
    const header = document.querySelector('.home-header') as HTMLElement;
    expect(header.textContent).toBe('JusPrinHome');
    expect(within(header).getByRole('button', { name: 'Preferences' })).toBeInTheDocument();
    expect(document.querySelector('.printer-column')!.querySelector('.preferences-button')).toBeNull();
  });

  it('starts with one drop target that carries both actions and names the printer', () => {
    const host = start();
    host.deliver('state', state({ projects: [], printers: [printer({ name: 'Bambu Lab A1', status: 'ready' })] }));
    const header = document.querySelector('.gallery-header') as HTMLElement;
    expect(within(header).getByText('0')).toBeInTheDocument();
    expect(within(header).queryByRole('button')).toBeNull();
    expect(screen.queryByRole('searchbox')).toBeNull();
    const panel = document.querySelector('.first-run') as HTMLElement;
    expect(within(panel).getByText(/then plans the print for your Bambu Lab A1\.$/)).toBeInTheDocument();
    expect(within(panel).getByRole('button', { name: 'Open…' })).toHaveClass('button-primary');
    expect(within(panel).getByRole('button', { name: 'New' })).toHaveClass('button-secondary');
    expect(within(panel).getByText('Your projects will appear here, most recent first.')).toBeInTheDocument();
    expect(document.querySelectorAll('.button-primary')).toHaveLength(1);
  });

  it('leaves the printer out of the first-run line when there is none', () => {
    const host = start();
    host.deliver('state', state({ projects: [], printers: [] }));
    expect(screen.getByText(/JusPrin asks what the part is for, then plans the print\.$/)).toBeInTheDocument();
  });

  it('leads the grid with a drop tile that opens the file dialog', async () => {
    const host = start();
    host.deliver('state', state());
    const grid = document.querySelector('.project-grid') as HTMLElement;
    expect(grid.firstElementChild).toHaveClass('drop-tile');
    expect(within(grid).getByText('STL · 3MF · STEP · OBJ · AMF · SVG · ZIP')).toBeInTheDocument();
    await userEvent.click(within(grid).getByText('Drop a model or project here'));
    expect(host.lastOfType('open_file')).toBeDefined();
  });

  it('opens a file dropped on the tile like one dropped anywhere else', async () => {
    const host = start();
    host.deliver('state', state());
    const model = new File(['solid cube'], 'cube.stl', { type: 'model/stl' });
    fireEvent.drop(screen.getByText('Drop a model or project here'), { dataTransfer: { files: [model] } });
    await waitFor(() => expect(host.lastOfType('open_dropped_files')).toBeDefined());
    expect(host.lastOfType('open_file')).toBeUndefined();
  });

  it('titles a card with when it was saved, over the host status line', () => {
    const host = start();
    const today = new Date();
    today.setHours(0, 1);
    host.deliver('state', state({
      projects: [
        project({ id: 'a', name: 'Vent grille', updatedAt: today.toISOString(), status: { kind: 'draft', text: 'Draft' } }),
        project({ id: 'b', name: 'Phone stand', status: { kind: 'unknown', text: '' } }),
      ],
    }));
    const [first, second] = [...document.querySelectorAll('.project-card')] as HTMLElement[];
    expect(within(first).getByText('Today')).toHaveClass('project-date');
    expect(within(first).getByText('Draft').closest('.project-status')).not.toBeNull();
    // No time and no line: neither is drawn empty.
    expect(second.querySelector('.project-date')).toBeNull();
    expect(second.querySelector('.project-status')).toBeNull();
  });

  it('offers search and sort only once the gallery is long', () => {
    const host = start();
    host.deliver('state', state({ projects: many(7) }));
    expect(screen.queryByRole('searchbox')).toBeNull();
    expect(screen.queryByRole('combobox', { name: 'Sort projects' })).toBeNull();
    host.deliver('state', state({ projects: many(8) }));
    expect(screen.getByRole('searchbox', { name: 'Search projects' })).toBeInTheDocument();
    expect(screen.getByRole('combobox', { name: 'Sort projects' })).toHaveValue('recent');
    expect(within(document.querySelector('.gallery-header') as HTMLElement).getByText('8')).toBeInTheDocument();
  });

  it('filters by name and sorts by name, keeping the host order for Recent', async () => {
    const host = start();
    host.deliver('state', state({ projects: many(8) }));
    const titles = () => [...document.querySelectorAll('.project-title')].map((title) => title.textContent);
    expect(titles()[0]).toBe('Part J');

    await userEvent.selectOptions(screen.getByRole('combobox', { name: 'Sort projects' }), 'name');
    expect(titles()[0]).toBe('Part C');
    expect(titles()[7]).toBe('Part J');

    await userEvent.type(screen.getByRole('searchbox'), 'part e');
    expect(titles()).toEqual(['Part E']);
    await userEvent.clear(screen.getByRole('searchbox'));
    await userEvent.type(screen.getByRole('searchbox'), 'gear');
    expect(titles()).toEqual([]);
    expect(screen.getByText('No projects match “gear”.')).toBeInTheDocument();
  });

  // No printer folds into a one-line chip: every card grows and shrinks
  // with its fields, so a ready printer is a short card, not a chip.
  it('shows every printer as its own card, whatever its status', () => {
    const host = start();
    const a1 = printer({
      id: 'a1',
      name: 'Bambu Lab A1',
      status: 'ready',
      job: undefined,
      progressPercent: undefined,
      spools: [{ material: 'PLA', colour: '#262626' }],
    });
    const offline = printer({ id: 'off', name: 'Garage X1', status: 'not_connected', action: 'reconnect' });
    host.deliver('state', state({ printers: [printer(), a1, offline] }));
    expect(document.querySelectorAll('.printer-card')).toHaveLength(3);
    expect(document.querySelector('.printer-chip')).toBeNull();
    const ready = screen.getByText('Bambu Lab A1').closest('.printer-card') as HTMLElement;
    expect(within(ready).getByText('Ready')).toBeInTheDocument();
    expect(within(ready).queryByRole('progressbar')).toBeNull();
    expect(within(ready).getByText('PLA')).toBeInTheDocument();
  });
});

// Boards history-01...05: one screen of every G-code that left a project, with
// the project and the printer as filters over it.
describe('Print history', () => {
  const today = new Date();
  const at = (hours: number, minutes = 0) =>
    new Date(today.getFullYear(), today.getMonth(), today.getDate(), hours, minutes).toISOString();

  function bracket(): HistoryEntry {
    return {
      projectId: 'p1',
      projectName: 'Garage bracket',
      hash: '4f9cb2e1aaaa',
      name: 'bracket-v3.gcode',
      plateName: 'Plate 1',
      createdAt: at(0, 2),
      printTimeSeconds: 9240,
      grams: 69,
      layers: 181,
      printer: 'Bambu X1C',
      material: 'Gray PETG',
      process: '0.20mm Strong',
      conversationId: 'chat-1',
      markerId: 'd-1',
      kept: true,
      printAgain: 'ready',
      printerIds: ['x1'],
      destinations: [
        { id: 'd-1', kind: 'export', at: at(0, 2), path: 'C:\\Users\\kj\\Desktop\\Prints\\bracket-v3.gcode', check: 'matches' },
        { id: 'd-2', kind: 'send', at: at(0, 4), printerName: 'Bambu X1C', printerId: 'x1', delivered: true, startRequested: true },
        { id: 'd-3', kind: 'export', at: at(0, 6), path: 'E:\\bracket-v3.gcode', check: 'changed' },
      ],
      prints: [{ id: 'p-1', printerName: 'Bambu X1C', printerId: 'x1', startedAt: at(0, 5), live: { percent: 38, remainingSeconds: 5700 } }],
    };
  }

  function vent(): HistoryEntry {
    return {
      projectId: 'p2',
      projectName: 'Vent grille',
      hash: 'c2d811afbbbb',
      name: 'vent-grille.gcode',
      plateName: 'Plate 1',
      createdAt: at(0, 1),
      kept: false,
      printAgain: 'not_kept',
      printerIds: [],
      destinations: [{ id: 'd-1', kind: 'export', at: at(0, 1), path: 'C:\\Users\\kj\\Desktop\\Prints\\vent-grille.gcode', check: 'missing' }],
      prints: [],
    };
  }

  function openFromHome(): MockHost {
    const host = start();
    host.deliver('state', state({ projects: [project(), project({ id: 'p2', name: 'Vent grille' })] }));
    fireEvent.click(screen.getByRole('button', { name: 'Print history' }));
    host.deliver('history', { entries: [bracket(), vent()] });
    return host;
  }

  it('opens unfiltered from the gallery header and asks for its entries', () => {
    const host = openFromHome();
    expect(host.lastOfType('history_request')).toBeDefined();
    expect(screen.getByText('Print history')).toBeInTheDocument();
    expect(screen.getByText('2 G-codes · 1 print · all projects, all printers')).toBeInTheDocument();
    expect(screen.getByText('TODAY')).toBeInTheDocument();
    // Each entry names its project while no project filter is on.
    expect(screen.getByRole('button', { name: 'Garage bracket' })).toBeInTheDocument();
  });

  it('draws an entry with its destinations and what the printer reported', () => {
    openFromHome();
    const entry = screen.getByRole('article', { name: 'bracket-v3.gcode' });
    expect(within(entry).queryByText(/4f9c/)).toBeNull();
    expect(within(entry).getByText('2h 34m · 69 g · 181 layers')).toBeInTheDocument();
    expect(within(entry).getByText(/Made with Bambu X1C · Gray PETG · 0.20mm Strong/)).toBeInTheDocument();
    expect(within(entry).getByText('Desktop / Prints · matches, checked just now')).toBeInTheDocument();
    expect(within(entry).getByText(/on the printer’s SD · started/)).toBeInTheDocument();
    expect(within(entry).getByText('E: · changed outside JusPrin, checked just now').closest('li')).toHaveClass('greyed');
    expect(within(entry).getByText('Printing')).toBeInTheDocument();
    expect(within(entry).getByText(/38% · 1h 35m left/)).toBeInTheDocument();
  });

  // Decided 2026-10-27: there is no reliable way to know a print failed.
  it('draws no failure state anywhere', () => {
    const host = openFromHome();
    const stopped = { ...bracket(), prints: [{ id: 'p-1', printerName: 'Bambu X1C', printerId: 'x1', startedAt: at(0, 5) }] };
    host.deliver('history', { entries: [stopped] });
    expect(screen.queryByText(/fail|cancel|didn.t go well|discuss/i)).toBeNull();
  });

  // Decided 2026-11-06: no outcome words for a print nobody saw end.
  it('gives a print that stopped being watched no row of its own', () => {
    const host = openFromHome();
    const stopped = { ...bracket(), prints: [{ id: 'p-1', printerName: 'Bambu X1C', printerId: 'x1', startedAt: at(0, 5) }] };
    host.deliver('history', { entries: [stopped] });
    const entry = screen.getByRole('article', { name: 'bracket-v3.gcode' });
    expect(within(entry).queryByText(/Started|Printing|Finished/)).toBeNull();
    expect(within(entry).getByText(/on the printer’s SD · started/)).toBeInTheDocument();
  });

  it('filters by a project, and widens again when the chip is removed', async () => {
    openFromHome();
    await userEvent.click(screen.getByRole('button', { name: 'Filter' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Project: Vent grille' }));
    expect(screen.queryByRole('article', { name: 'bracket-v3.gcode' })).toBeNull();
    expect(screen.getByRole('article', { name: 'vent-grille.gcode' })).toBeInTheDocument();
    expect(screen.getByText('1 G-code · 0 prints')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Remove filter Project: Vent grille' }));
    expect(screen.getAllByRole('article')).toHaveLength(2);
  });

  it('opens filtered to a printer from its card', () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ history: { gcodes: 1, exports: 1, sends: 1, prints: 1, printingNow: 1 } })] }));
    fireEvent.click(screen.getByRole('button', { name: '1 print · 1 printing now' }));
    host.deliver('history', { entries: [bracket(), vent()] });
    expect(screen.getByText('Printer: X1 Carbon')).toBeInTheDocument();
    expect(screen.getAllByRole('article')).toHaveLength(1);
  });

  it('opens filtered to a project from its card line', () => {
    const host = start();
    host.deliver('state', state({ projects: [project({ history: { gcodes: 2, exports: 1, sends: 1, prints: 2, printingNow: 1 } })] }));
    fireEvent.click(screen.getByRole('button', { name: '2 prints · 1 printing now' }));
    host.deliver('history', { entries: [bracket(), vent()] });
    expect(screen.getByText('Project: Garage bracket')).toBeInTheDocument();
    // The project is in the chip, so the entry does not repeat it.
    expect(screen.queryByRole('button', { name: 'Garage bracket' })).toBeNull();
  });

  // Preferences is not this leaf's: the title bar's menu and Ctrl+, reach it
  // from here, and the page's own Ctrl+, stays.
  it('has no Preferences button, and Ctrl+, still opens Preferences', () => {
    const host = openFromHome();
    expect(screen.queryByRole('button', { name: 'Preferences' })).toBeNull();
    fireEvent.keyDown(window, { key: ',', ctrlKey: true });
    expect(host.lastOfType('open_preferences')).toBeDefined();
  });

  it('goes back to the gallery it was opened from', () => {
    const host = openFromHome();
    fireEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(screen.getByText('Projects')).toBeInTheDocument();
    expect(host.lastOfType('history_back')).toBeUndefined();
  });

  it('opens where the project asks, and gives Back to the project', () => {
    const host = start();
    host.deliver('state', state());
    host.deliver('screen', { screen: 'history', projectId: 'p1', focus: '4f9cb2e1aaaa', back: 'project' });
    host.deliver('history', { entries: [bracket(), vent()] });
    expect(screen.getByText('Project: Garage bracket')).toBeInTheDocument();
    expect(screen.getByRole('article', { name: 'bracket-v3.gcode' })).toHaveClass('focused');
    fireEvent.click(screen.getByRole('button', { name: 'Back' }));
    expect(host.lastOfType('history_back')).toBeDefined();
    // Home coming on screen again shows the gallery.
    host.deliver('screen', { screen: 'home' });
    expect(screen.getByText('Projects')).toBeInTheDocument();
  });

  it('asks again when the host state moves under it', () => {
    const host = openFromHome();
    const asked = host.received.filter((envelope) => envelope.type === 'history_request').length;
    host.deliver('state', state());
    expect(host.received.filter((envelope) => envelope.type === 'history_request')).toHaveLength(asked + 1);
  });

  it('exports a kept copy, writing a changed file’s new one beside it', () => {
    const host = openFromHome();
    const entry = screen.getByRole('article', { name: 'bracket-v3.gcode' });
    fireEvent.click(within(entry).getByRole('button', { name: 'Export…' }));
    expect(host.lastOfType('export_gcode')!.payload).toEqual({ projectId: 'p1', hash: '4f9cb2e1aaaa' });
    fireEvent.click(within(entry).getByRole('button', { name: 'Export again' }));
    expect(host.lastOfType('export_gcode')!.payload).toEqual({ projectId: 'p1', hash: '4f9cb2e1aaaa', beside: 'E:\\bracket-v3.gcode' });
  });

  it('says when the copy is gone instead of offering an export', () => {
    openFromHome();
    const entry = screen.getByRole('article', { name: 'vent-grille.gcode' });
    expect(within(entry).getByText('File no longer kept · slice again to print it')).toBeInTheDocument();
    expect(within(entry).queryByRole('button', { name: /Export/ })).toBeNull();
  });

  // Print again sends the kept file through OrcaSlicer's own dialog; the page
  // only asks for it, by the record.
  it('prints a kept G-code again', () => {
    const host = openFromHome();
    const entry = screen.getByRole('article', { name: 'bracket-v3.gcode' });
    fireEvent.click(within(entry).getByRole('button', { name: 'Print again' }));
    expect(host.lastOfType('print_again')!.payload).toEqual({ projectId: 'p1', hash: '4f9cb2e1aaaa' });
  });

  it('offers no Print again for a G-code that cannot be sent as it was kept', () => {
    const host = openFromHome();
    host.deliver('history', {
      entries: [
        { ...bracket(), printAgain: 'not_for_this_printer' },
        { ...vent(), hash: 'c2d811afcccc', kept: true, printAgain: 'changed' },
      ],
    });
    expect(screen.queryByRole('button', { name: 'Print again' })).toBeNull();
    expect(screen.getByText('Not kept for this printer · slice again to print it')).toBeInTheDocument();
    // A kept file that no longer holds what was kept is said, never sent.
    expect(screen.getByRole('alert')).toHaveTextContent('Kept file changed on disk · slice again to print it');
  });

  it('opens the chat at the marker where the G-code went out', () => {
    const host = openFromHome();
    fireEvent.click(screen.getByRole('button', { name: 'Open in chat' }));
    expect(host.lastOfType('open_in_chat')!.payload).toEqual({ projectId: 'p1', conversationId: 'chat-1', markerId: 'd-1' });
  });

  it('searches by text', async () => {
    openFromHome();
    await userEvent.type(screen.getByRole('searchbox', { name: 'Search prints' }), 'vent');
    expect(screen.getAllByRole('article')).toHaveLength(1);
    expect(screen.getByRole('article', { name: 'vent-grille.gcode' })).toBeInTheDocument();
  });

  describe('print reports', () => {
    const LONG = 'The first layers went down well, then the left corner lifted at about layer forty and the rest of the wall leaned with it. '.repeat(2);
    const photo = { id: '1', name: 'bracket.jpg', sizeBytes: 2_400_000, thumbnail: 'data:image/png;base64,AA' };
    const offer = 'Tell us how it printed · 25 credits';

    function openHistory(entries: HistoryEntry[], reportsOffered = true) {
      const host = start();
      host.deliver('state', state());
      fireEvent.click(screen.getByRole('button', { name: 'Print history' }));
      host.deliver('history', { entries, reportsOffered });
      return host;
    }

    function fillIn(host: MockHost) {
      fireEvent.click(screen.getByRole('button', { name: 'Choose a photo…' }));
      expect(host.lastOfType('print_report_choose_photo')).toBeDefined();
      host.deliver('print_report_photo', { photo });
      fireEvent.change(screen.getByRole('textbox', { name: /What happened/ }), { target: { value: LONG } });
      fireEvent.click(screen.getByRole('radio', { name: 'Partly' }));
    }

    const finished = (): HistoryEntry => ({
      ...bracket(),
      prints: [{ id: 'p-1', printerName: 'X1 Carbon', startedAt: at(0, 5), finishedAt: at(0, 9) }],
    });

    it('offers the report beside Print again and Export, but not while printing', () => {
      openHistory([finished(), { ...bracket(), hash: 'beef' }]);
      expect(screen.getAllByRole('button', { name: offer })).toHaveLength(1);
    });

    it('offers it on an entry that only went to a file', () => {
      openHistory([vent()]);
      expect(screen.getByRole('button', { name: offer })).toBeInTheDocument();
    });

    it('is not offered without the account', () => {
      openHistory([finished()], false);
      expect(screen.queryByRole('button', { name: offer })).toBeNull();
    });

    it('names the entry and holds Send until photo, text and answer are all there', () => {
      const host = openHistory([finished()]);
      fireEvent.click(screen.getByRole('button', { name: offer }));
      const form = within(screen.getByRole('dialog', { name: 'Tell us how it printed' }));
      expect(form.getByText(/→ X1 Carbon$/)).toBeInTheDocument();
      expect(form.getByText('A photo of the printed object and a few sentences earn 25 credits.')).toBeInTheDocument();
      const send = form.getByRole('button', { name: 'Send report' });
      expect(send).toBeDisabled();
      fireEvent.click(form.getByRole('button', { name: 'Choose a photo…' }));
      host.deliver('print_report_photo', { photo });
      expect(form.getByText('bracket.jpg')).toBeInTheDocument();
      expect(form.getByText('2.4 MB')).toBeInTheDocument();
      fireEvent.change(form.getByRole('textbox', { name: /What happened/ }), { target: { value: 'Too short.' } });
      expect(form.getByText('10 of 200 characters')).toBeInTheDocument();
      fireEvent.click(form.getByRole('radio', { name: 'It didn’t work' }));
      expect(send).toBeDisabled();
      fireEvent.change(form.getByRole('textbox', { name: /What happened/ }), { target: { value: LONG } });
      expect(send).toBeEnabled();
      fireEvent.click(form.getByRole('button', { name: 'Remove' }));
      expect(send).toBeDisabled();
    });

    it('a cancelled file dialog keeps the photo already chosen', () => {
      const host = openHistory([finished()]);
      fireEvent.click(screen.getByRole('button', { name: offer }));
      fillIn(host);
      host.deliver('print_report_photo', {});
      expect(screen.getByText('bracket.jpg')).toBeInTheDocument();
    });

    it('sends the report and closes; the entry’s own row says it was sent', () => {
      const entry = finished();
      const host = openHistory([entry]);
      fireEvent.click(screen.getByRole('button', { name: offer }));
      fillIn(host);
      fireEvent.click(screen.getByRole('button', { name: 'Send report' }));
      expect(host.lastOfType('print_report_send')?.payload).toEqual({
        projectId: entry.projectId, hash: entry.hash, photoId: '1', text: LONG.trim(), outcome: 'partly' });
      expect(screen.getByRole('button', { name: 'Sending…' })).toBeDisabled();

      host.deliver('print_report_result', { hash: entry.hash, state: 'sent', credits: 25 });
      host.deliver('history', { entries: [{ ...entry, report: { sentAt: entry.createdAt, credits: 25 } }], reportsOffered: true });
      expect(screen.queryByRole('dialog')).toBeNull();
      expect(screen.getByText('Report sent')).toBeInTheDocument();
      expect(screen.getByText(/25 credits added$/)).toBeInTheDocument();
      expect(screen.queryByRole('button', { name: offer })).toBeNull();
    });

    it('keeps what was typed when sending fails', () => {
      const entry = finished();
      const host = openHistory([entry]);
      fireEvent.click(screen.getByRole('button', { name: offer }));
      fillIn(host);
      fireEvent.click(screen.getByRole('button', { name: 'Send report' }));
      host.deliver('print_report_result', { hash: entry.hash, state: 'failed', credits: 0 });
      expect(screen.getByRole('alert')).toHaveTextContent('The report couldn’t be sent.');
      expect(screen.getByRole('textbox', { name: /What happened/ })).toHaveValue(LONG);
      expect(screen.getByText('bracket.jpg')).toBeInTheDocument();
      expect(screen.getByRole('button', { name: 'Send report' })).toBeEnabled();
    });

    it('treats a report sent before as sent', () => {
      const entry = finished();
      const host = openHistory([entry]);
      fireEvent.click(screen.getByRole('button', { name: offer }));
      fillIn(host);
      fireEvent.click(screen.getByRole('button', { name: 'Send report' }));
      host.deliver('print_report_result', { hash: entry.hash, state: 'already_sent', credits: 0 });
      expect(screen.queryByRole('dialog')).toBeNull();
    });

    it('asks on the project card, and the line opens that project’s history', () => {
      const host = start();
      host.deliver('state', state({ projects: [project({
        reportDue: true, history: { gcodes: 1, exports: 0, sends: 1, prints: 1, printingNow: 0, lastFinishedAt: '2026-11-01T15:00:00Z' } })] }));
      fireEvent.click(screen.getByRole('button', { name: offer }));
      host.deliver('history', { entries: [bracket(), vent()], reportsOffered: true });
      expect(screen.getByText('Project: Garage bracket')).toBeInTheDocument();
    });
  });
});
