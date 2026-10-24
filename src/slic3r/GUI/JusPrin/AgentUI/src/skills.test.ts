import { describe, expect, it } from 'vitest';
import { loadSkills, parseSkill, skills } from './skills';

const encoder = new TextEncoder();

describe('project skills', () => {
  it('loads every SKILL.md, sorted by name, one per trigger', () => {
    expect(skills.map((skill) => [skill.name, skill.trigger])).toEqual([
      ['calibration', 'calibration_started'],
      ['file-loaded', 'file_loaded'],
      ['print-issues', 'print_issues'],
      ['project-setup-changed', 'setup_changed'],
      ['slice-failed', 'slice_failed'],
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
    expect(text).toContain('Say what you take the object to be, and how it should be printed, only when that changes the advice');
    expect(text).not.toContain('make an educated guess');
    expect(text).toContain('suggest the one to use, without changing it');
    expect(text).toContain('name a set-up printer it fits, from fitsSetupPrinters');
    expect(text).toContain('Write a short message, a few sentences');
    expect(text).toContain('Do not explain routine loading');
    expect(text).toContain('End with one question or offer about the next step, not a menu of alternatives');
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
    // The app's own fit check: a placement problem is not a size problem.
    for (const field of ['bedMm', 'selectedPrinterBedMm', 'placedOnBed', 'fitsSelectedBed', 'fitsSetupPrinters', 'mesh', 'customized', 'fileFacts'])
      expect(text).toContain(field);
    expect(text).toContain('an object that is off the bed but fits it needs moving, not resizing');
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

describe('the skill for a slice that did not succeed', () => {
  const text = skills.find((skill) => skill.trigger === 'slice_failed')!.text;
  const fileLoaded = skills.find((skill) => skill.trigger === 'file_loaded')!.text;

  it('stands on the standing rules and stays well under the file-loaded skill', () => {
    expect(text).toMatch(/^# [^\n]+\n\nThe rules above still apply\./);
    expect(encoder.encode(text).length).toBeLessThan(encoder.encode(fileLoaded).length * 0.6);
  });

  it('tells a refusal from a failed run, and what counts as fixed', () => {
    expect(text).toContain('"refused"');
    expect(text).toContain('"failed"');
    expect(text).toContain('about 150 distinct messages');
    expect(text).toContain('settings_search or settings_get');
    expect(text).toContain('fixed only when a new slice no longer reports it');
  });

  it('speaks as the app, never of OrcaSlicer as another program', () => {
    expect(text).not.toMatch(/OrcaSlicer (warned|detected|reports|refused|checked)/);
    expect(text).not.toMatch(/OrcaSlicer's (check|message)/);
    expect(text).toContain('I checked the plate before slicing');
  });

  it('never speaks of approving a call', () => {
    for (const word of [/approv/i, /authori[sz]/i, /confirm before/i, /ask the user/i])
      expect(text).not.toMatch(word);
  });
});

describe('the skill for issues OrcaSlicer reports', () => {
  const text = skills.find((skill) => skill.trigger === 'print_issues')!.text;
  const fileLoaded = skills.find((skill) => skill.trigger === 'file_loaded')!.text;

  it('is well under the file-loaded skill', () => {
    expect(encoder.encode(text).length).toBeLessThan(0.75 * encoder.encode(fileLoaded).length);
  });

  it('carries what the chip buttons ask for, with the scope of a fix', () => {
    expect(text).toContain('"Explain this issue and my options." is a question');
    expect(text).toContain('"Help fix this issue." asks you to change what the issue names, and only that');
    expect(text).toContain('<print_issue>');
  });

  it('says how to read a finding and where the warnings are', () => {
    expect(text).toContain('sliceWarnings');
    expect(text).toContain('The current heatbed temperature is relatively high');
    expect(text).toContain("slice_report's findings");
    expect(text).toContain('settings_search or settings_get with that key');
    expect(text).toContain('The object, when named, narrows where');
    expect(text).toContain('not proof the print will fail');
    expect(text).toContain("the person's call");
  });

  it('counts a finding resolved only when a new slice no longer reports it', () => {
    expect(text).toContain('resolved only when a new slice no longer reports it');
    expect(text).toContain('never because a setting changed or a chip was closed or ignored');
  });

  it('guides the warning families OrcaSlicer raises, in its own words', () => {
    for (const words of ['floating cantilever', "Object can't be printed for empty layer", 'is too close to others',
      'Found gcode path conflicts', 'A G-code path goes beyond the plate boundaries', 'Orca will automatically cap',
      'Partial flushing volume set to 0', 'The nozzle hardness required by the filament'])
      expect(text).toContain(words);
    expect(text).toContain('I raise no slicing warning for thin walls, bridges, bed adhesion, brims or cooling');
  });

  it('leaves standing policy to the project prompt and speaks as the app', () => {
    expect(text).toContain('The rules above still apply');
    expect(text).not.toContain('first person');
    expect(text).not.toMatch(/OrcaSlicer (warned|detected|reports)/);
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
    expect(text).toContain('record the plan, with what is unverified, using setup_record first');
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

describe('the skill for calibrating a filament', () => {
  const skill = skills.find((candidate) => candidate.name === 'calibration')!;
  const text = skill.text;

  it("is fired by a calibration project's handover, listed by its description, and fits in the app", () => {
    expect(skill.trigger).toBe('calibration_started');
    expect(skill.description).toContain('calibrate a filament or printer');
    expect(skill.description).toContain('a calibration project');
    expect(encoder.encode(skill.description!).length).toBeLessThanOrEqual(300);
    expect(encoder.encode(text).length).toBeLessThanOrEqual(16 * 1024);
    expect(text).toMatch(/^# [^\r\n]+\r?\n\r?\nThe rules above still apply\./);
  });

  it('says which symptom points to which test, and what comes before any test', () => {
    expect(text).toContain('Stringing, hairs or blobs between parts');
    expect(text).toContain('temperature first');
    expect(text).toContain('retraction is next');
    expect(text).toContain('Gaps between lines');
    expect(text).toContain('Bulging corners and seams');
    expect(text).toContain('temperature, then flow, then pressure advance');
    expect(text).toContain('suggest drying it first');
    expect(text).toContain('a partial clog, a worn nozzle');
  });

  it("asks before the person's project closes, and hands the symptom over", () => {
    expect(text).toContain('the open project will be saved to its version history and closed');
    expect(text).toContain('nothing is saved to a preset until the person chooses');
    expect(text).toContain('Choices: Start the temperature test | Not now');
    expect(text).toContain('Say both in the message that offers the test');
    expect(text).toContain('what they saw, in their words, as symptom');
  });

  it('in a calibration project, asks which test only when the handover leaves it open, and makes it first', () => {
    expect(text).toContain('do not repeat the two facts');
    expect(text).toContain('ask first which filament is in the printer, as reply choices');
    expect(text).toContain('or the installed filaments (presets_list). Never assume one.');
    expect(text).toContain('When the note\'s symptom, or what the person says, decides the test, make it at once');
    expect(text).toContain('"Choices: Temperature tower | Flow tiles | Pressure advance"');
    expect(text).toContain('Make the test before changing any setting');
  });

  it('takes its temperature ranges from OrcaSlicer\'s own dialog', () => {
    // Temp_Calibration_Dlg::on_filament_type_changed.
    for (const range of ['PLA 230 to 190', 'ABS and ASA 270 to 230', 'PETG 250 to 230', 'PCTG 280 to 240',
      'TPU 240 to 210', 'PA-CF and PET-CF 320 to 280'])
      expect(text).toContain(range);
  });

  it('records the test, then reads a reopened one by the workspace\'s calibration fact', () => {
    expect(text).toContain("Call setup_record with the project's name as the purpose");
    expect(text).not.toContain('projectName');
    expect(text).toContain("the plate's calibration carries the test and its range");
    expect(text).toContain('tell the person to slice and print it');
    expect(text).toContain('while the plate carries calibration, its steps are in place');
    expect(text).toContain('offer to make it again with the same range');
    expect(text).toContain('"Choices: Regenerate tower | Just look at it"');
    expect(text).toContain('"Temperature tower, 230 → 190 °C in 5 °C blocks; result 215 °C saved"');
  });

  it('saves one result under the naming rule, asking only about a preset of the person\'s own', () => {
    expect(text).toContain('settings_preview_patch on the filament preset in use, with persistAs');
    expect(text).toContain('never call settings_apply_patch for it');
    expect(text).toContain('"<filament preset> · <printer> calibrated"');
    expect(text).toContain('never a printer preset\'s name with its nozzle');
    expect(text).toContain('never a second copy');
    expect(text).toContain('ask whether to write the result into it or save it as a copy');
    expect(text).toContain('saves_unsaved_edits');
    expect(text).toContain('at its saved value');
    expect(text).toContain('Say the preset\'s name exactly');
    expect(text).toContain('other projects keep their filament until the person picks this one');
    expect(text).toContain('without starting it');
  });

  it('never words a change as needing the person\'s permission', () => {
    for (const word of [/approv/i, /authori[sz]/i, /confirm before/i])
      expect(text).not.toMatch(word);
  });
});

describe('the print-issues skill points material symptoms at calibration', () => {
  it('names the calibration skill once', () => {
    const text = skills.find((skill) => skill.trigger === 'print_issues')!.text;
    expect(text.match(/the calibration skill/g)).toHaveLength(1);
  });
});
