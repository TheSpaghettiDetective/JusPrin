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
    render(<PrinterCredentialCard activity={connect()} onDecision={decide} onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Stays on this computer.')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(decide).toHaveBeenLastCalledWith('t1', 'approve', { credential: '' });
    await userEvent.type(screen.getByLabelText('API key (if the printer asks for one)'), 'key123{Enter}');
    expect(decide).toHaveBeenLastCalledWith('t1', 'approve', { credential: 'key123' });
  });

  it('asks a Bambu Lab printer for its access code in a field that hides it', () => {
    render(<PrinterCredentialCard activity={connect({ arguments: { provider: 'bambu' } })} onDecision={vi.fn()} onCancelConnection={vi.fn()} />);
    expect(screen.getByLabelText('Access code')).toHaveAttribute('type', 'password');
  });

  it('cancels without sending anything typed', async () => {
    const decide = vi.fn();
    render(<PrinterCredentialCard activity={connect()} onDecision={decide} onCancelConnection={vi.fn()} />);
    await userEvent.type(screen.getByLabelText('API key (if the printer asks for one)'), 'key123');
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(decide).toHaveBeenCalledWith('t1', 'reject');
  });

  it('says it is connecting while the app waits, with Cancel in place of the buttons', async () => {
    const cancel = vi.fn();
    render(
      <PrinterCredentialCard
        activity={connect({ state: 'succeeded' })}
        connection={{ state: 'connecting', target: '192.168.1.42' }}
        onDecision={vi.fn()}
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

  it('says it is connecting from the tap on, before the app has started the attempt', () => {
    render(<PrinterCredentialCard activity={connect({ state: 'approved' })} onDecision={vi.fn()} onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Connecting…')).toBeInTheDocument();
    // Nothing to cancel yet.
    expect(screen.queryByRole('button')).toBeNull();
  });

  it.each([
    ['verified', 'Connected'],
    ['failed', "Couldn't reach 192.168.1.42"],
    ['cancelled', 'Cancelled'],
  ] as const)('ends %s in plain words, with nothing left to tap', (state, words) => {
    render(
      <PrinterCredentialCard
        activity={connect({ state: 'succeeded' })}
        connection={{ state, target: '192.168.1.42' }}
        onDecision={vi.fn()}
        onCancelConnection={vi.fn()}
      />,
    );
    expect(screen.getByText(words)).toBeInTheDocument();
    expect(screen.queryByRole('progressbar')).toBeNull();
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('folds to one line once decided, with no field left', () => {
    render(<PrinterCredentialCard activity={connect({ state: 'rejected' })} onDecision={vi.fn()} onCancelConnection={vi.fn()} />);
    expect(screen.getByText('Cancelled')).toBeInTheDocument();
    expect(screen.queryByRole('textbox')).toBeNull();
    expect(screen.queryByRole('button')).toBeNull();
  });
});
