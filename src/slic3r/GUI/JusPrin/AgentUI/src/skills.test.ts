import { describe, expect, it } from 'vitest';
import { loadSkills, parseSkill, skills } from './skills';

describe('project skills', () => {
  it('loads every canonical SKILL.md in deterministic order', () => {
    expect(skills.map((skill) => skill.name)).toEqual([
      'choose-support-strategy',
      'diagnose-print-failure',
      'prepare-print',
      'review-slice',
      'select-print-setup',
    ]);
    expect(skills.every((skill) => skill.text.startsWith(`# `))).toBe(true);
    expect(skills.every((skill) => !skill.text.startsWith('---'))).toBe(true);
  });

  it('rejects malformed metadata, mismatched directories, and duplicate names', () => {
    expect(() => parseSkill('not frontmatter', './skills/bad/SKILL.md')).toThrow('frontmatter');
    expect(() => parseSkill('---\nname: wrong\ndescription: Useful.\n---\n', './skills/right/SKILL.md')).toThrow('directory');
    const text = '---\nname: same\ndescription: Useful.\n---\n# Same\n';
    expect(() => loadSkills({ './skills/same/SKILL.md': text, './other/same/SKILL.md': text })).toThrow('duplicate');
  });

  it('rejects unsupported frontmatter and oversized content', () => {
    expect(() => parseSkill('---\nname: bad\ndescription: Useful.\nextra: no\n---\n', './skills/bad/SKILL.md')).toThrow('only name and description');
    const oversized = `---\nname: huge\ndescription: Useful.\n---\n${'x'.repeat(16 * 1024 + 1)}`;
    expect(() => parseSkill(oversized, './skills/huge/SKILL.md')).toThrow('16 KiB');
    const longDescription = `---\nname: long\ndescription: ${'é'.repeat(151)}\n---\n# Long\n`;
    expect(() => parseSkill(longDescription, './skills/long/SKILL.md')).toThrow('300 UTF-8 bytes');
  });

  it('decodes quoted YAML scalars without putting quotes in the skill index', () => {
    const doubleQuoted = parseSkill('---\nname: quoted\ndescription: "Useful: when needed"\n---\n# Quoted\n',
      './skills/quoted/SKILL.md');
    expect(doubleQuoted.description).toBe('Useful: when needed');
    const singleQuoted = parseSkill("---\nname: quoted\ndescription: 'It''s useful'\n---\n# Quoted\n",
      './skills/quoted/SKILL.md');
    expect(singleQuoted.description).toBe("It's useful");
  });
});
