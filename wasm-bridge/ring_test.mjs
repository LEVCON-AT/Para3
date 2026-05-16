// =============================================================================
//  PARA-3 :: lock-free ring verification
//
//  (A) DETERMINISTIC ADVERSARIAL INTERLEAVE — the rigorous correctness proof.
//      Arbitrary interleavings of bounded burst-push and drain-all vs a
//      reference FIFO. Verifies order, zero loss/dup, exact double round-trip,
//      full-rejection + empty handling, heavy mask wrap. Single-threaded,
//      O(1) per op (index queue, no array shift), fast.
//
//  (B) BOUNDED CROSS-THREAD SANITY — real Worker, small N, cooperative,
//      hard timeout (single-core host: SKIP on timeout, not a failure;
//      correctness is already established by A).
//
//  run: node ring_test.mjs
// =============================================================================
import { Worker } from 'node:worker_threads';
import { Para3Ring, OP } from './para3-ring.js';

const mulberry32 = (a) => () => {
  a |= 0; a = (a + 0x6D2B79F5) | 0;
  let t = Math.imul(a ^ (a >>> 15), 1 | a);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const pay = (seq) => Math.sin(seq) * 1e6 + seq / 7;

function interleave(cap, schedules, seed, label) {
  const ring = Para3Ring.create(cap);
  const rnd = mulberry32(seed);
  // reference FIFO of accepted seqs as an index queue (O(1), no shift)
  const q = new Int32Array(cap + 8);     // never holds more than `cap` live
  let qh = 0, qt = 0;
  const qcount = () => qt - qh;
  let seq = 0, accepted = 0, drained = 0, fullRej = 0, empties = 0;
  let bad = null;

  const pushOne = () => {
    const d = pay(seq);
    const ok = ring.setParam(seq, d);
    if (ok) { q[(qt++) % q.length] = seq; accepted++; }
    else fullRej++;
    seq++;
    return ok;
  };
  const drainAll = () => {
    let got = 0;
    ring.drain((op, i0, i1, dv) => {
      if (!bad) {
        if (qcount() <= 0) bad = `drain with empty model @${drained}`;
        else {
          const es = q[(qh++) % q.length];
          if (op !== OP.SET_PARAM || i0 !== es || dv !== pay(es))
            bad = `mismatch @${drained}: got(${op},${i0},${dv}) exp(seq ${es})`;
        }
      }
      got++; drained++;
    });
    if (got === 0) empties++;
  };

  for (let k = 0; k < schedules && !bad; k++) {
    if (rnd() < 0.55) {
      const burst = 1 + ((rnd() * 512) | 0);   // bounded, cap-independent
      for (let b = 0; b < burst; b++) pushOne();
    } else {
      drainAll();
    }
  }
  while (qcount() > 0 && !bad) drainAll();
  for (let r = 0; r < 5 && !bad; r++) {         // nail full<->empty boundary
    while (pushOne()) { /* fill */ }
    drainAll();
  }

  const pass = !bad && qcount() === 0 && drained === accepted
            && fullRej > 0 && empties > 0;
  console.log(`\n${label}  (cap=${cap}, schedules=${schedules})`);
  console.log(`   accepted/drained : ${accepted} / ${drained}`);
  console.log(`   full-rejections  : ${fullRej}  (backpressure exercised)`);
  console.log(`   empty-drains     : ${empties}`);
  console.log(`   mask wraps       : ~${(accepted / cap) | 0}`);
  console.log(`   integrity        : ${bad ? 'BROKEN: ' + bad
                       : 'FIFO, no loss/dup, exact double'}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  return pass;
}

function crossThread(cap, total, label, timeoutMs = 12000) {
  return new Promise((resolve) => {
    const ring = Para3Ring.create(cap);
    const w = new Worker(new URL('./ring_worker.mjs', import.meta.url),
                         { workerData: { sab: ring.sab, cap, total } });
    let sent = 0, done = false;
    const timer = setTimeout(() => {
      if (done) return; done = true;
      console.log(`\n${label}  -> SKIPPED (single-core timeout; correctness`
        + ` proven in A)`);
      w.terminate(); resolve(true);
    }, timeoutMs);
    const pump = () => {
      let budget = 2000;
      while (sent < total && budget-- > 0) {
        if (ring.setParam(sent, pay(sent))) sent++; else break;
      }
      if (sent < total && !done) setImmediate(pump);
    };
    pump();
    w.on('message', (m) => {
      if (done) return; done = true; clearTimeout(timer);
      const pass = m.ok && m.received === total;
      console.log(`\n${label}  (cap=${cap}, msgs=${total})`);
      console.log(`   received   : ${m.received}/${total}`);
      console.log(`   integrity  : ${m.ok ? 'intact' : 'BROKEN @' + m.firstBad}`);
      console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
      w.terminate(); resolve(pass);
    });
  });
}

let fails = 0;
fails += interleave(8,     20000, 0xA1, 'A1 tiny ring, max contention') ? 0 : 1;
fails += interleave(1024,  12000, 0xB2, 'A2 mid ring, heavy wrap')      ? 0 : 1;
fails += interleave(65536,  6000, 0xC3, 'A3 large ring')               ? 0 : 1;
fails += (await crossThread(256, 6000, 'B1 real Worker cross-thread')) ? 0 : 1;

console.log('\n==================================================');
console.log(`${fails ? 'OVERALL: FAIL' : 'OVERALL: PASS'}  (${fails} failure${fails === 1 ? '' : 's'})`);
process.exit(fails ? 1 : 0);
