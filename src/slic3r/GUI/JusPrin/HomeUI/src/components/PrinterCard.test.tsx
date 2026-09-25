// The printer card's one shape: every line rests on something the host
// verified, and a row with nothing to say is left out.

import { describe, expect, it, vi } from 'vitest';
import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterInfo } from '../bridge/protocol';
import { PrinterCard } from './PrinterCard';

function printer(overrides: Partial<PrinterInfo> = {}): PrinterInfo {
  return {
    id: 'named:Bench',
    name: 'Bench',
    kind: 'named',
    canOpenSettings: true,
    canRename: true,
    canRemove: true,
    state: 'idle',
    connectionState: 'none',
    connectionText: 'Not connected',
    modelText: 'X1 Carbon · 0.4 mm',
    spools: [],
    canLaunchMonitor: false,
    ...overrides,
  };
}

function card(info: PrinterInfo, onLaunchMonitor = vi.fn(), onConnect = vi.fn()) {
  const actions = { onConnect, onOpenSettings: vi.fn(), onRename: vi.fn(), onRemove: vi.fn() };
  render(<PrinterCard printer={info} otherNames={[]} actions={actions} onLaunchMonitor={onLaunchMonitor} />);
  return document.querySelector('.printer-card') as HTMLElement;
}

// The value beside a row label, or null when the row is left out.
function row(label: string): HTMLElement | null {
  const term = [...document.querySelectorAll('.printer-fact dt')].find((dt) => dt.textContent === label);
  return (term?.nextElementSibling as HTMLElement | null) ?? null;
}

describe('PrinterCard', () => {
  it.each([
    ['online', 'Online · LAN', 'Online'],
    ['connected', 'Connected · 192.168.1.42', 'Connected'],
    ['offline', 'Offline', 'Offline'],
  ] as const)('draws a %s dot labelled with its word', (connectionState, connectionText, word) => {
    const element = card(printer({ connectionState, connectionText }));
    expect(row('Connection')).toHaveTextContent(connectionText);
    const dot = element.querySelector('.status-dot')!;
    expect(dot).toHaveClass(connectionState);
    expect(dot).toHaveTextContent(word);
    expect(dot.querySelector('.visually-hidden')).not.toBeNull();
  });

  it('says Not connected in words and draws no dot', () => {
    const element = card(printer());
    expect(row('Connection')).toHaveTextContent('Not connected');
    expect(element.querySelector('.status-dot')).toBeNull();
  });

  it('keeps the rows in their fixed order', () => {
    card(printer({ spools: [{ material: 'PLA', colour: '#000000' }] }));
    expect([...document.querySelectorAll('.printer-fact dt')].map((dt) => dt.textContent)).toEqual([
      'Connection',
      'Model',
      'Loaded',
    ]);
    expect(row('Model')).toHaveTextContent('X1 Carbon · 0.4 mm');
  });

  it('leaves out a row with nothing to say', () => {
    card(printer({ modelText: undefined }));
    expect(row('Model')).toBeNull();
    expect(row('Loaded')).toBeNull();
    expect(screen.queryByText(/unknown/i)).toBeNull();
  });

  it('names the materials once and draws a swatch per coloured spool', () => {
    card(
      printer({
        spools: [
          { material: 'PLA', colour: '#9A9A9A' },
          { material: 'PETG', colour: '#C8202D' },
          { material: 'PLA', colour: '#FFFFFF' },
        ],
      }),
    );
    const loaded = row('Loaded')!;
    expect(within(loaded).getByText('PLA, PETG')).toBeInTheDocument();
    expect(loaded.querySelectorAll('.spool-swatch')).toHaveLength(3);
  });

  it('names the material without a swatch when no colour is known', () => {
    card(printer({ spools: [{ material: 'PLA' }, { material: 'PLA' }] }));
    const loaded = row('Loaded')!;
    expect(loaded).toHaveTextContent('PLA');
    expect(loaded.querySelectorAll('.spool-swatch')).toHaveLength(0);
  });

  it('shows the job and its bar only while printing', () => {
    const element = card(
      printer({
        state: 'printing',
        statusText: 'Printing · 43% · 2h left',
        progressPercent: 43,
        connectionState: 'online',
        connectionText: 'Online · Cloud',
        connectionKind: 'cloud',
        canLaunchMonitor: true,
      }),
    );
    // Directly under the name, ahead of the bar and the rows.
    const job = element.querySelector('.printer-head + .printer-job') as HTMLElement;
    expect(job).toHaveTextContent('Printing · 43% · 2h left');
    expect(job.nextElementSibling).toHaveClass('printer-progress');
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    // The dot stays the connection's, not the job's.
    expect(element.querySelector('.status-dot')).toHaveClass('online');
    expect(element.querySelector('.status-dot')).not.toHaveClass('printing');
  });

  it('offers Launch monitor on a connected Bambu printer', async () => {
    const launch = vi.fn();
    card(
      printer({ connectionState: 'online', connectionText: 'Online · LAN', connectionKind: 'lan', canLaunchMonitor: true }),
      launch,
    );
    await userEvent.click(screen.getByRole('button', { name: 'Launch monitor' }));
    expect(launch).toHaveBeenCalledWith('named:Bench');
    expect(screen.queryByRole('button', { name: 'Open printer window' })).toBeNull();
  });

  it('offers Open printer window on a print host with an address', async () => {
    const launch = vi.fn();
    card(
      printer({
        connectionState: 'connected',
        connectionText: 'Connected · 192.168.1.42',
        connectionKind: 'host',
        address: '192.168.1.42',
        canLaunchMonitor: true,
      }),
      launch,
    );
    await userEvent.click(screen.getByRole('button', { name: 'Open printer window' }));
    expect(launch).toHaveBeenCalledWith('named:Bench');
    expect(screen.queryByRole('button', { name: 'Launch monitor' })).toBeNull();
  });

  it('offers Reconnect on an offline printer', async () => {
    const connect = vi.fn();
    card(
      printer({ state: 'offline', connectionState: 'offline', connectionText: 'Offline', connectionAction: 'reconnect' }),
      vi.fn(),
      connect,
    );
    await userEvent.click(screen.getByRole('button', { name: 'Reconnect' }));
    expect(connect).toHaveBeenCalledWith('named:Bench');
  });
});
