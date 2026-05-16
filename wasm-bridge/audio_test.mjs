// =============================================================================
//  PARA-3 :: end-to-end control-path verification (headless)
//
//  Drives the real UI-facing Para3Controls, then decodes the ring EXACTLY as
//  para3-worklet.js does, and asserts the decoded op stream equals the intended
//  scenario — order, ids, packed step bits, bit-exact doubles. This proves the
//  whole UI -> ring -> worklet-dispatch contract without a browser. Also checks
//  backpressure does not corrupt the in-order prefix.
//
//  run: node audio_test.mjs
// =============================================================================
import { Para3Ring, OP } from './para3-ring.js';
import { Para3Controls, PARAM, MODE } from './para3-audio.js';

// worklet-identical decode (mirrors para3-worklet.js layout)
const RING_HDR = 2, SLOT_INTS = 6;
function drainDecode(ring) {
  const i32 = ring.i32, mask = ring.mask;
  const f64 = new Float64Array(1), fi = new Int32Array(f64.buffer);
  let r = Atomics.load(i32, 1); const w = Atomics.load(i32, 0);
  const out = [];
  while (r !== w) {
    const b = RING_HDR + (r & mask) * SLOT_INTS;
    fi[0] = i32[b + 3]; fi[1] = i32[b + 4];
    out.push([i32[b], i32[b + 1], i32[b + 2], f64[0]]);
    r = (r + 1) | 0;
  }
  Atomics.store(i32, 1, r);
  return out;
}

let fails = 0;
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// ---- E1: realistic scripted session, exact op-stream match ----------------
{
  const ring = Para3Ring.create(1024);
  const c = new Para3Controls(ring);
  const exp = [];
  const packStep = (note, g, m) => (note & 0xff) | ((g ? 1 : 0) << 8) | ((m ? 1 : 0) << 9);

  c.setParam(PARAM.CUTOFF, 0.62);      exp.push([OP.SET_PARAM, PARAM.CUTOFF, 0, 0.62]);
  c.setParam(PARAM.RESONANCE, 0.30);   exp.push([OP.SET_PARAM, PARAM.RESONANCE, 0, 0.30]);
  c.setMode(MODE.UNISON);              exp.push([OP.SET_MODE, MODE.UNISON, 0, 0]);
  c.seqTempo(124);                     exp.push([OP.SEQ_TEMPO, 0, 0, 124]);
  c.seqSwing(0.18);                    exp.push([OP.SEQ_SWING, 0, 0, 0.18]);
  for (let s = 0; s < 16; s++) {
    const note = 48 + (s % 5), g = (s % 2) === 0, mc = s / 15;
    c.seqStep(s, note, g, true, mc);
    exp.push([OP.SEQ_STEP, s, packStep(note, g, true), mc]);
  }
  c.seqLength(16);                     exp.push([OP.SEQ_LEN, 16, 0, 0]);
  c.seqCommit();                       exp.push([OP.SEQ_COMMIT, 0, 0, 0]);
  c.seqStart();                        exp.push([OP.SEQ_START, 0, 0, 0]);
  c.noteOn(60);                        exp.push([OP.NOTE_ON, 60, 0, 0]);
  for (let k = 0; k < 50; k++) {
    const v = 0.2 + 0.6 * Math.sin(k);
    c.midiCC(74, v);                   exp.push([OP.MIDI_CC, 74, 0, v]);
  }
  c.noteOff(60);                       exp.push([OP.NOTE_OFF, 60, 0, 0]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }
  const pass = bad < 0;
  console.log(`\nE1 scripted session  (${exp.length} ops)`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${pass ? 'yes (order, ids, exact doubles)'
                     : 'NO @ ' + bad}`);
  console.log(`   dropped (UI)     : ${c.dropped}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E2: backpressure prefix integrity (tiny ring, no drain) --------------
{
  const cap = 16;
  const ring = Para3Ring.create(cap);
  const c = new Para3Controls(ring);
  // push many without draining: ring fills, Controls counts drops, never blocks
  for (let i = 0; i < 1000; i++) c.setParam(PARAM.CUTOFF, i / 1000);
  const got = drainDecode(ring);
  // whatever made it in must be a correct in-order prefix of the intended seq
  let ordered = true;
  for (let i = 0; i < got.length; i++) {
    const want = [OP.SET_PARAM, PARAM.CUTOFF, 0, i / 1000];
    if (!eq(got[i], want)) { ordered = false; break; }
  }
  const pass = ordered && got.length <= cap && c.dropped > 0
            && got.length + 0 <= 1000;
  console.log(`\nE2 backpressure prefix  (cap=${cap}, 1000 pushed, no drain)`);
  console.log(`   accepted (<=cap) : ${got.length}`);
  console.log(`   in-order prefix  : ${ordered ? 'intact' : 'BROKEN'}`);
  console.log(`   dropped (UI)     : ${c.dropped}  (never blocked)`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E3: interleaved push/drain keeps exact FIFO correspondence -----------
{
  const ring = Para3Ring.create(64);
  const c = new Para3Controls(ring);
  const pushed = [];                 // values that were actually accepted
  const drained = [];
  let seq = 0;
  for (let round = 0; round < 500; round++) {
    const burst = 1 + (round % 90);
    for (let b = 0; b < burst; b++) {
      const before = c.dropped;
      const ok = c.setParam(PARAM.DRIVE, seq);
      if (ok && c.dropped === before) pushed.push(seq);   // accepted
      seq++;
    }
    for (const g of drainDecode(ring)) drained.push(g);
  }
  let bad = -1;
  if (drained.length !== pushed.length) bad = 1e9;
  else for (let i = 0; i < pushed.length; i++) {
    const g = drained[i];
    if (g[0] !== OP.SET_PARAM || g[1] !== PARAM.DRIVE || g[3] !== pushed[i]) {
      bad = i; break;
    }
  }
  const accountOk = pushed.length + c.dropped === seq;
  const pass = bad < 0 && accountOk && c.dropped > 0;
  console.log(`\nE3 interleaved push/drain  (500 rounds, cap=64)`);
  console.log(`   attempted        : ${seq}`);
  console.log(`   accepted=drained : ${pushed.length} / ${drained.length}`);
  console.log(`   dropped (full)   : ${c.dropped}`);
  console.log(`   accounting       : ${pushed.length}+${c.dropped}=${pushed.length + c.dropped} == ${seq} ${accountOk ? 'ok' : 'MISMATCH'}`);
  console.log(`   FIFO exact       : ${bad < 0 ? 'yes' : 'NO @ ' + bad}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

console.log('\n==================================================');
console.log(`${fails ? 'OVERALL: FAIL' : 'OVERALL: PASS'}  (${fails} failure${fails === 1 ? '' : 's'})`);
process.exit(fails ? 1 : 0);
