// One test per approved Figma frame of sections 5 to 7: the production card,
// rendered from that frame's state inputs, shows every element the frame has.
// Where the card's wording differs from the frame's sample text, the frame
// pictured something the data cannot support and the test says which.

import { describe, expect, it, vi } from 'vitest';
import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { renderVisualCase, setupVisualCases } from './SetupCard.visual-cases';

function show(id: string) {
  const entry = setupVisualCases.find((item) => item.id === id);
  if (!entry) throw new Error(`Missing visual case ${id}`);
  const handlers = { onToggle: vi.fn(), onCompute: vi.fn(), onUndo: vi.fn() };
  render(renderVisualCase(entry, handlers));
  const card = screen.getByTestId('current-setup');
  return { card, ...handlers, icons: (name: string) => card.querySelectorAll(`.jp-icon-${name}`).length };
}

// The rows every summary card shares: header, title, two setting lines, the
// time / material / plate row, and the details disclosure.
function expectAnatomy(card: HTMLElement, title: string, lines: [string, string]) {
  expect(within(card).getByRole('heading', { name: title })).toBeInTheDocument();
  for (const line of lines) expect(card).toHaveTextContent(line);
  expect(within(card).getByRole('button', { name: 'Setup details' })).toBeInTheDocument();
}

const quickLines: [string, string] = ['0.28 mm layers · 2 walls', '5% gyroid infill · supports off'];
const strongLines: [string, string] = ['0.20 mm layers · 4 walls; Mounting tabs: 6',
  '30% gyroid infill · supports from build plate; local support edits'];

describe('section 5 · approved setup and saved-chat states', () => {
  it('Current · Collapsed', async () => {
    const { card, onToggle, icons } = show('5-current');
    expect(card).toHaveClass('current-setup--live');
    expect(card).toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('Current');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('~42 min');
    expect(card).toHaveTextContent('18 g');
    expect(card).toHaveTextContent('Plate 1');
    expect([icons('clock'), icons('package'), icons('grid-3x3'), icons('chevron-right')]).toEqual([1, 1, 1, 1]);
    expect(screen.queryByTestId('current-setup-expansion')).toBeNull();
    await userEvent.click(within(card).getByRole('button', { name: 'Setup details' }));
    expect(onToggle).toHaveBeenCalledOnce();
  });

  it('Current · Expanded', async () => {
    const { card, icons } = show('5-expanded');
    expectAnatomy(card, 'Strong bracket, clear screw holes', strongLines);
    expect(card).toHaveTextContent('~2h 18m');
    expect(card).toHaveTextContent('46 g');
    // The frame says "Material mismatch · Printer reports PLA"; the card can
    // only vouch for what the printer last reported, so it says that.
    expect(card).toHaveTextContent('Last reported material differs');
    expect(card).toHaveTextContent('Setup uses PETG. Printer last reported PLA.');
    expect(icons('triangle-alert')).toBe(1);
    expect(within(card).getByRole('button', { name: 'Review material' })).toBeInTheDocument();
    expect(within(card).getByRole('button', { name: 'Setup details' })).toHaveAttribute('aria-expanded', 'true');
    expect(icons('chevron-down')).toBe(1);

    const details = within(card).getByTestId('current-setup-expansion');
    const headings = within(details).getAllByRole('heading').map((heading) => heading.textContent);
    expect(headings).toEqual(['Printer & material', 'Applied settings · Plate 1', 'Local overrides · bracket v3']);
    for (const row of ['PrinterBambu A1', 'Nozzle0.4 mm', 'FilamentPETG', 'Build plateTextured PEI',
      'Layer height0.20 mm', 'Wall loops4; Mounting tabs: 6', 'Infill30% · gyroid', 'Top / bottom5 / 4 layers',
      'SupportsNormal · build plate only; local support edits', 'Brim5 mm outer brim',
      'Mounting tabs (modifier)', 'Wall loops 6 · rest of object: 4', 'Screw holes', 'Support blocker',
      'Base preset0.20 mm Standard'])
      expect(details).toHaveTextContent(row);
    expect(details).not.toHaveTextContent(/brim_width|wall_loops/);
    await userEvent.click(within(details).getByRole('button', { name: '4 changes from preset' }));
    expect(within(details).getByTestId('setup-preset-changes')).toHaveTextContent('Brim width0 → 5');
  });

  it('Earlier saved', () => {
    const { card, icons } = show('5-earlier');
    expect(card).not.toHaveClass('current-setup--live');
    expect(card).toHaveTextContent('Earlier setup');
    expect(card).toHaveTextContent('Saved Oct 3, 10:42');
    expect(card).not.toHaveTextContent('Setup summary');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('~42 min');
    expect(card).toHaveTextContent('Saved with this chat. The canvas shows your current project.');
    // The saved-at clock and the estimate clock.
    expect([icons('clock'), icons('info')]).toEqual([2, 1]);
  });
});

