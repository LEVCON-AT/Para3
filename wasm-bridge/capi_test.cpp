// =============================================================================
//  PARA-3 :: C-API bridge — native verification
//
//  Drives the EXACT translation unit Emscripten will compile, exactly the way
//  the AudioWorklet drives it (fixed 128-frame quanta into a heap buffer).
//  Proves the bridge preserves the engine's measured quality before any
//  browser is involved. Measures, never asserts.
//
//  WA1 click-free gate through the C API
//  WA2 param funnel through the C API: finite, bounded, continuous
//  WA3 BLOCK-SIZE INVARIANCE: 128-frame blocks == one big call (bit-identical)
//      -> the core AudioWorklet-readiness proof (quantum never aligns to steps)
//  WA4 sample-accurate sequencer through the C API
//  WA5 long stress (seq + ring + max reso/drive): no NaN/Inf
//
//  build: g++ -O2 -std=c++17 -Wall -Wextra -msse2
//             para3_capi.cpp capi_test.cpp -o capi_test
// =============================================================================
#include "para3_capi.h"
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

static double maxAbsDelta(const std::vector<float>& x, int a, int b) {
    double m = 0.0;
    for (int i = a + 1; i < b; ++i) {
        m = std::max(m, (double)std::fabs(x[i] - x[i - 1]));
    }
    return m;
}

