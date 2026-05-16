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
