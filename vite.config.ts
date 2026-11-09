/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import pkg from './package.json' with { type: 'json' };

export default defineConfig({
  plugins: [react()],
  // Relative base so the built app can be served from any local path.
  base: './',
  define: { __APP_VERSION__: JSON.stringify(pkg.version) },
  build: { sourcemap: true, chunkSizeWarningLimit: 1200 },
  test: {
    include: ['tests/unit/**/*.test.ts'],
    environment: 'node',
    setupFiles: ['tests/unit/setup.ts'],
  },
});
