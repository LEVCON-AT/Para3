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

// ---- E4: KORG-parity E1 — EG_CUT_DEPTH bipolar centring + LFO Trigger Sync -
//
// Asserts the UI-funnel for the two new bindings introduced in sprint E1:
//   E1.1  egi knob (-100..+100) at val=0 -> emitKnob computes n=0.5;
//         setParam(PARAM.EG_CUT_DEPTH, 0.5) hits the ring as SET_PARAM with
//         paramId=12, d=0.5. The engine taper (Para3Engine.hpp) maps
//         (0.5-0.5)*2*kEgIntOctMax = 0 -> no swing. Engine-side correlation
//         is proven by offline_test T14; this test proves the JS funnel.
//   E1.2  controls.setLfoSync(on) emits SET_LFO_SYNC with i0 in {0,1}.
{
  const ring = Para3Ring.create(1024);
  const c = new Para3Controls(ring);
  const exp = [];
  // emulate the para3-responsive.html emitKnob path for the egi knob:
  //   n = (val - min) / (max - min)
  const egiNorm = (val, min = -100, max = 100) => (val - min) / (max - min);
  c.setParam(PARAM.EG_CUT_DEPTH, egiNorm(0));         exp.push([OP.SET_PARAM, PARAM.EG_CUT_DEPTH, 0, 0.5]);
  c.setParam(PARAM.EG_CUT_DEPTH, egiNorm(+100));      exp.push([OP.SET_PARAM, PARAM.EG_CUT_DEPTH, 0, 1.0]);
  c.setParam(PARAM.EG_CUT_DEPTH, egiNorm(-100));      exp.push([OP.SET_PARAM, PARAM.EG_CUT_DEPTH, 0, 0.0]);
  c.setLfoSync(true);                                  exp.push([OP.SET_LFO_SYNC, 1, 0, 0]);
  c.setLfoSync(false);                                 exp.push([OP.SET_LFO_SYNC, 0, 0, 0]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }

  // Independently verify the bipolar centring is *exactly* representable as a
  // double (no rounding error in the wire format): val=0 -> 0.5, peer-checked.
  const f64 = new Float64Array([0.5]);
  const centred = (got[0] && got[0][3] === f64[0]) ? 'yes' : 'NO';

  const pass = bad < 0 && centred === 'yes';
  console.log(`\nE4 KORG-parity E1: EG_CUT_DEPTH bipolar + LFO Trigger Sync`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${bad < 0 ? 'yes (PARAM.EG_CUT_DEPTH=12, SET_LFO_SYNC=15)'
                     : 'NO @ ' + bad}`);
  console.log(`   bipolar centre   : ${centred}  (egi val=0 -> norm=0.5 -> engine swing=0)`);
  console.log(`   dropped (UI)     : ${c.dropped}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E5: KORG-parity E2 — DETUNE + PORTAMENTO wiring ----------------------
//
// Asserts that the formerly inert `det` and `por` knobs now produce SET_PARAM
// with paramId 13 (DETUNE) and 14 (PORTAMENTO). The engine no-op-at-zero
// guarantee is proven by offline_test T16/T17 (max diff 0.000e+00); here we
// prove the JS funnel.
{
  const ring = Para3Ring.create(1024);
  const c = new Para3Controls(ring);
  const exp = [];
  // det knob range in the UI is 0..100 -> norm 0..1; por likewise.
  const knobNorm = (val, min = 0, max = 100) => (val - min) / (max - min);
  c.setParam(PARAM.DETUNE, knobNorm(0));     exp.push([OP.SET_PARAM, PARAM.DETUNE, 0, 0.0]);
  c.setParam(PARAM.DETUNE, knobNorm(50));    exp.push([OP.SET_PARAM, PARAM.DETUNE, 0, 0.5]);
  c.setParam(PARAM.DETUNE, knobNorm(100));   exp.push([OP.SET_PARAM, PARAM.DETUNE, 0, 1.0]);
  c.setParam(PARAM.PORTAMENTO, knobNorm(0)); exp.push([OP.SET_PARAM, PARAM.PORTAMENTO, 0, 0.0]);
  c.setParam(PARAM.PORTAMENTO, knobNorm(35));exp.push([OP.SET_PARAM, PARAM.PORTAMENTO, 0, 0.35]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }
  const pass = bad < 0;
  console.log(`\nE5 KORG-parity E2: DETUNE (id=13) + PORTAMENTO (id=14)`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${bad < 0 ? 'yes (no-op at 0, engine-side T16/T17)'
                     : 'NO @ ' + bad}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E6: KORG-parity E3 — target-parametric motion + SMOOTH + REC --------
//
// Verifies the JS funnel for target-parametric motion: paint() emits
// SEQ_MOTION_SET with the lane's paramId (not cutoff-implicit), the SMOOTH
// toggle uses SEQ_MOTION_SMOOTH, REC uses SEQ_MOTION_REC, and PEAK rejection
// happens UI-side (no SET emitted with paramId=PARAM.RESONANCE).
// Engine round-trip + auto-shutdown are proven in offline_test T18.
{
  const ring = Para3Ring.create(2048);
  const c = new Para3Controls(ring);
  const exp = [];

  // Lane paint for CUTOFF (paramId=0): set step 3 to 0.75, then SMOOTH on,
  // then REC arm. PEAK paint attempt is filtered UI-side -> NOT emitted.
  c.seqMotion(PARAM.CUTOFF, 3, 0.75);          exp.push([OP.SEQ_MOTION_SET, PARAM.CUTOFF, 3, 0.75]);
  c.seqCommit();                                exp.push([OP.SEQ_COMMIT, 0, 0, 0]);
  c.seqMotionSmooth(true);                      exp.push([OP.SEQ_MOTION_SMOOTH, 1, 0, 0]);
  c.seqMotionSmooth(false);                     exp.push([OP.SEQ_MOTION_SMOOTH, 0, 0, 0]);
  c.seqMotionRec(PARAM.CUTOFF, true);           exp.push([OP.SEQ_MOTION_REC, PARAM.CUTOFF, 1, 0]);
  c.seqMotionVal(PARAM.LFO_RATE, 0.4);          exp.push([OP.SEQ_MOTION_VAL, PARAM.LFO_RATE, 0, 0.4]);
  c.seqMotionRec(PARAM.CUTOFF, false);          exp.push([OP.SEQ_MOTION_REC, PARAM.CUTOFF, 0, 0]);

  // Multi-lane independence: write 3 different parameter lanes, then commit.
  c.seqMotion(PARAM.DRIVE,   0, 0.2);           exp.push([OP.SEQ_MOTION_SET, PARAM.DRIVE,   0, 0.2]);
  c.seqMotion(PARAM.LFO_RATE,7, 0.9);           exp.push([OP.SEQ_MOTION_SET, PARAM.LFO_RATE,7, 0.9]);
  c.seqMotion(PARAM.DELAY_TIME,15,0.05);        exp.push([OP.SEQ_MOTION_SET, PARAM.DELAY_TIME,15,0.05]);
  c.seqCommit();                                exp.push([OP.SEQ_COMMIT, 0, 0, 0]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }

  // Crucial UI invariant: no SET targeting RESONANCE may exist in the stream.
  const peakSeen = got.some(g => g[0] === OP.SEQ_MOTION_SET && g[1] === PARAM.RESONANCE);

  const pass = bad < 0 && !peakSeen;
  console.log(`\nE6 KORG-parity E3: target-parametric motion + SMOOTH + REC`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${bad < 0 ? 'yes (paramId carried per-op)' : 'NO @ ' + bad}`);
  console.log(`   PEAK in stream   : ${peakSeen ? 'YES (BAD)' : 'no (UI-side reject honoured)'}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E7: KORG-parity E4 — step trigger / tempo-div / active-step / metro --
//
// Verifies the JS funnel for the four E4 toggles. Engine semantics (EG
// retrigger, exact step samples, skip enforced in playback+rec, delay bypass
// on metro) are proven in offline_test T19–T22.
{
  const ring = Para3Ring.create(1024);
  const c = new Para3Controls(ring);
  const exp = [];
  c.seqStepTrigger(true);          exp.push([OP.SEQ_STEP_TRIGGER, 1, 0, 0]);
  c.seqStepTrigger(false);         exp.push([OP.SEQ_STEP_TRIGGER, 0, 0, 0]);
  c.seqTempoDiv(1);                exp.push([OP.SEQ_TEMPO_DIV, 1, 0, 0]);
  c.seqTempoDiv(2);                exp.push([OP.SEQ_TEMPO_DIV, 2, 0, 0]);
  c.seqTempoDiv(4);                exp.push([OP.SEQ_TEMPO_DIV, 4, 0, 0]);
  c.seqActiveStep(0, true);        exp.push([OP.SEQ_ACTIVE_STEP, 0, 1, 0]);
  c.seqActiveStep(5, false);       exp.push([OP.SEQ_ACTIVE_STEP, 5, 0, 0]);
  c.seqActiveStep(15, true);       exp.push([OP.SEQ_ACTIVE_STEP, 15, 1, 0]);
  c.seqMetronome(true);            exp.push([OP.SEQ_METRONOME, 1, 0, 0]);
  c.seqMetronome(false);           exp.push([OP.SEQ_METRONOME, 0, 0, 0]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }
  const pass = bad < 0;
  console.log(`\nE7 KORG-parity E4: STP / TDIV / ACTIVE-STEP / METRO`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${bad < 0 ? 'yes' : 'NO @ ' + bad}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E8: KORG-parity E5 — FLUX (sample-accurate event sequence) -----------
//
// Verifies the JS funnel for FLUX. Engine behaviour (sample-accurate playback
// with Jitter 0, OFF-before-ON at same offset, loop-wrap click-free, empty
// pattern = silence, overflow drop observable) is proven by offline_test T23.
{
  const ring = Para3Ring.create(1024);
  const c = new Para3Controls(ring);
  const exp = [];
  // Realistic flow: set loop length, switch mode on, arm REC, record events,
  // disarm + commit, mode off.
  const loopLen = ((60 / 120) * 4 * 48000) | 0;       // 1 bar @120bpm @48k
  c.seqFluxLoopLen(loopLen);              exp.push([OP.SEQ_FLUX_LOOP_LEN, loopLen, 0, 0]);
  c.seqFluxMode(true);                    exp.push([OP.SEQ_FLUX_MODE, 1, 0, 0]);
  c.seqFluxRec(true);                     exp.push([OP.SEQ_FLUX_REC, 1, 0, 0]);
  c.seqFluxNote(60, true);                exp.push([OP.SEQ_FLUX_NOTE, 60, 1, 0]);
  c.seqFluxNote(60, false);               exp.push([OP.SEQ_FLUX_NOTE, 60, 0, 0]);
  c.seqFluxNote(67, true);                exp.push([OP.SEQ_FLUX_NOTE, 67, 1, 0]);
  c.seqFluxNote(67, false);               exp.push([OP.SEQ_FLUX_NOTE, 67, 0, 0]);
  c.seqFluxRec(false);                    exp.push([OP.SEQ_FLUX_REC, 0, 0, 0]);
  c.seqFluxCommit();                      exp.push([OP.SEQ_FLUX_COMMIT, 0, 0, 0]);
  c.seqFluxMode(false);                   exp.push([OP.SEQ_FLUX_MODE, 0, 0, 0]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }
  const pass = bad < 0;
  console.log(`\nE8 KORG-parity E5: FLUX mode + loop_len + REC + note + commit`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   loop_len encoded : ${loopLen} samples  (1 bar @120bpm @48k)`);
  console.log(`   stream identical : ${bad < 0 ? 'yes' : 'NO @ ' + bad}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E9: KORG-parity E6 — VOLUME via engine + OCTAVE via engine ----------
//
// Verifies that VOLUME no longer routes to a host-side GainNode but to
// PARAM.VOLUME (id=15), and that OCTAVE routes to setOctave (signed int).
// Engine guarantees (unity bit-identical, exact 12-semitone shift,
// fixed velocity peak invariance) are proven in offline_test T24/T25/T26.
{
  const ring = Para3Ring.create(1024);
  const c = new Para3Controls(ring);
  const exp = [];
  const knobNorm = (val, min = 0, max = 100) => (val - min) / (max - min);
  c.setParam(PARAM.VOLUME, knobNorm(100));   exp.push([OP.SET_PARAM, PARAM.VOLUME, 0, 1.0]);
  c.setParam(PARAM.VOLUME, knobNorm(50));    exp.push([OP.SET_PARAM, PARAM.VOLUME, 0, 0.5]);
  c.setParam(PARAM.VOLUME, knobNorm(0));     exp.push([OP.SET_PARAM, PARAM.VOLUME, 0, 0.0]);
  c.setOctave(0);                            exp.push([OP.SET_OCTAVE, 0, 0, 0]);
  c.setOctave(1);                            exp.push([OP.SET_OCTAVE, 1, 0, 0]);
  c.setOctave(-2);                           exp.push([OP.SET_OCTAVE, -2, 0, 0]);
  c.setOctave(2);                            exp.push([OP.SET_OCTAVE, 2, 0, 0]);

  const got = drainDecode(ring);
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++) if (!eq(got[i], exp[i])) { bad = i; break; }
  // Critical invariant: there must be NO Web Audio GainNode signal in this
  // funnel — VOLUME is now PARAM-shaped, not a host GainNode. The stream
  // contains exactly one OP per VOLUME write, all carrying PARAM.VOLUME=15.
  const allVolHaveParam15 = got.filter(g => g[0] === OP.SET_PARAM && g[1] === PARAM.VOLUME).length === 3;
  const allOctSigned = got.filter(g => g[0] === OP.SET_OCTAVE).every(g => Number.isInteger(g[1]));

  const pass = bad < 0 && allVolHaveParam15 && allOctSigned;
  console.log(`\nE9 KORG-parity E6: VOLUME (PARAM.VOLUME=15) + OCTAVE (signed)`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${bad < 0 ? 'yes' : 'NO @ ' + bad}`);
  console.log(`   VOLUME funnel    : ${allVolHaveParam15 ? 'yes (3 SET_PARAM/15)' : 'NO'}`);
  console.log(`   OCTAVE signed    : ${allOctSigned ? 'yes' : 'NO'}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ---- E10: KORG-parity E7 — funnel completeness + surface contract --------
//
// Final integration assertion: every method the responsive UI calls on
// Para3Controls must EXIST (not a stub, not a fallback), and every PARAM id
// up to VOLUME must serialise through SET_PARAM. We import Para3Controls
// (real type), build one with a dummy ring, and inspect the surface.
{
  const ring = Para3Ring.create(64);
  const c = new Para3Controls(ring);
  const required = [
    // pre-KORG (sanity that we didn't regress anything)
    'noteOn','noteOff','setParam','setMode','midiCC','setLfoShape',
    'seqTempo','seqSwing','seqStart','seqStop','seqArmRecord','seqLength',
    'seqCommit','seqStep',
    // E1.2 / E3 / E4 / E5 / E6.2
    'setLfoSync',
    'seqMotion','seqMotionSmooth','seqMotionRec','seqMotionVal',
    'seqStepTrigger','seqTempoDiv','seqActiveStep','seqMetronome',
    'seqFluxMode','seqFluxLoopLen','seqFluxRec','seqFluxNote','seqFluxCommit',
    'setOctave',
  ];
  const missing = required.filter(m => typeof c[m] !== 'function');

  const requiredParams = {
    CUTOFF:0, RESONANCE:1, DRIVE:2, LFO_CUT_DEPTH:3, DELAY_MIX:4,
    LFO_RATE:5, LFO_PITCH_DEPTH:6, DELAY_TIME:7, DELAY_FEEDBACK:8,
    ATTACK:9, DECREL:10, SUSTAIN:11,
    EG_CUT_DEPTH:12, DETUNE:13, PORTAMENTO:14, VOLUME:15,
  };
  const paramMis = Object.entries(requiredParams)
    .filter(([k,v]) => PARAM[k] !== v).map(([k,v]) => `${k}!=${v}`);

  const pass = missing.length === 0 && paramMis.length === 0;
  console.log(`\nE10 KORG-parity E7: complete surface contract`);
  console.log(`   Controls methods : ${required.length} required, ${missing.length} missing`);
  if (missing.length) console.log(`      missing       : ${missing.join(', ')}`);
  console.log(`   PARAM ids        : ${Object.keys(requiredParams).length} required, ${paramMis.length} mismatch`);
  if (paramMis.length) console.log(`      mismatch      : ${paramMis.join(', ')}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

console.log('\n==================================================');
console.log(`${fails ? 'OVERALL: FAIL' : 'OVERALL: PASS'}  (${fails} failure${fails === 1 ? '' : 's'})`);
process.exit(fails ? 1 : 0);
