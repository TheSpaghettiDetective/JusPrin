import { loadSkills } from './skillParser';
export { loadSkills, parseSkill, type Skill } from './skillParser';

// Every skills/<name>/SKILL.md, bundled with the page and sent to the app
// once per connection with the skills message.
const skillFiles = import.meta.glob('./skills/*/SKILL.md', {
  query: '?raw',
  import: 'default',
  eager: true,
}) as Record<string, string>;

export const skills = loadSkills(skillFiles);
