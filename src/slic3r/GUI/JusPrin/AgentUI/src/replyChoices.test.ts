import { describe, expect, it } from 'vitest';
import { splitChoices, withoutPartialChoices } from './replyChoices';

describe('reply choices', () => {
  it('splits a trailing choices line off a finished message', () => {
    expect(splitChoices('Is that right?\nChoices: Yes, add it | Different printer')).toEqual({
      body: 'Is that right?',
      choices: ['Yes, add it', 'Different printer'],
    });
  });

  it('ignores blank lines after the choices line and empty choices in it', () => {
    expect(splitChoices('Is that right?\n\nChoices: Yes |  | No \n\n')).toEqual({
      body: 'Is that right?',
      choices: ['Yes', 'No'],
    });
  });

  it('leaves a message without a choices line as it is', () => {
    const text = 'What address do you use to open the printer in a browser?';
    expect(splitChoices(text)).toEqual({ body: text, choices: [] });
  });

  it('reads choices the model left on the end of its last sentence', () => {
    expect(splitChoices('Set the K1 to a 0.6 mm nozzle? Choices: Yes, change it | Not yet')).toEqual({
      body: 'Set the K1 to a 0.6 mm nozzle?',
      choices: ['Yes, change it', 'Not yet'],
    });
    expect(withoutPartialChoices('Set the K1 to a 0.6 mm nozzle? Choi')).toBe('Set the K1 to a 0.6 mm nozzle?');
  });

  it('leaves a word that merely contains the name alone', () => {
    const text = 'Your MultiChoices: setting is fine.';
    expect(splitChoices(text)).toEqual({ body: text, choices: [] });
  });

  it('only reads the last line, so a choices line in the middle stays text', () => {
    const text = 'Choices: A | B\nThen tell me which one.';
    expect(splitChoices(text)).toEqual({ body: text, choices: [] });
  });

  it('accepts a message that is nothing but its choices', () => {
    expect(splitChoices('Choices: Done')).toEqual({ body: '', choices: ['Done'] });
  });

  it('hides a streaming last line that is, or may become, the choices line', () => {
    expect(withoutPartialChoices('Is that right?\nCho')).toBe('Is that right?');
    expect(withoutPartialChoices('Is that right?\nChoices: Yes, ad')).toBe('Is that right?');
    expect(withoutPartialChoices('Is that right?\nChoices: Yes | No\n')).toBe('Is that right?');
  });

  it('keeps a streaming last line that cannot become the choices line', () => {
    expect(withoutPartialChoices('Is that right?\nChecking')).toBe('Is that right?\nChecking');
    expect(withoutPartialChoices('That is the Kobra 3')).toBe('That is the Kobra 3');
  });
});