describe('section 6 · behavioral states', () => {
  it('Just changed', async () => {
    const { card, onUndo } = show('6-just-changed');
    expect(card).toHaveTextContent('Updated');
    expectAnatomy(card, 'Strong bracket, clear screw holes', strongLines);
    expect(card).toHaveTextContent('Layer height 0.28 → 0.2 · Wall loops 2 → 4 · Sparse infill density 5% → 30%');
    expect(card).toHaveTextContent('Estimate: ~42 min / 18 g → ~2h 18m / 46 g');
    await userEvent.click(within(card).getByRole('button', { name: 'Undo' }));
    expect(onUndo).toHaveBeenCalledWith(4);
  });

  it('Agent working', () => {
    const { card } = show('6-working');
    expect(card).toHaveTextContent('Agent working');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('Last confirmed estimates');
    expect(card).toHaveTextContent('~42 min');
    expect(card).toHaveTextContent('Agent is applying and evaluating changes…');
    expect(card).toHaveTextContent('Shown settings remain the last confirmed setup.');
  });

  it('Estimate recomputing', () => {
    const { card, icons } = show('6-recomputing');
    expect(card).toHaveTextContent('Confirmed');
    expectAnatomy(card, 'Strong bracket, clear screw holes', strongLines);
    expect(card).toHaveTextContent('Time & material estimates recomputing…');
    expect(card).toHaveTextContent('Settings confirmed · Plate 1');
    expect(card).not.toHaveTextContent('~2h 18m');
    expect(icons('clock')).toBe(0);
  });

  it('Out of date', async () => {
    const { card, onCompute, icons } = show('6-stale');
    // The frame drops the edge, greys the header, and calls the card an
    // earlier setup: the project has moved on from what it summarised.
    expect(card).not.toHaveClass('current-setup--live');
    expect(card).toHaveTextContent('Earlier setup');
    expect(card).not.toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('Out of date');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('Previous estimates · not current');
    expect(card).toHaveTextContent('~42 min');
    expect(card).toHaveTextContent('Estimates no longer match the current project.');
    expect(card).toHaveTextContent('Project settings changed after this estimate.');
    expect(icons('triangle-alert')).toBe(1);
    await userEvent.click(within(card).getByRole('button', { name: 'Refresh setup & recompute' }));
    expect(onCompute).toHaveBeenCalledOnce();
  });

  it('Edited by hand', () => {
    const { card, icons } = show('6-manual');
    expect(card).toHaveClass('current-setup--live');
    expect(card).toHaveTextContent('Edited by you');
    expectAnatomy(card, 'Strong bracket, clear screw holes', strongLines);
    expect(card).toHaveTextContent('Estimate unavailable for these edits · Plate 1');
    expect(card).toHaveTextContent('You set Wall loops 4 and Sparse infill density 30% outside the agent. Your current settings are preserved.');
    expect(icons('info')).toBe(1);
    expect(card).not.toHaveTextContent('~2h 18m');
  });

  it('One-line risk', () => {
    const { card, icons } = show('6-risk');
    expect(card).toHaveTextContent('Current');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('Coarse layers may leave visible surface lines.');
    expect(icons('triangle-alert')).toBe(1);
    // One row: nothing to open, and no score.
    expect(within(card).queryByRole('button', { name: /Review/ })).toBeNull();
  });

  it('No agent configured', () => {
    const { card } = show('6-no-agent');
    expect(card).toHaveTextContent('Preset fallback');
    expect(card).toHaveTextContent('No agent');
    expect(within(card).getByRole('heading', { name: '0.20 mm Standard' })).toBeInTheDocument();
    expect(card).toHaveTextContent('No agent configured · using the selected preset.');
    expect(within(card).queryByRole('button')).toBeNull();
  });

  it('Sliced and sent', () => {
    const { card } = show('6-sent');
    expect(card).not.toHaveClass('current-setup--live');
    expect(card).toHaveTextContent('Saved sliced snapshot');
    expect(card).toHaveTextContent('Sliced & sent');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('Saved pre-slice estimates');
    expect(card).toHaveTextContent('~42 min');
    expect(card).toHaveTextContent('Actual time & material unavailable.');
    expect(card).toHaveTextContent('Oct 3, 10:42 · sent to Bambu A1');
    expect(card).toHaveTextContent('Saved snapshot. The canvas shows your current project.');
  });
});

