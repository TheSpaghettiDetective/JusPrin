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
    browse: null,
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
    live: true,
    printers: [
      {
        catalogId: 'BBL/Bambu Lab A1 mini',
        deviceId: '',
        vendor: 'Bambu Lab',
        model: 'A1 mini',
        subline: '180 × 180 × 180 mm',
        picture: '',
        action: 'add',
      },
    ],
  };

  it('gives one printer the full card, brand and model on their own lines', () => {
    render(<PrinterBlockView block={printers} onAction={vi.fn()} />);

    expect(screen.getByText('Bambu Lab')).toBeInTheDocument();
    expect(screen.getByText('A1 mini')).toBeInTheDocument();
    expect(screen.getByText('180 × 180 × 180 mm')).toBeInTheDocument();
  });

  it('owns "Add this printer" on the card and offers "Not this one" beside it', async () => {
    const onAction = vi.fn();
    render(<PrinterBlockView block={printers} onAction={onAction} />);

    await userEvent.click(screen.getByRole('button', { name: 'Add this printer' }));
    expect(onAction).toHaveBeenCalledWith('add', 'BBL/Bambu Lab A1 mini');

    await userEvent.click(screen.getByRole('button', { name: 'Not this one' }));
    expect(onAction).toHaveBeenCalledWith('reject', 'BBL/Bambu Lab A1 mini');
  });

  it('asks which of two printers it is, on the cards themselves, with no Add anywhere', async () => {
    const onAction = vi.fn();
    render(
      <PrinterBlockView
        block={{
          ...printers,
          printers: [
            { catalogId: 'Creality/Ender-3 V2', deviceId: '', vendor: 'Creality', model: 'Ender-3 V2', subline: '', picture: '', action: 'choose' },
            { catalogId: 'Creality/Ender-3 S1', deviceId: '', vendor: 'Creality', model: 'Ender-3 S1', subline: '', picture: '', action: 'choose' },
          ],
        }}
        onAction={onAction}
      />,
    );

    const buttons = screen.getAllByRole('button', { name: 'This one' });
    expect(buttons).toHaveLength(2);
    expect(screen.queryByRole('button', { name: 'Add this printer' })).toBeNull();
    await userEvent.click(buttons[1]);
    expect(onAction).toHaveBeenCalledWith('candidate_pick', 'Creality/Ender-3 S1');
  });

  it('collapses a superseded or rejected card to one grey line, no button', () => {
    render(<PrinterBlockView block={{ ...printers, live: false }} onAction={vi.fn()} />);

    expect(screen.getByText('Not this one · A1 mini')).toBeInTheDocument();
    expect(screen.queryByRole('button')).toBeNull();
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

  it('draws its own way out when the printer is not on the list', async () => {
    const onAction = vi.fn();
    render(
      <PrinterBlockView
        block={{ id: 'b4', seq: 4, afterMessageId: 'm1', kind: 'unsupported', reason: 'not_listed' }}
        onAction={onAction}
      />,
    );

    expect(screen.getByText('Browse the full list')).toBeInTheDocument();
    expect(screen.getByText('Set it up myself')).toBeInTheDocument();
    await userEvent.click(screen.getByText('Browse the full list'));
    expect(onAction).toHaveBeenCalledWith('browse', '');
    await userEvent.click(screen.getByText('Set it up myself'));
    expect(onAction).toHaveBeenCalledWith('manual_setup', '');
  });

  // WP11: a network find that went quiet between "Use this" and Add draws
  // one red line with its fix, as a chip either way.
  it('offers to add anyway or check again when the network printer went quiet', async () => {
    const onAction = vi.fn();
    render(<PrinterBlockView block={{ id: 'b5', seq: 5, afterMessageId: 'm1', kind: 'offline' }} onAction={onAction} />);

    expect(screen.getByText('It stopped answering. Add it anyway, or check that it’s on.')).toBeInTheDocument();
    await userEvent.click(screen.getByText('Add it anyway'));
    expect(onAction).toHaveBeenCalledWith('add_anyway', '');
    await userEvent.click(screen.getByText('Check again'));
    expect(onAction).toHaveBeenCalledWith('add', '');
  });
});

describe('the chips', () => {
  const chips: PrinterChip[] = [
    { id: 's1', label: 'Use 0.3 mm layers', style: 'suggested', say: 'Use 0.3 mm layers', opensPhotoPicker: false },
  ];

  it('sends a chip as the person\'s own words', async () => {
    const onSay = vi.fn();
    render(<PrinterChipRow chips={chips} hint="or just type" disabled={false} onSay={onSay} onAttachFiles={vi.fn()} />);

    await userEvent.click(screen.getByRole('button', { name: 'Use 0.3 mm layers' }));
    expect(onSay).toHaveBeenCalledWith('Use 0.3 mm layers');
    expect(screen.getByText('or just type')).toBeInTheDocument();
  });

  it('offers nothing while the agent is working', async () => {
    const onSay = vi.fn();
    render(<PrinterChipRow chips={chips} hint="" disabled onSay={onSay} onAttachFiles={vi.fn()} />);
    await userEvent.click(screen.getByRole('button', { name: 'Use 0.3 mm layers' }));
    expect(onSay).not.toHaveBeenCalled();
  });

  it('"Photo of the label" opens the picker instead of sending its label', async () => {
    const onSay = vi.fn();
    const photoChips: PrinterChip[] = [
      { id: 's1', label: 'Photo of the label', style: 'suggested', say: 'Photo of the label', opensPhotoPicker: true },
    ];
    const { container } = render(
      <PrinterChipRow chips={photoChips} hint="" disabled={false} onSay={onSay} onAttachFiles={vi.fn()} />,
    );
    const clickSpy = vi.spyOn(HTMLInputElement.prototype, 'click');

    await userEvent.click(screen.getByRole('button', { name: 'Photo of the label' }));

    expect(onSay).not.toHaveBeenCalled();
    expect(clickSpy).toHaveBeenCalled();
    expect(container.querySelector('input[type="file"]')).not.toBeNull();
  });
});
