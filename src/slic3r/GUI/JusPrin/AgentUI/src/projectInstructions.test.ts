import { describe, expect, it } from 'vitest';
import { projectInstructions } from './projectInstructions';
import { skills } from './skills';

// One home per rule: the prompt states the standing rules, a skill adds
// judgement for one situation, a description says when its skill applies.
// These tests fail when a rule is stated in a second home, which is how the
// earlier prompt came to give the model instructions that disagreed.
describe('projectInstructions', () => {
  const text = projectInstructions(skills);
  // Everything before the skill index, which is the last thing in the text.
  const prose = text.slice(0, text.indexOf(`\n- ${skills[0].name}: `));
  const count = (haystack: string, needle: string) => haystack.split(needle).length - 1;

  it('states each standing rule once, in a fixed order of sections', () => {
    const headings = [...text.matchAll(/^# (.+)$/gm)].map((match) => match[1]);
    expect(headings).toEqual(['How far a request reaches', 'Working on a print', 'Evidence', 'Asking', 'App messages', 'Skills']);
    for (const rule of ['a question is not a request to act', 'Export only when asked', 'one question per reply',
                        'only after its function_call_output says so', 'read it with skill_read first, before any other tool'])
      expect(count(text, rule), rule).toBe(1);
  });

  it('ends with the index of every shipped skill and names no skill in its prose', () => {
    expect(text.endsWith(skills.map((skill) => `- ${skill.name}: ${skill.description}`).join('\n'))).toBe(true);
    for (const skill of skills) expect(prose, skill.name).not.toContain(skill.name);
  });

  it('has no skills section when the page ships no skills', () => {
    const bare = projectInstructions([]);
    expect(bare).not.toContain('# Skills');
    expect(bare).not.toContain('skill_read');
    expect(text.startsWith(bare)).toBe(true);
  });

  it('gives reasons in plain words instead of emphasis', () => {
    for (const shout of ['MUST', 'NEVER', 'ALWAYS', 'CRITICAL', 'IMPORTANT'])
      for (const source of [text, ...skills.map((skill) => skill.text)]) expect(source).not.toContain(shout);
    expect(prose).toContain('because the person asked for a judgement');
  });

  it('keeps the skill descriptions to what each covers and when to use it', () => {
    for (const skill of skills) {
      expect(skill.description, skill.name).toMatch(/\. Use (only )?when /);
      // A rule in a description is a standing rule by the back door: the
      // index is in every request whether or not the skill is ever read.
      expect(skill.description, skill.name).not.toMatch(/\b(do not|don't|never|must|always)\b/i);
    }
  });

  it('leaves the standing rules out of the skill bodies', () => {
    // What the prompt owns: how a skill is chosen, whether a request may
    // change the project, recording intent and the plan, exporting, how many
    // questions to ask. What the tools own: previewing before applying.
    const owned = [/skill_read/, /\bexport/i, /plan_set|intent_update/, /authori[sz]/i, /permission/i, /one question/i,
                   /preview (it|them|first|before)/i, /settings_preview_patch/];
    for (const skill of skills) {
      for (const pattern of owned) expect(skill.text, `${skill.name} ${pattern}`).not.toMatch(pattern);
      for (const other of skills) expect(skill.text, `${skill.name} names ${other.name}`).not.toContain(other.name);
    }
  });

  it('calls the person the person', () => {
    for (const source of [prose, ...skills.map((skill) => skill.text), ...skills.map((skill) => skill.description)])
      expect(source).not.toMatch(/\b(the user|users|someone)\b/i);
  });
});
