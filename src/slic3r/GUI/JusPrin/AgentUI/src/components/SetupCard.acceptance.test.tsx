// Acceptance for the setup card and the All settings panel: the three
// designed frames, with the plan's headline in the card where the differences
// were, every remaining state of the card, and the two guards that keep the
// card and the panel from sliding back to a list of settings somebody picked.

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it, vi } from 'vitest';
import { cleanup, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { PresetDeltaInfo } from '../bridge/protocol';
import { renderVisualCase, setupVisualCases, setupWorkspace } from './SetupCard.visual-cases';
import { SetupCard } from './SetupCard';
import { AllSettings, catalogFromContext } from './AllSettings';

function visual(id: string) {
  const entry = setupVisualCases.find((item) => item.id === id);
  if (!entry) throw new Error(`Missing visual case ${id}`);
  const handlers = { onViewSetup: vi.fn(), onCompute: vi.fn(), onUndo: vi.fn(), onBack: vi.fn() };
  render(renderVisualCase(entry, handlers));
  return { ...handlers, root: screen.getByTestId(entry.kind === 'page' ? 'all-settings' : 'current-setup') };
}

// Every setting OrcaSlicer defines, read from the definitions libslic3r
// builds its configuration from.
const settingKeys: string[] = (() => {
  const source = readFileSync(resolve(__dirname, '../../../../../../libslic3r/PrintConfig.cpp'), 'utf8');
  return [...new Set([...source.matchAll(/this->add(?:_nullable)?\("([a-z0-9_]+)",\s*co[A-Z]/g)].map((match) => match[1]))];
})();

// A small seeded generator: the draw is random across seeds and the same on
// every run of one.
function draw(seed: number, count: number): string[] {
  let state = seed;
  const next = () => { state = (state * 1664525 + 1013904223) % 4294967296; return state / 4294967296; };
  const pool = [...settingKeys];
  return Array.from({ length: count }, () => pool.splice(Math.floor(next() * pool.length), 1)[0]);
}

describe('the three designed frames', () => {
  it('A1 · the card says how the agent means to print, not what differs', async () => {
    const { root: card, onViewSetup } = visual('A1');
    expect(card).toHaveTextContent('Setup summary');
    expect(card).toHaveTextContent('Current');
    expect(within(card).getByRole('heading')).toHaveTextContent('Stop the corners lifting');
    expect(card).toHaveTextContent('0.20mm Standard · Generic ABS +2');
    expect(card.querySelector('.current-setup-strategy')).toHaveTextContent(
      'Wide brim and a slow first layer for adhesion');
    expect(card.querySelectorAll('.current-setup-row--change')).toHaveLength(0);
    expect(card.querySelector('.current-setup-note')).toHaveTextContent(/^1 object with overrides$/);
    expect(card).toHaveTextContent('~2h 18m');
    expect(card).toHaveTextContent('41 g');
    await userEvent.click(within(card).getByRole('button', { name: 'All settings' }));
    expect(onViewSetup).toHaveBeenCalledOnce();
  });

  it('A2 · an untouched project is titled by its preset and says nothing changed', async () => {
    const { root: card, onCompute } = visual('A2');
    expect(card).toHaveTextContent('Not sliced');
    expect(within(card).getByRole('heading')).toHaveTextContent('0.20mm Standard @MyKlipper');
    expect([...card.querySelectorAll('.current-setup-line')].map((line) => line.textContent)).toEqual([
      'Generic ABS +2', 'Not sliced yet · estimates unavailable · Plate 1']);
    expect(card.querySelector('.current-setup-strategy')).toBeNull();
    await userEvent.click(within(card).getByRole('button', { name: 'Compute estimates' }));
    expect(onCompute).toHaveBeenCalledOnce();
    expect(within(card).getByRole('button', { name: 'All settings' })).toBeInTheDocument();
  });

  it('B · the panel holds what the card counts', async () => {
    const { root: panel, onBack } = visual('B');
    // The whole project: the presets in use, the object with settings of its
    // own, and the groups in OrcaSlicer's order with what changed in each.
    expect(screen.getByRole('button', { name: 'Showing: Whole project' })).toBeInTheDocument();
    expect(within(panel).getByTestId('all-settings-presets')).toHaveTextContent('MyKlipper 0.4 nozzle');
    expect(within(panel).getByTestId('all-settings-presets')).toHaveTextContent('Slot 1 · Generic ABS');
    expect(within(panel).getByTestId('all-settings-presets')).toHaveTextContent('0.20mm Standard4 changes from the preset');
    expect(within(panel).getByTestId('all-settings-presets')).toHaveTextContent('Plate 1Textured PEI PlateBed type set on this plate');
    expect(panel).toHaveTextContent('3x3_cali_RL.stl has settings of its own in 1 region: Corner tab.');
    expect([...panel.querySelectorAll('.all-settings-group-name')].map((name) => name.textContent)).toEqual([
      'Quality', 'Strength', 'Speed', 'Support', 'Multimaterial', 'Others', 'Filament', 'Printer']);
    expect(panel).toHaveTextContent('Quality1 changed');
    expect(panel).toHaveTextContent('Others2 changed');
    expect(panel).toHaveTextContent('Filament3 slots, side by side');
    // An open group lists its rows under OrcaSlicer's own groups; a changed
    // value is bold with the preset's under it.
    expect(panel).toHaveTextContent('First layer height0.28 mmPreset: 0.2 mm');
    expect(panel.querySelector('.all-settings-row--changed')).not.toBeNull();
    expect(screen.queryByRole('textbox')).toBeNull();
    await userEvent.click(screen.getByRole('button', { name: 'Back to chat' }));
    expect(onBack).toHaveBeenCalledOnce();
  });
});

describe('the card\'s other states', () => {
  const expected: Record<string, (string | RegExp)[]> = {
    earlier: ['Earlier setup', 'Saved Oct 8, 10:42', 'Wide brim and a slow first layer for adhesion', 'Saved with this chat.'],
    'just-changed': ['Updated', 'Add modifier: Corner tab', 'Estimate: ~2h 00m / 38 g → ~2h 18m / 41 g', 'Undo'],
    working: ['Agent working', 'Last confirmed estimates', 'Shown settings remain the last confirmed setup.'],
    recomputing: ['Confirmed', 'Time & material estimates recomputing…', 'Settings confirmed · Plate 1'],
    stale: ['Earlier setup', 'Out of date', 'Previous estimates · not current', 'a setting changed', 'Refresh setup & recompute'],
    manual: ['Edited by you', 'You set Bed temperature 105 ℃ outside the agent.', 'Estimate unavailable for these edits · Plate 1'],
    risk: ['Coarse layers may leave visible surface lines.'],
    'no-agent': ['Preset fallback', 'No agent', 'No agent configured · using the selected preset.'],
    sent: ['Saved sliced snapshot', 'Sliced & sent', 'Saved pre-slice estimates', 'Oct 8, 10:42 · sent to MyKlipper'],
    'with-currency': [/1\.12/],
    missing: ['Preset fallback', 'Estimate unavailable'],
    dense: ['Extra-wide brim, a slow first layer and a cooler bed so the long fixture stays flat on the plate',
      '1 object with overrides'],
    'no-plan': ['Stop the corners lifting', '1 object with overrides', 'Current'],
    'page-untouched': ['0.20mm Standardfrom the preset', 'Qualityfrom the preset', 'Strengthfrom the preset'],
    'page-objects': ['Shown: only what bracket.stl sets itself.',
      'bracket.stl uses the project’s settings, except in 2 regions: Mounting tabs and Screw holes.',
      'Wall loops6RegionMounting tabs · project: 2', 'Support blockerOnRegionScrew holes', 'Support paintingOnObjectbracket.stl',
      'Qualitysame as project'],
    'page-dense': ['20 changes from the preset', '6 slots', 'Slot 1 · Workshop PETG carbon fibre reinforced high temperature',
      'Slot 6 · Generic PLA'],
  };

  it('has an expectation for every state that is not one of the three frames', () => {
    expect(Object.keys(expected).sort()).toEqual(setupVisualCases.map((item) => item.id)
      .filter((id) => !['A1', 'A2', 'B'].includes(id)).sort());
  });

  it.each(Object.keys(expected))('%s', (id) => {
    const { root } = visual(id);
    for (const text of expected[id]) expect(root).toHaveTextContent(text);
    expect(root).not.toHaveTextContent(/undefined|NaN/);
  });

  it('a change that is new is not listed on the card', () => {
    const { root } = visual('just-changed');
    expect(root).not.toHaveTextContent(/Brim width|0 mm → 8 mm/);
  });

  it('no card lists a difference or counts them', () => {
    for (const entry of setupVisualCases.filter((item) => item.kind === 'card')) {
      const { root } = visual(entry.id);
      expect(root.querySelectorAll('.current-setup-row--change')).toHaveLength(0);
      // A hand edit's notice names the setting from the change log; the
      // difference itself, preset value to value, is the page's.
      for (const delta of entry.context.presetDeltas)
        expect(root).not.toHaveTextContent(delta.display || `${delta.preset} → ${delta.value}`);
      expect(root).not.toHaveTextContent(/more change|No changes from presets/);
      cleanup();
    }
  });

  it('a card with no plan has no strategy line, and no stand-in for one', () => {
    for (const id of ['no-plan', 'A2']) {
      const { root } = visual(id);
      expect(root.querySelector('.current-setup-strategy')).toBeNull();
      cleanup();
    }
  });

  it('a card that claims nothing offers nothing to open', () => {
    for (const id of ['no-agent', 'missing']) {
      expect(within(visual(id).root).queryByRole('button')).toBeNull();
      cleanup();
    }
  });

  it('a saved card offers no live action', () => {
    for (const id of ['earlier', 'sent']) {
      const { root } = visual(id);
      expect(within(root).getAllByRole('button').map((button) => button.textContent)).toEqual(['All settings']);
      cleanup();
    }
  });
});

describe('any setting, not a chosen few', () => {
  it('reads the settings from libslic3r\'s own definitions', () => {
    expect(settingKeys.length).toBeGreaterThan(500);
    expect(settingKeys).toEqual(expect.arrayContaining(['layer_height', 'hot_plate_temp', 'retraction_length', 'extruder']));
  });

  it.each([7, 19, 43, 101, 20261008])('shows five settings drawn at random (seed %i) on the panel and none on the card', (seed) => {
    const context = setupWorkspace();
    const types = ['process', 'filament', 'printer'] as const;
    context.presetDeltas = draw(seed, 5).map((key, index): PresetDeltaInfo => ({ key, label: `Label of ${key}`,
      preset: 'a', value: 'b', display: `before ${key} → after ${key}`,
      presetType: types[index % 3], presetName: `Preset ${index % 3}`, page: `Page ${index}`, group: `Group ${index}` }));
    const panel = render(<AllSettings context={context} catalog={catalogFromContext(context)} />);
    for (const setting of context.presetDeltas) {
      expect(screen.getByText(setting.label)).toBeInTheDocument();
      expect(screen.getByTestId('all-settings')).toHaveTextContent(`after ${setting.key}`);
      expect(screen.getByTestId('all-settings')).toHaveTextContent(`before ${setting.key}`);
    }
    expect(screen.getByTestId('all-settings').querySelectorAll('.all-settings-row--changed')).toHaveLength(5);
    panel.unmount();
    render(<SetupCard context={context} onViewSetup={() => {}} />);
    const card = screen.getByTestId('current-setup');
    expect(card.querySelectorAll('.current-setup-row--change')).toHaveLength(0);
    for (const setting of context.presetDeltas) expect(card).not.toHaveTextContent(setting.label);
  });

  it('names no setting in the card\'s or the panel\'s source, but an object\'s filament slot', () => {
    // A key with an underscore is unmistakable wherever it stands; a one-word
    // key is one only as a quoted string.
    const mentions = (source: string) => settingKeys.filter((key) => key.includes('_')
      ? new RegExp(`(^|[^A-Za-z0-9_])${key}([^A-Za-z0-9_]|$)`).test(source)
      : new RegExp(`['"\`]${key}['"\`]`).test(source));
    expect(mentions(readFileSync(resolve(__dirname, 'SetupCard.tsx'), 'utf8'))).toEqual([]);
    expect(mentions(readFileSync(resolve(__dirname, 'AllSettings.tsx'), 'utf8'))).toEqual(['extruder']);
    // The guard can see a key: these are how a fixed list would be written.
    expect(mentions("const KEYS = ['layer_height', 'wipe'];")).toEqual(expect.arrayContaining(['layer_height', 'wipe']));
    expect(mentions('settingOf(setup, "wall_loops")')).toEqual(['wall_loops']);
  });
});
