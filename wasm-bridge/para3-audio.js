// =============================================================================
//  PARA-3 :: main-thread integration
//
//  Para3Controls — pure, browser-agnostic UI-facing API. Every call pushes one
//  protocol message into the lock-free ring (the same single funnel the engine
//  enforces). Testable headless (audio_test.mjs drives it and decodes the ring
//  exactly as the worklet does).
//
//  Para3Audio — browser wrapper: AudioContext + AudioWorklet + wasm wiring.
//  Imports cleanly in Node (class only constructs Web Audio objects in start()).
// =============================================================================

import { Para3Ring } from './para3-ring.js';

export const PARAM = Object.freeze({
  CUTOFF: 0, RESONANCE: 1, DRIVE: 2, LFO_CUT_DEPTH: 3, DELAY_MIX: 4,
  LFO_RATE: 5, LFO_PITCH_DEPTH: 6, DELAY_TIME: 7, DELAY_FEEDBACK: 8,
  ATTACK: 9, DECREL: 10, SUSTAIN: 11,
  // KORG-parity (E1.1, E2.1, E2.2, E6.1): bipolar EG INT centres at norm=0.5.
  EG_CUT_DEPTH: 12, DETUNE: 13, PORTAMENTO: 14, VOLUME: 15,
  // EXT-ARP-MOTION: Controller-level discrete param (0..4). Lane 0..1 maps
  // to mode 0..4 via floor(v*5) in Controller::applyMotionParam_().
  ARP_MODE: 16,
});
export const LFO_SHAPE = Object.freeze({ SINE:0, TRIANGLE:1, SAW:2, SQUARE:3 });
export const MODE = Object.freeze({
  POLY: 0, UNISON: 1, OCTAVE: 2, FIFTH: 3, UNIRING: 4, POLYRING: 5,
});

// UI -> ring. Control-rate; if the ring is momentarily full we retry a tiny
// bounded amount then count a drop (observable, never blocks the UI thread).
export class Para3Controls {
  constructor(ring) { this.ring = ring; this.dropped = 0; }
  _do(fn) {
    for (let i = 0; i < 64; i++) if (fn()) return true;
    this.dropped++; return false;
  }
  noteOn(note)              { return this._do(() => this.ring.noteOn(note)); }
  noteOff(note)             { return this._do(() => this.ring.noteOff(note)); }
  setParam(id, norm01)      { return this._do(() => this.ring.setParam(id, norm01)); }
  setMode(mode)             { return this._do(() => this.ring.setMode(mode)); }
  midiCC(cc, norm01)        { return this._do(() => this.ring.midiCC(cc, norm01)); }
  setLfoShape(shape)        { return this._do(() => this.ring.setLfoShape(shape)); }
  setLfoSync(on)            { return this._do(() => this.ring.setLfoSync(on)); }
  // E3 — KORG-parity motion (target-parametric). PEAK/TEMPO are rejected
  // engine-side (observable via para3_seq_motion_rejects in the native tests).
  seqMotion(paramId, step, v01)
                            { return this._do(() => this.ring.seqMotion(paramId, step, v01)); }
  seqMotionSmooth(on)       { return this._do(() => this.ring.seqMotionSmooth(on)); }
  seqMotionRec(paramId, on) { return this._do(() => this.ring.seqMotionRec(paramId, on)); }
  seqMotionVal(paramId, v01){ return this._do(() => this.ring.seqMotionVal(paramId, v01)); }
  // E4 — sequencer behaviours (engine T19–T22; metronome bypasses delay).
  seqStepTrigger(on)        { return this._do(() => this.ring.seqStepTrigger(on)); }
  seqTempoDiv(div)          { return this._do(() => this.ring.seqTempoDiv(div)); }
  seqActiveStep(idx, on)    { return this._do(() => this.ring.seqActiveStep(idx, on)); }
  seqMetronome(on)          { return this._do(() => this.ring.seqMetronome(on)); }
  // E5 — FLUX (sample-accurate event sequence). REC arms append-on-noteOn;
  // engine guards via fluxRec_ so calling seqFluxNote outside REC is a no-op.
  seqFluxMode(on)           { return this._do(() => this.ring.seqFluxMode(on)); }
  seqFluxLoopLen(samples)   { return this._do(() => this.ring.seqFluxLoopLen(samples)); }
  seqFluxRec(on)            { return this._do(() => this.ring.seqFluxRec(on)); }
  seqFluxNote(note, on)     { return this._do(() => this.ring.seqFluxNote(note, on)); }
  // EXT-FLUX — param events recorded into the FLUX bank (sample-accurate at
  // replay). pid 0..15 = PARAM ids (Cutoff..Volume). Used while fluxRecOn is
  // true (engine guards via fluxRec_, calls outside REC are no-ops).
  seqFluxParam(pid, norm01) { return this._do(() => this.ring.seqFluxParam(pid, norm01)); }
  seqFluxClear()            { return this._do(() => this.ring.seqFluxClear()); }
  seqFluxCommit()           { return this._do(() => this.ring.seqFluxCommit()); }
  // E6.2 — engine pitch offset (integer semitones × 12). Replaces host-side
  // +oct*12 in midiOfKey so the engine owns the pitch path (band-limited).
  setOctave(oct)            { return this._do(() => this.ring.setOctave(oct)); }
  // EXT-ARP — controller settings (no taper trichter; mirrors seqTempo/seqSwing).
  arpEnable(on)             { return this._do(() => this.ring.arpEnable(on)); }
  arpMode(m)                { return this._do(() => this.ring.arpMode(m)); }
  arpRate(r)                { return this._do(() => this.ring.arpRate(r)); }
  arpGate(g01)              { return this._do(() => this.ring.arpGate(g01)); }
  arpOctaves(o)             { return this._do(() => this.ring.arpOctaves(o)); }
  arpHold(on)               { return this._do(() => this.ring.arpHold(on)); }
  arpSeed(seed)             { return this._do(() => this.ring.arpSeed(seed)); }
  seqTempo(bpm)             { return this._do(() => this.ring.seqTempo(bpm)); }
  seqSwing(s01)             { return this._do(() => this.ring.seqSwing(s01)); }
  seqStart()                { return this._do(() => this.ring.seqStart()); }
  seqStop()                 { return this._do(() => this.ring.seqStop()); }
  seqArmRecord(on)          { return this._do(() => this.ring.seqArmRecord(on)); }
  seqLength(len)            { return this._do(() => this.ring.seqLength(len)); }
  seqCommit()               { return this._do(() => this.ring.seqCommit()); }
  seqStep(idx, note, gate, motionOn = false, motionCut = 0.5) {
    return this._do(() => this.ring.seqStep(idx, note, gate, motionOn, motionCut));
  }
}

