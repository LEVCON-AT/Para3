// =============================================================================
//  PARA-3 :: WASM == native parity (Phase A3)
//
//  Instantiates para3.wasm in Node with the SAME minimal imports the worklet
//  uses, drives the SAME scenario the native driver (parity_native) drove, and
//  compares the float output sample-by-sample.
//
//  Pass criterion (Runbook A3):
//    * max |wasm - native| <= 1e-6  (float-rounding tolerance)
//    * no NaN / Inf  (both sides)
//    * signal is alive (max |x| above a low floor)
//
//  Scenario lives in parity_seq.h (single source of truth, also consumed by
//  parity_native.cpp). When you change it there, rebuild the native binary.
//
//  run: bash build_wasm.sh ..
//       g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp \
//           parity_native.cpp -o parity_native
//       ./parity_native > parity_native.f32
//       node wasm_parity.mjs
// =============================================================================
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));

// ---------- scenario (must mirror parity_seq.h exactly) ----------------------
const SR     = 48000.0;
const BLOCK  = 128;
const FRAMES = 48000;

// param ids (mirror para3_capi.h)
const P_CUTOFF = 0, P_RESONANCE = 1, P_DRIVE = 2,
      P_LFO_CUT_DEPTH = 3, P_DELAY_MIX = 4;
const M_POLY = 0;

const OPS = [
  { op: 'mode',  i0: M_POLY,           d: 0.0  },
  { op: 'param', i0: P_CUTOFF,         d: 0.55 },
  { op: 'param', i0: P_RESONANCE,      d: 0.30 },
  { op: 'param', i0: P_DRIVE,          d: 0.40 },
  { op: 'param', i0: P_LFO_CUT_DEPTH,  d: 0.20 },
  { op: 'param', i0: P_DELAY_MIX,      d: 0.25 },
  { op: 'note_on', i0: 57,             d: 0.0  },
];

// ---------- worklet-identical imports (only WASI is actually used) ----------
function makeImports() {
  return {
    wasi_snapshot_preview1: {
      proc_exit: (c) => { throw new Error('proc_exit ' + c); },
      fd_close: () => 0, fd_write: () => 0, fd_seek: () => 0, fd_read: () => 0,
      environ_sizes_get: () => 0, environ_get: () => 0,
      clock_time_get: () => 0, random_get: () => 0,
    },
  };
}

// ---------- run ---------------------------------------------------------------
const wasmBytes  = await readFile(join(HERE, 'para3.wasm'));
const wasmModule = await WebAssembly.compile(wasmBytes);
const inst       = await WebAssembly.instantiate(wasmModule, makeImports());
const x = inst.exports;
const memory = x.memory;

const p = x.para3_create(SR, BLOCK);
if (!p) { console.error('para3_create failed'); process.exit(2); }

for (const o of OPS) {
  if      (o.op === 'param')   x.para3_set_param(p, o.i0, o.d);
  else if (o.op === 'mode')    x.para3_set_mode (p, o.i0);
  else if (o.op === 'note_on') x.para3_note_on  (p, o.i0);
  else if (o.op === 'note_off')x.para3_note_off (p, o.i0);
}

const bufPtr = x.malloc(BLOCK * 4);
if (!bufPtr) { console.error('malloc failed'); process.exit(2); }

const wasmOut = new Float32Array(FRAMES);
let i = 0;
while (i < FRAMES) {
  const n = Math.min(BLOCK, FRAMES - i);
  x.para3_render(p, bufPtr, n);
  const view = new Float32Array(memory.buffer, bufPtr, n);
  wasmOut.set(view, i);
  i += n;
}
x.free(bufPtr);
x.para3_destroy(p);

// ---------- compare with parity_native.f32 ----------------------------------
let nativeBytes;
try { nativeBytes = await readFile(join(HERE, 'parity_native.f32')); }
catch (e) {
  console.error('missing parity_native.f32 — build & run parity_native first');
  process.exit(2);
}
if (nativeBytes.byteLength !== FRAMES * 4) {
  console.error(`size mismatch: got ${nativeBytes.byteLength} bytes,`
                + ` expected ${FRAMES * 4}`);
  process.exit(2);
}
const nativeOut = new Float32Array(nativeBytes.buffer, nativeBytes.byteOffset,
                                   FRAMES);

let maxAbsDiff = 0, sumSq = 0;
let nanW = 0, nanN = 0;
let peakW = 0, peakN = 0;
let firstBadIdx = -1;
const tol = 1e-6;

for (let k = 0; k < FRAMES; k++) {
  const w = wasmOut[k], n = nativeOut[k];
  if (!Number.isFinite(w)) nanW++;
  if (!Number.isFinite(n)) nanN++;
  const aw = Math.abs(w), an = Math.abs(n);
  if (aw > peakW) peakW = aw;
  if (an > peakN) peakN = an;
  const d = Math.abs(w - n);
  if (d > maxAbsDiff) {
    maxAbsDiff = d;
    if (d > tol && firstBadIdx < 0) firstBadIdx = k;
  }
  sumSq += d * d;
}
const rmsDiff = Math.sqrt(sumSq / FRAMES);

const alive = peakW > 1e-3 && peakN > 1e-3;
const finite = nanW === 0 && nanN === 0;
const equiv  = maxAbsDiff <= tol;
const pass   = alive && finite && equiv;

console.log('\nPARA-3 WASM == native parity  (1s, fs=48000, block=128)');
console.log('==================================================');
console.log(`frames           : ${FRAMES}`);
console.log(`max |wasm-nat|   : ${maxAbsDiff.toExponential(3)}  (tol ${tol})`);
console.log(`rms |wasm-nat|   : ${rmsDiff.toExponential(3)}`);
console.log(`peak |wasm|      : ${peakW.toFixed(6)}`);
console.log(`peak |native|    : ${peakN.toFixed(6)}`);
console.log(`finite           : wasm=${nanW === 0 ? 'yes' : 'NO ('+nanW+' NaN/Inf)'}`
            + `  native=${nanN === 0 ? 'yes' : 'NO ('+nanN+' NaN/Inf)'}`);
if (!equiv) console.log(`first |Δ|>tol at : sample ${firstBadIdx}`);
console.log(`\n${pass ? 'OVERALL: PASS' : 'OVERALL: FAIL'}`);
process.exit(pass ? 0 : 1);
