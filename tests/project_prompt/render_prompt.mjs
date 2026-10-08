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

function joinedCppStrings(fragment) {
  return [...fragment.matchAll(/"(?:\\.|[^"\\])*"/g)].map((match) => JSON.parse(match[0])).join('');
}

function cppStringInitializer(contents, symbol) {
  const start = contents.indexOf(symbol);
  if (start < 0) throw new Error(`Could not locate ${symbol}`);
  const match = contents.slice(start).match(/^[^=]*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);/);
  if (!match) throw new Error(`Could not parse ${symbol}`);
  return joinedCppStrings(match[1]);
}

function nativeCorePrompt() {
  const adapter = source(path.join(repository, 'src/slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.cpp'));
  const guidanceHeader = source(path.join(repository, 'src/slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp'));
  const coreStart = adapter.indexOf('"You are JusPrin');
  const coreEnd = adapter.indexOf('std::string(kPrintJourneyGuidance)', coreStart);
  if (coreStart < 0 || coreEnd < 0)
    throw new Error('Could not locate the production project prompt');
  const core = joinedCppStrings(adapter.slice(coreStart, coreEnd));
  const guidance = cppStringInitializer(guidanceHeader, 'kPrintJourneyGuidance');
  if (!guidance.includes('For a print request:') || !guidance.includes('export_file'))
    throw new Error('Production print guidance was not rendered completely');
  return core + guidance;
}

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
const index = skills.map((skill) => `- ${skill.name}: ${skill.description}`).join('\n');
const coreInstructions = nativeCorePrompt();
const skillIndex = "When the user's request matches a listed skill, your first action must be skill_read for exactly one skill, before any project tool; " +
  'do not inspect the workspace first to decide and do not merely say you read it. A specific setup, support, slice-review, or failed-print problem ' +
  'outranks prepare-print even when the user also says to set it up or get it ready. Read another skill only when the first skill directs you to it. ' +
  'Answer general questions directly without reading a skill. ' +
  `Skills you can read with skill_read, by name:\n${index}\n`;
process.stdout.write(JSON.stringify({
  coreInstructions,
  skillIndex,
  instructions: `${coreInstructions}\n\n${skillIndex}`,
  skills,
}));
