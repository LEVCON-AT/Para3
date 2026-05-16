// =============================================================================
//  PARA-3 :: SAB-free control transport verification  (Phase B1)
//
//  Drives Para3Controls over a PortTransport (postMessage), receives messages
//  on the other side of a MessageChannel, decodes them with the SAME logic the
//  worklet uses (para3PortDrain), and asserts the decoded op stream equals
//  the intended scenario — order, ids, exact doubles. This is the offline-
//  mobile control path proof.
//
//  Also drives the SAME scenario over a RingTransport and proves both decoded
//  streams are bit-identical (no logic drift between transports — both feed
//  the worklet's single _dispatch() with identical inputs).
//
//  run: node port_test.mjs
// =============================================================================
import { MessageChannel } from 'node:worker_threads';
import { Para3Ring, OP } from './para3-ring.js';
import { Para3Port, para3PortPush, para3PortDrain } from './para3-port.js';
import { Para3Controls, PARAM, MODE } from './para3-audio.js';

// ---- decoders (mirror para3-worklet.js drains) ------------------------------
const RING_HDR = 2, SLOT_INTS = 6;
function drainDecodeRing(ring) {
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
function makePortDecoder(port) {
  const queue = [];
  port.on('message', (m) => para3PortPush(queue, m));
  return () => {
    const out = [];
    para3PortDrain(queue, (op, i0, i1, d) => out.push([op, i0, i1, d]));
    return out;
  };
}

// ---- scenario runners (single source for both transports) ------------------
async function runOverPort(scenario) {
  const ch = new MessageChannel();
  const decode = makePortDecoder(ch.port2);
  const transport = new Para3Port(ch.port1);
  const c = new Para3Controls(transport);
  scenario(c);
  await new Promise((r) => setImmediate(r));
  const got = decode();
  ch.port1.close(); ch.port2.close();
  return { got, dropped: c.dropped };
}
function runOverRing(scenario, cap = 1024) {
  const ring = Para3Ring.create(cap);
  const c = new Para3Controls(ring);
  scenario(c);
  return { got: drainDecodeRing(ring), dropped: c.dropped };
}

// recording controls: same surface as Para3Controls but pushes expected ops
// instead of forwarding to a transport. This guarantees the expectation is
// built from the EXACT same scenario function we run on the real transport.
function makeRecorder() {
  const exp = [];
  const packStep = (n, g, m) => (n & 0xff) | ((g ? 1 : 0) << 8)
                                            | ((m ? 1 : 0) << 9);
  const rec = {
    noteOn(n)        { exp.push([OP.NOTE_ON,  n, 0, 0]); },
    noteOff(n)       { exp.push([OP.NOTE_OFF, n, 0, 0]); },
    setParam(id, v)  { exp.push([OP.SET_PARAM, id, 0, v]); },
    setMode(m)       { exp.push([OP.SET_MODE,  m, 0, 0]); },
    midiCC(cc, v)    { exp.push([OP.MIDI_CC, cc, 0, v]); },
    seqTempo(b)      { exp.push([OP.SEQ_TEMPO, 0, 0, b]); },
    seqSwing(s)      { exp.push([OP.SEQ_SWING, 0, 0, s]); },
    seqStart()       { exp.push([OP.SEQ_START, 0, 0, 0]); },
    seqStop()        { exp.push([OP.SEQ_STOP,  0, 0, 0]); },
    seqArmRecord(o)  { exp.push([OP.SEQ_ARMREC, o ? 1 : 0, 0, 0]); },
    seqLength(l)     { exp.push([OP.SEQ_LEN, l, 0, 0]); },
    seqCommit()      { exp.push([OP.SEQ_COMMIT, 0, 0, 0]); },
    seqStep(i, n, g, mo, mc) {
      exp.push([OP.SEQ_STEP, i, packStep(n, g, mo), mc]);
    },
  };
  return { rec, exp };
}

// ---- scenarios -------------------------------------------------------------
function sessionA(c) {
  c.setParam(PARAM.CUTOFF, 0.62);
  c.setParam(PARAM.RESONANCE, 0.30);
  c.setMode(MODE.UNISON);
  c.seqTempo(124);
  c.seqSwing(0.18);
  for (let s = 0; s < 16; s++) {
    c.seqStep(s, 48 + (s % 5), (s % 2) === 0, true, s / 15);
  }
  c.seqLength(16);
  c.seqCommit();
  c.seqStart();
  c.noteOn(60);
  for (let k = 0; k < 50; k++) c.midiCC(74, 0.2 + 0.6 * Math.sin(k));
  c.noteOff(60);
}
function sessionStress(c) {
  for (let k = 0; k < 1000; k++)
    c.setParam(PARAM.DRIVE, Math.sin(k * 0.137) * 0.5 + 0.5);
}

// ---- tests -----------------------------------------------------------------
let fails = 0;
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
function compare(label, got, exp, dropped) {
  let bad = -1;
  if (got.length !== exp.length) bad = 1e9;
  else for (let i = 0; i < exp.length; i++)
    if (!eq(got[i], exp[i])) { bad = i; break; }
  const pass = bad < 0 && dropped === 0;
  console.log(`\n${label}  (${exp.length} ops)`);
  console.log(`   decoded ops      : ${got.length} / ${exp.length}`);
  console.log(`   stream identical : ${pass ? 'yes (order, ids, exact doubles)'
                       : 'NO @ ' + bad}`);
  console.log(`   dropped (UI)     : ${dropped}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
  return pass;
}

// E1: 76-op session over PORT, decoded stream matches the recorded expectation
{
  const { rec, exp } = makeRecorder();
  sessionA(rec);
  const { got, dropped } = await runOverPort(sessionA);
  compare('E1 port: scripted 76-op session', got, exp, dropped);
}

// E2: same session over PORT and RING -> bit-identical decoded streams
{
  const port = await runOverPort(sessionA);
  const ring = runOverRing(sessionA);
  let bad = -1;
  if (port.got.length !== ring.got.length) bad = 1e9;
  else for (let i = 0; i < port.got.length; i++)
    if (!eq(port.got[i], ring.got[i])) { bad = i; break; }
  const pass = bad < 0;
  console.log(`\nE2 no transport drift  (port == ring decoded)`);
  console.log(`   port decoded     : ${port.got.length}`);
  console.log(`   ring decoded     : ${ring.got.length}`);
  console.log(`   bit-identical    : ${pass ? 'yes' : 'NO @ ' + bad}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// E3: 1000-op continuous param sweep over PORT, in-order intact
{
  const { rec, exp } = makeRecorder();
  sessionStress(rec);
  const { got, dropped } = await runOverPort(sessionStress);
  compare('E3 port: 1000-op stress sweep', got, exp, dropped);
}

console.log('\n==================================================');
console.log(`${fails ? 'OVERALL: FAIL' : 'OVERALL: PASS'}  (${fails} failure${fails === 1 ? '' : 's'})`);
process.exit(fails ? 1 : 0);
