// The source map Sentry needs to name the page's own code in an error's
// stack (Packaging/sentry_page_maps.sh). The page's code runs from a script
// inlined into index.html (vite-plugin-singlefile), so a stack gives lines
// and columns in index.html, not in the script Vite built. This maps those
// positions to the page's source files. The map is written beside the
// build, never into the folder the app ships.
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import remapping, { type SourceMapInput } from '@jridgewell/remapping';
import MagicString from 'magic-string';
import type { Plugin } from 'vite';

// The map from positions in `html` to the sources of `code`, the script
// vite-plugin-singlefile inlined into it with `codeMap` as its map. Makes the
// plugin's edits to the script again (its replaceScript) and fails unless
// they give back the script exactly as `html` holds it.
export function mapInlinedScript(html: string, code: string, codeMap: SourceMapInput, codeFile: string): string {
  const edited = new MagicString(code);
  for (const found of code.matchAll(/"?__VITE_PRELOAD__"?/g)) {
    edited.overwrite(found.index, found.index + found[0].length, 'void 0');
  }
  for (const found of code.matchAll(/<(\/script>|!--)/g)) {
    edited.overwrite(found.index, found.index + 1, '\\x3C');
  }
  edited.trim();
  const inlined = edited.toString();
  const at = html.indexOf(inlined);
  if (at < 0 || html.indexOf(inlined, at + 1) >= 0) {
    throw new Error(`index.html does not hold ${codeFile} exactly once as vite-plugin-singlefile inlines it, so no source map can be made`);
  }
  edited.prepend(html.slice(0, at));
  edited.append(html.slice(at + inlined.length));
  const htmlMap = edited.generateMap({ source: codeFile, file: 'index.html', hires: 'boundary' });
  const map = remapping(htmlMap.toString(), (source) => (source === codeFile ? codeMap : null));
  map.file = 'index.html';
  return map.toString();
}

// Vite plugins: the first keeps the built script and its map before
// vite-plugin-singlefile inlines it, the second writes the map of the
// finished index.html to `mapFile`. List them after viteSingleFile().
export function inlinedScriptSourceMap(mapFile: string): Plugin[] {
  const scripts = new Map<string, { code: string; map: SourceMapInput }>();
  return [
    {
      name: 'jusprin:keep-script-map',
      apply: 'build',
      config: () => ({ build: { sourcemap: 'hidden' } }),
      generateBundle: {
        order: 'pre',
        handler(_options, bundle) {
          for (const output of Object.values(bundle)) {
            if (output.type !== 'chunk' || output.map == null) continue;
            scripts.set(output.fileName, { code: output.code, map: output.map as SourceMapInput });
            // The script's own map would ship with the page.
            delete bundle[`${output.fileName}.map`];
          }
        },
      },
    },
    {
      name: 'jusprin:inlined-script-map',
      apply: 'build',
      enforce: 'post',
      generateBundle(_options, bundle) {
        const page = bundle['index.html'];
        if (page?.type !== 'asset' || scripts.size !== 1) {
          this.error(`expected one index.html and one script, found ${scripts.size} script(s)`);
        }
        const [[codeFile, script]] = [...scripts];
        const map = mapInlinedScript(String(page.source), script.code, script.map, codeFile);
        mkdirSync(dirname(mapFile), { recursive: true });
        writeFileSync(mapFile, map);
      },
    },
  ];
}
