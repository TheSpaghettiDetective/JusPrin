import { describe, expect, it } from 'vitest';
import { fileReportInstructions } from './fileReportInstructions';

describe('fileReportInstructions', () => {
  const text = fileReportInstructions();

  it('gives the Agent raw-dialog guidance without asking native code to classify wording', () => {
    expect(text).toContain('messages.items');
    expect(text).toContain('The guidance is deliberately not a code classification table');
    expect(text).toContain('the answer JusPrin supplied automatically');
    expect(text).not.toContain('noticeInterpretations');
  });

  it('keeps app outcomes, physical uncertainty, and file advice distinct', () => {
    expect(text).toContain('setupPrinters');
    expect(text).toContain('name both its from and to selections');
    expect(text).toContain('state its final numeric size');
    expect(text).toContain('Neither a selection nor that list proves which physical machine');
    expect(text).toContain('details.profileDescription');
    expect(text).toContain('Write in uiLanguage');
  });

  it('leaves standing policy to the project prompt', () => {
    expect(text).toContain('The rules above still apply');
    expect(text).not.toContain('You are JusPrin');
    expect(text).not.toContain('first person');
    expect(text).not.toContain('instructions to');
  });

  it('explains precise printing terms', () => {
    expect(text).toContain('G-code is the instructions a printer runs');
    expect(text).toContain('do not invent a line number');
    expect(text).toContain('claim they were applied only when loadFacts.settingsOutcome says applied');
    expect(text).not.toContain('Avoid “preset”, “profile”, “slicer” and “G-code”');
  });
});
