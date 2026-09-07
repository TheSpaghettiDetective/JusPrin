// The card's contract is that nothing missing can break it: every field's
// fallback ends in omitting the row, so bad or absent data makes the card
// shorter and never leaves a placeholder, a zero, or an empty frame. These
// cases walk that ladder from a full card down to nothing at all.

import { describe, expect, it } from 'vitest';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { SetupCard, formatCost, formatGrams, formatPrintTime } from './SetupCard';
import { PresetDeltaInfo, SliceEstimateInfo, WorkspaceContext } from '../bridge/protocol';

function makeContext(overrides: {
  setupIntent?: string;
  preset?: string;
  estimate?: SliceEstimateInfo | null;
  deltas?: PresetDeltaInfo[];
  sliced?: boolean;
  currency?: string;
} = {}): WorkspaceContext {
  return {
    sessionId: '1',
    revision: 1,
    projectName: 'Bracket',
    projectDirty: false,
    printer: { preset: 'MyKlipper 0.2 nozzle', filament: 'Generic PLA', process: overrides.preset ?? '0.20 mm Standard' },
    plates: [
      {
        id: '1',
        name: 'Plate 1',
        active: true,
        sliced: overrides.sliced ?? overrides.estimate != null,
        estimate: overrides.estimate ?? null,
        objects: [],
      },
    ],
    selection: { status: 'none', objectIds: [] },
    history: { canUndo: false, canRedo: false },
    presetDeltas: overrides.deltas ?? [],
    currency: overrides.currency ?? 'USD',
    setupIntent: overrides.setupIntent ?? '',
  };
}

const estimate: SliceEstimateInfo = { printTimeSeconds: 13800, materialGrams: 47, materialCost: null };

function deltas(count: number): PresetDeltaInfo[] {
  return Array.from({ length: count }, (_, index) => ({
    key: `key_${index}`,
    label: `Setting ${index}`,
    preset: '1',
    value: '2',
  }));
}

function renderCard(context: WorkspaceContext, expanded = false) {
  return render(<SetupCard context={context} expanded={expanded} onToggle={() => {}} />);
}

