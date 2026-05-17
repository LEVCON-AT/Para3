// =============================================================================
//  PARA-3 :: lock-free SPSC ring over SharedArrayBuffer  (+ control protocol)
//
//  Single producer (main/UI thread) -> single consumer (AudioWorklet thread).
//  Wait-free on both sides: no locks, no allocation in push/pop, the audio
//  thread never blocks. Synchronisation via Atomics (sequentially consistent),
//  which establishes the happens-before between the data writes and the index
//  publish — the textbook correct SPSC pattern.
//
//  Layout (one SharedArrayBuffer):
//    Int32[0] = write index (producer-owned, monotonic)
//    Int32[1] = read  index (consumer-owned, monotonic)
//    Int32[2 ..]                = slot data, capacity * SLOT_INTS int32s
//
//  capacity must be a power of two. Full  <=> (write - read) === capacity.
//  Empty <=> write === read. 32-bit monotonic indices (control-rate traffic
//  is far below any wrap concern within a session).
// =============================================================================

export const SLOT_INTS = 6;   // [op, i0, i1, dHi, dLo, pad]  (double via 2x i32)

export const OP = Object.freeze({
  NOTE_ON:    1,
  NOTE_OFF:   2,
  SET_PARAM:  3,   // i0 = paramId,  double = norm01
  SET_MODE:   4,   // i0 = mode
  SEQ_TEMPO:  5,   // double = bpm
  SEQ_SWING:  6,   // double = swing01
  SEQ_START:  7,
  SEQ_STOP:   8,
  SEQ_ARMREC: 9,   // i0 = on
  SEQ_STEP:  10,   // i0 = idx, i1 = (note | gate<<8 | motionOn<<9), double = motionCut
  SEQ_LEN:   11,   // i0 = length
  SEQ_COMMIT:12,
  MIDI_CC:   13,   // i0 = cc, double = norm01
  SET_LFO_SHAPE: 14, // i0 = shape (0 sine,1 tri,2 saw,3 square)
  SET_LFO_SYNC:  15, // i0 = on (0|1)                                 -- E1.2
  // E3 KORG-parity: target-parametric motion. PEAK (RESONANCE) is rejected
  // engine-side (para3_seq_motion_rejects increments). publish via SEQ_COMMIT.
  SEQ_MOTION_SET:    16, // i0=paramId, i1=stepIdx, d=v01
  SEQ_MOTION_SMOOTH: 17, // i0 = on (0|1)  -- global per-pattern
  SEQ_MOTION_REC:    18, // i0 = paramId, i1 = on (0|1)  -- one-loop capture
  SEQ_MOTION_VAL:    19, // i0 = paramId, d = v01  -- live capture value
  // E4 KORG-parity: sequencer behaviours.
  SEQ_STEP_TRIGGER:  20, // i0 = on (0|1)  -- EG retrigger every step
  SEQ_TEMPO_DIV:     21, // i0 = div (1|2|4)
  SEQ_ACTIVE_STEP:   22, // i0 = idx, i1 = enabled (0|1)
  SEQ_METRONOME:     23, // i0 = on (0|1)  -- bypasses delay engine-side
  // E5 KORG-parity: FLUX (sample-accurate event sequence).
  SEQ_FLUX_MODE:     24, // i0 = on (0|1)  -- click-free mode switch
  SEQ_FLUX_LOOP_LEN: 25, // i0 = samples (signed; up to ~12h @ 48k)
  SEQ_FLUX_REC:      26, // i0 = on (0|1)  -- arms event capture
  SEQ_FLUX_NOTE:     27, // i0 = note, i1 = on (0|1) -- append at live cursor
  SEQ_FLUX_COMMIT:   28, // -- stable-sort + publish
  SET_OCTAVE:        29, // E6.2 — i0 = oct (signed; typical -2..+2)
});

const HDR = 2;                       // header int32 count
const _f64 = new Float64Array(1);    // scratch for double<->2x int32 (no alloc in hot path)
const _i32 = new Int32Array(_f64.buffer);

export class Para3Ring {
  // Allocate a new ring (call on the main thread, transfer .sab to the worklet).
  static create(capacityPow2 = 1024) {
    if ((capacityPow2 & (capacityPow2 - 1)) !== 0)
      throw new Error('capacity must be a power of two');
    const ints = HDR + capacityPow2 * SLOT_INTS;
    const sab  = new SharedArrayBuffer(ints * 4);
    return new Para3Ring(sab, capacityPow2);
  }
  // Re-wrap an existing SAB (call inside the worklet with the transferred sab).
  constructor(sab, capacityPow2) {
    this.sab   = sab;
    this.cap   = capacityPow2;
    this.mask  = capacityPow2 - 1;
    this.i32   = new Int32Array(sab);
  }

