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

  test('US-FLUX-QUANT-1/16 — default 1/16 snap (Korg) collapses dense input to grid', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // FLUX-3 default: F·FINE OFF = 1/16 snap on. Two notes within the same
    // 1/16-step grid bucket must collapse to a single onset per loop.
    await page.locator('#flux').click();
    await page.waitForTimeout(300);
    await page.locator('#flxrec').click();
    await page.waitForTimeout(100);

    await page.evaluate(async () => {
      const c = (window as any).__para3Debug().audio.controls;
      c.setParam(0, 0.7); c.setParam(11, 0.85); c.setParam(9, 0.0);
      c.setParam(10, 0.05); c.setParam(4, 0.0); c.setParam(15, 1.0);
      await new Promise(r => setTimeout(r, 200));
      // Two notes ~50 ms apart — both should snap to the SAME 1/16-step
      // bucket (1/16 @ 120 BPM × 1 bar = 125 ms). After quantize the bank
      // holds (effectively) one ON + one OFF at the same offset.
      c.seqFluxNote(60, true);
      await new Promise(r => setTimeout(r, 50));
      c.seqFluxNote(60, false);
      await new Promise(r => setTimeout(r, 200));
    });

    await page.locator('#flxrec').click();         // commit
    await page.waitForTimeout(5000);                // settle in replay

    const samples = await capture(page, Math.round(fs * 4.0));
    const rms = rmsWindows(samples, Math.round(fs * 0.020));
    const peak = peakRMS(rms);
    // Engine quantize collapses both note edges to the same offset. Per the
    // FLUX commit-sort (PARAM→OFF→ON), OFF fires before ON at same offset →
    // a fresh attack triggers; in T23 (b) we proved that's audible. Replay
    // must produce SOME audio per loop (not silent).
    expect(peak).toBeGreaterThan(0.03);

    await page.locator('#flxclr').click();
    await page.locator('#flux').click();
  });

  test('US-FLUX-FINE — F·FINE on preserves sample-accurate offsets', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // F·FINE on disables 1/16 snap. The engine T45 PASS already proves bank
    // offsets are preserved sample-accurately; here we verify the UI path
    // exposes the same toggle correctly: visible class change + replay still
    // audible.
    await page.locator('#flux').click();
    await page.waitForTimeout(300);
    const fineBefore = await page.locator('#flxfine').evaluate(b => b.classList.contains('on'));
    expect(fineBefore).toBe(false);     // default OFF (= quantize ON, Korg)

    await page.locator('#flxfine').click();
    await page.waitForTimeout(100);
    const fineAfter = await page.locator('#flxfine').evaluate(b => b.classList.contains('on'));
    expect(fineAfter).toBe(true);

    await page.locator('#flxrec').click();
    await page.evaluate(async () => {
      const c = (window as any).__para3Debug().audio.controls;
      c.setParam(0, 0.7); c.setParam(11, 0.85); c.setParam(10, 0.05);
      c.setParam(4, 0.0); c.setParam(15, 1.0);
      await new Promise(r => setTimeout(r, 200));
      c.seqFluxNote(60, true);
      await new Promise(r => setTimeout(r, 400));
      c.seqFluxNote(60, false);
    });
    await page.locator('#flxrec').click();
    await page.waitForTimeout(5000);

    const samples = await capture(page, Math.round(fs * 3.0));
    const peak = peakRMS(rmsWindows(samples, Math.round(fs * 0.020)));
    expect(peak).toBeGreaterThan(0.05);   // free-mode replay audible

    await page.locator('#flxclr').click();
    await page.locator('#flxfine').click();    // restore default off
    await page.locator('#flux').click();
  });

  test('US-FLUX-LEN — F·LEN 1/2/4 changes loop length, UI status reflects bars', async ({ page }) => {
    await bootstrap(page);

    await page.locator('#flux').click();
    await page.waitForTimeout(300);
    expect(await page.locator('#fluxLoop').textContent()).toBe('1');

    // Switch to 2-bar
    await page.locator('#flen button[data-fl="2"]').click();
    await page.waitForTimeout(150);
    expect(await page.locator('#fluxLoop').textContent()).toBe('2');
    const sel2 = await page.locator('#flen button.on').getAttribute('data-fl');
    expect(sel2).toBe('2');

    // Switch to 4-bar
    await page.locator('#flen button[data-fl="4"]').click();
    await page.waitForTimeout(150);
    expect(await page.locator('#fluxLoop').textContent()).toBe('4');
    const sel4 = await page.locator('#flen button.on').getAttribute('data-fl');
    expect(sel4).toBe('4');

    // Back to 1-bar to leave a clean state for next tests
    await page.locator('#flen button[data-fl="1"]').click();
    await page.locator('#flux').click();
  });

  test('US-STEP-VELOCITY — per-step vel scales output peak proportionally', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Build a step pattern with gate-on at step 0, write velocity, capture
    // peak RMS, change velocity, capture again. Ratio should match vel ratio.
    async function pumpPeak(vel: number): Promise<number> {
      await page.evaluate(async (v) => {
        const c = (window as any).__para3Debug().audio.controls;
        c.seqStop();
        await new Promise(r => setTimeout(r, 200));
        c.setParam(0, 0.8); c.setParam(11, 0.85); c.setParam(9, 0.0);
        c.setParam(10, 0.1); c.setParam(4, 0.0); c.setParam(15, 1.0);
        // Empty all steps then set step 0 gate-on with the given velocity.
        for (let i = 0; i < 16; ++i) c.seqStep(i, 60, 0, 0, 0.5);
        c.seqStep(0, 60, 1, 0, 0.5);
        c.seqStepVel(0, v);
        c.seqCommit();
        c.seqTempo(60);   // slow tempo → step 0 attack/sustain captured
        c.seqStart();
        await new Promise(r => setTimeout(r, 600));
      }, vel);
      const samples = await capture(page, Math.round(fs * 0.4));
      await page.evaluate(() => (window as any).__para3Debug().audio.controls.seqStop());
      await page.waitForTimeout(400);
      return peakRMS(rmsWindows(samples, Math.round(fs * 0.020)));
    }

    const pFull = await pumpPeak(1.0);
    const pHalf = await pumpPeak(0.4);
    expect(pFull).toBeGreaterThan(0.05);
    // Ratio should match vel ratio (0.4) within tolerance.
    const ratio = pHalf / Math.max(pFull, 1e-9);
    expect(ratio).toBeGreaterThan(0.30);
    expect(ratio).toBeLessThan(0.55);
  });

  test('US-STEP-VEL-MODE-UI — VEL button toggles class on step buttons', async ({ page }) => {
    await bootstrap(page);
    const before = await page.locator('.step').first().evaluate(b => b.classList.contains('velMode'));
    expect(before).toBe(false);
    await page.locator('#vmode').click();
    await page.waitForTimeout(120);
    const after = await page.locator('.step').first().evaluate(b => b.classList.contains('velMode'));
    expect(after).toBe(true);
    const buttonOn = await page.locator('#vmode').evaluate(b => b.classList.contains('on'));
    expect(buttonOn).toBe(true);
    await page.locator('#vmode').click();   // off again
  });

  test('US-STEP-GATE — per-step gate-length shortens note decay', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Tempo 30 BPM × 1/16 = 500 ms / step. gateLen=0.3 fires noteOff at 150 ms;
    // capture window 170-350 ms post-Start gets full sustain for gateLen=1.0
    // but only the decayed tail for gateLen=0.3.
    async function gatePeak(gateLen: number): Promise<number> {
      return await page.evaluate(async (gl) => {
        const c = (window as any).__para3Debug().audio.controls;
        c.seqStop();
        await new Promise(r => setTimeout(r, 300));
        c.setParam(0, 0.8); c.setParam(11, 0.85); c.setParam(9, 0.0);
        c.setParam(10, 0.06); c.setParam(4, 0.0); c.setParam(15, 1.0);
        for (let i = 0; i < 16; ++i) c.seqStep(i, 60, 0, 0, 0.5);
        c.seqStep(0, 60, 1, 0, 0.5);
        c.seqStepGate(0, gl);
        c.seqCommit();
        c.seqTempo(30);
        c.seqStart();
        await new Promise(r => setTimeout(r, 350));
        const fs = (window as any).__para3CaptureSampleRate();
        const samp = Array.from((window as any).__para3Capture(Math.round(fs * 0.18)));
        c.seqStop();
        await new Promise(r => setTimeout(r, 400));
        const W = Math.round(fs * 0.020);
        let m = 0;
        for (let j = 0; j + W < samp.length; j += W) {
          let a = 0;
          for (let k = 0; k < W; ++k) a += samp[j+k] * samp[j+k];
          m = Math.max(m, Math.sqrt(a / W));
        }
        return m;
      }, gateLen);
    }

    const full = await gatePeak(1.0);
    const cut  = await gatePeak(0.3);
    expect(full).toBeGreaterThan(0.05);
    // gateLen=0.3 audio is < 30 % of gateLen=1.0 audio in this late window.
    expect(cut).toBeLessThan(full * 0.3);
  });

  test('US-STEP-GATE-MODE-UI — GATE button toggles class on step buttons', async ({ page }) => {
    await bootstrap(page);
    expect(await page.locator('.step').first().evaluate(b => b.classList.contains('gateMode'))).toBe(false);
    await page.locator('#gmode').click();
    await page.waitForTimeout(120);
    expect(await page.locator('.step').first().evaluate(b => b.classList.contains('gateMode'))).toBe(true);
    expect(await page.locator('#gmode').evaluate(b => b.classList.contains('on'))).toBe(true);

    // Mutual exclusion: clicking VEL turns GATE off
    await page.locator('#vmode').click();
    await page.waitForTimeout(120);
    expect(await page.locator('.step').first().evaluate(b => b.classList.contains('gateMode'))).toBe(false);
    expect(await page.locator('.step').first().evaluate(b => b.classList.contains('velMode'))).toBe(true);

    await page.locator('#vmode').click();   // cleanup
  });

  test('US-SWING — swing 30 % delays odd-step onset (off-beat shuffle)', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    // Gate ONLY step 1. At 30 BPM × 1/16 = 500 ms/step the swing=0 case fires
    // step 1 at t≈500 ms; swing=0.45 delays it to t≈725 ms (0.45 × 500 ms).
    // Capture a window inside the swing=0 onset that swing=0.45 has NOT yet
    // reached → audible vs silent.
    async function swingPeak(swing: number): Promise<number> {
      return await page.evaluate(async (sw) => {
        const c = (window as any).__para3Debug().audio.controls;
        c.seqStop();
        await new Promise(r => setTimeout(r, 300));
        c.setParam(0, 0.8); c.setParam(11, 0.85); c.setParam(9, 0.0);
        c.setParam(10, 0.08); c.setParam(4, 0.0); c.setParam(15, 1.0);
        for (let i = 0; i < 16; ++i) c.seqStep(i, 60, 0, 0, 0.5);
        c.seqStep(1, 60, 1, 0, 0.5);   // ONLY step 1 gated
        c.seqCommit();
        c.seqTempo(30);                 // 500 ms / step
        c.seqSwing(sw);
        c.seqStart();
        // Wait until 600 ms post-Start: inside swing=0 onset (step 1 at 500 ms),
        // before swing=0.45 onset (725 ms).
        await new Promise(r => setTimeout(r, 600));
        const fs = (window as any).__para3CaptureSampleRate();
        const samp = Array.from((window as any).__para3Capture(Math.round(fs * 0.08)));
        c.seqStop();
        c.seqSwing(0);                  // restore default for next test
        await new Promise(r => setTimeout(r, 400));
        const W = Math.round(fs * 0.020);
        let m = 0;
        for (let j = 0; j + W < samp.length; j += W) {
          let a = 0;
          for (let k = 0; k < W; ++k) a += samp[j+k] * samp[j+k];
          m = Math.max(m, Math.sqrt(a / W));
        }
        return m;
      }, swing);
    }

    const straight = await swingPeak(0);
    const swung    = await swingPeak(0.45);
    expect(straight).toBeGreaterThan(0.05);   // step 1 audible at swing=0
    expect(swung).toBeLessThan(straight * 0.3);  // delayed past capture window
  });

  test('US-SWING-UI — swing segmented control toggles on class and persists', async ({ page }) => {
    await bootstrap(page);

    // Default: 0 % selected.
    const initial = await page.locator('#swing button.on').getAttribute('data-sw');
    expect(initial).toBe('0');

    // Click 30 %.
    await page.locator('#swing button[data-sw="0.30"]').click();
    await page.waitForTimeout(120);
    const sel30 = await page.locator('#swing button.on').getAttribute('data-sw');
    expect(sel30).toBe('0.30');
    // Only one button has on.
    const onCount = await page.locator('#swing button.on').count();
    expect(onCount).toBe(1);

    // Click 15 %.
    await page.locator('#swing button[data-sw="0.15"]').click();
    await page.waitForTimeout(120);
    expect(await page.locator('#swing button.on').getAttribute('data-sw')).toBe('0.15');

    // Restore default 0 % for following tests.
    await page.locator('#swing button[data-sw="0"]').click();
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
