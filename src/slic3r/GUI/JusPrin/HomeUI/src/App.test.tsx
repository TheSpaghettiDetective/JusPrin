// Deterministic interaction tests: a scripted mock host plays the native side
// of the bridge while the real page runs in jsdom.

import { describe, expect, it } from 'vitest';
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { App } from './App';
import { Envelope, PrinterInfo, ProjectInfo, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from './bridge/protocol';

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
    state: 'printing',
    statusText: 'Printing · 43% · 2h left',
    progressPercent: 43,
    connectionState: 'online',
    connectionText: 'Online · LAN',
    connectionKind: 'lan',
    modelText: 'X1 Carbon · 0.4 mm',
    spools: [
      { material: 'PLA', colour: '#9A9A9A' },
      { material: 'PLA', colour: '#C8202D' },
      { material: 'PLA', colour: '#FFFFFF' },
      { material: 'PLA', colour: '#000000' },
    ],
    canLaunchMonitor: true,
    ...overrides,
  };
}

function state(overrides: Partial<StatePayload> = {}): StatePayload {
  return { appearance: 'light', projects: [project()], printers: [printer()], ...overrides };
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
    const idle = screen.getByText('Garage bracket').closest('button')!;
    const printing = screen.getByText('Vent grille').closest('button')!;
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

  it('shows a printing printer with its job and a never-connected one without an action', async () => {
    const host = start();
    const never = printer({
      id: 'mk4',
      name: 'Prusa MK4',
      state: 'idle',
      statusText: undefined,
      progressPercent: undefined,
      connectionState: 'none',
      connectionText: 'Not connected',
      connectionKind: undefined,
      modelText: 'MK4 · 0.4 mm',
      spools: [],
      canLaunchMonitor: false,
    });
    host.deliver('state', state({ printers: [printer(), never] }));
    expect(screen.getByText('Printing · 43% · 2h left')).toBeInTheDocument();
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    expect(screen.getByText('X1 Carbon · 0.4 mm')).toBeInTheDocument();
    // Never connected: a chip that says so in words, with no dot.
    const chip = screen.getByRole('button', { name: 'Prusa MK4, Not connected' });
    expect(chip.querySelector('.status-dot')).toBeNull();
    // Opened, the card says so too, and connecting is in its menu.
    await userEvent.click(chip);
    const idle = screen.getByText('Prusa MK4').closest('.printer-card') as HTMLElement;
    expect(within(idle).getByText('Not connected')).toBeInTheDocument();
    expect(within(idle).queryByRole('button', { name: /Connect|Reconnect|Launch monitor|Open printer window/ })).toBeNull();
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
    host.deliver('state', state({ printers: [printer(), printer({ id: 'mk4', name: 'Prusa MK4', state: 'idle' })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Prusa MK4, Idle' }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Prusa MK4' }));
    const menu = screen.getByRole('menu');
      expect(within(menu).getAllByRole('menuitem').map((item) => item.textContent)).toEqual([
        'Connect…',
      'Printer settings…',
      'Calibrate…',
      'Rename',
      'Remove printer…',
    ]);
    await userEvent.click(within(menu).getByText('Printer settings…'));
    expect(host.lastOfType('open_printer_settings')!.payload).toEqual({ id: 'mk4' });
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
  });

  it('starts a calibration on a named printer from its card header', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ id: 'mk4', name: 'Prusa MK4', state: 'idle' })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Prusa MK4, Idle' }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Prusa MK4' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Calibrate…' }));
    expect(host.lastOfType('calibrate_printer')!.payload).toEqual({ id: 'mk4' });
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
  });

  // The question the host then asks on Home (board 3): the Confirmation
  // dialog, answered once with its request id.
  it("asks the host's question over Home and sends the answer back", async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ id: 'mk4', name: 'Prusa MK4', state: 'idle' })] }));
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
    host.deliver('state', state({ printers: [printer({ state: 'idle', canLaunchMonitor: false })] }));
    await userEvent.click(screen.getByRole('button', { name: 'X1 Carbon, Idle' }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    await userEvent.click(screen.getByRole('menuitem', { name: 'Connect…' }));
    expect(host.lastOfType('connect_printer')!.payload).toEqual({ id: 'x1' });
    expect(host.lastOfType('add_printer')).toBeUndefined();
  });

  it('renames a named printer after checking the new name', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer(), printer({ id: 'mk4', name: 'Prusa MK4', state: 'idle' })] }));
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
    const idle = printer({ state: 'idle', statusText: undefined, progressPercent: undefined });
    host.deliver('state', state({ printers: [idle] }));
    await userEvent.click(screen.getByRole('button', { name: 'X1 Carbon, Idle' }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for X1 Carbon' }));
    expect(screen.getByRole('menu')).toBeInTheDocument();

    host.deliver('state', state({ printers: [printer()] }));
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    expect(screen.getByRole('menu')).toBeInTheDocument();

    host.deliver('state', state({ printers: [printer({ state: 'offline', connectionState: 'offline', connectionText: 'Offline' })] }));
    expect(within(document.querySelector('.printer-facts') as HTMLElement).getByText('Offline')).toBeInTheDocument();
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

    host.deliver('state', state({ printers: [printer({ progressPercent: 44, statusText: 'Printing · 44% · 2h left' })] }));
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

  it('offers Reconnect only on a printer that was connected before', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ state: 'offline', canLaunchMonitor: false, connectionAction: 'reconnect',
      connectionState: 'offline', connectionText: 'Offline' })] }));
    expect(within(document.querySelector('.printer-facts') as HTMLElement).getByText('Offline')).toBeInTheDocument();
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
    host.deliver('state', state({ projects: [], printers: [printer({ name: 'Bambu Lab A1', state: 'idle' })] }));
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

  it('folds an idle printer into a chip with its material, and opens it on a click', async () => {
    const host = start();
    const a1 = printer({
      id: 'a1',
      name: 'Bambu Lab A1',
      state: 'idle',
      statusText: undefined,
      progressPercent: undefined,
      spools: [{ material: 'PLA', colour: '#262626' }],
    });
    host.deliver('state', state({ printers: [printer(), a1] }));
    // The printing printer is its full card; the idle one is a chip.
    expect(document.querySelectorAll('.printer-card')).toHaveLength(1);
    const chip = screen.getByRole('button', { name: 'Bambu Lab A1, Idle' });
    expect(chip).toHaveAttribute('aria-expanded', 'false');
    expect(chip.querySelector('.status-dot')).not.toBeNull();
    expect(within(chip).getByText('PLA')).toBeInTheDocument();
    expect(chip.querySelectorAll('.spool-swatch')).toHaveLength(1);

    await userEvent.click(chip);
    expect(document.querySelectorAll('.printer-card')).toHaveLength(2);
    // It stays open through the host's next push.
    host.deliver('state', state({ printers: [printer(), a1] }));
    expect(document.querySelectorAll('.printer-card')).toHaveLength(2);
  });

  it('keeps a printer that needs reconnecting, or was just added, as its full card', () => {
    const host = start();
    const offline = printer({
      id: 'off',
      name: 'Garage X1',
      state: 'offline',
      statusText: undefined,
      progressPercent: undefined,
      connectionState: 'offline',
      connectionText: 'Offline',
      connectionAction: 'reconnect',
      canLaunchMonitor: false,
    });
    const added = printer({ id: 'new', name: 'Desk A1 mini', state: 'idle', statusText: undefined, progressPercent: undefined });
    host.deliver('state', state({ printers: [added, offline], highlightPrinter: 'Desk A1 mini' }));
    expect(screen.queryByRole('button', { name: /^Desk A1 mini,|^Garage X1,/ })).toBeNull();
    expect(document.querySelectorAll('.printer-card')).toHaveLength(2);
    // The added printer stays open once the push that named it has passed.
    host.deliver('state', state({ printers: [added, offline] }));
    expect(document.querySelectorAll('.printer-card')).toHaveLength(2);
  });
});
