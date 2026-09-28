// What the printer panel's page makes of a session, for run_prompt_tests.py:
// the model's system prompt (printerInstructions.ts) and the panel's opening
// line (printerWords.ts), built from the page's own source on every run.
// Reads a PrinterSessionPayload as JSON on stdin; writes
// {"instructions": ..., "opening": ...} on stdout. With a git revision as the
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
const bundle = await esbuild.build({
  stdin: {
    contents: "export { printerInstructions } from './src/printerInstructions'; export { opening } from './src/printerWords';",
    resolveDir: agentUI,
    loader: 'ts',
  },
  bundle: true,
  format: 'esm',
  platform: 'node',
  write: false,
  plugins: revision ? [atRevision] : [],
});
const page = await import('data:text/javascript;base64,' + Buffer.from(bundle.outputFiles[0].text).toString('base64'));
const session = JSON.parse(readFileSync(0, 'utf8'));
process.stdout.write(JSON.stringify({ instructions: page.printerInstructions(session), opening: page.opening(session) }));
