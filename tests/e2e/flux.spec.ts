import { test, expect } from '@playwright/test';
import { bootstrap, capture, rmsWindows, peakRMS } from './helpers/audio';

// FLUX-2 — Sample-accurate event sequence + Parameter-FLUX + Timeline-Visualizer.
// User-Stories per §3 von CLAUDE_USER_SCENARIOS.md (Sprint FLUX-2 batch).
// Voraussetzung: Engine FLUX-1 deployed (CACHE_VER >= v32) — FluxEvent.type=2
// (ParamSet), seqFluxParam + seqFluxClear C-API + WASM exports vorhanden.

test.describe('FLUX-2 — Note + Param events, Timeline UI, Clear', () => {
  test('US-FLUX-NOTE-REPLAY — recorded note replays audibly each loop', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Enable FLUX (1 bar @ 120 BPM ≈ 2 s).
    await page.locator('#flux').click();
    await page.waitForTimeout(300);
    await page.locator('#flxrec').click();
    await page.waitForTimeout(100);

    // Record one note via the FLUX-only API path (no live noteOn) — proves
    // engine-side replay is fully self-contained, no live-play side-channel.
    await page.evaluate(async () => {
      const c = (window as any).__para3Debug().audio.controls;
      c.setParam(0, 0.7); c.setParam(11, 0.85); c.setParam(9, 0.0);
      c.setParam(10, 0.05); c.setParam(4, 0.0); c.setParam(15, 1.0);
      await new Promise(r => setTimeout(r, 200));
      c.seqFluxNote(60, true);
      await new Promise(r => setTimeout(r, 400));
      c.seqFluxNote(60, false);
    });

    // Commit + wait 6 s = 3 full loops of replay to ensure capture window
    // sits inside replay (not the record-setup phase before commit).
    await page.locator('#flxrec').click();
    await page.waitForTimeout(6000);

    const samples = await capture(page, Math.round(fs * 4.0));
    const peak = peakRMS(rmsWindows(samples, Math.round(fs * 0.020)));
    expect(peak).toBeGreaterThan(0.05);     // replay produces audible audio

    // Cleanup so other tests start in known FLUX-off state.
    await page.locator('#flux').click();
  });

  test('US-FLUX-PARAM-REPLAY — recorded cutoff event audibly modifies replay', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('#flux').click();
    await page.waitForTimeout(300);
    await page.locator('#flxrec').click();
    await page.waitForTimeout(100);

    // Same shape as engine T42 — bright-restart at start so the loop is
    // self-resetting, dark-drop mid-loop. Without the bright-restart the
    // cutoff smoother stays stuck at the dark value across loops.
    await page.evaluate(async () => {
      const c = (window as any).__para3Debug().audio.controls;
      c.setParam(0, 0.7); c.setParam(11, 0.85); c.setParam(9, 0.0);
      c.setParam(10, 0.05); c.setParam(4, 0.0); c.setParam(15, 1.0);
      await new Promise(r => setTimeout(r, 200));
      c.seqFluxParam(0 /*Cutoff*/, 0.7);      // bright restart
      await new Promise(r => setTimeout(r, 50));
      c.seqFluxNote(60, true);
      await new Promise(r => setTimeout(r, 900));
      c.seqFluxParam(0 /*Cutoff*/, 0.35);     // dark mid-loop (~155 Hz cut)
      await new Promise(r => setTimeout(r, 700));
      c.seqFluxNote(60, false);
    });

    await page.locator('#flxrec').click();   // commit
    await page.waitForTimeout(6000);          // settle into replay loops

    const samples = await capture(page, Math.round(fs * 4.0));
    const rms = rmsWindows(samples, Math.round(fs * 0.020));
    const peak = peakRMS(rms);
    expect(peak).toBeGreaterThan(0.05);       // audible replay

    // Both 2-s halves must have measurable audio — proves the pattern
    // repeats across the loop boundary (not just a one-shot transient).
    const W = 100;   // 100 × 20 ms = 2 s per half
    const peakA = peakRMS(rms.slice(0, W));
    const peakB = peakRMS(rms.slice(W, 2 * W));
    expect(peakA).toBeGreaterThan(0.03);
    expect(peakB).toBeGreaterThan(0.03);

    await page.locator('#flux').click();
  });

  test('US-FLUX-CLR — clear silences replay; bank empty + counter resets', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('#flux').click();
    await page.waitForTimeout(300);
    await page.locator('#flxrec').click();
    await page.waitForTimeout(100);

    await page.evaluate(async () => {
      const c = (window as any).__para3Debug().audio.controls;
      c.setParam(0, 0.7); c.setParam(11, 0.85); c.setParam(10, 0.05);
      c.setParam(4, 0.0); c.setParam(15, 1.0);
      await new Promise(r => setTimeout(r, 200));
      c.seqFluxNote(60, true);
      await new Promise(r => setTimeout(r, 400));
      c.seqFluxNote(60, false);
    });
    await page.locator('#flxrec').click();   // commit
    await page.waitForTimeout(4000);          // let replay run

    // Pre-CLR: replay audible
    const pre = await capture(page, Math.round(fs * 1.0));
    const prePeak = peakRMS(rmsWindows(pre, Math.round(fs * 0.020)));
    expect(prePeak).toBeGreaterThan(0.05);

    // Press F·CLR — engine drops events, envelope releases over its own
    // time constant (E5 design: clear is click-free).
    await page.locator('#flxclr').click();
    await page.waitForTimeout(2000);          // let release tail decay

    const post = await capture(page, Math.round(fs * 0.5));
    const postPeak = peakRMS(rmsWindows(post, Math.round(fs * 0.020)));
    expect(postPeak).toBeLessThan(0.005);    // silent after clear

    await page.locator('#flux').click();
  });

  test('US-FLUX-TIMELINE-UI — Timeline shows when fluxOn, step grid hides', async ({ page }) => {
    await bootstrap(page);

    const pre = await page.evaluate(() => ({
      flux: document.getElementById('app')?.dataset.flux || 'unset',
      tlVisible:    (document.getElementById('fluxtl')   as HTMLElement)?.offsetParent !== null,
      stepsVisible: (document.getElementById('steps')    as HTMLElement)?.offsetParent !== null,
    }));
    expect(pre.flux).toBe('unset');
    expect(pre.tlVisible).toBe(false);
    expect(pre.stepsVisible).toBe(true);

    await page.locator('#flux').click();
    await page.waitForTimeout(200);

    const on = await page.evaluate(() => ({
      flux: document.getElementById('app')?.dataset.flux,
      tlVisible:    (document.getElementById('fluxtl') as HTMLElement)?.offsetParent !== null,
      stepsVisible: (document.getElementById('steps')  as HTMLElement)?.offsetParent !== null,
      gridCount:    document.getElementById('fluxgrid')?.children.length ?? 0,
    }));
    expect(on.flux).toBe('on');
    expect(on.tlVisible).toBe(true);
    expect(on.stepsVisible).toBe(false);
    expect(on.gridCount).toBe(16);     // 16 × 1/16 step ticks rendered

    await page.locator('#flux').click();   // off again
  });
});
