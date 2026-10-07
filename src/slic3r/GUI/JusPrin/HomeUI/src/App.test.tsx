// Deterministic interaction tests: a scripted mock host plays the native side
// of the bridge while the real page runs in jsdom.

import { describe, expect, it } from 'vitest';
import { act, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { App } from './App';
import { Envelope, OnboardingState, PrinterInfo, ProjectInfo, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload } from './bridge/protocol';

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

function onboarding(overrides: Partial<OnboardingState> = {}): OnboardingState {
  return {
    visible: false,
    step: 'hidden',
    status: 'unfinished',
    agentConfigured: false,
    profiles: {
      available: false,
      imported: false,
      partialImportPending: false,
      failed: [],
      printerCount: 0,
      filamentCount: 0,
      processCount: 0,
      source: '/tmp/orca',
    },
    setup: { usable: false, offlineExample: false, summary: '', printer: '', nozzle: '', plate: '', material: '', process: '' },
    projectOpen: false,
    ...overrides,
  };
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

  it('shows a printing printer with its job and a never-connected one without an action', () => {
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
    // Never connected: the card says so, and connecting is in its menu.
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
        'Connect…',
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

describe('local onboarding', () => {
  it('matches the welcome frame actions and routes account setup to its placeholder', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'welcome' }) }));

    expect(screen.getByText('Your Orca workflow, with help planning the print.')).toBeInTheDocument();
    expect(screen.getByText('Example interaction · Not live')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Explore first' }));
    expect(host.lastOfType('onboarding_begin')).toBeDefined();
    await userEvent.click(screen.getByText('Set up JusPrin'));
    expect(host.lastOfType('onboarding_account_stub')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: 'Dismiss onboarding and continue locally' }));
    expect(host.lastOfType('onboarding_dismiss')).toBeDefined();
  });

  it('shows detected Orca profile counts and offers a real local import', async () => {
    const host = start();
    host.deliver('state', state({
      onboarding: onboarding({
        visible: true,
        step: 'profiles',
        profiles: {
          available: true,
          imported: false,
          partialImportPending: false,
          failed: [],
          printerCount: 2,
          filamentCount: 3,
          processCount: 4,
          source: '/Users/test/Library/Application Support/OrcaSlicer',
        },
      }),
    }));

    expect(screen.getByText('9 selected')).toBeInTheDocument();
    expect(screen.getByText('Detected on this computer')).toBeInTheDocument();
    // The frame's sample preset names are not the user's: with none reported,
    // there is nothing to review and nothing is listed.
    expect(screen.queryByRole('button', { name: 'Review selection' })).toBeNull();
    expect(screen.queryByText(/Bambu Lab A1/)).toBeNull();

    // Leaving a kind out takes its presets out of the count and the request.
    await userEvent.click(screen.getByRole('checkbox', { name: /Filament presets/ }));
    expect(screen.getByText('6 selected')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Import selected' }));
    expect(host.lastOfType('onboarding_use_profiles')!.payload).toEqual({ categories: ['printer', 'process'] });
    await userEvent.click(screen.getByRole('button', { name: 'Skip for now' }));
    expect(host.lastOfType('onboarding_defer_profiles')).toBeDefined();
  });

  it('lists the detected presets for review and imports nothing when none is chosen', async () => {
    const host = start();
    host.deliver('state', state({
      onboarding: onboarding({
        visible: true,
        step: 'profiles',
        profiles: {
          available: true,
          imported: false,
          partialImportPending: false,
          failed: [],
          printerCount: 1,
          filamentCount: 1,
          processCount: 0,
          source: '/Users/test/Library/Application Support/OrcaSlicer',
          printerNames: ['Voron 2.4 0.4 nozzle'],
          filamentNames: ['Fast PLA'],
          processNames: [],
        },
      }),
    }));

    expect(screen.getByText('Voron 2.4 0.4 nozzle · Fast PLA')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Review selection' }));
    expect(screen.getByText('Fast PLA')).toBeInTheDocument();
    expect(screen.getByText('Filament preset')).toBeInTheDocument();
    // A kind with no presets cannot be chosen.
    expect(screen.getByRole('checkbox', { name: /Process presets/ })).toBeDisabled();

    await userEvent.click(screen.getByRole('checkbox', { name: /Printer presets/ }));
    await userEvent.click(screen.getByRole('checkbox', { name: /Filament presets/ }));
    expect(screen.getByText('0 selected')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Import selected' })).toBeDisabled();
  });

  it('holds a partial profile import for deliberate acknowledgement', async () => {
    const host = start();
    host.deliver('state', state({
      onboarding: onboarding({
        visible: true,
        step: 'profiles',
        profiles: {
          available: true,
          imported: true,
          partialImportPending: true,
          failed: ['filament/Fast PLA.json'],
          printerCount: 1,
          filamentCount: 1,
          processCount: 1,
          source: '/Users/test/Library/Application Support/OrcaSlicer',
          importedFiles: ['machine/Voron 2.4 0.4 nozzle.json'],
        },
      }),
    }));

    const failed = screen.getByText('Fast PLA').closest('.onboarding-result') as HTMLElement;
    expect(within(failed).getByText('Filament preset · not imported')).toBeInTheDocument();
    expect(screen.getByText('Not imported')).toBeInTheDocument();
    const copied = screen.getByText('Voron 2.4 0.4 nozzle').closest('.onboarding-result') as HTMLElement;
    expect(within(copied).getByText('Printer preset · copied into JusPrin')).toBeInTheDocument();
    // No sample rows and no claimed diagnosis: only what the host reported.
    expect(screen.getAllByText('1 item')).toHaveLength(2);
    expect(screen.queryByText(/example items/)).toBeNull();
    expect(screen.queryByText(/Conflict resolved/)).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Retry failed items' }));
    expect(host.lastOfType('onboarding_use_profiles')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: 'Continue with imported settings' }));
    expect(host.lastOfType('onboarding_accept_partial_profiles')).toBeDefined();
  });

  it('offers the three approved local setup paths without requiring an account', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'setup' }) }));

    await userEvent.click(screen.getByRole('button', { name: /Configure manually/ }));
    expect(host.lastOfType('onboarding_manual_setup')).toBeDefined();
    await userEvent.click(screen.getByRole('button', { name: /I don’t have a printer here/ }));
    expect(host.lastOfType('onboarding_offline_example')).toBeDefined();
    expect(screen.getByRole('link', { name: /Set up with Agent/ })).toHaveAttribute('href', 'https://jusprin.com/account');
  });

  it('says in place why a setup path did not finish', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'setup' }) }));
    host.deliver('onboarding_error', { message: 'No printer setup was saved.' });
    expect(screen.getByRole('alert')).toHaveTextContent('No printer setup was saved.');
    // It is not a project failure, so the recovery frame stays away.
    expect(screen.queryByText('Couldn’t open the file.')).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: /Configure manually/ }));
    expect(screen.queryByRole('alert')).toBeNull();
  });

  it('confirms the imported physical setup before the project gate', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'confirm_setup', setup: {
      usable: true, offlineExample: false, summary: 'Bambu Lab A1 · 0.4 mm · PLA', printer: 'Bambu Lab A1', nozzle: '0.4 mm',
      plate: 'Textured PEI Plate', material: 'PLA', process: '0.20 mm Standard',
    } }) }));

    expect(screen.getByRole('combobox', { name: 'Printer' })).toHaveValue('Bambu Lab A1');
    expect(screen.getByText('The preset uses Textured PEI Plate. Check the plate on your printer.')).toBeInTheDocument();
    expect(screen.getByText('Process preset: 0.20 mm Standard')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Use this setup' }));
    expect(host.lastOfType('onboarding_confirm_setup')).toBeDefined();
  });

  it('opens a model, bundled example, or saved project from the final gate', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'project' }) }));

    await userEvent.click(screen.getByRole('button', { name: /Open a project or model/ }));
    await userEvent.click(screen.getByRole('button', { name: 'Try the example' }));
    expect(host.lastOfType('import_project')).toBeDefined();
    expect(host.lastOfType('onboarding_open_example')).toBeDefined();
  });

  it('shows the project recovery frame after a failed open', async () => {
    const host = start();
    host.deliver('state', state({ onboarding: onboarding({ visible: true, step: 'project' }) }));
    await userEvent.click(screen.getByRole('button', { name: 'Try the example' }));
    host.deliver('onboarding_error', { message: 'The file is incomplete or no longer available.' });
    expect(screen.getByText('Couldn’t open the file.')).toBeInTheDocument();
    expect(screen.getByText('The file is incomplete or no longer available.')).toBeInTheDocument();
    // The frame's "reference" label is a note to the designer, not product copy.
    expect(screen.queryByText(/recovery reference/i)).toBeNull();

    // Retry repeats the request that failed.
    host.received.length = 0;
    await userEvent.click(screen.getByRole('button', { name: 'Retry' }));
    expect(host.lastOfType('onboarding_open_example')).toBeDefined();
    expect(screen.queryByText('Couldn’t open the file.')).toBeNull();

    host.deliver('onboarding_error', { message: 'The file is incomplete or no longer available.' });
    await userEvent.click(screen.getByRole('button', { name: 'Choose another' }));
    expect(host.lastOfType('import_project')).toBeDefined();

    // Closing returns to the project step and tells the host nothing.
    host.deliver('onboarding_error', { message: 'The file is incomplete or no longer available.' });
    host.received.length = 0;
    await userEvent.click(screen.getByRole('button', { name: 'Close' }));
    expect(screen.getByRole('button', { name: /Open a project or model/ })).toBeInTheDocument();
    expect(host.received).toEqual([]);
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
