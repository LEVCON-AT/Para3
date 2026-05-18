import { defineConfig, devices } from '@playwright/test';

// PARA·3 User-Story-Gate (Playwright). Per CLAUDE_USER_SCENARIOS.md §2:
//   * Chromium, headless, workers:1 (serial), retries:0 (Flakiness = Defekt)
//   * Tests run against the live deploy at https://para3.levcon.at/ with
//     the `?e2e=1` query to install the E2E-TAP capture hook.
//   * No waitForTimeout for correctness — use sample-count helpers instead.
export default defineConfig({
  testDir: '.',
  timeout: 60_000,
  expect: { timeout: 10_000 },
  retries: 0,
  workers: 1,
  fullyParallel: false,
  reporter: [['list']],
  use: {
    baseURL: 'https://para3.levcon.at',
    headless: true,
    viewport: { width: 1280, height: 800 },
    trace: 'retain-on-failure',
    // Audio context needs a user gesture; we click TAP TO START in setup.
    launchOptions: {
      args: [
        '--autoplay-policy=no-user-gesture-required',
        '--disable-features=AudioServiceOutOfProcess',
      ],
    },
  },
  projects: [
    { name: 'chromium', use: { ...devices['Desktop Chrome'] } },
  ],
});
