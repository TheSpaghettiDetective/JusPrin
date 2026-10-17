// A project skill is text for one kind of situation. The page writes each one
// as skills/<name>/SKILL.md: YAML frontmatter, then the body, which is the
// text the model gets. A skill with a `trigger` is added by the app to one
// request when the event it names happens; the model does not choose it. A
// skill with a `description` is listed for the model, which reads it with
// skill_read when the description fits. A skill may have both.
export interface Skill {
  name: string;
  trigger?: string;
  description?: string;
  text: string;
}

const SKILL_NAME = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const TRIGGER = /^[a-z]+(?:_[a-z]+)*$/;
const FRONTMATTER = /^---\r?\n([\s\S]*?)\r?\n---(?:\r?\n|$)/;
// The app refuses a longer text (AgentHost, Protocol::kSkills).
export const SKILL_TEXT_LIMIT = 16 * 1024;
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
    if (!['name', 'trigger', 'description'].includes(key) || fields.has(key) || !raw)
      throw new Error(`${source}: frontmatter must contain only name, trigger and description`);
    fields.set(key, scalar(raw, source));
  }

  const name = fields.get('name') ?? '';
  const trigger = fields.get('trigger');
  const description = fields.get('description');
  const directory = source.replace(/\\/g, '/').split('/').at(-2) ?? '';
  if (!SKILL_NAME.test(name) || name.length > 64 || name !== directory)
    throw new Error(`${source}: name must match its lowercase hyphenated directory`);
  if (trigger === undefined && description === undefined)
    throw new Error(`${source}: a skill needs a trigger, a description or both`);
  if (trigger !== undefined && (!TRIGGER.test(trigger) || trigger.length > 64))
    throw new Error(`${source}: trigger must be lowercase words joined by underscores`);
  if (description !== undefined && (/[\r\n]/.test(description) || encoder.encode(description).length > 300))
    throw new Error(`${source}: description must be one line of at most 300 UTF-8 bytes`);

  const body = text.slice(match[0].length).replace(/^(?:\r?\n)+/, '').trimEnd();
  if (!body) throw new Error(`${source}: skill body is empty`);
  if (encoder.encode(body).length > SKILL_TEXT_LIMIT)
    throw new Error(`${source}: skill body exceeds 16 KiB`);

  return {
    name,
    ...(trigger === undefined ? {} : { trigger }),
    ...(description === undefined ? {} : { description }),
    text: body,
  };
}

// Sorted by name, as the app expects them; at most one skill per trigger,
// since the app adds the one its event names.
export function loadSkills(files: Record<string, string>): Skill[] {
  const skills = Object.entries(files).map(([source, text]) => parseSkill(text, source));
  skills.sort((left, right) => left.name < right.name ? -1 : left.name > right.name ? 1 : 0);
  for (let index = 1; index < skills.length; index += 1)
    if (skills[index - 1].name === skills[index].name)
      throw new Error(`duplicate skill name: ${skills[index].name}`);
  const triggers = new Set<string>();
  for (const skill of skills) {
    if (skill.trigger === undefined) continue;
    if (triggers.has(skill.trigger)) throw new Error(`two skills share the trigger ${skill.trigger}`);
    triggers.add(skill.trigger);
  }
  return skills;
}
