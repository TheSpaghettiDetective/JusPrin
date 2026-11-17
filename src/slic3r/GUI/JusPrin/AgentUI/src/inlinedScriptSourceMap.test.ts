// @vitest-environment node
// Node: vite-plugin-singlefile brings Vite, whose esbuild refuses jsdom.
import { TraceMap, originalPositionFor } from '@jridgewell/trace-mapping';
import MagicString from 'magic-string';
import { replaceScript } from 'vite-plugin-singlefile';
import { describe, expect, it } from 'vitest';
import { mapInlinedScript } from '@shared/inlinedScriptSourceMap';

// A built script whose text vite-plugin-singlefile changes as it inlines it:
// `<` before `/script>` and `!--` becomes `\x3C`, which moves what follows.
const SOURCE = [
  'const closing = "</script>";',
  'const comment = "<!-- -->";',
  'function failsHere() { return closing + comment + missing.value; }',
].join('\n');

function built(): { code: string; map: string } {
  // The script as the bundler wrote it, one line, with its map to SOURCE.
  const script = new MagicString(SOURCE);
  script.replaceAll('\n', ' ');
  return { code: script.toString(), map: script.generateMap({ source: 'src/page.ts', hires: true, includeContent: true }).toString() };
}

function positionIn(text: string, needle: string): { line: number; column: number } {
  const at = text.indexOf(needle);
  const before = text.slice(0, at).split('\n');
  return { line: before.length, column: before[before.length - 1].length };
}

describe('the source map of the inlined script', () => {
  it("maps a position in index.html to the page's source file and line", () => {
    const { code, map } = built();
    const html = replaceScript(
      '<!doctype html>\n<html>\n<head>\n<script type="module" crossorigin src="./index-abc.js"></script>\n</head>\n<body></body>\n</html>\n',
      'index-abc.js',
      code,
    );
    expect(html).toContain('\\x3C/script>');

    const found = originalPositionFor(new TraceMap(mapInlinedScript(html, code, map, 'index-abc.js')), positionIn(html, 'missing.value'));

    expect(found.source).toBe('src/page.ts');
    expect(found.line).toBe(3);
    expect(found.column).toBe(SOURCE.split('\n')[2].indexOf('missing.value'));
  });

  it('refuses to make a map when index.html does not hold the script as inlined', () => {
    const { code, map } = built();
    expect(() => mapInlinedScript(`<script>${code}</script>`, code, map, 'index-abc.js')).toThrow(/exactly once/);
  });
});