describe('setup card', () => {
  it('rests at kicker, title and one facts line when everything is present', () => {
    renderCard(makeContext({ setupIntent: "Strong - it'll bear weight", estimate, deltas: deltas(5) }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('Current setup');
    expect(card).toHaveTextContent("Strong - it'll bear weight");
    expect(card).toHaveTextContent('~3h 50');
    expect(card).toHaveTextContent('47 g');
    expect(card).toHaveTextContent('5 changes from preset');
    // Three lines: the heading, the title, and the facts. Nothing else.
    expect(card.querySelectorAll('p')).toHaveLength(3);
  });

  it('keeps a title at its ceiling on one line rather than wrapping to two', () => {
    const title = 'Strong, smooth top, no marks on the face'; // 39 characters
    renderCard(makeContext({ setupIntent: title, estimate, deltas: deltas(9) }));
    const line = screen.getByTitle(title);
    expect(line).toHaveClass('current-setup-title');
    expect(line.tagName).toBe('P');
  });

  it('gives the identity slot to the preset name when there is no intent to restate', () => {
    renderCard(makeContext({ estimate, deltas: deltas(2) }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('0.20 mm Standard');
    // The row is absent, not empty: no title element was rendered at all.
    expect(card.querySelector('.current-setup-title')).toBeNull();
  });

  it('names the process preset, never the machine', () => {
    // Seen for real in the app: the card showed "MyKlipper 0.2 nozzle", which
    // is the printer. The changes are measured against the process preset, so
    // that is the only name that belongs here.
    const context = makeContext({ estimate, deltas: deltas(2) });
    context.printer = { preset: 'MyKlipper 0.2 nozzle', filament: 'Generic PLA', process: '0.08mm Extra Fine' };
    renderCard(context);
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('0.08mm Extra Fine');
    expect(card).not.toHaveTextContent('MyKlipper');
  });

  it('moves a long preset name off the facts line so the useful clauses survive', () => {
    // Also seen for real: preset + estimate + count on one line clipped the
    // clauses to "not sli...". With a count to carry, the preset takes the
    // identity row instead.
    renderCard(makeContext({ preset: '0.08mm Extra Fine @MyKlipper', estimate: null, deltas: deltas(2) }));
    const card = screen.getByTestId('current-setup');
    expect(card.querySelector('.current-setup-identity')).toHaveTextContent('0.08mm Extra Fine @MyKlipper');
    // The facts line keeps what the reader actually needs, in full.
    expect(card.querySelector('.current-setup-facts')).toHaveTextContent('not sliced yet');
    expect(card.querySelector('.current-setup-facts')).toHaveTextContent('2 changes from preset');
  });

  it('keeps the preset inline when the facts line has room for it', () => {
    // No count to carry, so the card stays two lines as the design has it.
    renderCard(makeContext({ estimate }));
    const card = screen.getByTestId('current-setup');
    expect(card.querySelector('.current-setup-identity')).toBeNull();
    expect(card.querySelector('.current-setup-facts')).toHaveTextContent('0.20 mm Standard');
  });

  it('omits money entirely rather than inventing a zero when no price is set', () => {
    renderCard(makeContext({ setupIntent: 'Cheap and quick', estimate, deltas: deltas(1) }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('47 g');
    expect(card.textContent).not.toMatch(/0\.00|\$|£|€/);
  });

  it('adds money as a clause when the filament profile carries a price', () => {
    renderCard(makeContext({
      setupIntent: 'Cheap and quick',
      estimate: { printTimeSeconds: 13800, materialGrams: 47, materialCost: 1.12 },
      deltas: deltas(1),
    }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('1.12');
    // Still three lines: a clause joins the facts row, it does not add one.
    expect(card.querySelectorAll('p')).toHaveLength(3);
  });

  it('writes money the way the regional settings write it', () => {
    renderCard(makeContext({
      setupIntent: 'Cheap and quick',
      estimate: { printTimeSeconds: 13800, materialGrams: 47, materialCost: 1.12 },
      deltas: deltas(1),
      currency: 'EUR',
    }));
    // Symbol, not a bare number -- the reader can tell it is money.
    expect(screen.getByTestId('current-setup').textContent).toMatch(/€/);
  });

  it('falls back to the slicer\'s own wording when the OS names no currency', () => {
    renderCard(makeContext({
      setupIntent: 'Cheap and quick',
      estimate: { printTimeSeconds: 13800, materialGrams: 47, materialCost: 1.12 },
      deltas: deltas(1),
      currency: '',
    }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('cost 1.12');
    expect(card.textContent).not.toMatch(/[$£€¥]/);
  });

  it('drops a priced job that rounds to nothing rather than printing 0.00', () => {
    renderCard(makeContext({
      setupIntent: 'A tiny part',
      estimate: { printTimeSeconds: 600, materialGrams: 0.4, materialCost: 0.004 },
      deltas: deltas(1),
    }));
    expect(screen.getByTestId('current-setup').textContent).not.toContain('0.00');
  });

  it('says it has not sliced instead of growing a row for the missing estimate', () => {
    renderCard(makeContext({ setupIntent: "Strong - it'll bear weight", estimate: null, deltas: deltas(5) }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('not sliced yet');
    expect(card).toHaveTextContent('5 changes from preset');
    expect(card.querySelectorAll('p')).toHaveLength(3);
  });

  it('never calls a sliced plate unsliced just because it has no estimate', () => {
    // A slice can land without a usable estimate. Saying "not sliced yet"
    // there is not a missing fact, it is a false one.
    const context = makeContext({ setupIntent: 'Strong', deltas: deltas(2) });
    context.plates = [{ id: '1', name: 'Plate 1', active: true, sliced: true, estimate: null, objects: [] }];
    renderCard(context);
    const card = screen.getByTestId('current-setup');
    expect(card).not.toHaveTextContent('not sliced yet');
    expect(card).toHaveTextContent('2 changes from preset');
  });

  it('omits the facts row rather than leaving an empty line on it', () => {
    // Sliced but no usable estimate, and the preset untouched: there is
    // nothing to put on that line, so the line is not there.
    const context = makeContext({ setupIntent: 'Strong' });
    context.plates = [{ id: '1', name: 'Plate 1', active: true, sliced: true, estimate: null, objects: [] }];
    renderCard(context);
    const card = screen.getByTestId('current-setup');
    expect(card.querySelector('.current-setup-facts')).toBeNull();
    // Heading and title only.
    expect(card.querySelectorAll('p')).toHaveLength(2);
  });

  it('states the count for many objects rather than concatenating intents', () => {
    // One title cannot hold three intents, so the agent sends the count and
    // the card stays the same height as every other case.
    renderCard(makeContext({ setupIntent: '3 objects, different settings', estimate, deltas: deltas(11) }));
    expect(screen.getByTestId('current-setup').querySelectorAll('p')).toHaveLength(3);
  });

  it('degrades to a bare label, not an empty frame, when everything is missing', () => {
    renderCard(makeContext());
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveClass('bare');
    expect(card).toHaveTextContent('0.20 mm Standard');
    // No heading over a single line, and no handle onto an empty list.
    expect(screen.queryByText('Current setup')).not.toBeInTheDocument();
    expect(screen.queryByTestId('current-setup-handle')).not.toBeInTheDocument();
  });

  it('falls back to the material when the printer has no process preset', () => {
    // A non-FFF printer has no process settings, so there is no drift to
    // report -- but the material the plan is written against is still a fact
    // about the plan, and this line is where it lives.
    renderCard(makeContext({ preset: '' }));
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveClass('bare');
    expect(card).toHaveTextContent('Generic PLA');
    expect(card).not.toHaveTextContent('MyKlipper');
  });

  it('renders nothing when there is neither a preset nor a material to name', () => {
    const context = makeContext({ preset: '' });
    context.printer = { preset: 'MyKlipper 0.2 nozzle', filament: '', process: '' };
    const { container } = renderCard(context);
    expect(container).toBeEmptyDOMElement();
  });

  it('opens the deltas as a layer over the thread rather than a taller card', async () => {
    const context = makeContext({ setupIntent: 'Strong', estimate, deltas: deltas(3) });
    const { rerender } = render(<SetupCard context={context} expanded={false} onToggle={() => {}} />);
    expect(screen.queryByTestId('current-setup-expansion')).not.toBeInTheDocument();

    rerender(<SetupCard context={context} expanded onToggle={() => {}} />);
    const expansion = screen.getByTestId('current-setup-expansion');
    expect(expansion).toHaveTextContent('Setting 0');
    expect(screen.getByTestId('current-setup-handle')).toHaveAttribute('aria-expanded', 'true');
  });

  it('opens from the kicker chevron as well as from the change count', async () => {
    let toggles = 0;
    const card = (
      <SetupCard
        context={makeContext({ setupIntent: 'Strong', estimate, deltas: deltas(2) })}
        expanded={false}
        onToggle={() => { toggles += 1; }}
      />
    );
    render(card);
    await userEvent.click(screen.getByTestId('current-setup-chevron'));
    await userEvent.click(screen.getByTestId('current-setup-handle'));
    expect(toggles).toBe(2);
  });

  it('has no chevron when there is nothing to open', () => {
    renderCard(makeContext({ setupIntent: 'Strong', estimate }));
    expect(screen.queryByTestId('current-setup-chevron')).not.toBeInTheDocument();
    expect(screen.queryByTestId('current-setup-handle')).not.toBeInTheDocument();
  });

  it('names the preset the changes are measured against, inside the expansion', () => {
    renderCard(makeContext({ setupIntent: 'Strong', estimate, deltas: deltas(2) }), true);
    expect(screen.getByTestId('current-setup-expansion')).toHaveTextContent('on 0.20 mm Standard');
  });

  it('reports a toggle when the handle is used', async () => {
    let toggles = 0;
    render(
      <SetupCard
        context={makeContext({ setupIntent: 'Strong', estimate, deltas: deltas(1) })}
        expanded={false}
        onToggle={() => { toggles += 1; }}
      />,
    );
    await userEvent.click(screen.getByTestId('current-setup-handle'));
    expect(toggles).toBe(1);
  });

  it('reads the estimate from the active plate, not the first one', () => {
    const context = makeContext({ setupIntent: 'Strong', deltas: deltas(1) });
    context.plates = [
      { id: '1', name: 'Plate 1', active: false, sliced: true, estimate, objects: [] },
      {
        id: '2',
        name: 'Plate 2',
        active: true,
        sliced: true,
        estimate: { printTimeSeconds: 600, materialGrams: 4.25, materialCost: null },
        objects: [],
      },
    ];
    renderCard(context);
    const card = screen.getByTestId('current-setup');
    expect(card).toHaveTextContent('~10 min');
    expect(card).toHaveTextContent('4.3 g');
    expect(card).not.toHaveTextContent('~3h 50');
  });
});

describe('setup card formatting', () => {
  it('shows hours and minutes above an hour and minutes below it', () => {
    expect(formatPrintTime(13800)).toBe('~3h 50');
    expect(formatPrintTime(3000)).toBe('~50 min');
    expect(formatPrintTime(3660)).toBe('~1h 01');
    expect(formatPrintTime(0)).toBe('~0 min');
  });

  it('prints money unadorned, the way the slicer does, since Orca has no currency', () => {
    // With a currency from the OS, money is written the way that locale
    // writes money -- including how many decimals it uses.
    expect(formatCost(1.1249, 'USD')).toBe('$1.12');
    expect(formatCost(1234.5, 'JPY')).toBe('¥1,235');
    // Without one there is nothing to denominate it in, so it reads as the
    // slicer's own legend does.
    expect(formatCost(1.1249, '')).toBe('cost 1.12');
    // An unknown code must not lose the number.
    expect(formatCost(12, 'NOTACODE')).toBe('cost 12.00');
  });

  it('keeps a decimal only where a whole gram would be a lie', () => {
    expect(formatGrams(47.4)).toBe('47 g');
    expect(formatGrams(4.25)).toBe('4.3 g');
    expect(formatGrams(0.4)).toBe('0.4 g');
  });
});
