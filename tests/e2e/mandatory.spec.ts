import { test, expect } from '@playwright/test';
import { bootstrap, capture, rmsWindows, peakRMS } from './helpers/audio';

// Sprint 8 — Pflicht-Stories aus §1 von CLAUDE_USER_SCENARIOS.md.
// Diese Stories adressieren die Bug-Klasse "Knopf wirkt erst nach Nach-Anfassen"
// und müssen IMMER grün sein, unabhängig von freigegebenen Demo-Stories.

test.describe('Sprint 8 — Pflicht-Stories US-COLD/ONE/PERSIST/IDEM/ORDER', () => {
  test('US-COLD — Cold-Start ohne Klick ist still', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());
    // Audio is ready, but NO preset clicked, NO Play. Engine should be silent.
    await page.waitForTimeout(400);
    const samples = await capture(page, Math.round(fs * 0.3));
    const peak = peakRMS(rmsWindows(samples, Math.round(fs * 0.020)));
    expect(peak).toBeLessThan(0.005);
  });

  test('US-ONE — eine Note wirkt sofort & isoliert', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Programmatic keyboard noteOn via debug surface (mirrors the keyboard
    // click path; emit() routes through inTransport so the dirty-indicator
    // stays clean — note events are transport, not patch state).
    await page.evaluate(() => (window as any).__para3Debug().audio.controls.noteOn(60));
    await page.waitForTimeout(300);
    const samplesOn = await capture(page, Math.round(fs * 0.25));
    const peakOn = peakRMS(rmsWindows(samplesOn, Math.round(fs * 0.020)));
    expect(peakOn).toBeGreaterThan(0.02);

    await page.evaluate(() => (window as any).__para3Debug().audio.controls.noteOff(60));
    await page.waitForTimeout(1500);        // > DECREL release tail
    const samplesOff = await capture(page, Math.round(fs * 0.2));
    const peakOff = peakRMS(rmsWindows(samplesOff, Math.round(fs * 0.020)));
    expect(peakOff).toBeLessThan(0.005);
  });

  test('US-PERSIST — Preset-Wechsel hin/zurück restored den Zustand', async ({ page }) => {
    await bootstrap(page);

    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(200);
    const v1 = await page.evaluate(() =>
      [...document.querySelectorAll('#voice button')].findIndex(b => b.classList.contains('on')));

    await page.locator('.pslot[data-s="3"]').click();
    await page.waitForTimeout(200);
    const v3 = await page.evaluate(() =>
      [...document.querySelectorAll('#voice button')].findIndex(b => b.classList.contains('on')));

    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(200);
    const v1again = await page.evaluate(() =>
      [...document.querySelectorAll('#voice button')].findIndex(b => b.classList.contains('on')));

    expect(v1).not.toBe(v3);     // P1=POLY (0) != P3=UNISON (1)
    expect(v1again).toBe(v1);    // re-load P1 = same as first P1 load
  });

  test('US-IDEM — gleiches Preset zweimal klicken ist No-Op auf State', async ({ page }) => {
    await bootstrap(page);

    await page.locator('.pslot[data-s="2"]').click();
    await page.waitForTimeout(200);
    const snap = async () => page.evaluate(() => ({
      voice: [...document.querySelectorAll('#voice button')].findIndex(b => b.classList.contains('on')),
      tdiv: [...document.querySelectorAll('#tdiv button')].find(b => b.classList.contains('on'))?.getAttribute('data-d') || null,
      lfoSync: document.getElementById('lsync')?.classList.contains('on') || false,
      stepGates: [...document.querySelectorAll('.step')].map(b => b.classList.contains('on') ? 1 : 0),
    }));
    const A = await snap();

    await page.locator('.pslot[data-s="2"]').click();
    await page.waitForTimeout(200);
    const B = await snap();

    expect(B).toEqual(A);
  });

  test('US-ORDER — Reihenfolge der Preset-Wechsel ändert das Ergebnis nicht', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Pfad A: direkt P1 laden + spielen.
    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(200);
    await page.locator('#play').click();
    await page.waitForTimeout(1700);
    const A = await capture(page, Math.round(fs * 0.5));
    await page.locator('#play').click();
    await page.waitForTimeout(400);

    // Pfad B: erst P3 laden, dann zurück auf P1, dann spielen.
    await page.locator('.pslot[data-s="3"]').click();
    await page.waitForTimeout(150);
    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(200);
    await page.locator('#play').click();
    await page.waitForTimeout(1700);
    const B = await capture(page, Math.round(fs * 0.5));
    await page.locator('#play').click();

    const peakA = peakRMS(rmsWindows(A, Math.round(fs * 0.020)));
    const peakB = peakRMS(rmsWindows(B, Math.round(fs * 0.020)));
    // Beide Pfade müssen P1's Energie liefern (innerhalb Toleranz für
    // Step-Phase-Aliasing). Verhältnis < 1.5 = "im selben Klangbereich".
    const ratio = Math.max(peakA, peakB) / Math.max(Math.min(peakA, peakB), 0.001);
    expect(ratio).toBeLessThan(1.5);
  });
});
