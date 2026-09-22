// What the printer panel states and what a tap on it does. The panel renders
// host state and sends typed actions; nothing here decides a printer fact on
// its own, so these tests pin the reading and the sending, not a model.

import { describe, expect, it, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterBlock, PrinterCardInfo, ToolActivityInfo } from '../bridge/protocol';
import { PrinterAccessCode, PrinterBlockView, PrinterChangeCard, PrinterChipRow } from './PrinterPanel';

function card(overrides: Partial<PrinterCardInfo> = {}): PrinterCardInfo {
  return {
    catalogId: 'BBL/Bambu Lab A1 mini',
    deviceId: '',
    name: 'Bambu Lab A1 mini',
    buildVolume: '180 × 180 × 180 mm',
    picture: '',
    action: 'add',
    assumed: { nozzle: 0.4, plate: 'Textured PEI Plate', filament: 'Bambu PLA Basic @BBL A1M' },
    ...overrides,
  };
}

describe('the cards the agent draws', () => {
  const printers: PrinterBlock = {
    id: 'b1',
    seq: 1,
    afterMessageId: 'm1',
    kind: 'printers',
    printers: [card()],
  };

  it('gives one printer the full card and no button of its own', () => {
    render(<PrinterBlockView block={printers} onAction={vi.fn()} />);

    expect(screen.getByText('Bambu Lab A1 mini')).toBeInTheDocument();
    expect(screen.getByText('180 × 180 × 180 mm')).toBeInTheDocument();
    // Adding is the chip under the thread, not a second button on the card.
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('asks which of two printers it is, on the cards themselves', async () => {
    const onAction = vi.fn();
    const ender = { action: 'choose' as const, assumed: { nozzle: 0.6, plate: '', filament: 'Creality Generic PLA' } };
    render(
      <PrinterBlockView
        block={{
          ...printers,
          printers: [
            card({ ...ender, catalogId: 'Creality/Ender-3 V2', name: 'Ender-3 V2' }),
            card({ ...ender, catalogId: 'Creality/Ender-3 S1', name: 'Ender-3 S1' }),
          ],
        }}
        onAction={onAction}
      />,
    );

    const buttons = screen.getAllByRole('button', { name: 'This one' });
    expect(buttons).toHaveLength(2);
    await userEvent.click(buttons[1]);
    // The card is named too, so the app keeps the nozzle the model gave it,
    // and the note says what that card assumes.
    expect(onAction).toHaveBeenCalledWith('candidate_pick', 'Creality/Ender-3 S1', {
      blockId: 'b1',
      note: 'Selected Ender-3 S1. Nozzle choice: 0.6 mm.',
    });
  });

  it('states what a network printer reported under its name', () => {
    render(
      <PrinterBlockView
        block={{
          ...printers,
          printers: [
            card({
              deviceId: '01P00A3B',
              device: { nozzle: 0.4, ams: 'AMS lite', spools: [{ name: 'PLA Matte', material: 'PLA' }], reported: true },
            }),
          ],
        }}
        onAction={vi.fn()}
      />,
    );
    expect(screen.getByText('0.4 mm nozzle · AMS lite · PLA Matte · read from the printer just now')).toBeInTheDocument();
  });

  it('folds a replaced card to one line with nothing left to tap', () => {
    render(<PrinterBlockView block={{ ...printers, collapsed: true }} onAction={vi.fn()} />);
    expect(screen.getByText('Bambu Lab A1 mini')).toHaveClass('printer-cards-collapsed');
    expect(screen.queryByText('180 × 180 × 180 mm')).toBeNull();
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('names the last change and undoes it natively', async () => {
    const onAction = vi.fn();
    render(
      <PrinterBlockView
        block={{ id: 'b4', seq: 4, afterMessageId: 'm2', kind: 'undo', changed: { nozzle: { before: 0.4, after: 0.6 } } }}
        onAction={onAction}
      />,
    );
    expect(screen.getByText('Nozzle set to 0.6 mm')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Undo' }));
    expect(onAction).toHaveBeenCalledWith('undo', 'b4', { note: 'The person undid the change: the nozzle is 0.4 mm again.' });
  });

  it('lists what is on the network, with its serial and a way to use it', async () => {
    const onAction = vi.fn();
    render(
      <PrinterBlockView
        block={{
          id: 'b2',
          seq: 2,
          afterMessageId: 'm1',
          kind: 'network',
          printers: [
            {
              deviceId: '01P00A3B',
              name: 'Bambu Lab A1 mini',
              serial: '01P00A3B',
              online: true,
              match: { name: 'Bambu Lab A1 mini', nozzle: 0.4, reported: true },
            },
          ],
        }}
        onAction={onAction}
      />,
    );

    expect(screen.getByText('FOUND ON YOUR NETWORK')).toBeInTheDocument();
    expect(screen.getByText('01P00A3B')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Use this' }));
    expect(onAction).toHaveBeenCalledWith('network_pick', '01P00A3B', {
      note: 'Selected Bambu Lab A1 mini (01P00A3B), reporting a 0.4 mm nozzle.',
    });
  });

  it('says how a photo gets in, in words', () => {
    render(<PrinterBlockView block={{ id: 'b3', seq: 3, afterMessageId: 'm1', kind: 'tip' }} onAction={vi.fn()} />);
    expect(screen.getByText(/a photo is the fastest way/)).toBeInTheDocument();
  });
});

describe('the chips', () => {
  it('are there only while a printer is on its card', () => {
    render(<PrinterChipRow canAdd={false} disabled={false} onAdd={vi.fn()} onReject={vi.fn()} />);
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('adds the printer or refuses it natively, never as the person speaking', async () => {
    const onAdd = vi.fn();
    const onReject = vi.fn();
    render(<PrinterChipRow canAdd disabled={false} onAdd={onAdd} onReject={onReject} />);

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(onAdd).toHaveBeenCalledTimes(1);
    expect(onReject).not.toHaveBeenCalled();

    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(onReject).toHaveBeenCalledTimes(1);
  });

  it('is the chip shape either way, and differs only by fill', () => {
    render(<PrinterChipRow canAdd disabled={false} onAdd={vi.fn()} onReject={vi.fn()} />);
    const primary = screen.getByRole('button', { name: 'Add this printer' });
    const plain = screen.getByRole('button', { name: 'Not this one' });
    expect(primary).toHaveClass('printer-chip');
    expect(plain).toHaveClass('printer-chip');
    expect(primary).toHaveClass('printer-chip-primary');
    expect(plain).toHaveClass('printer-chip-plain');
  });

  it('offers nothing while the agent is working', async () => {
    const onReject = vi.fn();
    render(<PrinterChipRow canAdd disabled onAdd={vi.fn()} onReject={onReject} />);
    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(onReject).not.toHaveBeenCalled();
  });
});

describe('the access code', () => {
  it('is a field of its own that reports what is typed and sends nothing', async () => {
    const onChange = vi.fn();
    render(<PrinterAccessCode value="" onChange={onChange} />);
    await userEvent.type(screen.getByRole('textbox', { name: 'Access code' }), '1');
    expect(onChange).toHaveBeenCalledWith('1');
  });
});

function change(overrides: Partial<ToolActivityInfo> = {}): ToolActivityInfo {
  return {
    actionId: 't-1',
    correlationId: 'm2',
    server: 'jusprin',
    tool: 'printer_change',
    title: 'Change nozzle',
    arguments: { nozzle: 0.6, confirm: { printer: 'Bambu Lab A1 mini', before: { nozzle: 0.4 } } },
    actionClass: 'mutation',
    requiresApproval: true,
    sessionId: '1',
    expectedRevision: 1,
    state: 'pending',
    progress: { current: 0, total: 1 },
    ...overrides,
  };
}

describe('the change card', () => {
  it('states a nozzle change in words and names both answers', async () => {
    const onDecision = vi.fn();
    render(<PrinterChangeCard activity={change()} onDecision={onDecision} />);

    expect(screen.getByText('Change nozzle')).toBeInTheDocument();
    expect(screen.getByText(/0\.4 mm →/)).toHaveTextContent('0.4 mm → 0.6 mm on Bambu Lab A1 mini');
    expect(screen.getByText('Every project that uses this printer slices for 0.6 mm.')).toBeInTheDocument();
    // No tool name on the card.
    expect(screen.queryByText(/printer_change/)).toBeNull();

    await userEvent.click(screen.getByRole('button', { name: 'Keep 0.4 mm' }));
    expect(onDecision).toHaveBeenCalledWith('t-1', 'reject');
    await userEvent.click(screen.getByRole('button', { name: 'Set 0.6 mm' }));
    expect(onDecision).toHaveBeenCalledWith('t-1', 'approve');
  });

  it('lists the spools before and after', () => {
    render(
      <PrinterChangeCard
        activity={change({
          title: 'Change spools',
          arguments: {
            spools: [{ name: 'Teal PLA', material: 'PLA', colour: '#2a9d8f' }],
            confirm: { printer: 'Bambu Lab A1 mini', before: { spools: [{ name: 'Black PETG', material: 'PETG' }] } },
          },
        })}
        onDecision={vi.fn()}
      />,
    );
    expect(screen.getByText('Black PETG')).toBeInTheDocument();
    expect(screen.getByText('Teal PLA')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Set these spools' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Keep as it is' })).toBeInTheDocument();
  });

  it('says a kept printer was kept, and offers nothing more', () => {
    render(<PrinterChangeCard activity={change({ state: 'rejected' })} onDecision={vi.fn()} />);
    expect(screen.getByText('Kept as it was')).toBeInTheDocument();
    expect(screen.queryByRole('button')).toBeNull();
  });
});
