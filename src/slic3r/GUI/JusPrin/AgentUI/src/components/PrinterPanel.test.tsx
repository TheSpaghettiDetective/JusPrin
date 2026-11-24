// What the printer panel's cards state, and what the one card that asks for
// something sends. Nothing here decides a printer fact; these tests pin the
// reading and the sending.

import { describe, expect, it, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterBlock } from '../bridge/protocol';
import { PrinterBlockView, PrinterCredentialForm } from './PrinterPanel';

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
    expect(screen.getByText('+')).toBeInTheDocument();
  });
});

describe('the local credential form', () => {
  it('asks a host printer for an API key under the waiting call\'s title, and connects with or without one', async () => {
    const input = vi.fn();
    render(<PrinterCredentialForm request={{ actionId: 't1', target: '192.168.1.42', provider: 'host', fields: ['apiKey'] }}
      title="Waiting for an API key for 192.168.1.42" onConnect={input} onClose={vi.fn()} />);
    expect(screen.getByText('Waiting for an API key for 192.168.1.42')).toBeInTheDocument();
    expect(screen.getByText('The app saves this for this printer and sends it only to the printer. The AI doesn’t see it.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Help me find the key' })).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(input).toHaveBeenLastCalledWith('t1', { apiKey: '' });
    await userEvent.type(screen.getByLabelText('API key'), 'key123{Enter}');
    expect(input).toHaveBeenLastCalledWith('t1', { apiKey: 'key123' });
  });

  it('asks a Bambu Lab printer for its access code in a field that hides it', () => {
    render(<PrinterCredentialForm request={{ actionId: 't1', target: 'Studio A1', provider: 'bambu', fields: ['accessCode'] }}
      title="Waiting for the access code for Studio A1" onConnect={vi.fn()} onClose={vi.fn()} />);
    expect(screen.getByLabelText('Access code')).toHaveAttribute('type', 'password');
  });

  it('ends the waiting call when the person has no credential or closes the form, and sends nothing typed', async () => {
    const connect = vi.fn();
    const close = vi.fn();
    render(<PrinterCredentialForm request={{ actionId: 't1', target: 'Studio A1', provider: 'bambu', fields: ['accessCode'] }}
      title="Waiting for the access code for Studio A1" onConnect={connect} onClose={close} />);
    await userEvent.type(screen.getByLabelText('Access code'), '12345678');
    await userEvent.click(screen.getByRole('button', { name: 'Help me find the code' }));
    expect(close).toHaveBeenLastCalledWith('t1', true);
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(close).toHaveBeenLastCalledWith('t1', false);
    expect(connect).not.toHaveBeenCalled();
  });

  it('asks for every box the call lists, hides only the secret ones, and sends each value by its box', async () => {
    const connect = vi.fn();
    render(<PrinterCredentialForm
      request={{ actionId: 't1', target: '192.168.1.42', provider: 'host', fields: ['serialNumber', 'accessCode'] }}
      title="Connect to the printer at 192.168.1.42" onConnect={connect} onClose={vi.fn()} />);
    expect(screen.getByLabelText('Serial number')).toHaveAttribute('type', 'text');
    expect(screen.getByLabelText('Access code')).toHaveAttribute('type', 'password');
    expect(screen.getByText('The app saves these for this printer and sends them only to the printer. The AI doesn’t see them.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Help me find these' })).toBeInTheDocument();
    await userEvent.type(screen.getByLabelText('Serial number'), 'SNMQRB9400123');
    await userEvent.type(screen.getByLabelText('Access code'), 'ab12cd34{Enter}');
    expect(connect).toHaveBeenLastCalledWith('t1', { serialNumber: 'SNMQRB9400123', accessCode: 'ab12cd34' });
  });

  it('hides a password and shows a user name', () => {
    render(<PrinterCredentialForm
      request={{ actionId: 't1', target: '192.168.1.37', provider: 'host', fields: ['userName', 'password'] }}
      title="Connect to the printer at 192.168.1.37" onConnect={vi.fn()} onClose={vi.fn()} />);
    expect(screen.getByLabelText('User name')).toHaveAttribute('type', 'text');
    expect(screen.getByLabelText('Password')).toHaveAttribute('type', 'password');
  });
});