export class Para3Audio {
  constructor() { this.ctx = null; this.node = null; this.controls = null; }

  // Must be called from a user gesture (AudioContext autoplay policy).
  async start({ wasmUrl, workletUrl = './para3-worklet.js',
                ringUrl = './para3-ring.js', ringCapacity = 1024 } = {}) {
    if (typeof AudioContext === 'undefined' &&
        typeof webkitAudioContext === 'undefined')
      throw new Error('Para3Audio.start requires a browser AudioContext');

    const Ctx = (typeof AudioContext !== 'undefined')
      ? AudioContext : webkitAudioContext;
    this.ctx = new Ctx({ latencyHint: 'interactive' });

    const ring = Para3Ring.create(ringCapacity);

    // compile wasm on the main thread, hand the Module to the worklet
    const bytes = await fetch(wasmUrl).then((r) => r.arrayBuffer());
    const wasmModule = await WebAssembly.compile(bytes);

    await this.ctx.audioWorklet.addModule(workletUrl);

    const ready = new Promise((resolve, reject) => {
      this._resolveReady = resolve; this._rejectReady = reject;
    });
    this.node = new AudioWorkletNode(this.ctx, 'para3-processor', {
      numberOfInputs: 0,
      outputChannelCount: [2],
      processorOptions: {
        wasmModule,
        ringSAB: ring.sab,
        ringCap: ringCapacity,
        sampleRate: this.ctx.sampleRate,
        maxBlock: 128,
      },
    });
    this.node.port.onmessage = (e) => {
      if (e.data && e.data.type === 'ready') this._resolveReady();
      else this._rejectReady(new Error('para3 worklet init failed'));
    };
    this.node.connect(this.ctx.destination);
    this.controls = new Para3Controls(ring);
    await ready;
    return this;
  }

  resume() { return this.ctx && this.ctx.resume(); }
  suspend() { return this.ctx && this.ctx.suspend(); }
}
