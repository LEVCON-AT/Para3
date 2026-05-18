import { test, expect } from '@playwright/test';
import { bootstrap, capture, rmsWindows, peakRMS } from './helpers/audio';

// Sprint 7 smoke: infrastructure + E2E-TAP roundtrip works.
// 1. Page loads, audio context starts.
// 2. Cold-start (no preset clicked) produces silence (sequencer idle).
// 3. After click P1 + Play, captured audio has measurable RMS.
test.describe('Sprint 7 — infrastructure smoke', () => {
  test('bootstrap installs __para3Capture + cold-start is silent', async ({ page }) => {
    await bootstrap(page);
    // __para3Capture must be present
    const hasCapture = await page.evaluate(() => typeof (window as any).__para3Capture === 'function');
    expect(hasCapture).toBe(true);

    // Cold-start: no preset, no Play → engine should be silent.
    // Capture 0.2 s and check RMS is near zero.
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());
    await page.waitForTimeout(250);             // let the ring fill with silence
    const samples = await capture(page, Math.round(fs * 0.2));
    const rms = rmsWindows(samples, Math.round(fs * 0.020));
    const peak = peakRMS(rms);
    // Cold-start with no preset, no Play: peak RMS should be near zero.
    expect(peak).toBeLessThan(0.005);
  });

  test('click P1 + Play yields audible audio in capture', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Load P1 (HIP-HOP @ 70 BPM, 1/16 = 214 ms/step). Motion lane on VOLUME
    // starts at 12 (quiet) and swells to ~95 by step 8-10 (~1700-2150 ms).
    // We wait 1.8 s after Play to land mid-swell, then capture 1.2 s — that
    // window crosses the loud middle of the swell and yields a reliable RMS.
    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(300);             // preset apply settles
    await page.locator('#play').click();
    await page.waitForTimeout(1800);
    const samples = await capture(page, Math.round(fs * 1.2));
    await page.locator('#play').click();        // stop
    const rms = rmsWindows(samples, Math.round(fs * 0.020));
    const peak = peakRMS(rms);
    expect(peak).toBeGreaterThan(0.05);          // mid-swell audible
  });
});
