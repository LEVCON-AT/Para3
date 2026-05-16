// Small cross-thread sanity consumer. Cooperative (yields) so it cannot
// starve the producer on a single-core host.
import { parentPort, workerData } from 'node:worker_threads';
import { Para3Ring, OP } from './para3-ring.js';

const { sab, cap, total } = workerData;
const ring = new Para3Ring(sab, cap);

let expected = 0, ok = true, firstBad = -1;

async function loop() {
  while (expected < total) {
    ring.drain((op, i0, i1, d) => {
      const want = Math.sin(expected) * 1e6 + expected / 7;
      if ((op !== OP.SET_PARAM || i0 !== expected || d !== want) && ok) {
        ok = false; firstBad = expected;
      }
      expected++;
    });
    await new Promise((r) => setTimeout(r, 0));   // yield the single core
  }
  parentPort.postMessage({ ok, received: expected, firstBad });
}
loop();
