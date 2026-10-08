import { loadSkills } from './skillParser';
export { loadSkills, parseSkill, type Skill } from './skillParser';

const skillFiles = import.meta.glob('./skills/*/SKILL.md', {
  query: '?raw',
  import: 'default',
  eager: true,
}) as Record<string, string>;

export const skills = loadSkills(skillFiles);
