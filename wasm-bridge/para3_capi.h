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
    PARA3_P_SUSTAIN = 11
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

// MIDI from the host/keyboard, applied at the next rendered sample
void para3_midi_cc      (Para3* p, int cc, double norm01);

// render n frames into out (mono). out is a pointer into the WASM heap.
// real-time safe: no allocation, no locks, no syscalls.
void para3_render(Para3* p, float* out, int n);

#ifdef __cplusplus
}
#endif
#endif // PARA3_CAPI_H
