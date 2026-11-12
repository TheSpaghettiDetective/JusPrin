// Keeps the app guide true to the app: each page's skill is the one built
// from it, and every on-screen label a page quotes is still in the source.
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { guideLabels, guideSkillFile, hasLabel } from './guide';
import { parseSkill } from './skillParser';

const src = __dirname;
const root = join(src, '../../../../../..');
const pages = join(root, 'docs/guide');
const areas = readdirSync(pages).filter((name) => name.endsWith('.md') && name !== 'README.md')
  .map((name) => name.slice(0, -3)).sort();
const page = (area: string) => readFileSync(join(pages, `${area}.md`), 'utf8');
const skillPath = (area: string) => join(src, 'skills', `guide-${area}`, 'SKILL.md');

function files(dir: string, wanted: RegExp): string[] {
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) return entry.name === 'node_modules' ? [] : files(path, wanted);
    return wanted.test(entry.name) && !/\.test\./.test(entry.name) ? [path] : [];
  });
}

// Everything that puts words on a screen: the native windows, the app's own
// and the ones it keeps, and the two pages.
const screens = files(join(root, 'src/slic3r/GUI'), /\.(cpp|hpp|tsx?)$/).map((path) => readFileSync(path, 'utf8')).join('\n');
const missingLabels = (text: string) => guideLabels(text).filter((label) => !hasLabel(screens, label));

describe('the app guide', () => {
  it('has a page', () => expect(areas).not.toEqual([]));

  it.each(areas)('%s: the skill the model reads is built from the page', (area) => {
    const built = guideSkillFile(area, page(area));
    if (process.env.JUSPRIN_UPDATE_GUIDE_SKILLS === '1') {
      mkdirSync(dirname(skillPath(area)), { recursive: true });
      writeFileSync(skillPath(area), built);
    }
    // Out of date: run this test with JUSPRIN_UPDATE_GUIDE_SKILLS=1.
    expect(existsSync(skillPath(area)) ? readFileSync(skillPath(area), 'utf8').replace(/\r\n/g, '\n') : '').toBe(built);
    // The limits the app holds a skill to: the description's and the text's size.
    expect(parseSkill(built, skillPath(area)).description).toBeTruthy();
  });

  it('has no guide skill without a page', () => {
    const built = readdirSync(join(src, 'skills')).filter((name) => name.startsWith('guide-')).sort();
    expect(built).toEqual(areas.map((area) => `guide-${area}`));
  });

  it.each(areas)('%s: every label it quotes is on a screen', (area) => {
    expect(missingLabels(page(area))).toEqual([]);
  });

  // tests/project_prompt/run_guide_tests.py asks the model these: a page with
  // none is a page nobody has checked the model can answer from.
  it('has questions to ask the model about every page, and none about a page that is gone', () => {
    const questions = JSON.parse(readFileSync(join(root, 'tests/project_prompt/guide_questions.json'), 'utf8')) as
      Record<string, { id: string; question: string; answer: string }[]>;
    expect(Object.keys(questions).sort()).toEqual(areas);
    for (const area of areas) expect(questions[area].length).toBeGreaterThanOrEqual(3);
  });

  // tests/shell/run_guide_checks.py runs these in the app as built and copies
  // their pictures into docs/guide/pictures. A section no run walks is a
  // section nobody has checked in the app.
  describe('against the running app', () => {
    const checks = JSON.parse(readFileSync(join(root, 'tests/shell/guide_checks.json'), 'utf8')) as
      Record<string, { covers: string[]; args: string[]; pictures?: Record<string, string> }[]>;
    const sections = (area: string) => [...page(area).matchAll(/^## (.+)$/gm)].map((match) => match[1]);

    it('has harness runs for every page, and none for a page that is gone', () => {
      expect(Object.keys(checks).sort()).toEqual(areas);
    });

    it.each(areas)('%s: a run walks every section', (area) => {
      const covered = new Set(checks[area].flatMap((run) => run.covers));
      expect(sections(area).filter((section) => !covered.has(section))).toEqual([]);
      expect([...covered].filter((section) => !sections(area).includes(section))).toEqual([]);
    });

    it.each(areas)('%s: its pictures are the ones the runs take', (area) => {
      const used = [...page(area).matchAll(/^!\[[^\]]+\]\(pictures\/([^)]+)\)$/gm)].map((match) => match[1]).sort();
      const taken = checks[area].flatMap((run) => Object.keys(run.pictures ?? {})).map((name) => `${area}/${name}`).sort();
      expect(used).toEqual(taken);
      expect(used.filter((name) => !existsSync(join(pages, 'pictures', name)))).toEqual([]);
    });
  });

  it('notices a label no screen has, and a label that is only part of another', () => {
    expect(missingLabels('Click **No such button** and then **All settings**.')).toEqual(['No such button']);
    // "Switch to classic" is on no screen by itself; "Switch to classic view…" is.
    expect(missingLabels('**Switch to classic**')).toEqual(['Switch to classic']);
  });

  it('builds a skill from a page', () => {
    const built = guideSkillFile('some-area', [
      '<!-- ai-description: Read before saying where things are. -->', '', '# Some area', '',
      '![The window](pictures/window.png)', '', '<!-- ai: I can do this myself. -->', '',
      'See [the other part](#the-other-part).', '',
    ].join('\r\n'));
    expect(built).toBe('---\nname: guide-some-area\ndescription: Read before saying where things are.\n---\n\n'
      + '# Some area\n\nI can do this myself.\n\nSee the other part.\n');
    expect(() => guideSkillFile('bad', '# No description\n')).toThrow('ai-description');
    expect(() => guideSkillFile('bad', '<!-- ai-description: Fine. -->\n# Title\n<!-- todo -->\n')).toThrow('comments');
  });
});
