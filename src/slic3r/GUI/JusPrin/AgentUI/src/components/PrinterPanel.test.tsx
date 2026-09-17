// What the printer panel states and what a tap on it does. The panel renders
// host state and sends typed actions; nothing here decides a printer fact on
// its own, so these tests pin the reading and the sending, not a model.

import { describe, expect, it, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterBlock, PrinterChip, PrinterSessionPayload } from '../bridge/protocol';
import { PrinterBlockView, PrinterChipRow, PrinterPinnedCard } from './PrinterPanel';

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
    chipHint: '',
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
    expect(onAction).toHaveBeenCalledWith('candidate_pick', 'Creality/Ender-3 S1');
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
    { id: 's1', label: 'Use 0.3 mm layers', style: 'suggested', say: 'Use 0.3 mm layers' },
  ];

  it('adds the printer natively and sends everything else as the person', async () => {
    const onAdd = vi.fn();
    const onSay = vi.fn();
    render(<PrinterChipRow chips={chips} hint="or just type" disabled={false} onAdd={onAdd} onSay={onSay} />);

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(onAdd).toHaveBeenCalledTimes(1);
    expect(onSay).not.toHaveBeenCalled();

    await userEvent.click(screen.getByRole('button', { name: 'Use 0.3 mm layers' }));
    expect(onSay).toHaveBeenCalledWith('Use 0.3 mm layers');
    expect(screen.getByText('or just type')).toBeInTheDocument();
  });

  it('is the chip shape either way, and differs only by fill', () => {
    render(<PrinterChipRow chips={chips} hint="" disabled={false} onAdd={vi.fn()} onSay={vi.fn()} />);
    const primary = screen.getByRole('button', { name: 'Add this printer' });
    const plain = screen.getByRole('button', { name: 'Use 0.3 mm layers' });
    expect(primary).toHaveClass('printer-chip');
    expect(plain).toHaveClass('printer-chip');
    expect(primary).toHaveClass('printer-chip-primary');
    expect(plain).toHaveClass('printer-chip-suggested');
  });

  it('offers nothing while the agent is working', async () => {
    const onSay = vi.fn();
    render(<PrinterChipRow chips={chips} hint="" disabled onAdd={vi.fn()} onSay={onSay} />);
    await userEvent.click(screen.getByRole('button', { name: 'Use 0.3 mm layers' }));
    expect(onSay).not.toHaveBeenCalled();
  });
});
