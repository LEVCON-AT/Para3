import { test, expect } from '@playwright/test';
import { bootstrap, capture, rmsWindows, peakRMS, detectOnsets, specDb, bandPeakDb } from './helpers/audio';

// Sprint 8 — User-Stories aus §3 von CLAUDE_USER_SCENARIOS.md.
// Status: FREIGEGEBEN durch User. Wörtlich umgesetzt, gleiche Metriken
// wie die C++/mjs-Suite (Onset/RMS/FFT-Peak).

test.describe('Sprint 8 — Demo-Tracks P1..P5', () => {
  test('US-DEMO-P1-HIPHOP — bass groove + VOL swell @ 70 BPM', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(300);
    await page.locator('#play').click();
    // P1 = 70 BPM × 1/16 = ~214 ms/step; 16 steps = 3.43 s/bar. Capture 4 s
    // = 1.17 bars so we always span the full VOL swell regardless of phase.
    await page.waitForTimeout(2200);
    const samples = await capture(page, Math.round(fs * 4.0));
    await page.locator('#play').click();

    const rms = rmsWindows(samples, Math.round(fs * 0.020));
    // Lower minRMS=0.012 catches the quiet half of the bar (vol-lane min=12).
    const onsets = detectOnsets(rms, { winMs: 20, minRMS: 0.012 });
    expect(onsets.length).toBeGreaterThanOrEqual(5);

    // VOL swell signature, phase-independent: smooth RMS over ~250 ms to wash
    // out per-step gate edges, then check the smoothed peak/min ratio. Lane
    // goes 12→95→18 (≈8× dynamic) — the smoothed envelope must show ≥2×.
    const win = 12;   // ~240 ms half-window
    const smoothed = rms.map((_, i) => {
      let s = 0, c = 0;
      for (let j = Math.max(0, i - win); j <= Math.min(rms.length - 1, i + win); ++j) {
        s += rms[j]; c++;
      }
      return s / c;
    });
    const sPeak = Math.max(...smoothed);
    const sMin = Math.min(...smoothed);
    expect(sPeak / Math.max(sMin, 0.001)).toBeGreaterThan(2);
  });

  test('US-DEMO-P2-BERLIN — FIFTH-interval + sustained pad', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('.pslot[data-s="2"]').click();
    await page.waitForTimeout(300);
    await page.locator('#play').click();
    // 88 BPM × 1/2 div × 1/16 = ~341 ms/step. Capture 4 s = ~12 steps (per
    // spec US-DEMO-P2-BERLIN Mess: 192000 samples). Long capture spans many
    // notes + their FIFTH partners; robust under portamento smearing.
    await page.waitForTimeout(2000);
    const samples = await capture(page, Math.round(fs * 4.0));
    await page.locator('#play').click();

    // Sustained pad: RMS sollte nicht in Stille fallen.
    const peak = peakRMS(rmsWindows(samples, Math.round(fs * 0.020)));
    expect(peak).toBeGreaterThan(0.05);

    // Multi-peak tonal signature (spec: "FFT zeigt Doppel-Peak"). FIFTH voice
    // stacks each base note with its +7-semitone partner. Use band-peak power
    // (sweeps Goertzel across the band) to be robust against portamento drift
    // and inter-bin pitch positions. True noise floor at 1.5-2.5 kHz: above
    // the cut=50 lowpass, no fundamentals or low-order harmonics survive.
    const baseBand = bandPeakDb(samples, fs, 60, 125, 1.0);      // C2..A#2 (oct=-1)
    const fifthBand = bandPeakDb(samples, fs, 130, 180, 1.0);     // C3..F3 (+7 semis)
    const noiseFloor = bandPeakDb(samples, fs, 1500, 2500, 5);    // filter-attenuated
    // Base notes are loud (sequencer + filter passes fundamentals).
    expect(baseBand).toBeGreaterThan(noiseFloor + 20);
    // FIFTH-stack band carries the +7-semitone partners; significantly above noise.
    expect(fifthBand).toBeGreaterThan(noiseFloor + 10);
  });

  test('US-DEMO-P3-VKBRASS — ARP runs without Play, Cm9 audible', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('.pslot[data-s="3"]').click();
    // ARP runs independent of Play (EXT-ARP-FIX4 industry standard).
    // 4 s capture per spec — covers all 4 chord tones × Oct×2 = 8 pitches
    // multiple times for stable Goertzel measurement (single 1 s capture
    // diluted each fundamental too much across the ARP sequence).
    await page.waitForTimeout(1500);
    const samples = await capture(page, Math.round(fs * 4.0));

    // Cm9 chord with arpOct=2 spans 250-950 Hz (C4..Bb5). Use band-peak power
    // (Goertzel swept across the band) — robust against UNISON+DETUNE spread,
    // LFO cutoff sweep, and inter-bin pitch positions. True noise floor at
    // 1.5-2.5 kHz: above the cut=60+LFO-SAW filter passband, clearly quiet.
    const chordBand = bandPeakDb(samples, fs, 250, 950, 1.0);
    const noiseFloor = bandPeakDb(samples, fs, 1500, 2500, 5);
    expect(chordBand).toBeGreaterThan(noiseFloor + 15);
  });

  test('US-DEMO-P4-ACID — many onsets per bar (STEP TRIGGER 303-style)', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('.pslot[data-s="4"]').click();
    await page.waitForTimeout(300);
    await page.locator('#play').click();
    // 122 BPM × 1/16 = ~123 ms/step; 16 steps = ~1.97 s/bar. Wait 2.5 s to
    // skip the bar-1 ramp-in, then capture 3 s ≈ 1.5 bars (~21 hit-candidates)
    // for robust onset count above the bar-low-cutoff portion.
    await page.waitForTimeout(2500);
    const samples = await capture(page, Math.round(fs * 3.0));
    await page.locator('#play').click();

    const rms = rmsWindows(samples, Math.round(fs * 0.020));
    // minRMS=0.02 to catch the swept-low filter portion (cut-lane[0]=20).
    const onsets = detectOnsets(rms, { winMs: 20, minRMS: 0.02 });

    // P4: 14 gated steps × 1.5 bar coverage ≈ 21 candidates; STEP TRIGGER
    // + filter-ring + portamento creates a wide count range across capture-
    // phases (Sprint 5+8 calibration: 9..21 typical, can go higher with
    // close-spaced double-detection).
    expect(onsets.length).toBeGreaterThanOrEqual(8);
    expect(onsets.length).toBeLessThanOrEqual(50);
  });

  test('US-DEMO-P5-RNG — MOTION-REC/ARP_MODE cycles produce different spectra', async ({ page }) => {
    await bootstrap(page);
    const fs = await page.evaluate(() => (window as any).__para3CaptureSampleRate());

    await page.locator('.pslot[data-s="5"]').click();
    await page.waitForTimeout(300);
    await page.locator('#play').click();
    // 140 BPM × 1/16 = ~107 ms/step. Mode blocks of 3 steps each. Sample at
    // step 2 (UP), 5 (DN), 14 (RND) — 3 audibly different ARP modes.
    const block = async (waitMs: number) => {
      await page.waitForTimeout(waitMs);
      return await capture(page, Math.round(fs * 0.3));
    };
    const upBlk  = await block(250);
    const dnBlk  = await block(330);
    const rndBlk = await block(880);
    await page.locator('#play').click();

    const probes = [261.6, 370.0, 415.3];     // C4, F#4, G#4
    const fp = (buf: number[]) => probes.map(f => specDb(buf, fs, f));
    const upFP = fp(upBlk), dnFP = fp(dnBlk), rndFP = fp(rndBlk);
    const diff = (a: number[], b: number[]) => Math.max(...a.map((v, i) => Math.abs(v - b[i])));
    const maxBlockDiff = Math.max(diff(upFP, dnFP), diff(upFP, rndFP), diff(dnFP, rndFP));
    expect(maxBlockDiff).toBeGreaterThan(4);   // mode change audibly detectable
  });

  test('US-DEMO-DIRTY — engine edit flips dirty class, re-click resets', async ({ page }) => {
    await bootstrap(page);

    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(200);
    const initial = await page.evaluate(() =>
      document.querySelector('.pslot[data-s="1"]')?.classList.contains('dirty'));
    expect(initial).toBe(false);

    // Touch voice mode (engine state change) → dirty.
    await page.locator('#voice button').nth(2).click();
    await page.waitForTimeout(120);
    const afterEdit = await page.evaluate(() =>
      document.querySelector('.pslot[data-s="1"]')?.classList.contains('dirty'));
    expect(afterEdit).toBe(true);

    // Re-click P1 → reset.
    await page.locator('.pslot[data-s="1"]').click();
    await page.waitForTimeout(200);
    const afterReset = await page.evaluate(() =>
      document.querySelector('.pslot[data-s="1"]')?.classList.contains('dirty'));
    expect(afterReset).toBe(false);
  });

  test('US-DEMO-ISOLATION — sequential P1..P5 each isolated', async ({ page }) => {
    await bootstrap(page);

    for (let i = 1; i <= 5; i++) {
      await page.locator(`.pslot[data-s="${i}"]`).click();
      await page.waitForTimeout(250);
      const state = await page.evaluate((slot) => ({
        loaded: document.querySelector(`.pslot[data-s="${slot}"]`)?.classList.contains('loaded') || false,
        dirty:  document.querySelector(`.pslot[data-s="${slot}"]`)?.classList.contains('dirty')  || false,
        otherLoaded: [...document.querySelectorAll('.pslot')]
          .filter(b => +((b as HTMLElement).dataset.s || '0') !== slot)
          .filter(b => b.classList.contains('loaded')).length,
      }), i);
      expect(state.loaded).toBe(true);
      expect(state.dirty).toBe(false);
      expect(state.otherLoaded).toBe(0);
    }
  });
});
