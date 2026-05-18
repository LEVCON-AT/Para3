// =============================================================================
//  PARA-3 :: C-API bridge  (WASM / AudioWorklet surface)
//
//  Thin, real-time-safe extern "C" surface over the verified engine. This is
//  the exact translation unit Emscripten compiles; it is also compiled and
//  measured natively (capi_test.cpp) so the bridge logic is proven without a
//  browser. No allocation / lock / exception in the render path; all memory is
//  acquired in para3_create() and released in para3_destroy().
//
//  Emscripten export names are the C names prefixed with '_'
//  (e.g. _para3_create). See build_wasm.sh (W-Sprint 2).
// =============================================================================
#ifndef PARA3_CAPI_H
#define PARA3_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Para3 Para3;

// lifecycle (allocation happens here only, never in render)
Para3* para3_create(double sample_rate, int max_block);
void   para3_destroy(Para3* p);
void   para3_reset(Para3* p);

// param ids mirror ParaEngine::Param
enum {
    PARA3_P_CUTOFF = 0,
    PARA3_P_RESONANCE = 1,
    PARA3_P_DRIVE = 2,
    PARA3_P_LFO_CUT_DEPTH = 3,
    PARA3_P_DELAY_MIX = 4,
    PARA3_P_LFO_RATE = 5,
    PARA3_P_LFO_PITCH_DEPTH = 6,
    PARA3_P_DELAY_TIME = 7,
    PARA3_P_DELAY_FEEDBACK = 8,
    PARA3_P_ATTACK = 9,
    PARA3_P_DECREL = 10,
    PARA3_P_SUSTAIN = 11,
    PARA3_P_EG_CUT_DEPTH = 12,      // E1.1 bipolar: norm 0.5 == 0 (centre)
    PARA3_P_DETUNE = 13,            // E2.1 unipolar
    PARA3_P_PORTAMENTO = 14,        // E2.2 unipolar, 0 = instant
    PARA3_P_VOLUME = 15             // E6.1 unipolar, 1.0 = unity
};
// voice modes mirror ParaAllocator::Mode
enum {
    PARA3_M_POLY = 0, PARA3_M_UNISON = 1, PARA3_M_OCTAVE = 2,
    PARA3_M_FIFTH = 3, PARA3_M_UNIRING = 4, PARA3_M_POLYRING = 5
};

// the unified funnel — every UI/MIDI/automation write goes here (normalized)
void para3_set_param(Para3* p, int param_id, double norm01);
void para3_set_mode (Para3* p, int mode);
void para3_set_lfo_shape(Para3* p, int shape);  // 0 sine,1 tri,2 saw,3 square
void para3_set_lfo_sync (Para3* p, int on);     // E1.2 LFO trigger sync (0/1)
void para3_set_octave   (Para3* p, int oct);    // E6.2 octave shift (..-2..+2..)
void para3_note_on  (Para3* p, int midi_note);
void para3_note_off (Para3* p, int midi_note);

// sequencer / transport
void para3_seq_set_tempo (Para3* p, double bpm);
void para3_seq_set_swing (Para3* p, double swing01);
void para3_seq_start     (Para3* p);
void para3_seq_stop      (Para3* p);
void para3_seq_arm_record(Para3* p, int on);
// pattern edit goes to a back buffer; commit publishes atomically (lock-free)
void para3_seq_set_step  (Para3* p, int idx, int note, int gate,
                          int motion_on, double motion_cut01);
void para3_seq_set_length(Para3* p, int length);
void para3_seq_commit    (Para3* p);
int  para3_seq_current_step(Para3* p);

// E3 motion (ziel-parametric, lock-free; commit via para3_seq_commit)
void para3_seq_motion_set        (Para3* p, int param_id, int step, double v01);
void para3_seq_motion_lane_commit(Para3* p, int param_id, const double* v01_16);
void para3_seq_motion_smooth     (Para3* p, int on);
void para3_seq_motion_rec        (Para3* p, int param_id, int on);  // one-loop capture
void para3_seq_motion_val        (Para3* p, int param_id, double v01);
long para3_seq_motion_rejects    (Para3* p);                        // observability

// E4 sequencer behaviours
void para3_seq_step_trigger(Para3* p, int on);            // E4.1 force EG every step
void para3_seq_tempo_div   (Para3* p, int div);           // E4.2 1, 2 or 4
void para3_seq_active_step (Para3* p, int idx, int on);   // E4.3 enable/skip step
void para3_seq_metronome   (Para3* p, int on);            // E4.4 metronome (delay bypass)

// E5 FLUX (sample-accurate event sequence)
void para3_seq_flux_mode     (Para3* p, int on);          // step-grid <-> flux (click-free)
void para3_seq_flux_loop_len (Para3* p, unsigned int samples);
void para3_seq_flux_rec      (Para3* p, int on);
void para3_seq_flux_note     (Para3* p, int note, int on);// append at live cursor
void para3_seq_flux_commit   (Para3* p);                  // stable-sort + publish
long para3_seq_flux_dropped  (Para3* p);                  // observable overflow

// MIDI from the host/keyboard, applied at the next rendered sample
void para3_midi_cc      (Para3* p, int cc, double norm01);

// EXT-ARP Block A — design-extension over Volca-Keys parity (NOT a Keys feature).
// Default off => engine output bit-identical to the pre-EXT build (T27a).
// Modes: 0=Up (1=Down, 2=UpDown, 3=AsPlayed, 4=Random added in Block B/C).
// Rates: index into {1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32}.
void para3_arp_enable (Para3* p, int on);                     // EXT-ARP
void para3_arp_mode   (Para3* p, int mode);                   // EXT-ARP
void para3_arp_rate   (Para3* p, int rate);                   // EXT-ARP
void para3_arp_gate   (Para3* p, double gate01);              // EXT-ARP staccato 0..1
void para3_arp_octaves(Para3* p, int oct);                    // EXT-ARP Block B 1..4

// render n frames into out (mono). out is a pointer into the WASM heap.
// real-time safe: no allocation, no locks, no syscalls.
void para3_render(Para3* p, float* out, int n);

#ifdef __cplusplus
}
#endif
#endif // PARA3_CAPI_H
