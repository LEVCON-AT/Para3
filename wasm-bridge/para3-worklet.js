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
  SET_LFO_SYNC:15,           // E1.2 KORG-parity
  SEQ_MOTION_SET:16,         // E3 — i0=paramId i1=stepIdx d=v01
  SEQ_MOTION_SMOOTH:17,      // E3 — i0=on
  SEQ_MOTION_REC:18,         // E3 — i0=paramId i1=on
  SEQ_MOTION_VAL:19,         // E3 — i0=paramId d=v01
  SEQ_STEP_TRIGGER:20,       // E4 — i0=on
  SEQ_TEMPO_DIV:21,          // E4 — i0=div (1|2|4)
  SEQ_ACTIVE_STEP:22,        // E4 — i0=idx i1=enabled
  SEQ_METRONOME:23,          // E4 — i0=on
  SEQ_FLUX_MODE:24,          // E5 — i0=on
  SEQ_FLUX_LOOP_LEN:25,      // E5 — i0=samples
  SEQ_FLUX_REC:26,           // E5 — i0=on
  SEQ_FLUX_NOTE:27,          // E5 — i0=note i1=on
  SEQ_FLUX_COMMIT:28,        // E5
  SET_OCTAVE:29,             // E6.2 — i0=oct
  ARP_ENABLE:30, ARP_MODE:31, ARP_RATE:32, ARP_GATE:33,    // EXT-ARP
  ARP_OCTAVES:34, ARP_HOLD:35, ARP_SEED:36,                 // EXT-ARP
  SEQ_FLUX_PARAM:37,         // EXT-FLUX — i0=pid (0..15) d=norm01
  SEQ_FLUX_CLEAR:38,         // EXT-FLUX — drop queued+published events
  SEQ_FLUX_QUANTIZE:39,      // EXT-FLUX — i0=on (1/16 snap; off=F·FINE)
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

    // STANDALONE_WASM=1: the wasm module EXPORTS its own memory and does not
    // import one. We still pass a placeholder Memory to makeImports() because
    // env.memory is the standard shape, but after instantiation we MUST use
    // exports.memory — otherwise our heap views read an empty, unused buffer
    // (silent engine, flat scope). scope_source_test would catch this natively;
    // browser-side the symptom is "tap to start works, but no audio".
    const placeholderMem = new WebAssembly.Memory({ initial: 512 });
    const inst = new WebAssembly.Instance(o.wasmModule, makeImports(placeholderMem));
    this.x = inst.exports;
    this.memory = this.x.memory || placeholderMem;

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
        case OP.SET_LFO_SYNC:  x.para3_set_lfo_sync(p, i0); break;   // E1.2
        case OP.SEQ_MOTION_SET:    x.para3_seq_motion_set(p, i0, i1, d); break; // E3
        case OP.SEQ_MOTION_SMOOTH: x.para3_seq_motion_smooth(p, i0); break;     // E3
        case OP.SEQ_MOTION_REC:    x.para3_seq_motion_rec(p, i0, i1); break;    // E3
        case OP.SEQ_MOTION_VAL:    x.para3_seq_motion_val(p, i0, d); break;     // E3
        case OP.SEQ_STEP_TRIGGER:  x.para3_seq_step_trigger(p, i0); break;      // E4.1
        case OP.SEQ_TEMPO_DIV:     x.para3_seq_tempo_div(p, i0); break;         // E4.2
        case OP.SEQ_ACTIVE_STEP:   x.para3_seq_active_step(p, i0, i1); break;   // E4.3
        case OP.SEQ_METRONOME:     x.para3_seq_metronome(p, i0); break;         // E4.4
        case OP.SEQ_FLUX_MODE:     x.para3_seq_flux_mode(p, i0); break;         // E5
        case OP.SEQ_FLUX_LOOP_LEN: x.para3_seq_flux_loop_len(p, i0); break;     // E5
        case OP.SEQ_FLUX_REC:      x.para3_seq_flux_rec(p, i0); break;          // E5
        case OP.SEQ_FLUX_NOTE:     x.para3_seq_flux_note(p, i0, i1); break;     // E5
        case OP.SEQ_FLUX_PARAM:    x.para3_seq_flux_param(p, i0, d); break;     // EXT-FLUX
        case OP.SEQ_FLUX_CLEAR:    x.para3_seq_flux_clear(p); break;            // EXT-FLUX
        case OP.SEQ_FLUX_QUANTIZE: x.para3_seq_flux_quantize(p, i0); break;     // EXT-FLUX
        case OP.SEQ_FLUX_COMMIT:   x.para3_seq_flux_commit(p); break;           // E5
        case OP.SET_OCTAVE:        x.para3_set_octave(p, i0); break;            // E6.2
        case OP.ARP_ENABLE:        x.para3_arp_enable (p, i0); break;           // EXT-ARP
        case OP.ARP_MODE:          x.para3_arp_mode   (p, i0); break;           // EXT-ARP
        case OP.ARP_RATE:          x.para3_arp_rate   (p, i0); break;           // EXT-ARP
        case OP.ARP_GATE:          x.para3_arp_gate   (p, d ); break;           // EXT-ARP
        case OP.ARP_OCTAVES:       x.para3_arp_octaves(p, i0); break;           // EXT-ARP
        case OP.ARP_HOLD:          x.para3_arp_hold   (p, i0); break;           // EXT-ARP
        case OP.ARP_SEED:          x.para3_arp_seed   (p, i0); break;           // EXT-ARP
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