  // ---- producer side (main/UI thread). Returns false if full (no block). ----
  _push(op, i0, i1, dval) {
    const w = Atomics.load(this.i32, 0);
    const r = Atomics.load(this.i32, 1);
    if ((w - r) >>> 0 >= this.cap) return false;          // full
    const base = HDR + (w & this.mask) * SLOT_INTS;
    _f64[0] = dval;
    this.i32[base + 0] = op;
    this.i32[base + 1] = i0 | 0;
    this.i32[base + 2] = i1 | 0;
    this.i32[base + 3] = _i32[0];
    this.i32[base + 4] = _i32[1];
    Atomics.store(this.i32, 0, (w + 1) | 0);              // publish (release)
    return true;
  }

  noteOn(n)            { return this._push(OP.NOTE_ON,  n, 0, 0); }
  noteOff(n)           { return this._push(OP.NOTE_OFF, n, 0, 0); }
  setParam(id, v)      { return this._push(OP.SET_PARAM, id, 0, v); }
  setMode(m)           { return this._push(OP.SET_MODE,  m, 0, 0); }
  seqTempo(bpm)        { return this._push(OP.SEQ_TEMPO, 0, 0, bpm); }
  seqSwing(s)          { return this._push(OP.SEQ_SWING, 0, 0, s); }
  seqStart()           { return this._push(OP.SEQ_START, 0, 0, 0); }
  seqStop()            { return this._push(OP.SEQ_STOP,  0, 0, 0); }
  seqArmRecord(on)     { return this._push(OP.SEQ_ARMREC, on ? 1 : 0, 0, 0); }
  seqStep(idx, note, gate, motionOn, motionCut) {
    const packed = (note & 0xff) | ((gate ? 1 : 0) << 8) | ((motionOn ? 1 : 0) << 9);
    return this._push(OP.SEQ_STEP, idx, packed, motionCut);
  }
  seqLength(len)       { return this._push(OP.SEQ_LEN, len, 0, 0); }
  seqCommit()          { return this._push(OP.SEQ_COMMIT, 0, 0, 0); }
  midiCC(cc, v)        { return this._push(OP.MIDI_CC, cc, 0, v); }
  setLfoShape(s)       { return this._push(OP.SET_LFO_SHAPE, s, 0, 0); }
  setLfoSync(on)       { return this._push(OP.SET_LFO_SYNC, on ? 1 : 0, 0, 0); }
  seqMotion(paramId, stepIdx, v01)
                       { return this._push(OP.SEQ_MOTION_SET, paramId, stepIdx, v01); }
  seqMotionSmooth(on)  { return this._push(OP.SEQ_MOTION_SMOOTH, on ? 1 : 0, 0, 0); }
  seqMotionRec(paramId, on)
                       { return this._push(OP.SEQ_MOTION_REC, paramId, on ? 1 : 0, 0); }
  seqMotionVal(paramId, v01)
                       { return this._push(OP.SEQ_MOTION_VAL, paramId, 0, v01); }
  seqStepTrigger(on)   { return this._push(OP.SEQ_STEP_TRIGGER, on ? 1 : 0, 0, 0); }
  seqTempoDiv(div)     { return this._push(OP.SEQ_TEMPO_DIV, div | 0, 0, 0); }
  seqActiveStep(idx, on)
                       { return this._push(OP.SEQ_ACTIVE_STEP, idx | 0, on ? 1 : 0, 0); }
  seqMetronome(on)     { return this._push(OP.SEQ_METRONOME, on ? 1 : 0, 0, 0); }
  seqFluxMode(on)      { return this._push(OP.SEQ_FLUX_MODE, on ? 1 : 0, 0, 0); }
  seqFluxLoopLen(samples)
                       { return this._push(OP.SEQ_FLUX_LOOP_LEN, samples | 0, 0, 0); }
  seqFluxRec(on)       { return this._push(OP.SEQ_FLUX_REC, on ? 1 : 0, 0, 0); }
  seqFluxNote(note, on){ return this._push(OP.SEQ_FLUX_NOTE, note | 0, on ? 1 : 0, 0); }
  seqFluxCommit()      { return this._push(OP.SEQ_FLUX_COMMIT, 0, 0, 0); }
  setOctave(oct)       { return this._push(OP.SET_OCTAVE, oct | 0, 0, 0); }

  // ---- consumer side (AudioWorklet thread). Wait-free drain. ----
  // cb(op, i0, i1, dval) is invoked for each pending message, in order.
  drain(cb) {
    let r = Atomics.load(this.i32, 1);
    const w = Atomics.load(this.i32, 0);                  // acquire
    let count = 0;
    while (r !== w) {
      const base = HDR + (r & this.mask) * SLOT_INTS;
      const op = this.i32[base + 0];
      const i0 = this.i32[base + 1];
      const i1 = this.i32[base + 2];
      _i32[0]  = this.i32[base + 3];
      _i32[1]  = this.i32[base + 4];
      cb(op, i0, i1, _f64[0]);
      r = (r + 1) | 0;
      ++count;
    }
    if (count) Atomics.store(this.i32, 1, r);             // release slots
    return count;
  }
}
