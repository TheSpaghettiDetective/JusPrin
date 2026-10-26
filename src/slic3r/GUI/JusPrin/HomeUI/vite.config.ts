import { defineConfig } from 'vitest/config';
import { searchForWorkspaceRoot } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { fileURLToPath } from 'node:url';

const resources = fileURLToPath(new URL('../../../../../resources', import.meta.url));
const shared = fileURLToPath(new URL('../WebShared', import.meta.url));

// The bundle must be a single self-contained file: WKWebView does not load
// file: subresources reliably (POC lesson), so everything inlines into one
// index.html packaged at resources/jusprin/home/index.html.
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  resolve: {
    alias: {
      '@resources': resources,
      '@shared': shared,
    },
  },
  server: {
    port: 5174,
    strictPort: true,
    // The CSS loads icons from resources/ and fonts from WebShared/, both outside
    // this folder, which the dev server refuses (403) unless they are allowed.
    fs: { allow: [searchForWorkspaceRoot(process.cwd()), resources, shared] },
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
