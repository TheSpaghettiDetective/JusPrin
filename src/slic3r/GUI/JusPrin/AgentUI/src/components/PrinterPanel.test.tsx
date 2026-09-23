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
    expect(screen.getByText('FOUND ON YOUR NETWORK')).toBeInTheDocument();
    expect(screen.getByText('01P00A3B')).toBeInTheDocument();
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
      actionClass: 'mutation', requiresApproval: true, sessionId: '1', expectedRevision: 1, state: 'pending',
      progress: { current: 0, total: 1 }, ...overrides,
    };
  }

  it('asks a host printer for an optional API key, and connects with or without one', async () => {
    const decide = vi.fn();
    render(<PrinterCredentialCard activity={connect()} onDecision={decide} />);
    expect(screen.getByText('Stays on this computer.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(decide).toHaveBeenLastCalledWith('t1', 'approve', { credential: '' });
    await userEvent.type(screen.getByLabelText('API key (if the printer asks for one)'), 'key123{Enter}');
    expect(decide).toHaveBeenLastCalledWith('t1', 'approve', { credential: 'key123' });
  });

  it('asks a Bambu Lab printer for its access code in a field that hides it', () => {
    render(<PrinterCredentialCard activity={connect({ arguments: { provider: 'bambu' } })} onDecision={vi.fn()} />);
    expect(screen.getByLabelText('Access code')).toHaveAttribute('type', 'password');
  });

  it('cancels without sending anything typed', async () => {
    const decide = vi.fn();
    render(<PrinterCredentialCard activity={connect()} onDecision={decide} />);
    await userEvent.type(screen.getByLabelText('API key (if the printer asks for one)'), 'key123');
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(decide).toHaveBeenCalledWith('t1', 'reject');
  });

  it('folds to one line once decided, with no field left', () => {
    render(<PrinterCredentialCard activity={connect({ state: 'rejected' })} onDecision={vi.fn()} />);
    expect(screen.getByText('Cancelled')).toBeInTheDocument();
    expect(screen.queryByRole('textbox')).toBeNull();
    expect(screen.queryByRole('button')).toBeNull();
  });
});
