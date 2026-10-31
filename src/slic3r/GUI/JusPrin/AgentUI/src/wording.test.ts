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

  // The model is the app, so it reads about "the app", not a second program.
  // Only the prompt names OrcaSlicer, to say that file data using the name
  // means this app. A throw goes to Sentry and a log line to the log, so
  // neither reaches the model; Orca's own warnings are rebranded in the
  // translation catalogs (Brand/brand_catalogs.py). with_product_name's loop
  // holds the names it removes from a tooltip, so they never reach the model;
  // changing the list or the loop's body brings this check back to it.
  const removesProductNames = /for \(const char\* name : \{"Orca Slicer", "OrcaSlicer", "Orca"\}\)\s+boost::replace_all\(text, name, SLIC3R_APP_NAME\);/;
  it('never names Orca in what the app writes for the model', () => {
    const owned = ['Agent', 'Workspace', 'PrinterSetup', 'Printers', 'Mcp'].flatMap((name) => {
      const dir = join(root, 'src/slic3r/GUI/JusPrin', name);
      return readdirSync(dir).filter((file) => /\.(cpp|hpp)$/.test(file)).map((file) => join(dir, file));
    });
    const literals = owned.map((path): [string, string] => {
      const lines = readFileSync(path, 'utf8').replace(removesProductNames, '').split('\n')
        .filter((line) => !/^\s*(\/\/|#include)|\bthrow\b|BOOST_LOG/.test(line));
      return [path, (lines.join('\n').match(/"(?:[^"\\\n]|\\.)*"/g) ?? []).join('\n')];
    });
    const text = [...modelFacingText().filter(([path]) => !path.endsWith('projectInstructions.ts')), ...literals];
    const hits = text.flatMap(([path, content]) =>
      content.split('\n').filter((line) => /\bOrca(Slicer)?\b/.test(line)).map((line) => `${path}: ${line.trim().slice(0, 120)}`));
    expect(hits).toEqual([]);
  });
});
