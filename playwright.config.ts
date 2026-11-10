import { defineConfig, devices } from '@playwright/test';

/**
 * Browser integration tests against the production build served locally.
 * Firefox runs when its Playwright browser is installed (`npx playwright install firefox`)
 * and E2E_FIREFOX=1 is set.
 */
export default defineConfig({
  testDir: 'tests/e2e',
  timeout: 60_000,
  fullyParallel: false,
  reporter: [['list']],
  use: {
    baseURL: 'http://localhost:4173',
    viewport: { width: 1400, height: 850 },
    acceptDownloads: true,
  },
  webServer: {
    command: 'npm run build && npm run preview',
    url: 'http://localhost:4173',
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
  },
  projects: [
    { name: 'chromium', use: { ...devices['Desktop Chrome'], viewport: { width: 1400, height: 850 } } },
    ...(process.env.E2E_FIREFOX ? [{ name: 'firefox', use: { ...devices['Desktop Firefox'], viewport: { width: 1400, height: 850 } } }] : []),
  ],
});
