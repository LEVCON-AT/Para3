// =============================================================================
//  PARA-3 :: AudioWorkletProcessor  (self-contained, NO es-import)
//
//  AudioWorkletGlobalScope cannot `import` in most browsers, so the ring
//  CONSUMER + protocol are inlined here. They mirror para3-ring.js exactly
//  (one shared SAB layout / OP table — the single source of truth is the
//  protocol spec in README_WASM_BRIDGE.md; ring_test.mjs verifies the
//  producer/consumer against that layout).
//
//  Real-time: no allocation, no locks, no async in process(). The heap output
//  buffer is malloc'd once. The 128-quantum path is proven artefact-free
//  natively (capi_test WA3: 128-quanta == one call, bit-identical).
// =============================================================================

// ---- shared ring layout (must match para3-ring.js) ----
const RING_HDR  = 2;
const SLOT_INTS = 6;
const OP = {
  NOTE_ON:1, NOTE_OFF:2, SET_PARAM:3, SET_MODE:4, SEQ_TEMPO:5, SEQ_SWING:6,
  SEQ_START:7, SEQ_STOP:8, SEQ_ARMREC:9, SEQ_STEP:10, SEQ_LEN:11,
  SEQ_COMMIT:12, MIDI_CC:13, SET_LFO_SHAPE:14,
};

function makeImports(memory) {
  const nop = () => {};
  return {
    env: {
      memory,
      emscripten_notify_memory_growth: nop,
      emscripten_resize_heap: () => 0,
      abort: () => { throw new Error('para3 wasm abort'); },
      __assert_fail: () => { throw new Error('para3 wasm assert'); },
    },
    wasi_snapshot_preview1: {
      proc_exit: (c) => { throw new Error('para3 wasm proc_exit ' + c); },
      fd_close: () => 0, fd_write: () => 0, fd_seek: () => 0, fd_read: () => 0,
      environ_sizes_get: () => 0, environ_get: () => 0,
      clock_time_get: () => 0, random_get: () => 0,
    },
  };
}

class Para3Processor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions;
    this.ready = false;

    // ring consumer state
    this.i32  = new Int32Array(o.ringSAB);
    this.mask = o.ringCap - 1;
    this._f64 = new Float64Array(1);
    this._fi  = new Int32Array(this._f64.buffer);

    this.memory = new WebAssembly.Memory({ initial: 512 }); // 512*64KiB = 32 MiB
    const inst = new WebAssembly.Instance(o.wasmModule, makeImports(this.memory));
    this.x = inst.exports;

    this.p = this.x.para3_create(o.sampleRate, (o.maxBlock | 0) || 128);
    this.bufPtr = this.x.malloc(128 * 4);
    this.ready = !!this.p && !!this.bufPtr;
    this.port.postMessage({ type: this.ready ? 'ready' : 'error' });
  }

  _drain() {
    const i32 = this.i32;
    let r = Atomics.load(i32, 1);
    const w = Atomics.load(i32, 0);                 // acquire
    if (r === w) return;
    const x = this.x, p = this.p;
    while (r !== w) {
      const b = RING_HDR + (r & this.mask) * SLOT_INTS;
      const op = i32[b], i0 = i32[b + 1], i1 = i32[b + 2];
      this._fi[0] = i32[b + 3]; this._fi[1] = i32[b + 4];
      const d = this._f64[0];
      switch (op) {
        case OP.NOTE_ON:    x.para3_note_on(p, i0); break;
        case OP.NOTE_OFF:   x.para3_note_off(p, i0); break;
        case OP.SET_PARAM:  x.para3_set_param(p, i0, d); break;
        case OP.SET_MODE:   x.para3_set_mode(p, i0); break;
        case OP.SEQ_TEMPO:  x.para3_seq_set_tempo(p, d); break;
        case OP.SEQ_SWING:  x.para3_seq_set_swing(p, d); break;
        case OP.SEQ_START:  x.para3_seq_start(p); break;
        case OP.SEQ_STOP:   x.para3_seq_stop(p); break;
        case OP.SEQ_ARMREC: x.para3_seq_arm_record(p, i0); break;
        case OP.SEQ_STEP:
          x.para3_seq_set_step(p, i0, i1 & 0xff, (i1 >> 8) & 1,
                               (i1 >> 9) & 1, d); break;
        case OP.SEQ_LEN:    x.para3_seq_set_length(p, i0); break;
        case OP.SEQ_COMMIT: x.para3_seq_commit(p); break;
        case OP.MIDI_CC:    x.para3_midi_cc(p, i0, d); break;
        case OP.SET_LFO_SHAPE: x.para3_set_lfo_shape(p, i0); break;
        default: break;
      }
      r = (r + 1) | 0;
    }
    Atomics.store(i32, 1, r);                        // release slots
  }

  process(_in, outputs) {
    if (!this.ready) return true;
    this._drain();
    const out = outputs[0];
    const n = out[0].length;
    this.x.para3_render(this.p, this.bufPtr, n);
    const heap = new Float32Array(this.memory.buffer, this.bufPtr, n);
    for (let c = 0; c < out.length; c++) out[c].set(heap.subarray(0, n));
    return true;
  }
}

registerProcessor('para3-processor', Para3Processor);
