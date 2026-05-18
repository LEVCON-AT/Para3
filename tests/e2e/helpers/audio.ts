// Shared audio helpers for PARA·3 E2E tests.
// Same metric semantics as the C++/mjs suite: RMS in samples, simple onset
// detection on RMS-derivative, Goertzel-based spectral peak at exact freq.
import type { Page } from '@playwright/test';

// ---- Page-side bootstrap -------------------------------------------------
//
// Loads the app with the E2E-TAP enabled, clicks TAP TO START, and waits
// until window.__para3Debug reports ready. After this returns, the page is
// in a state where __para3Capture(N) yields valid samples.
export async function bootstrap(page: Page, q = ''): Promise<void> {
  const url = `/?e2e=1${q ? '&' + q : ''}`;
  await page.goto(url);
  // Clear any stale SW + caches so we get fresh assets — the E2E hook is
  // gated, so old caches without it must be evicted.
  await page.evaluate(async () => {
    if ('serviceWorker' in navigator) {
      const regs = await navigator.serviceWorker.getRegistrations();
      for (const r of regs) await r.unregister();
    }
    if (typeof caches !== 'undefined') {
      const keys = await caches.keys();
      for (const k of keys) await caches.delete(k);
    }
  });
  await page.goto(url);
  await page.getByRole('button', { name: /TAP TO START/ }).click();
  await page.waitForFunction(() => {
    const d = (window as any).__para3Debug;
    return typeof d === 'function'
        && d().ready === true
        && typeof (window as any).__para3Capture === 'function';
  }, null, { timeout: 15_000 });
}

// ---- Sample-driven wait --------------------------------------------------
//
// Wait until N samples have been written to the capture ring after the
// current cursor position. Avoids wall-clock waits for correctness.
export async function waitSamples(page: Page, nSamples: number): Promise<void> {
  const cursor0 = await page.evaluate(() => (window as any).__para3CaptureCursor());
  const cap = await page.evaluate(() => (window as any).__para3Capture(1).length); // sanity 1 sample
  void cap;
  await page.waitForFunction((c0, n) => {
    const c = (window as any).__para3CaptureCursor();
    // Account for ring wrap: compute forward delta modulo capacity.
    const total = (window as any).__para3CaptureSampleRate() * 8; // capacity
    return ((c - c0 + total) % total) >= n;
  }, [cursor0, nSamples], { timeout: 60_000 });
}

// ---- Capture + metrics ---------------------------------------------------
//
// Pull N samples from the ring. Returns plain array for cross-context
// transport (Playwright serialises return values).
export async function capture(page: Page, nSamples: number): Promise<number[]> {
  return await page.evaluate((n) => {
    const buf = (window as any).__para3Capture(n);
    return Array.from(buf);
  }, nSamples);
}

// RMS in fixed-size windows. Returns the per-window RMS values.
export function rmsWindows(samples: number[], winSamp: number): number[] {
  const nWin = Math.floor(samples.length / winSamp);
  const out = new Array(nWin);
  for (let i = 0; i < nWin; ++i) {
    let s = 0; const o = i * winSamp;
    for (let j = 0; j < winSamp; ++j) s += samples[o + j] * samples[o + j];
    out[i] = Math.sqrt(s / winSamp);
  }
  return out;
}

// Goertzel power at exact frequency f (Hz). Returns linear power; convert
// to dB with 10*log10(p) if needed. N>=fs/f for stable measurement.
export function goertzelPower(samples: number[], fs: number, f: number): number {
  const k = f / fs;
  const omega = 2 * Math.PI * k;
  const cosw = Math.cos(omega);
  const coeff = 2 * cosw;
  let s1 = 0, s2 = 0;
  for (let i = 0; i < samples.length; ++i) {
    const s = samples[i] + coeff * s1 - s2;
    s2 = s1; s1 = s;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Spectral peak in dB at frequency f, computed via Goertzel. Returns -Infinity
// for empty/silent input.
export function specDb(samples: number[], fs: number, f: number): number {
  if (samples.length === 0) return -Infinity;
  const p = goertzelPower(samples, fs, f);
  if (p <= 0) return -Infinity;
  // Normalise to Goertzel "amplitude squared" / N for power density.
  const a = Math.sqrt(p) / samples.length;
  return 20 * Math.log10(Math.max(a, 1e-9));
}

// Simple onset detector on RMS curve. An onset is a local maximum exceeding
// (prev + rise) and minRMS. Returns array of {idx, t_ms, rms}.
export function detectOnsets(rms: number[], opts: {
  winMs: number;
  rise?: number;
  minRMS?: number;
} = { winMs: 20 }): Array<{ idx: number; t_ms: number; rms: number }> {
  const rise   = opts.rise   ?? 0.015;
  const minRMS = opts.minRMS ?? 0.02;
  const out: Array<{ idx: number; t_ms: number; rms: number }> = [];
  for (let i = 2; i < rms.length - 1; ++i) {
    if (rms[i] > minRMS && rms[i] > rms[i - 1] + rise && rms[i] >= rms[i + 1]) {
      out.push({ idx: i, t_ms: Math.round(i * opts.winMs), rms: +rms[i].toFixed(4) });
    }
  }
  return out;
}

// Convenience: peak (max RMS over the curve).
export function peakRMS(rms: number[]): number {
  let m = 0;
  for (const v of rms) if (v > m) m = v;
  return m;
}
