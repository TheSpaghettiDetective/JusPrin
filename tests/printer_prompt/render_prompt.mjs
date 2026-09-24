// What the printer panel's page makes of a session, for run_prompt_tests.py:
// the model's system prompt (printerInstructions.ts) and the panel's opening
// line (printerWords.ts), built from the page's own source on every run.
// Reads a PrinterSessionPayload as JSON on stdin; writes
// {"instructions": ..., "opening": ...} on stdout.

import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const agentUI = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../src/slic3r/GUI/JusPrin/AgentUI');
// The page's own esbuild, from its npm install.
const esbuild = createRequire(path.join(agentUI, 'package.json'))('esbuild');
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
});
const page = await import('data:text/javascript;base64,' + Buffer.from(bundle.outputFiles[0].text).toString('base64'));
const session = JSON.parse(readFileSync(0, 'utf8'));
process.stdout.write(JSON.stringify({ instructions: page.printerInstructions(session), opening: page.opening(session) }));