describe('section 7 · content stress tests', () => {
  it('Long title', () => {
    const { card } = show('7-long-title');
    const heading = within(card).getByRole('heading');
    expect(heading).toHaveAttribute('title', expect.stringContaining('under repeated use'));
    expectAnatomy(card, heading.textContent!, strongLines);
  });

  it('No title', () => {
    const { card } = show('7-no-title');
    expect(card).toHaveTextContent('Setup summary');
    expectAnatomy(card, '0.20 mm Standard', strongLines);
    expect(within(card).getAllByRole('heading')).toHaveLength(1);
  });

  it('No currency', () => {
    const { card } = show('7-no-currency');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card.querySelectorAll('.current-setup-metric')).toHaveLength(3);
    expect(card).not.toHaveTextContent(/cost|£|\$/);
  });

  it('With currency', () => {
    const { card } = show('7-with-currency');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card.querySelectorAll('.current-setup-metric')).toHaveLength(4);
    expect(card).toHaveTextContent('£1.12');
  });

  it('Never sliced', async () => {
    const { card, onCompute } = show('7-never-sliced');
    expect(card).toHaveTextContent('Not sliced');
    expectAnatomy(card, 'Quick fit check, under 1 hour', quickLines);
    expect(card).toHaveTextContent('Not sliced yet · estimates unavailable · Plate 1');
    expect(card).not.toHaveTextContent(/0 g|~0 min/);
    await userEvent.click(within(card).getByRole('button', { name: 'Compute estimates' }));
    expect(onCompute).toHaveBeenCalledOnce();
  });

  it('Three objects · Collapsed', () => {
    const { card } = show('7-three-collapsed');
    // No object's value is offered as the plate's: the first line is marked
    // as the default, the second counts the objects that leave it.
    expectAnatomy(card, '3 objects, different settings',
      ['Plate default: 0.20 mm layers · 4 walls', '30% gyroid infill · 2 objects with local overrides']);
    expect(card).toHaveTextContent('Not sliced yet · estimates unavailable · Plate 1');
  });

  it('Three objects · Expanded', () => {
    const { card } = show('7-three-expanded');
    const details = within(card).getByTestId('current-setup-expansion');
    expect(within(details).getAllByRole('heading').map((heading) => heading.textContent))
      .toEqual(['Printer & material', 'Applied settings · Plate 1', 'Local overrides · 3 objects']);
    for (const row of ['Wall loops4 · varies by object', 'Infill30% · gyroid · varies by object',
      'bracket.stl4 walls · 30% gyroidMounting tabs (modifier): wall loops 6 · Screw holes: support blocker',
      'cover.stl4 walls · 30% gyroidNo local overrides · uses plate settings',
      'spacer.stl2 walls · 5% gyroidObject override: wall loops 2, sparse infill 5%, supports off',
      'Base preset0.20 mm Standard'])
      expect(details).toHaveTextContent(row);
    expect(within(details).getByRole('button', { name: '1 change from preset' })).toBeInTheDocument();
  });

  it('Everything missing', () => {
    const { card } = show('7-missing');
    expect(card).toHaveTextContent('Preset fallback');
    expect(within(card).getByRole('heading', { name: '0.20 mm Standard' })).toBeInTheDocument();
    expect(card).toHaveTextContent('Estimate unavailable');
    expect(within(card).queryByRole('button')).toBeNull();
    expect(card).not.toHaveTextContent(/No local overrides|Current|No agent/);
  });
});
