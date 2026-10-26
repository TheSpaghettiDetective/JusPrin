// Guards words the model reads. A file or object is "opened", "imported",
// "loaded" or "added"; "arrived" is invented jargon for it (AGENTS.md, Code
// Style). The model-facing text never needs "arrive" in its ordinary sense
// either, so any form of it fails here.
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';

const src = __dirname;
const root = join(src, '../../../../../..');

function modelFacingText(): [string, string][] {
  const skillsDir = join(src, 'skills');
  const skills = readdirSync(skillsDir).map((name) => join(skillsDir, name, 'SKILL.md'));
  const instructions = readdirSync(src)
    .filter((name) => /(Instructions|Words)\.ts$/.test(name))
    .map((name) => join(src, name));
  const files: [string, string][] = [...skills, ...instructions].map((path) => [path, readFileSync(path, 'utf8')]);
  // Every tool description, in-app and MCP, as string literals of the registry.
  const registry = readFileSync(join(root, 'src/slic3r/GUI/JusPrin/Agent/ToolRegistry.cpp'), 'utf8');
  const literals = registry.match(/"(?:[^"\\\n]|\\.)*"/g) ?? [];
  files.push(['ToolRegistry.cpp string literals', literals.join('\n')]);
  files.push(['tests/project_prompt/project_tools.json', readFileSync(join(root, 'tests/project_prompt/project_tools.json'), 'utf8')]);
  return files;
}

describe('model-facing wording', () => {
  it('never says a file or object "arrived"', () => {
    const hits = modelFacingText().flatMap(([path, text]) =>
      text.split('\n').filter((line) => /arriv/i.test(line)).map((line) => `${path}: ${line.trim().slice(0, 120)}`));
    expect(hits).toEqual([]);
  });
});
