// The project assistant's prompt is pinned to a fixture so that any change to
// it is a deliberate one: prompt evaluations compare a candidate against this
// text, and the provider's prompt cache keys on it.

import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { projectInstructions } from './projectInstructions';

describe('projectInstructions', () => {
  it('is exactly the pinned text', () => {
    // The fixture has no final newline. To change the prompt on purpose, edit
    // projectInstructions.ts and this fixture together.
    const fixture = readFileSync(join(__dirname, 'projectInstructions.fixture.txt'), 'utf8');
    expect(projectInstructions()).toBe(fixture);
  });

  it('leaves the print-issue buttons to their skill (skills/print-issues)', () => {
    expect(projectInstructions()).not.toContain('Help fix this issue.');
    expect(projectInstructions()).not.toContain('Explain this issue and my options.');
    expect(projectInstructions()).toContain('<print_issue>');
  });

  it('folds readiness into the print bullet, with no procedural section', () => {
    // A slice's findings reach the model in the workspace without a call.
    expect(projectInstructions()).not.toContain('# Preparing');
    expect(projectInstructions()).toContain('verified by a slice whose findings you have read before you call it ready');
    expect(projectInstructions().split('\n# ').map((section) => section.replace(/^# /, '').split('\n')[0]))
      .toEqual(['Role', 'When to act', 'Grounding', 'Replies']);
  });

  it('never words a change as needing the person\'s permission', () => {
    for (const word of [/approv/i, /authori[sz]/i, /confirm before/i, /ask the user then call again/i])
      expect(projectInstructions()).not.toMatch(word);
  });

  it('renders the same text every time', () => {
    expect(projectInstructions()).toBe(projectInstructions());
  });
});
