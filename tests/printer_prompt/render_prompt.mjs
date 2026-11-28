// What the printer panel's page makes of a session, for run_prompt_tests.py:
// the model's system prompt (printerInstructions.ts) and the panel's opening
// line (printerWords.ts), built from the page's own source on every run. A
// filament chat's session ("kind": "filament") gets filamentInstructions.ts
// and the opening PrinterPanel::build_runtime posts for it. The project
// chat's ("kind": "project", for tests/project_prompt) gets
// projectInstructions.ts alone: the project chat has no opening line.
// Reads a PrinterSessionPayload or FilamentSessionPayload as JSON on stdin; writes
// {"instructions": ..., "opening": ...} on stdout, and for a printer chat the
// printer's facts (printerFacts), which the app sends the model as a message of
// its own beside the system prompt (AgentHost::session_facts). With a git revision as the
// argument, the page's source is read as it was at that revision, so a prompt
// edit can be run against the one before it without touching the checkout.

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(here, '../..');
const agentUI = path.join(repository, 'src/slic3r/GUI/JusPrin/AgentUI');
const revision = process.argv[2];
// The page's own esbuild, from its npm install.
const esbuild = createRequire(path.join(agentUI, 'package.json'))('esbuild');
const atRevision = {
  name: 'at-revision',
  setup(build) {
    build.onLoad({ filter: /\.tsx?$/ }, (args) => ({
      contents: execFileSync('git', ['show', `${revision}:${path.relative(repository, args.path).split(path.sep).join('/')}`],
                             { cwd: repository, encoding: 'utf8' }),
      loader: 'ts',
    }));
  },
};
// The page's Lingui macro is a Babel transform that only the page's own build
// runs. Here it gives the English source text, which is what the page shows in
// English and what these tests read.
const linguiMacro = {
  name: 'lingui-macro-as-source',
  setup(build) {
    build.onResolve({ filter: /^@lingui\/core\/macro$/ }, () => ({ path: 'lingui-macro', namespace: 'lingui-macro' }));
    build.onLoad({ filter: /.*/, namespace: 'lingui-macro' }, () => ({
      contents: 'export const t = (strings, ...values) => typeof strings === "string" ? strings : ' +
                'strings.message ?? strings.reduce((text, part, i) => text + values[i - 1] + part);',
      loader: 'js',
    }));
  },
};
const session = JSON.parse(readFileSync(0, 'utf8'));
const filament = session.kind === 'filament';
const project = session.kind === 'project';
const bundle = await esbuild.build({
  stdin: {
    contents: project ? "export { projectInstructions } from './src/projectInstructions';" :
              filament ? "export { filamentInstructions } from './src/filamentInstructions';" :
                         // A namespace import, so a revision without printerFacts still builds.
                         "import * as prompt from './src/printerInstructions'; export const printerInstructions = prompt.printerInstructions; " +
                         "export const printerFacts = prompt.printerFacts; export { opening } from './src/printerWords';",
    resolveDir: agentUI,
    loader: 'ts',
  },
  bundle: true,
  format: 'esm',
  platform: 'node',
  write: false,
  plugins: revision ? [linguiMacro, atRevision] : [linguiMacro],
});
const page = await import('data:text/javascript;base64,' + Buffer.from(bundle.outputFiles[0].text).toString('base64'));
process.stdout.write(JSON.stringify(project ? { instructions: page.projectInstructions() } : filament ?
  { instructions: page.filamentInstructions(session),
    // PrinterPanel::build_runtime's own words, which the app posts.
    opening: `I can help with settings for ${session.shown || 'the filament'} in slot ${session.slot}. What would you like to change?` } :
  // A revision from before the facts were split out has them in the prompt.
  { instructions: page.printerInstructions(session), opening: page.opening(session),
    facts: page.printerFacts ? page.printerFacts(session) : '' }));
