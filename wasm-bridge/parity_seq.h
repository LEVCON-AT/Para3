// =============================================================================
//  PARA-3 :: shared parity scenario (single source of truth, Phase A3)
//
//  The native driver (parity_native.cpp) and the WASM driver (wasm_parity.mjs)
//  consume the SAME op stream so the comparison is meaningful. Edit only here.
//  Keep the scenario deterministic (no time, no randomness, fixed params).
// =============================================================================
#ifndef PARA3_PARITY_SEQ_H
#define PARA3_PARITY_SEQ_H

#include "para3_capi.h"

#define PARITY_SR     48000.0
#define PARITY_BLOCK  128
#define PARITY_FRAMES 48000      // exactly 1 second

// op kinds (kept narrow on purpose — only what the scenario uses)
enum {
    PARITY_OP_SET_PARAM = 1,
    PARITY_OP_SET_MODE  = 2,
    PARITY_OP_NOTE_ON   = 3,
    PARITY_OP_NOTE_OFF  = 4,
};

typedef struct {
    int    op;
    int    i0;        // param id / mode / midi note
    double d;         // norm01 for SET_PARAM, ignored otherwise
} ParityOp;

// Fixed-parameter scenario: poly, cutoff 0.55, reso 0.30, drive 0.40,
// lfo->cut 0.20, delay-mix 0.25; note 57 played for the full second.
// Note: legato — note_off is NOT issued; we measure the steady playing region.
static const ParityOp PARITY_OPS[] = {
    { PARITY_OP_SET_MODE,  PARA3_M_POLY,         0.0  },
    { PARITY_OP_SET_PARAM, PARA3_P_CUTOFF,       0.55 },
    { PARITY_OP_SET_PARAM, PARA3_P_RESONANCE,    0.30 },
    { PARITY_OP_SET_PARAM, PARA3_P_DRIVE,        0.40 },
    { PARITY_OP_SET_PARAM, PARA3_P_LFO_CUT_DEPTH,0.20 },
    { PARITY_OP_SET_PARAM, PARA3_P_DELAY_MIX,    0.25 },
    { PARITY_OP_NOTE_ON,   57,                   0.0  },
};
static const int PARITY_OPS_N =
    (int)(sizeof(PARITY_OPS) / sizeof(PARITY_OPS[0]));

#ifdef __cplusplus
static inline void parity_apply(Para3* p, const ParityOp* o) {
    switch (o->op) {
        case PARITY_OP_SET_PARAM: para3_set_param(p, o->i0, o->d); break;
        case PARITY_OP_SET_MODE:  para3_set_mode (p, o->i0);       break;
        case PARITY_OP_NOTE_ON:   para3_note_on  (p, o->i0);       break;
        case PARITY_OP_NOTE_OFF:  para3_note_off (p, o->i0);       break;
        default: break;
    }
}
#endif

#endif // PARA3_PARITY_SEQ_H
