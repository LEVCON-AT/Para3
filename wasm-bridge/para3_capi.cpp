// =============================================================================
//  PARA-3 :: C-API bridge implementation
//  Wraps the verified ParaEngine + Controller. RT-safe; allocation only in
//  para3_create (engine delay buffer, pattern bank) — never in para3_render.
// =============================================================================
#include "para3_capi.h"
#include "Para3Engine.hpp"
#include <new>

struct Para3 {
    para3::ParaEngine engine;
    para3::Controller ctrl;
};

extern "C" {

Para3* para3_create(double sr, int maxBlock) {
    Para3* p = new (std::nothrow) Para3();              // create-time only
    if (!p) return nullptr;
    p->engine.prepare(sr, maxBlock);
    p->ctrl.prepare(p->engine, sr);
    return p;
}
void para3_destroy(Para3* p) { delete p; }
void para3_reset(Para3* p)   { if (p) p->engine.reset(); }

static para3::ParaEngine::Param mapParam(int id) {
    using P = para3::ParaEngine::Param;
    switch (id) {
        case PARA3_P_RESONANCE:        return P::Resonance;
        case PARA3_P_DRIVE:            return P::Drive;
        case PARA3_P_LFO_CUT_DEPTH:    return P::LfoCutDepth;
        case PARA3_P_DELAY_MIX:        return P::DelayMix;
        case PARA3_P_LFO_RATE:         return P::LfoRate;
        case PARA3_P_LFO_PITCH_DEPTH:  return P::LfoPitchDepth;
        case PARA3_P_DELAY_TIME:       return P::DelayTime;
        case PARA3_P_DELAY_FEEDBACK:   return P::DelayFeedback;
        case PARA3_P_ATTACK:           return P::Attack;
        case PARA3_P_DECREL:           return P::DecRel;
        case PARA3_P_SUSTAIN:          return P::Sustain;
        case PARA3_P_EG_CUT_DEPTH:     return P::EgCutDepth;   // E1.1
        case PARA3_P_DETUNE:           return P::Detune;       // E2.1
        case PARA3_P_PORTAMENTO:       return P::Portamento;   // E2.2
        case PARA3_P_VOLUME:           return P::Volume;       // E6.1
        default:                       return P::Cutoff;
    }
}
static para3::ParaAllocator::Mode mapMode(int m) {
    using M = para3::ParaAllocator::Mode;
    switch (m) {
        case PARA3_M_UNISON:   return M::Unison;
        case PARA3_M_OCTAVE:   return M::Octave;
        case PARA3_M_FIFTH:    return M::Fifth;
        case PARA3_M_UNIRING:  return M::UniRing;
        case PARA3_M_POLYRING: return M::PolyRing;
        default:               return M::Poly;
    }
}

void para3_set_param(Para3* p, int id, double n) {
    if (p) p->engine.setParamNorm(mapParam(id), n);
}
void para3_set_mode(Para3* p, int m)  { if (p) p->engine.setMode(mapMode(m)); }
void para3_set_lfo_shape(Para3* p, int s) {
    if (!p) return;
    using S = para3::Lfo::Shape;
    S sh = (s == 1) ? S::Triangle : (s == 2) ? S::Saw
         : (s == 3) ? S::Square   : S::Sine;
    p->engine.setLfoShape(sh);
}
// EXT-ARP Block A: keyboard/MIDI notes now route through Controller::midiNoteOn/Off
// so the arp can transform them when enabled. When arpEnabled_=false (default)
// Controller::midiNoteOn calls eng_->noteOn(n) directly — same as before, with
// one extra function-call hop the optimiser inlines. T27a proves bit-identity.
void para3_note_on (Para3* p, int n)  { if (p) p->ctrl.midiNoteOn(n);  }
void para3_note_off(Para3* p, int n)  { if (p) p->ctrl.midiNoteOff(n); }
void para3_set_lfo_sync(Para3* p, int on) { if (p) p->engine.setLfoSync(on != 0); } // E1.2
void para3_set_octave  (Para3* p, int oct){ if (p) p->engine.setOctave(oct); }       // E6.2

// EXT-ARP Block A: route tempo through Controller::setSeqTempo so arp's own
// step-samples accumulator is refreshed on every BPM change (spec §2.3 / §1.4).
void para3_seq_set_tempo (Para3* p, double bpm)  { if (p) p->ctrl.setSeqTempo(bpm,4); }
void para3_seq_set_swing (Para3* p, double s)    { if (p) p->ctrl.clock().setSwing(s); }
void para3_seq_start     (Para3* p)              { if (p) p->ctrl.seqStart(); } // B4 restart-from-0
// B2-fix: Stop must panic the engine BEFORE halting the clock. Otherwise any
// voice the sequencer started with a gate-on step that hasn't reached its
// matching gate-off step yet stays gated forever (the observed "stuck note"
// when the user clicks Stop). The order matters — engine.allNotesOff first
// so the envelope's Release stage begins; clock.stop after so no further
// step events are scheduled. Click-free via the existing envelope logic.
void para3_seq_stop      (Para3* p)              {
    if (!p) return;
    p->engine.allNotesOff();
    p->ctrl.clock().stop();
}
void para3_seq_arm_record(Para3* p, int on)      { if (p) p->ctrl.armRecord(on != 0); }

void para3_seq_set_step(Para3* p, int idx, int note, int gate,
                        int motionOn, double motionCut) {
    if (!p || idx < 0 || idx >= 16) return;
    para3::Step& s = p->ctrl.editPattern().steps[idx];   // accumulating model
    s.note      = note;
    s.gate      = gate != 0;
    s.motionOn  = motionOn != 0;
    s.motionCut = motionCut;
}
void para3_seq_set_length(Para3* p, int len) {
    if (!p) return;
    p->ctrl.editPattern().length = (len < 1) ? 1 : (len > 16 ? 16 : len);
}
void para3_seq_commit(Para3* p)        { if (p) p->ctrl.commitEdit(); }
int  para3_seq_current_step(Para3* p)  { return p ? p->ctrl.currentStep() : -1; }

void para3_seq_motion_set(Para3* p, int pid, int step, double v) {
    if (p) p->ctrl.motionSet(pid, step, v);
}
void para3_seq_motion_lane_commit(Para3* p, int pid, const double* v16) {
    if (p && v16) p->ctrl.motionLaneCommit(pid, v16);
}
void para3_seq_motion_smooth(Para3* p, int on) { if (p) p->ctrl.motionSmooth(on!=0); }
void para3_seq_motion_rec(Para3* p, int pid, int on) {
    if (p) p->ctrl.motionRec(pid, on!=0);
}
void para3_seq_motion_val(Para3* p, int pid, double v) {
    if (p) p->ctrl.motionVal(pid, v);
}
long para3_seq_motion_rejects(Para3* p) { return p ? p->ctrl.motionRejects() : 0; }

void para3_seq_step_trigger(Para3* p, int on)        { if (p) p->ctrl.setStepTrigger(on!=0); }
void para3_seq_tempo_div   (Para3* p, int d)         { if (p) p->ctrl.setTempoDiv(d); }
void para3_seq_active_step (Para3* p, int i, int on) { if (p) p->ctrl.setActiveStep(i, on!=0); }
void para3_seq_metronome   (Para3* p, int on)        { if (p) p->ctrl.setMetro(on!=0); }

void para3_seq_flux_mode    (Para3* p, int on)            { if (p) p->ctrl.setFluxMode(on!=0); }
void para3_seq_flux_loop_len(Para3* p, unsigned int s)    { if (p) p->ctrl.fluxSetLoopLen(s); }
void para3_seq_flux_rec     (Para3* p, int on)            { if (p) p->ctrl.fluxRec(on!=0); }
void para3_seq_flux_note    (Para3* p, int n, int on)     { if (p) p->ctrl.fluxNote(n,on!=0); }
void para3_seq_flux_param   (Para3* p, int pid, double n) { if (p) p->ctrl.fluxParam(pid, n); }
void para3_seq_flux_clear   (Para3* p)                    { if (p) p->ctrl.fluxClear(); }
void para3_seq_flux_quantize(Para3* p, int on)            { if (p) p->ctrl.setFluxQuantize(on!=0); }
void para3_seq_flux_commit  (Para3* p)                    { if (p) p->ctrl.fluxCommit(); }
long para3_seq_flux_dropped (Para3* p) { return p ? p->ctrl.fluxDropped() : 0; }

void para3_midi_cc(Para3* p, int cc, double n) { if (p) p->ctrl.midiCC(cc, n); }

// EXT-ARP Block A: controller-level settings (NOT in setParamNorm trichter —
// no taper, no smoothing). Each call is RT-safe (one assignment + optional
// step-samples recalc). All defaults are design defaults, NOT CALIB(E8).
void para3_arp_enable (Para3* p, int on)       { if (p) p->ctrl.setArpEnabled(on != 0); }
void para3_arp_mode   (Para3* p, int mode)     { if (p) p->ctrl.setArpMode(mode); }
void para3_arp_rate   (Para3* p, int rate)     { if (p) p->ctrl.setArpRate(rate); }
void para3_arp_gate   (Para3* p, double g)     { if (p) p->ctrl.setArpGate(g); }
void para3_arp_octaves(Para3* p, int oct)      { if (p) p->ctrl.setArpOctaves(oct); } // EXT-ARP Block B
void para3_arp_hold   (Para3* p, int on)       { if (p) p->ctrl.setArpHold(on != 0); } // EXT-ARP Block C
void para3_arp_seed   (Para3* p, unsigned int s){ if (p) p->ctrl.setArpSeed(s); }      // EXT-ARP Block C
long para3_arp_dropped(Para3* p)               { return p ? p->ctrl.arpDropped() : 0; } // EXT-ARP Block C

void para3_render(Para3* p, float* out, int n) {
    if (!p) return;
    p->ctrl.render(out, n);                              // RT-safe path
}

} // extern "C"
