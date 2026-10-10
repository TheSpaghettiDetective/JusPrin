#!/usr/bin/env node

import { createRequire } from 'node:module';
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(here, '../..');
const agentUI = path.join(repository, 'src/slic3r/GUI/JusPrin/AgentUI');
const revision = process.argv[2];
const relative = (file) => path.relative(repository, file).split(path.sep).join('/');
const source = (file) => revision
  ? execFileSync('git', ['show', `${revision}:${relative(file)}`], { cwd: repository, encoding: 'utf8' })
  : readFileSync(file, 'utf8');

const skillRoot = path.join(agentUI, 'src/skills');
const skillNames = revision
  ? execFileSync('git', ['ls-tree', '--name-only', `${revision}:${relative(skillRoot)}`],
                 { cwd: repository, encoding: 'utf8' }).trim().split(/\r?\n/).filter(Boolean)
  : readdirSync(skillRoot, { withFileTypes: true }).filter((entry) => entry.isDirectory()).map((entry) => entry.name);
const imports = skillNames.sort().map((name, index) =>
  `import skill${index} from './src/skills/${name}/SKILL.md';`).join('\n');
const entries = skillNames.map((name, index) =>
  `${JSON.stringify(`./skills/${name}/SKILL.md`)}: skill${index}`).join(', ');
const esbuild = createRequire(path.join(agentUI, 'package.json'))('esbuild');
const loader = {
  name: 'skill-markdown',
  setup(build) {
    build.onLoad({ filter: /\.md$/ }, (args) => ({ contents: source(args.path), loader: 'text' }));
    if (revision) build.onLoad({ filter: /\.ts$/ }, (args) => ({ contents: source(args.path), loader: 'ts' }));
  },
};
const bundle = await esbuild.build({
  stdin: {
    contents: `${imports}\nimport { loadSkills } from './src/skillParser';\n` +
      `export { projectInstructions } from './src/projectInstructions';\n` +
      `export const renderedSkills = loadSkills({${entries}});`,
    resolveDir: agentUI,
    loader: 'ts',
  },
  bundle: true,
  format: 'esm',
  platform: 'node',
  write: false,
  plugins: [loader],
});
const page = await import('data:text/javascript;base64,' + Buffer.from(bundle.outputFiles[0].text).toString('base64'));
const skills = page.renderedSkills;
// The prompt is the page's own function, so what is measured here is what the
// app sends: no copy of the words lives in this script.
process.stdout.write(JSON.stringify({
  coreInstructions: page.projectInstructions([]),
  instructions: page.projectInstructions(skills),
  skills,
}));
