export interface Skill {
  name: string;
  description: string;
  text: string;
}

const SKILL_NAME = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const FRONTMATTER = /^---\r?\n([\s\S]*?)\r?\n---(?:\r?\n|$)/;
const encoder = new TextEncoder();

function scalar(raw: string, source: string): string {
  if (raw.startsWith('"')) {
    try {
      const value: unknown = JSON.parse(raw);
      if (typeof value === 'string') return value;
    } catch {
      // Report one stable frontmatter error below.
    }
    throw new Error(`${source}: invalid quoted frontmatter value`);
  }
  if (raw.startsWith("'")) {
    if (!raw.endsWith("'") || raw.length < 2)
      throw new Error(`${source}: invalid quoted frontmatter value`);
    return raw.slice(1, -1).replace(/''/g, "'");
  }
  if (raw.endsWith('"') || raw.endsWith("'"))
    throw new Error(`${source}: invalid quoted frontmatter value`);
  return raw;
}

export function parseSkill(text: string, source: string): Skill {
  const match = text.match(FRONTMATTER);
  if (!match) throw new Error(`${source}: SKILL.md must start with YAML frontmatter`);

  const fields = new Map<string, string>();
  for (const line of match[1].split(/\r?\n/)) {
    const separator = line.indexOf(':');
    if (separator < 1) throw new Error(`${source}: invalid frontmatter line`);
    const key = line.slice(0, separator).trim();
    const raw = line.slice(separator + 1).trim();
    if (!['name', 'description'].includes(key) || fields.has(key) || !raw)
      throw new Error(`${source}: frontmatter must contain only name and description`);
    fields.set(key, scalar(raw, source));
  }

  const name = fields.get('name') ?? '';
  const description = fields.get('description') ?? '';
  const directory = source.replace(/\\/g, '/').split('/').at(-2) ?? '';
  if (!SKILL_NAME.test(name) || name.length > 64 || name !== directory)
    throw new Error(`${source}: name must match its lowercase hyphenated directory`);
  if (!description.trim() || /[\r\n]/.test(description) || encoder.encode(description).length > 300)
    throw new Error(`${source}: description must be one non-empty line of at most 300 UTF-8 bytes`);

  const body = text.slice(match[0].length).replace(/^(?:\r?\n)+/, '');
  if (!body.trim()) throw new Error(`${source}: skill body is empty`);
  if (encoder.encode(body).length > 16 * 1024)
    throw new Error(`${source}: skill body exceeds 16 KiB`);

  return { name, description, text: body };
}

export function loadSkills(files: Record<string, string>): Skill[] {
  const skills = Object.entries(files).map(([source, text]) => parseSkill(text, source));
  skills.sort((left, right) => left.name < right.name ? -1 : left.name > right.name ? 1 : 0);
  for (let index = 1; index < skills.length; index += 1)
    if (skills[index - 1].name === skills[index].name)
      throw new Error(`duplicate skill name: ${skills[index].name}`);
  return skills;
}
