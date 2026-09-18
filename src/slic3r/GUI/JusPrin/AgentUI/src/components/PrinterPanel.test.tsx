// What the printer panel states and what a tap on it does. The panel renders
// host state and sends typed actions; nothing here decides a printer fact on
// its own, so these tests pin the reading and the sending, not a model.

import { describe, expect, it, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterBlock, PrinterChip, PrinterSessionPayload, ToolActivityInfo } from '../bridge/protocol';
import { PrinterAccessCode, PrinterBlockView, PrinterChangeCard, PrinterChipRow, PrinterPinnedCard } from './PrinterPanel';

function session(overrides: Partial<PrinterSessionPayload> = {}): PrinterSessionPayload {
  return {
    mode: 'add',
    caption: 'NEW PRINTER',
    facts: {
      printer: { value: '', provenance: 'settled' },
      nozzle: { value: '', provenance: 'settled' },
      plate: { value: '', provenance: 'settled' },
      filament: { value: '', provenance: 'settled' },
    },
    blocks: [],
    chips: [],
    placeholder: 'e.g. "bambu a1 mini" or "not sure, the small one"',
    ...overrides,
  };
}

describe('the pinned card', () => {
  it('states the four facts in order, with an em dash for what nobody has said', () => {
    render(<PrinterPinnedCard session={session()} />);

    expect(screen.getByText('NEW PRINTER')).toBeInTheDocument();
    const labels = screen.getAllByText(/^(Printer|Nozzle|Plate|Filament)$/).map((node) => node.textContent);
    expect(labels).toEqual(['Printer', 'Nozzle', 'Plate', 'Filament']);
    expect(screen.getAllByText('—')).toHaveLength(4);
  });

  it('marks an assumed fact as assumed and a changed one as changed', () => {
    render(
      <PrinterPinnedCard
        session={session({
          caption: 'PRINTER',
          mode: 'change',
          facts: {
            printer: { value: 'Bambu Lab A1 Combo', provenance: 'settled' },
            nozzle: { value: '0.6 mm', provenance: 'changed' },
            plate: { value: 'Textured PEI', provenance: 'assumed' },
            filament: { value: 'AMS · 4 slots', provenance: 'settled', swatch: '#5f7d4f' },
          },
        })}
      />,
    );

    expect(screen.getByText(/Textured PEI · assumed/)).toBeInTheDocument();
    const changed = screen.getByText(/0\.6 mm · changed/);
    expect(changed).toHaveClass('printer-fact-changed');
    // A settled fact says nothing about where it came from.
    expect(screen.getByText(/Bambu Lab A1 Combo/)).not.toHaveTextContent('assumed');
  });
});

describe('the cards the agent draws', () => {
  const printers: PrinterBlock = {
    id: 'b1',
    seq: 1,
    afterMessageId: 'm1',
    kind: 'printers',
    printers: [
      {
        catalogId: 'BBL/Bambu Lab A1 mini',
        deviceId: '',
        name: 'Bambu Lab A1 mini',
        subline: '180 × 180 × 180 mm',
        picture: '',
        action: 'add',
      },
    ],
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
    render(
      <PrinterBlockView
        block={{
          ...printers,
          printers: [
            { catalogId: 'Creality/Ender-3 V2', deviceId: '', name: 'Ender-3 V2', subline: '', picture: '', action: 'choose' },
            { catalogId: 'Creality/Ender-3 S1', deviceId: '', name: 'Ender-3 S1', subline: '', picture: '', action: 'choose' },
          ],
        }}
        onAction={onAction}
      />,
    );

    const buttons = screen.getAllByRole('button', { name: 'This one' });
    expect(buttons).toHaveLength(2);
    await userEvent.click(buttons[1]);
    // The card is named too, so the app keeps the nozzle the model gave it.
    expect(onAction).toHaveBeenCalledWith('candidate_pick', 'Creality/Ender-3 S1', 'b1');
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
        block={{ id: 'b4', seq: 4, afterMessageId: 'm2', kind: 'undo', text: 'Nozzle set to 0.6 mm' }}
        onAction={onAction}
      />,
    );
    expect(screen.getByText('Nozzle set to 0.6 mm')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Undo' }));
    expect(onAction).toHaveBeenCalledWith('undo', 'b4');
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
          printers: [{ deviceId: '01P00A3B', name: 'Bambu Lab A1 mini', serial: '01P00A3B', online: true }],
        }}
        onAction={onAction}
      />,
    );

    expect(screen.getByText('FOUND ON YOUR NETWORK')).toBeInTheDocument();
    expect(screen.getByText('01P00A3B')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Use this' }));
    expect(onAction).toHaveBeenCalledWith('network_pick', '01P00A3B');
  });

  it('says how a photo gets in, in words', () => {
    render(<PrinterBlockView block={{ id: 'b3', seq: 3, afterMessageId: 'm1', kind: 'tip' }} onAction={vi.fn()} />);
    expect(screen.getByText(/a photo is the fastest way/)).toBeInTheDocument();
  });
});

describe('the chips', () => {
  const chips: PrinterChip[] = [
    { id: 'add', label: 'Add this printer', style: 'primary', action: 'add' },
    { id: 'reject', label: 'Not this one', style: 'plain', action: 'reject' },
  ];

  it('adds the printer or refuses it natively, never as the person speaking', async () => {
    const onAdd = vi.fn();
    const onReject = vi.fn();
    render(<PrinterChipRow chips={chips} disabled={false} onAdd={onAdd} onReject={onReject} />);

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(onAdd).toHaveBeenCalledTimes(1);
    expect(onReject).not.toHaveBeenCalled();

    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(onReject).toHaveBeenCalledTimes(1);
  });

  it('is the chip shape either way, and differs only by fill', () => {
    render(<PrinterChipRow chips={chips} disabled={false} onAdd={vi.fn()} onReject={vi.fn()} />);
    const primary = screen.getByRole('button', { name: 'Add this printer' });
    const plain = screen.getByRole('button', { name: 'Not this one' });
    expect(primary).toHaveClass('printer-chip');
    expect(plain).toHaveClass('printer-chip');
    expect(primary).toHaveClass('printer-chip-primary');
    expect(plain).toHaveClass('printer-chip-plain');
  });

  it('offers nothing while the agent is working', async () => {
    const onReject = vi.fn();
    render(<PrinterChipRow chips={chips} disabled onAdd={vi.fn()} onReject={onReject} />);
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
