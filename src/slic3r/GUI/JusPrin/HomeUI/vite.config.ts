import { defineConfig } from 'vitest/config';
import { searchForWorkspaceRoot } from 'vite';
import react from '@vitejs/plugin-react';
import { lingui } from '@lingui/vite-plugin';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { fileURLToPath } from 'node:url';
import { inlinedScriptSourceMap } from '../WebShared/inlinedScriptSourceMap';

const resources = fileURLToPath(new URL('../../../../../resources', import.meta.url));
const shared = fileURLToPath(new URL('../WebShared', import.meta.url));
const localization = fileURLToPath(new URL('../Localization', import.meta.url));

// The bundle must be a single self-contained file: WKWebView does not load
// file: subresources reliably (POC lesson), so everything inlines into one
// index.html packaged at resources/jusprin/home/index.html.
export default defineConfig({
  // The Lingui macro turns marked text into catalog lookups; the Lingui plugin
  // compiles the .po catalogs the page imports (../Localization/pages).
  plugins: [
    react({ babel: { plugins: ['@lingui/babel-plugin-lingui-macro'] } }),
    lingui(),
    viteSingleFile(),
    // For Sentry only (Packaging/sentry_page_maps.sh); dist/ is not shipped.
    ...inlinedScriptSourceMap(fileURLToPath(new URL('./dist/index.html.map', import.meta.url))),
  ],
  // The build Sentry files this page's source map under (WebShared/errorReports.ts).
  define: { __JUSPRIN_PAGE_BUILD__: JSON.stringify(process.env.JUSPRIN_PAGE_BUILD || 'local') },
  resolve: {
    // The first launch draws the Agent page's add-printer form, whose files
    // would otherwise find the Agent page's own copy of React.
    dedupe: ['react', 'react-dom', '@lingui/core', '@lingui/react', '@sentry/browser'],
    alias: {
      '@resources': resources,
      '@shared': shared,
      '@jusprin/i18n': fileURLToPath(new URL('../AgentUI/src/i18n.tsx', import.meta.url)),
    },
  },
  server: {
    port: 5174,
    strictPort: true,
    // The CSS loads icons from resources/ and fonts from WebShared/, both outside
    // this folder, which the dev server refuses (403) unless they are allowed.
    fs: { allow: [searchForWorkspaceRoot(process.cwd()), resources, shared, localization] },
  },
  build: {
    outDir: fileURLToPath(new URL('../../../../../resources/jusprin/home', import.meta.url)),
    // protocol.json lives in the same directory and must survive builds.
    emptyOutDir: false,
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test/setup.ts'],
  },
});
