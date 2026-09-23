// Deterministic interaction tests: a scripted mock host plays the native side
// of the bridge while the real page runs in jsdom.

import { describe, expect, it } from 'vitest';
import { act, render, screen, within } from '@testing-library/react';
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
    status: { kind: 'sliced', text: 'Sliced · ready to send' },
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
    connectionText: 'Connected · LAN',
    nozzleText: '0.4 mm nozzle',
    materialLabel: 'PLA',
    spools: [{ colour: '#9A9A9A' }, { colour: '#C8202D' }, { colour: '#FFFFFF' }, { colour: '#000000' }],
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

  it('waits for the host rather than showing an empty gallery', () => {
    const host = new MockHost();
    render(<App getTransport={() => host.transport} />);
    expect(screen.getByText('Connecting…')).toBeInTheDocument();
    expect(screen.queryByText('No projects yet.')).not.toBeInTheDocument();
    host.deliver('hello_ack', {});
    host.deliver('state', state({ projects: [], printers: [] }));
    expect(screen.getByText('No projects yet.')).toBeInTheDocument();
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
    const idle = screen.getByTitle('C:/projects/garage.3mf');
    const printing = screen.getByText('Vent grille').closest('button')!;
    expect(idle.className).not.toContain('printing');
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

  it('shows a printing printer in full and an idle one as one row', () => {
    const host = start();
    host.deliver(
      'state',
      state({ printers: [printer(), printer({ id: 'mk4', name: 'Prusa MK4', state: 'idle', canLaunchMonitor: false })] }),
    );
    expect(screen.getByText('Printing · 43% · 2h left')).toBeInTheDocument();
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    expect(screen.getByText('0.4 mm nozzle')).toBeInTheDocument();
    const idle = screen.getByText('Prusa MK4').closest('.printer-card')!;
    expect(within(idle as HTMLElement).getByRole('button', { name: 'Connect printer' })).toBeInTheDocument();
    expect(within(idle as HTMLElement).queryByText('Launch monitor')).not.toBeInTheDocument();
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
    await userEvent.click(screen.getByText('Import'));
    await userEvent.click(screen.getByText('New'));
    await userEvent.click(screen.getByText('Launch monitor'));
    await userEvent.click(screen.getByText('+ Add printer'));
    expect(host.lastOfType('import_project')).toBeDefined();
    expect(host.lastOfType('new_project')).toBeDefined();
    expect(host.lastOfType('launch_monitor')!.payload).toEqual({ id: 'x1' });
    expect(host.lastOfType('add_printer')).toBeDefined();
  });

  it('offers connection and printer actions from each named card header', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer(), printer({ id: 'mk4', name: 'Prusa MK4', state: 'idle' })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Actions for Prusa MK4' }));
    const menu = screen.getByRole('menu');
      expect(within(menu).getAllByRole('menuitem').map((item) => item.textContent)).toEqual([
        'Connection settings…',
      'Printer settings…',
      'Rename',
      'Remove printer…',
    ]);
    await userEvent.click(within(menu).getByText('Printer settings…'));
    expect(host.lastOfType('open_printer_settings')!.payload).toEqual({ id: 'mk4' });
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
  });

  it('connects an existing saved printer without asking to add it again', async () => {
    const host = start();
    host.deliver('state', state({ printers: [printer({ state: 'idle', canLaunchMonitor: false })] }));
    await userEvent.click(screen.getByRole('button', { name: 'Connect printer' }));
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

  it('shows what an added printer assumed, highlights its card, and can be dismissed', async () => {
    const host = start();
    host.deliver('state', state());
    host.deliver('printer_added', {
      name: 'X1 Carbon',
      nozzleText: '0.4 mm',
      nozzleAssumed: false,
      plateText: 'Textured PEI Plate',
      plateAssumed: true,
      filamentText: 'Bambu PLA Basic',
      filamentAssumed: true,
    });

    expect(screen.getByText('X1 Carbon', { selector: 'b' })).toBeInTheDocument();
    expect(screen.getByText(/0\.4 mm, Textured PEI Plate · assumed, Bambu PLA Basic · assumed/)).toBeInTheDocument();
    // The strip is its own receipt, drawn above the printer's own card.
    const cards = document.querySelectorAll('.printer-card');
    expect(cards[0]).toHaveClass('printer-card-added');

    await userEvent.click(screen.getByRole('button', { name: 'Dismiss' }));
    expect(screen.queryByText(/0\.4 mm, Textured PEI Plate/)).not.toBeInTheDocument();
  });

  // F4 review fix: a later state used to leave the receipt showing stale
  // facts (and the card highlighted) after whatever it described had
  // already changed underneath it -- e.g. a nozzle changed through the
  // receipt's own Change link.
  it('ends the receipt and the highlight on the very next state, for any reason', async () => {
    const host = start();
    host.deliver('state', state());
    host.deliver('printer_added', {
      name: 'X1 Carbon',
      nozzleText: '0.4 mm',
      nozzleAssumed: false,
      plateText: 'Textured PEI Plate',
      plateAssumed: true,
      filamentText: 'Bambu PLA Basic',
      filamentAssumed: true,
    });
    expect(screen.getByText('X1 Carbon', { selector: 'b' })).toBeInTheDocument();
    expect(document.querySelectorAll('.printer-card')[0]).toHaveClass('printer-card-added');

    // Any later state, whatever prompted it (here: nothing but a refresh).
    host.deliver('state', state());

    expect(screen.queryByText('X1 Carbon', { selector: 'b' })).not.toBeInTheDocument();
    expect(document.querySelector('.printer-card-added')).toBeNull();
  });

  it('sends the added printer\'s id when Change is tapped on its receipt', async () => {
    const host = start();
    host.deliver('state', state());
    host.deliver('printer_added', {
      name: 'X1 Carbon',
      nozzleText: '0.4 mm',
      nozzleAssumed: false,
      plateText: '',
      plateAssumed: false,
      filamentText: '',
      filamentAssumed: false,
    });

    await userEvent.click(screen.getByRole('button', { name: 'Change' }));
    expect(host.lastOfType('open_printer_settings')!.payload).toEqual({ id: 'x1' });
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

  it('follows the host into dark mode', () => {
    const host = start();
    host.deliver('state', state({ appearance: 'dark' }));
    expect(document.documentElement.dataset.appearance).toBe('dark');
    host.deliver('appearance', { appearance: 'light' });
    expect(document.documentElement.dataset.appearance).toBe('light');
  });

  // The printer conversation opens in this column; the page hands the place
  // over rather than drawing a second list beside it.
  it("gives the printers column to the conversation panel while it is open", () => {
    const host = start();
    host.deliver("state", state({ printers: [printer()] }));
    expect(screen.getByText("+ Add printer")).toBeInTheDocument();

    host.deliver("printer_panel", { open: true });
    expect(screen.queryByText("+ Add printer")).not.toBeInTheDocument();
    expect(screen.queryByText("X1 Carbon")).not.toBeInTheDocument();
    // The projects stay in view beside it.
    expect(screen.getByText("Projects")).toBeInTheDocument();

    host.deliver("printer_panel", { open: false });
    expect(screen.getByText("+ Add printer")).toBeInTheDocument();
  });

  it("leaves the column to the panel across a reload", () => {
    const host = start();
    host.deliver("state", state({ printers: [printer()], printerPanelOpen: true }));
    expect(screen.queryByText("+ Add printer")).not.toBeInTheDocument();
  });
});

 it.each([['settings', 'Connection settings'], ['reconnect', 'Reconnect'], ['connect', 'Connect printer']] as const)(
   'offers the appropriate %s action for a saved printer', async (connectionAction, label) => {
     const host = new MockHost();
     render(<App getTransport={() => host.transport} />);
     host.deliver('hello_ack', {});
     host.deliver('state', state({ printers: [printer({ state: 'idle', canLaunchMonitor: false, connectionAction,
       connectionText: connectionAction === 'settings' ? 'File sending configured' : 'Status unknown' })] }));
     const button = screen.getByRole('button', { name: label });
     expect(button).toHaveClass('launch-monitor');
     await userEvent.click(button);
     expect(host.lastOfType('connect_printer')!.payload).toEqual({ id: 'x1' });
   });
