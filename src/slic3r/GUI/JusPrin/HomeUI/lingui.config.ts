import { defineConfig } from '@lingui/cli';
import { formatter } from '@lingui/format-po';
import languages from '../Localization/languages.json';

// Both pages share one catalog per language in ../Localization/pages: Home
// draws some of the AI page's components, and a translator sees each string
// once. Extraction runs from AgentUI (Localization/update_catalogs.py); this
// file has a twin in the other page so each page's build finds it.
export default defineConfig({
  sourceLocale: languages.source,
  locales: languages.languages.map((l) => l.code),
  catalogs: [
    {
      path: '<rootDir>/../Localization/pages/{locale}',
      include: ['<rootDir>/../AgentUI/src', '<rootDir>/../HomeUI/src'],
      exclude: ['**/node_modules/**', '**/*.test.*', '**/visual/**', '**/*.visual-cases.*', '**/test/**'],
    },
  ],
  format: formatter({ lineNumbers: false }),
  // See Trans and useLingui in AgentUI/src/i18n.tsx; vite.config.ts maps the
  // module name to that file.
  runtimeConfigModule: { Trans: ['@jusprin/i18n', 'Trans'], useLingui: ['@jusprin/i18n', 'useLingui'] },
});
