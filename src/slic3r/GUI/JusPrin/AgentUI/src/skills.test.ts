import { describe, expect, it } from 'vitest';
import { loadSkills, parseSkill, skills } from './skills';

const encoder = new TextEncoder();

describe('project skills', () => {
  it('loads every SKILL.md, sorted by name, one per trigger', () => {
    expect(skills.map((skill) => [skill.name, skill.trigger])).toEqual([
      ['file-loaded', 'file_loaded'],
      ['project-setup-changed', 'setup_changed'],
    ]);
    expect(skills.every((skill) => skill.text.startsWith('# '))).toBe(true);
    expect(skills.every((skill) => !skill.text.includes('---\nname:'))).toBe(true);
  });

  it('reads the frontmatter and keeps the body as the text the model gets', () => {
    const skill = parseSkill('---\nname: some-event\ntrigger: some_event\n---\n\n# Some event\n\nSay so.\n',
      './skills/some-event/SKILL.md');
    expect(skill).toEqual({ name: 'some-event', trigger: 'some_event', text: '# Some event\n\nSay so.' });
    // A skill for the model to read on its own has a description and no trigger.
    expect(parseSkill('---\nname: tune-strength\ndescription: When a part must carry load.\n---\n# Strength\n',
      './skills/tune-strength/SKILL.md'))
      .toEqual({ name: 'tune-strength', description: 'When a part must carry load.', text: '# Strength' });
    const quoted = parseSkill('---\nname: "quoted"\ntrigger: \'some_event\'\n---\n# Quoted\n', './skills/quoted/SKILL.md');
    expect([quoted.name, quoted.trigger]).toEqual(['quoted', 'some_event']);
  });

  it('refuses a file the app could not use', () => {
    const file = (front: string, body = '# Body\n') => `---\n${front}\n---\n${body}`;
    expect(() => parseSkill('# No frontmatter', './skills/bad/SKILL.md')).toThrow('frontmatter');
    expect(() => parseSkill(file('name: wrong\ntrigger: some_event'), './skills/right/SKILL.md')).toThrow('directory');
    expect(() => parseSkill(file('name: Bad_Name\ntrigger: some_event'), './skills/Bad_Name/SKILL.md')).toThrow('directory');
    expect(() => parseSkill(file('name: bad'), './skills/bad/SKILL.md')).toThrow('a trigger, a description or both');
    expect(() => parseSkill(file('name: bad\ntrigger: Some-Event'), './skills/bad/SKILL.md')).toThrow('trigger');
    expect(() => parseSkill(file(`name: bad\ndescription: ${'é'.repeat(151)}`), './skills/bad/SKILL.md'))
      .toThrow('300 UTF-8 bytes');
    expect(() => parseSkill(file('name: bad\ntrigger: some_event\nwhen: always'), './skills/bad/SKILL.md'))
      .toThrow('only name, trigger and description');
    expect(() => parseSkill(file('name: bad\ntrigger: some_event', '\n\n'), './skills/bad/SKILL.md')).toThrow('empty');
    expect(() => parseSkill(file('name: huge\ntrigger: some_event', 'x'.repeat(16 * 1024 + 1)), './skills/huge/SKILL.md'))
      .toThrow('16 KiB');
    expect(() => parseSkill(file('name: bad\ntrigger: "unterminated'), './skills/bad/SKILL.md')).toThrow('quoted');
  });

  it('refuses two skills with one name or one trigger', () => {
    const text = (name: string, trigger: string) => `---\nname: ${name}\ntrigger: ${trigger}\n---\n# ${name}\n`;
    expect(() => loadSkills({ './skills/same/SKILL.md': text('same', 'a'), './other/same/SKILL.md': text('same', 'b') }))
      .toThrow('duplicate');
    expect(() => loadSkills({ './skills/one/SKILL.md': text('one', 'a'), './skills/two/SKILL.md': text('two', 'a') }))
      .toThrow('share the trigger');
    expect(loadSkills({ './skills/zeta/SKILL.md': text('zeta', 'z'), './skills/alpha/SKILL.md': text('alpha', 'a') })
      .map((skill) => skill.name)).toEqual(['alpha', 'zeta']);
    // Skills without a trigger never share one.
    const described = (name: string) => `---\nname: ${name}\ndescription: Useful.\n---\n# ${name}\n`;
    expect(loadSkills({ './skills/one/SKILL.md': described('one'), './skills/two/SKILL.md': described('two') })).toHaveLength(2);
  });
});

describe('the skill for a file opened or imported', () => {
  const text = skills.find((skill) => skill.trigger === 'file_loaded')!.text;

  it('fits in the app with the sentence the app adds', () => {
    // The app refuses a text over 16 KiB; it adds its no-purpose sentence,
    // 97 bytes and a blank line, when nothing is recorded.
    expect(encoder.encode(text).length + 2 + 97).toBeLessThanOrEqual(16 * 1024);
  });

  it('covers both ways a file is loaded, the picture, the presets and the next step', () => {
    expect(text).toContain('the file just opened or imported, by the person or by a tool call of mine');
    expect(text).toContain('change nothing the person has not asked for');
    expect(text).not.toContain('They have asked for nothing yet');
    expect(text).toContain('The picture, when present, is the active plate after the load');
    expect(text).toContain('Look at the attached picture when there is one');
    expect(text).toContain('make an educated guess at what the object is, what it is likely for, and how it should be sliced');
    expect(text).toContain('against the presets available (presets_list)');
    expect(text).toContain('suggest the one to use, without changing it');
    expect(text).toContain('End with your best guess at what the person wants to do next');
    expect(text).not.toContain('90 words');
  });

  it('gives the Agent raw-dialog guidance without asking native code to classify wording', () => {
    expect(text).toContain('messages.items');
    expect(text).toContain('deliberately not a code classification table');
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
  });

  it('never words a change as needing the person\'s permission', () => {
    for (const word of [/approv/i, /authori[sz]/i, /confirm before/i, /ask the user then call again/i])
      expect(text).not.toMatch(word);
  });
});

describe('the skill for a setup to check against the print\'s purpose', () => {
  const text = skills.find((skill) => skill.trigger === 'setup_changed')!.text;
  const arrived = skills.find((skill) => skill.trigger === 'file_loaded')!.text;

  it('opens under the standing rules and stays short', () => {
    expect(text.split('\n\n')[1]).toMatch(/^The rules above still apply\./);
    expect(encoder.encode(text).length).toBeLessThan(encoder.encode(arrived).length / 4);
  });

  it('says what checking the setup against the purpose means here', () => {
    expect(text).toContain('A preset\'s name does not prove its values');
    expect(text).toContain('settings_get, including the objects\' own overrides');
    expect(text).toContain('compatible presets with presets_list');
    expect(text).toContain('plate type against the filament with printer_setup_preview');
    expect(text).toContain('A material and its saved filament preset are separate choices');
    expect(text).toContain('Keep customizations that still fit');
    expect(text).toContain('recommend a material, a compatible preset or a focused setting change');
    expect(text).toContain('record the plan and its assumptions with plan_set first');
    expect(text).toContain('restores an earlier version, the plan may be stale');
  });

  it('leaves asking and scope to the standing prompt', () => {
    expect(text).not.toMatch(/\bask\b/i);
    expect(text).not.toContain('did not ask for');
    expect(text).not.toContain('You are JusPrin');
  });

  it('never words a change as needing the person\'s permission', () => {
    for (const word of [/approv/i, /authori[sz]/i, /confirm before/i, /ask the user then call again/i])
      expect(text).not.toMatch(word);
  });
});