int main() {
    const double sr = 48000.0;
    const int    Q  = 128;                 // AudioWorklet render quantum
    int failures = 0;

    std::printf("PARA-3 C-API bridge verification  (fs=%.0f, quantum=%d)\n", sr, Q);
    std::printf("==================================================\n");

    // ---- WA1  click-free gate through the C API ----------------------------
    {
        Para3* p = para3_create(sr, Q);
        para3_set_param(p, PARA3_P_CUTOFF,    0.7);
        para3_set_param(p, PARA3_P_RESONANCE, 0.3);
        const int total = 32000;
        std::vector<float> x(total, 0.f);
        int i = 0;
        auto blocks = [&](int cnt){ for (int k=0;k<cnt;k+=Q){
            int c=std::min(Q,cnt-k); para3_render(p, x.data()+i, c); i+=c; } };
        blocks(4096);
        para3_note_on(p, 57);
        blocks(16000);
        para3_note_off(p, 57);
        blocks(total - i);
        para3_destroy(p);

        bool finite = true;
        for (float v : x) if (!std::isfinite(v)) finite = false;
        const double steadyD = maxAbsDelta(x, 9000, 14000);
        const double globalD = maxAbsDelta(x, 1, total);
        double tail = 0.0;
        for (int j=total-256;j<total;++j) tail=std::max(tail,(double)std::fabs(x[j]));
        const bool pass = finite && globalD <= steadyD*1.6 && tail < 1e-4;
        std::printf("\nWA1 gate via C-API\n");
        std::printf("   steady|dx| %.5f  global|dx| %.5f  tail %.2e -> %s\n",
                    steadyD, globalD, tail, pass?"PASS":"FAIL");
        if (!pass) ++failures;
    }

    // ---- WA2  param funnel through the C API -------------------------------
    {
        Para3* p = para3_create(sr, Q);
        para3_set_param(p, PARA3_P_RESONANCE, 0.3);
        para3_note_on(p, 45);
        const int total = 40000;
        std::vector<float> x(total);
        int i = 0; bool finite = true; double peak = 0.0;
        while (i < total) {
            if ((i % 1024) == 0)
                para3_set_param(p, PARA3_P_CUTOFF, 0.15 + 0.7*((i/1024)&1));
            int c = std::min(Q, total - i);
            para3_render(p, x.data()+i, c);
            i += c;
        }
        for (float v : x){ if(!std::isfinite(v)) finite=false;
                           peak=std::max(peak,(double)std::fabs(v)); }
        const double gD = maxAbsDelta(x, 1, total);
        const bool pass = finite && peak < 4.0 && gD < 0.6;   // no zipper spike
        para3_destroy(p);
        std::printf("\nWA2 param funnel via C-API\n");
        std::printf("   peak %.3f  global|dx| %.5f  finite %s -> %s\n",
                    peak, gD, finite?"yes":"NO", pass?"PASS":"FAIL");
        std::printf("   (deep zipper proof: engine T4/T11; here: C boundary clean)\n");
        if (!pass) ++failures;
    }

    // ---- WA3  block-size invariance (THE AudioWorklet-readiness proof) ------
    {
        const int total = 50000;
        auto setup = [&](Para3* p){
            para3_set_param(p, PARA3_P_CUTOFF, 0.6);
            para3_set_param(p, PARA3_P_RESONANCE, 0.4);
            para3_set_mode(p, PARA3_M_UNISON);
            // a running pattern so the clock/seq is exercised too
            for (int s=0;s<16;++s)
                para3_seq_set_step(p, s, 48 + (s%5), 1, 1, (double)s/15.0);
            para3_seq_commit(p);
            para3_seq_set_tempo(p, 127.0);          // step length NOT a /128
            para3_seq_start(p);
            para3_note_on(p, 48);
        };
        Para3* pa = para3_create(sr, Q);  setup(pa);
        Para3* pb = para3_create(sr, total); setup(pb);

        std::vector<float> A(total), B(total);
        int i = 0;
        while (i < total) {                          // (a) 128-frame quanta
            int c = std::min(Q, total - i);
            para3_render(pa, A.data()+i, c);
            i += c;
        }
        para3_render(pb, B.data(), total);           // (b) one big call

        double maxDiff = 0.0; bool finite = true;
        for (int j=0;j<total;++j){
            if(!std::isfinite(A[j])||!std::isfinite(B[j])) finite=false;
            maxDiff = std::max(maxDiff,(double)std::fabs(A[j]-B[j]));
        }
        para3_destroy(pa); para3_destroy(pb);
        const bool pass = finite && maxDiff == 0.0;  // bit-identical
        std::printf("\nWA3 block-size invariance  (128-quanta vs single call)\n");
        std::printf("   max sample diff %.3e  -> %s\n",
                    maxDiff, pass?"PASS (bit-identical)":"FAIL");
        std::printf("   (proves zero block-boundary artefact in the 128 quantum)\n");
        if (!pass) ++failures;
    }

    // ---- WA4  sample-accurate sequencer through the C API ------------------
    {
        Para3* p = para3_create(sr, Q);
        for (int s=0;s<16;++s) para3_seq_set_step(p, s, 57, 1, 0, 0.5);
        para3_seq_commit(p);
        para3_seq_set_tempo(p, 120.0);               // step = 6000 samples
        para3_seq_start(p);
        const int total = 160000;
        std::vector<float> one(1);
        int prev = para3_seq_current_step(p), last = -1;
        double jit = 0.0; int n = 0;
        for (int i=0;i<total;++i){
            para3_render(p, one.data(), 1);          // 1-frame: probe timing
            int cs = para3_seq_current_step(p);
            if (cs != prev){
                if (last>=0){ jit=std::max(jit,std::fabs((i-last)-6000.0)); ++n; }
                last=i; prev=cs;
            }
        }
        para3_destroy(p);
        const bool pass = n > 8 && jit == 0.0;
        std::printf("\nWA4 sequencer timing via C-API\n");
        std::printf("   jitter %.2f smp over %d steps -> %s\n",
                    jit, n, pass?"PASS":"FAIL");
        if (!pass) ++failures;
    }

    // ---- WA5  long stress: seq + ring + max reso/drive, no NaN/Inf ---------
    {
        Para3* p = para3_create(sr, Q);
        para3_set_mode(p, PARA3_M_UNIRING);
        para3_set_param(p, PARA3_P_RESONANCE, 1.0);
        para3_set_param(p, PARA3_P_DRIVE,     1.0);
        para3_set_param(p, PARA3_P_DELAY_MIX, 0.5);
        for (int s=0;s<16;++s) para3_seq_set_step(p, s, 36+s, (s%2)==0, 1,
                                                  (double)((s*7)%16)/15.0);
        para3_seq_commit(p);
        para3_seq_set_tempo(p, 150.0);
        para3_seq_start(p);
        const int total = 480000;                    // 10 s
        std::vector<float> buf(Q);
        bool finite = true; double peak = 0.0;
        for (int i=0;i<total;i+=Q){
            int c=std::min(Q,total-i);
            para3_render(p, buf.data(), c);
            for (int k=0;k<c;++k){ float v=buf[k];
                if(!std::isfinite(v)) finite=false;
                peak=std::max(peak,(double)std::fabs(v)); }
        }
        para3_destroy(p);
        const bool pass = finite && peak > 1e-3 && peak < 8.0;  // alive & bounded
        std::printf("\nWA5 10 s stress (ring+maxQ+drive+delay+seq)\n");
        std::printf("   peak %.3f  finite %s -> %s\n",
                    peak, finite?"yes":"NO", pass?"PASS":"FAIL");
        if (!pass) ++failures;
    }

    std::printf("\n==================================================\n");
    std::printf("%s  (%d failure%s)\n",
                failures ? "OVERALL: FAIL" : "OVERALL: PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
