// What a filament's chat tells the model, written from the facts the app
// sends as its session.

import { describe, expect, it } from 'vitest';
import type { FilamentSessionPayload } from './bridge/protocol';
import { filamentInstructions } from './filamentInstructions';
import { splitChoices } from './replyChoices';

const shipped: FilamentSessionPayload = {
  kind: 'filament',
  slot: 2,
  preset: 'Bambu PLA Basic @BBL X1C',
  shown: 'Bambu PLA Basic',
  material: 'PLA',
  stock: true,
  copyName: 'Bambu PLA Basic @BBL X1C - Copy',
};
const saved: FilamentSessionPayload = { ...shipped, preset: 'My PLA', shown: 'My PLA', stock: false, copyName: undefined };

describe('the filament instructions', () => {
  it('name the filament by its preset name, and the slot and material', () => {
    const text = filamentInstructions(shipped);
    expect(text).toContain('Name: Bambu PLA Basic @BBL X1C\nShown as: Bambu PLA Basic\nMaterial: PLA\nSlot: 2\n');
  });

  it('say a shipped filament is saved as the copy the app names, and one of the person\'s own in place', () => {
    expect(filamentInstructions(shipped)).toContain('Copy to save as: Bambu PLA Basic @BBL X1C - Copy\n');
    expect(filamentInstructions(saved)).not.toContain('Copy to save as');
    expect(filamentInstructions(saved)).not.toContain('Kind:');
  });

  it('keep every settings call on this filament, saved, and read before a relative change', () => {
    const text = filamentInstructions(saved);
    for (const rule of [
      'scope "filament" and target {"preset": the filament\'s Name below}',
      'read its current value with settings_get before changing it',
      'pass persistAs set to the filament\'s Name below, exactly as written there',
      'give the same number of values, separated by commas',
      'When a preview says read_only_preset',
      'do not claim to open them',
      'Only this filament is changed here',
      'from what to what',
      'Never say something is done before a tool says so',
      'including one before a tool call: say "settings" instead',
      'change the first layer\'s with the others, by the same amount or to the same value, without asking',
      'call what it is a copy of "the settings OrcaSlicer comes with"',
    ])
      expect(text).toContain(rule);
  });

  it('leave out the project assistant\'s print journey', () => {
    const text = filamentInstructions(shipped);
    for (const word of ['intent_update', 'plan_set', 'slice_start', 'export_file', 'workspace_inspect']) expect(text).not.toContain(word);
  });

  it('offer reply choices the page parses', () => {
    const text = filamentInstructions(shipped);
    expect(text).toContain('Choices: first | second | third');
    expect(splitChoices('Question?\nChoices: Yes | No').choices).toEqual(['Yes', 'No']);
  });
});
