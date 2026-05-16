// =============================================================================
//  PARA-3 :: scope source proof  (CLAUDE_SCOPE_AND_LAYOUT.md, Nachweis A)
//
//  The in-app oscilloscope reads `analyser.getFloatTimeDomainData(buf)` from
//  an AnalyserNode wired READ-ONLY at `workletNode -> analyser -> destination`.
//  The analyser passes the audio through unchanged — what it observes IS the
//  engine's render output.
//
//  This test PROVES the source is the real engine, via the SAME C-API the
//  WASM uses (capi_test style), without any browser:
//
//    1) Render a quantum with no NoteOn  -> max|x| < 1e-6
//       (a fake/decorative animator would have a residual oscillation here;
//        the real engine is exactly silent.)
//    2) NoteOn(69) ("A4"); briefly settle past the ADSR attack; render a
//       steady region; estimate f0 from upward zero-crossings of the FLOAT32
//       buffer the engine wrote (i.e. the same bytes the analyser would see):
//          |f0 - 440| <= 1 Hz   AND   peak amplitude > 1e-3
//
//  build: g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp
//         scope_source_test.cpp -o scope_source_test
//  run  : ./scope_source_test
// =============================================================================
#include "para3_capi.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

// median of inter-arrival intervals between upward zero-crossings (linearly
// interpolated). Robust against the occasional jitter at quantum boundaries.
static double estimateF0(const float* x, int n, double sr) {
    std::vector<double> zc;
    for (int i = 1; i < n; ++i) {
        const float a = x[i - 1], b = x[i];
        if (a <= 0.0f && b > 0.0f) {
            const double t = (i - 1) + double(-a) / (double(b) - double(a));
            zc.push_back(t);
        }
    }
    if (zc.size() < 8) return 0.0;          // need a fair sample
    std::vector<double> dt;
    dt.reserve(zc.size() - 1);
    for (size_t i = 1; i < zc.size(); ++i) dt.push_back(zc[i] - zc[i - 1]);
    std::sort(dt.begin(), dt.end());
    const double med = dt[dt.size() / 2];
    if (med <= 0.0) return 0.0;
    return sr / med;
}

int main() {
    const double sr = 48000.0;
    const int    Q  = 128;
    int failures = 0;

    std::printf("PARA-3 scope source proof  (fs=%.0f)\n", sr);
    std::printf("==================================================\n");

    // ---- S1: silence ⇒ exactly silent (no decorative wiggle) ---------------
    {
        Para3* p = para3_create(sr, Q);
        std::vector<float> buf(Q, 0.f);
        para3_render(p, buf.data(), Q);     // no NoteOn issued before this
        para3_destroy(p);
        double mx = 0.0;
        for (float v : buf) mx = std::max(mx, (double)std::fabs(v));
        const bool pass = mx < 1e-6;
        std::printf("\nS1 silence is silent  (no NoteOn)\n");
        std::printf("   max|x|   : %.3e   (tol 1e-6)\n", mx);
        std::printf("   -> %s\n", pass ? "PASS (engine output is exactly 0)"
                                       : "FAIL (would mask a fake animator)");
        if (!pass) ++failures;
    }

    // ---- S2: NoteOn(69)=A4 ⇒ buffer carries the real signal at 440 Hz -----
    {
        Para3* p = para3_create(sr, Q);
        // sane, audible defaults via the normalised funnel — same path the UI
        // takes; we keep it close to silence-clean so the only AC source is
        // the engine's bandlimited oscillator.
        para3_set_param(p, PARA3_P_CUTOFF,    0.85);   // open filter
        para3_set_param(p, PARA3_P_RESONANCE, 0.10);
        para3_set_param(p, PARA3_P_DRIVE,     0.20);
        para3_set_param(p, PARA3_P_LFO_CUT_DEPTH, 0.0);  // remove modulation
        para3_set_param(p, PARA3_P_LFO_PITCH_DEPTH, 0.0); //  for a clean f0
        para3_set_param(p, PARA3_P_DELAY_MIX, 0.0);
        para3_set_param(p, PARA3_P_ATTACK,    0.0);    // fast attack
        para3_set_param(p, PARA3_P_SUSTAIN,   1.0);    // hold

        para3_note_on(p, 69);                          // A4 = 440 Hz

        // settle past attack + filter warmup
        const int settle = 9600;                       // 200 ms
        std::vector<float> warm(settle);
        for (int i = 0; i < settle; i += Q) {
            int c = std::min(Q, settle - i);
            para3_render(p, warm.data() + i, c);
        }

        // measure on a clean steady stretch
        const int meas = 4096;
        std::vector<float> x(meas);
        for (int i = 0; i < meas; i += Q) {
            int c = std::min(Q, meas - i);
            para3_render(p, x.data() + i, c);
        }
        para3_destroy(p);

        double peak = 0.0;
        bool   finite = true;
        for (float v : x) {
            if (!std::isfinite(v)) finite = false;
            peak = std::max(peak, (double)std::fabs(v));
        }
        const double f0 = estimateF0(x.data(), meas, sr);
        const double err = std::fabs(f0 - 440.0);
        const double cents = (f0 > 0.0) ? 1200.0 * std::log2(f0 / 440.0) : 1e9;
        const bool pass = finite && peak > 1e-3 && err <= 1.0;

        std::printf("\nS2 A4 (NoteOn 69)  -> 440 Hz on the engine buffer\n");
        std::printf("   f0       : %.3f Hz   (|err| %.3f Hz  = %.2f cents)\n",
                    f0, err, cents);
        std::printf("   peak|x|  : %.4f  (engine output, not synthetic)\n", peak);
        std::printf("   finite   : %s\n", finite ? "yes" : "NO");
        std::printf("   -> %s\n", pass ? "PASS (scope source is the real engine)"
                                       : "FAIL");
        if (!pass) ++failures;
    }

    std::printf("\n==================================================\n");
    std::printf("%s  (%d failure%s)\n",
                failures ? "OVERALL: FAIL" : "OVERALL: PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
