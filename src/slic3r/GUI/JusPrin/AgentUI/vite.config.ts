import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { fileURLToPath } from 'node:url';

// The bundle must be a single self-contained file: WKWebView does not load
// file: subresources reliably (POC lesson), so everything inlines into one
// index.html packaged at resources/jusprin/agent/index.html.
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  resolve: {
    alias: {
      '@resources': fileURLToPath(new URL('../../../../../resources', import.meta.url)),
    },
  },
  build: {
    outDir: fileURLToPath(new URL('../../../../../resources/jusprin/agent', import.meta.url)),
    // protocol.json lives in the same directory and must survive builds.
    emptyOutDir: false,
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test/setup.ts'],
  },
});
