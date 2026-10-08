// What the printer panel's cards state, and what the one card that asks for
// something sends. Nothing here decides a printer fact; these tests pin the
// reading and the sending.

import { describe, expect, it, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterBlock, ToolActivityInfo } from '../bridge/protocol';
import { PrinterBlockView, PrinterCredentialCard } from './PrinterPanel';

const mini = { catalogId: 'BBL/Bambu Lab A1 mini', name: 'Bambu Lab A1 mini', buildVolume: '180 × 180 × 180 mm', picture: '' };

function block(overrides: Partial<PrinterBlock>): PrinterBlock {
  return { id: 'b1', seq: 1, afterMessageId: 'm1', kind: 'printers', ...overrides };
}

describe('the cards the agent draws', () => {
  it('names each printer and its size, with nothing to tap', () => {
    render(<PrinterBlockView block={block({ printers: [mini, { ...mini, catalogId: 'x', name: 'Bambu Lab A1' }] })} />);
    expect(screen.getByText('Bambu Lab A1 mini')).toBeInTheDocument();
    expect(screen.getByText('Bambu Lab A1')).toBeInTheDocument();
    expect(screen.getAllByText('180 × 180 × 180 mm')).toHaveLength(2);
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('lists what is on the network, with its serial, and nothing to tap', () => {
    render(<PrinterBlockView block={block({ kind: 'network', printers: [{ name: 'Workshop', serial: '01P00A3B', online: true }] })} />);
    expect(screen.getByText('Found on your network')).toBeInTheDocument();
    expect(screen.getByText('Online · 01P00A3B')).toBeInTheDocument();
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('says a printer was added, with its name and nozzle, and nothing to tap', () => {
    render(<PrinterBlockView block={block({ kind: 'added', printer: { name: 'Creality K1', nozzle: 0.4 } })} />);
    const receipt = screen.getByRole('status');
    expect(receipt).toHaveTextContent('Added: Creality K1');
    expect(receipt).toHaveTextContent('0.4 mm nozzle');
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('leaves out a nozzle the saved printer does not state', () => {
    render(<PrinterBlockView block={block({ kind: 'added', printer: { name: 'Creality K1', nozzle: 0 } })} />);
    expect(screen.getByRole('status')).toHaveTextContent(/^Added: Creality K1$/);
  });

  // The app does not send these yet; each is drawn only when it is there.
  it('adds the model to a network row, a receipt and a card once the app reports it', () => {
    const { unmount } = render(<PrinterBlockView block={block({ kind: 'network', printers: [
      { name: 'Studio A1', serial: 'DEMO-A1-2048', online: true, model: 'Bambu A1' },
      { name: 'Workshop M4', serial: 'DEMO-M4-0031', online: false, model: 'Qidi M4' },
    ] })} />);
    expect(screen.getByText('Online · Bambu A1 · DEMO-A1-2048')).toBeInTheDocument();
    // An unreachable printer says only that.
    expect(screen.getByText('Offline')).toBeInTheDocument();
    unmount();

    const receipt = render(<PrinterBlockView block={block({ kind: 'added', printer: { name: 'Studio A1', nozzle: 0.4, model: 'Bambu A1' } })} />);
    expect(screen.getByRole('status')).toHaveTextContent('Bambu A1 · 0.4 mm nozzle');
    receipt.unmount();

    render(<PrinterBlockView block={block({ kind: 'printers', printers: [
      { catalogId: 'a1-mini', name: 'Bambu Lab A1 mini', brand: 'Bambu Lab', model: 'A1 mini', buildVolume: '180 × 180 × 180 mm', picture: '' },
    ] })} />);
    expect(screen.getByText('Bambu Lab')).toHaveClass('printer-card-brand');
    expect(screen.getByText('A1 mini')).toBeInTheDocument();
    expect(screen.queryByText('Bambu Lab A1 mini')).toBeNull();
  });

  it('says the plug-in is needed, and Install asks the app to install it', async () => {
    const install = vi.fn();
    render(<PrinterBlockView block={block({ kind: 'plugin' })} onInstallPlugin={install} />);
    expect(screen.getByRole('status')).toHaveTextContent('Bambu plug-in needed');
    expect(screen.getByRole('status')).toHaveTextContent('You can prepare prints with this printer before installing the Bambu plug-in.');
    await userEvent.click(screen.getByRole('button', { name: 'Install plug-in' }));
    expect(install).toHaveBeenCalledTimes(1);
  });

  it('says once the plug-in is installed, with nothing left to tap', () => {
    render(<PrinterBlockView block={block({ kind: 'plugin', installed: true })} onInstallPlugin={vi.fn()} />);
    expect(screen.getByRole('status')).toHaveTextContent('Bambu plug-in installed');
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('says how a photo gets in, in words', () => {
    render(<PrinterBlockView block={block({ kind: 'tip' })} />);
    expect(screen.getByText(/a photo is the fastest way/)).toBeInTheDocument();
  });
});

describe('the credential card', () => {
  function connect(overrides: Partial<ToolActivityInfo> = {}): ToolActivityInfo {
    return {
      actionId: 't1', correlationId: 'm1', server: 'jusprin', tool: 'printer_connect', title: 'Connect to 192.168.1.42',
      arguments: { printerName: 'Kobra 3', hostType: 'moonraker', address: '192.168.1.42', provider: 'host' },
      actionClass: 'mutation', requiresInput: true, sessionId: '1', expectedRevision: 1, state: 'input_required',
      progress: { current: 0, total: 1 }, ...overrides,
    };
  }

  it('asks a host printer for an optional API key, and connects with or without one', async () => {
    const input = vi.fn();
    render(<PrinterCredentialCard activity={connect()} onInput={input} onCancelTool={vi.fn()} onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Stays on this computer.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(input).toHaveBeenLastCalledWith('t1', { credential: '' });
    await userEvent.type(screen.getByLabelText('API key (if the printer asks for one)'), 'key123{Enter}');
    expect(input).toHaveBeenLastCalledWith('t1', { credential: 'key123' });
  });

  it('asks a Bambu Lab printer for its access code in a field that hides it', () => {
    render(<PrinterCredentialCard activity={connect({ arguments: { provider: 'bambu' } })} onInput={vi.fn()} onCancelTool={vi.fn()}
      onCancelConnection={vi.fn()} />);
    expect(screen.getByLabelText('Access code')).toHaveAttribute('type', 'password');
  });

  it('cancels without sending anything typed', async () => {
    const cancel = vi.fn();
    render(<PrinterCredentialCard activity={connect()} onInput={vi.fn()} onCancelTool={cancel} onCancelConnection={vi.fn()} />);
    await userEvent.type(screen.getByLabelText('API key (if the printer asks for one)'), 'key123');
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(cancel).toHaveBeenCalledWith('t1');
  });

  it('says it is connecting while the app waits, with Cancel in place of the buttons', async () => {
    const cancel = vi.fn();
    render(
      <PrinterCredentialCard
        activity={connect({ state: 'succeeded' })}
        connection={{ state: 'connecting', target: '192.168.1.42' }}
        onInput={vi.fn()}
        onCancelTool={vi.fn()}
        onCancelConnection={cancel}
      />,
    );
    expect(screen.getByText('Connecting…')).toBeInTheDocument();
    expect(screen.getByRole('progressbar', { name: 'Connecting' })).toBeInTheDocument();
    expect(screen.queryByText('Sent')).toBeNull();
    expect(screen.queryByRole('button', { name: 'Connect' })).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(cancel).toHaveBeenCalledWith('t1');
  });

  it.each([
    ['verified', 'Connected to 192.168.1.42.'],
    ['failed', "Couldn't reach 192.168.1.42"],
    ['cancelled', 'Cancelled. No connection was made.'],
  ] as const)('ends %s in plain words, with nothing left to tap', (state, words) => {
    render(
      <PrinterCredentialCard
        activity={connect({ state: 'succeeded' })}
        connection={{ state, target: '192.168.1.42' }}
        onInput={vi.fn()}
        onCancelTool={vi.fn()}
        onCancelConnection={vi.fn()}
      />,
    );
    expect(screen.getByText(words)).toBeInTheDocument();
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('shows how far the connection has got once the app measures it', () => {
    render(<PrinterCredentialCard activity={connect({ state: 'running' })} connection={{ state: 'connecting', target: '192.168.1.42', percent: 60 }}
      onInput={vi.fn()} onCancelTool={vi.fn()} onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Connecting · 60%')).toBeInTheDocument();
    expect(screen.getByRole('progressbar', { name: 'Connecting' })).toHaveAttribute('aria-valuenow', '60');
  });

  it('names the kind of printer on the waiting card when the app knows it', () => {
    const { rerender } = render(<PrinterCredentialCard activity={connect()} onInput={vi.fn()} onCancelTool={vi.fn()}
      onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Pending')).toBeInTheDocument();
    rerender(<PrinterCredentialCard activity={connect()} model="Bambu A1" onInput={vi.fn()} onCancelTool={vi.fn()}
      onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Pending · Bambu A1')).toBeInTheDocument();
  });

});
