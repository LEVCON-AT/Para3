// =============================================================================
//  PARA-3 :: SAB-free control transport (postMessage)  (Phase B1)
//
//  Same protocol semantics as Para3Ring (identical OP codes, identical
//  packed-step bits, identical exact-double payload), but the transport is the
//  AudioWorkletNode.port instead of a SharedArrayBuffer. This is the path that
//  works on an OFFLINE-INSTALLED PWA on mobile, where COOP/COEP — and therefore
//  SAB — are not available. The audio data path (wasm in the worklet) does
//  not need SAB; only the control path does, and a postMessage at control rate
//  is well within budget.
//
//  Both Para3Ring and Para3Port expose the same high-level API
//  (noteOn/noteOff/setParam/...). Para3Controls treats them interchangeably.
//  The worklet drains BOTH (ring when present, port queue always) through ONE
//  shared dispatch — see para3-worklet.js (process()).
// =============================================================================

import { OP } from './para3-ring.js';     // single source of truth for OP codes

export class Para3Port {
  // `port` is anything with .postMessage({op,i0,i1,d}): in the browser this is
  // the AudioWorkletNode.port; in port_test.mjs it's MessageChannel.port1.
  constructor(port) { this.port = port; }

  // Wire-format: a plain object. Structured-clone copies fields by value, and
  // JS numbers are 64-bit floats end-to-end, so the double round-trip is exact.
  _push(op, i0, i1, dval) {
    this.port.postMessage({ op, i0: i0 | 0, i1: i1 | 0, d: +dval });
    return true;   // postMessage has no bounded backpressure to surface here
  }

  // ---- identical surface to Para3Ring (so Para3Controls is transport-blind) -
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
    const packed = (note & 0xff) | ((gate ? 1 : 0) << 8)
                                 | ((motionOn ? 1 : 0) << 9);
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
}

// ---- worklet-side queue protocol (consumed in process(), pre-render) -------
// Worklet:    this.port.onmessage = (e) => para3PortPush(this._portQ, e.data);
// And in process(), call para3PortDrain(this._portQ, (op,i0,i1,d) => dispatch())
// para3PortDrain calls cb for every message and then resets the queue length
// (single-consumer; main thread only ever appends, never reads).
export function para3PortPush(queue, msg) {
  // Filter to protocol messages: must have a numeric op field. This rejects
  // the worklet's own outgoing { type:'ready' } if it ever bounced back.
  if (msg && typeof msg === 'object' && Number.isInteger(msg.op))
    queue.push(msg);
}

export function para3PortDrain(queue, cb) {
  // process() and port.onmessage cannot overlap (Web Audio spec; Node event
  // loop is single-threaded). The queue is therefore stable for the duration
  // of this loop, and a wholesale clear at the end is safe and allocation-free.
  for (let i = 0; i < queue.length; i++) {
    const m = queue[i];
    cb(m.op, m.i0 | 0, m.i1 | 0, +m.d);
  }
  queue.length = 0;
}
