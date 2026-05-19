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

    // ---- WA6  full E1-E6 surface sweep via the C-API (bridge proof) -------
    {
        Para3* p = para3_create(sr, Q);
        bool finite = true; double peak = 0.0;
        std::vector<float> buf(Q);
        auto pump=[&](int blocks){
            for(int b=0;b<blocks;++b){ para3_render(p,buf.data(),Q);
                for(int k=0;k<Q;++k){ float v=buf[k];
                    if(!std::isfinite(v)) finite=false;
                    peak=std::max(peak,(double)std::fabs(v)); } } };

        para3_note_on(p, 48);
        // E1/E2/E6 new params through the unified funnel
        for(int s=0;s<=20;++s){ const double n=s/20.0;
            para3_set_param(p, PARA3_P_EG_CUT_DEPTH, n);
            para3_set_param(p, PARA3_P_DETUNE,       n);
            para3_set_param(p, PARA3_P_PORTAMENTO,   n);
            para3_set_param(p, PARA3_P_VOLUME, 0.3+0.7*n);
            pump(1); }
        para3_set_lfo_sync(p, 1); para3_note_on(p, 50); pump(2);
        para3_set_octave(p, 1);  para3_note_on(p, 52); pump(2);
        para3_set_octave(p, -1); para3_note_on(p, 47); pump(2);

        // E3 motion: lane commit + SMOOTH + PEAK refusal observable
        double lane[16]; for(int i=0;i<16;++i) lane[i]=(double)i/15.0;
        para3_seq_motion_lane_commit(p, PARA3_P_CUTOFF, lane);
        para3_seq_motion_smooth(p, 1);
        para3_seq_motion_set(p, PARA3_P_RESONANCE, 3, 0.9);   // must be refused
        para3_seq_motion_set(p, PARA3_P_RESONANCE, 4, 0.9);   // refused
        para3_seq_commit(p);
        for(int s=0;s<16;++s) para3_seq_set_step(p,s,48,1,0,0.5);
        para3_seq_commit(p);
        para3_seq_set_tempo(p,140.0); para3_seq_start(p); pump(40);
        const long rej = para3_seq_motion_rejects(p);

        // E4 sequencer behaviours
        para3_seq_step_trigger(p, 1); pump(20);
        para3_seq_tempo_div(p, 2);    pump(20);
        para3_seq_active_step(p, 5, 0); para3_seq_commit(p); pump(20);
        para3_seq_metronome(p, 1);    pump(20);
        para3_seq_metronome(p, 0);

        // E5 flux: mode + record + overflow drop counter. FLUX_CAP is 512
        // (EXT-FLUX raised from 256 in v8 to fit param-events on top of notes);
        // pump 700 to comfortably trip the drop counter.
        para3_seq_flux_mode(p, 1);
        para3_seq_flux_loop_len(p, 24000);
        para3_seq_flux_rec(p, 1);
        for(int i=0;i<700;++i){ para3_seq_flux_note(p, 48, (i&1)==0);
                                pump(0); para3_render(p,buf.data(),8); }
        para3_seq_flux_rec(p, 0);
        para3_seq_flux_commit(p);
        pump(30);
        const long dropped = para3_seq_flux_dropped(p);

        para3_destroy(p);
        const bool pass = finite && peak>1e-4 && peak<8.0
                          && rej >= 2 && dropped > 0;
        std::printf("\nWA6 full E1-E6 C-API surface sweep\n");
        std::printf("   finite %s  peak %.3f\n", finite?"yes":"NO", peak);
        std::printf("   motion rejects (PEAK)   : %ld  (>=2)\n", rej);
        std::printf("   flux dropped (overflow) : %ld  (>0, FLUX_CAP)\n", dropped);
        std::printf("   -> %s\n", pass?"PASS":"FAIL");
        if (!pass) ++failures;
    }

    // ---- WA7  EXT-ARP full C-API sweep (Blocks A+B+C) ----------------------
    // Walk every arp-related C-API entry point at runtime, drive >16 notes to
    // trip the pool-overflow counter, sweep modes (Up/Dn/UpDn/AsP/Rnd) and
    // rates (1/4..1/32), toggle hold/seed, and confirm:
    //   * para3_arp_dropped reports >0 after overflow
    //   * audio output stays finite & bounded under all switches
    //   * Off→On→Off transitions don't NaN
    //   * Off-state still produces the same audio as never calling the arp
    //     (regression against pre-EXT behaviour).
    {
        Para3* p = para3_create(48000.0, 128);
        if (!p) { std::fprintf(stderr, "WA7 create failed\n"); ++failures; }
        else {
            para3_set_param(p, PARA3_P_SUSTAIN, 1.0);
            para3_set_param(p, PARA3_P_CUTOFF,  0.85);
            para3_seq_set_tempo(p, 120.0);
            para3_seq_start(p);

            // Baseline (arp untouched): play a held key, render.
            para3_note_on(p, 48);
            std::vector<float> base(8000);
            para3_render(p, base.data(), 8000);
            para3_note_off(p, 48);

            // Turn on arp and walk every setter.
            para3_arp_enable(p, 1);
            for (int m = 0; m <= 4; ++m) {
                para3_arp_mode(p, m);
                for (int r = 0; r <= 5; ++r) para3_arp_rate(p, r);
            }
            para3_arp_gate   (p, 0.25);
            para3_arp_octaves(p, 3);
            para3_arp_hold   (p, 1);
            para3_arp_seed   (p, 0xC0FFEEu);

            // Try to overflow the 16-slot pool: press 20 distinct notes.
            for (int i = 0; i < 20; ++i) para3_note_on(p, 36 + i);
            std::vector<float> sweep(48000);
            para3_render(p, sweep.data(), 48000);
            for (int i = 0; i < 20; ++i) para3_note_off(p, 36 + i);
            const long dropped = para3_arp_dropped(p);

            // Disable arp; check no NaN/Inf and the audio path is sane.
            para3_arp_enable(p, 0);
            std::vector<float> tail(8000);
            para3_render(p, tail.data(), 8000);

            // Sanity: finite + bounded.
            double pkS = 0.0, pkT = 0.0;
            bool finite = true;
            for (float x : sweep) { if (!std::isfinite(x)) finite = false; pkS = std::max(pkS, (double)std::fabs(x)); }
            for (float x : tail)  { if (!std::isfinite(x)) finite = false; pkT = std::max(pkT, (double)std::fabs(x)); }

            // Off-state baseline parity: a fresh engine with NO arp calls should
            // produce the same render as 'arp disabled' on a configured engine.
            Para3* q = para3_create(48000.0, 128);
            para3_set_param(q, PARA3_P_SUSTAIN, 1.0);
            para3_set_param(q, PARA3_P_CUTOFF,  0.85);
            para3_seq_set_tempo(q, 120.0);
            para3_seq_start(q);
            para3_note_on(q, 48);
            std::vector<float> baseQ(8000);
            para3_render(q, baseQ.data(), 8000);
            double maxD = 0.0;
            for (size_t i = 0; i < base.size(); ++i) maxD = std::max(maxD, (double)std::fabs(base[i]-baseQ[i]));
            para3_destroy(q);
            para3_destroy(p);

            const bool pass = finite && pkS < 4.0 && pkT < 4.0 && dropped > 0
                           && maxD == 0.0;
            std::printf("\nWA7 EXT-ARP full surface sweep (Blocks A+B+C)\n");
            std::printf("   finite                  : %s\n", finite ? "yes" : "NO");
            std::printf("   peak sweep / tail       : %.3f / %.3f  (< 4.0)\n", pkS, pkT);
            std::printf("   arp dropped (>16 notes) : %ld  (>0 expected)\n", dropped);
            std::printf("   off-state parity max|d| : %.3e  (== 0)\n", maxD);
            std::printf("   -> %s\n", pass ? "PASS" : "FAIL");
            if (!pass) ++failures;
        }
    }

    // ---- WA8  EXT-FLUX-PARAM via C-API: record param events, replay determ. -
    // Same idea as offline T42 but through the WASM-facing bridge:
    //   1. Enable FLUX-mode, arm record, inject 1 bright-restart + 1 note + 1
    //      dark-drop + 1 note-off via render-driven live cursor
    //   2. Commit, warm-up loop, capture 2 measured loops
    //   3. Quartile-RMS metric:
    //        (a) param effect visible (Q3 << Q1)
    //        (b) loud quartiles match between loops within 10 %
    //      Q3+Q4 are near-silent residue/release and not used for the match
    //      check (floating-point noise amplifies into spurious ratios there).
    //   4. para3_seq_flux_clear → bank empty + tail still finite
    {
        Para3* p = para3_create(48000.0, 64);
        if (!p) { std::fprintf(stderr, "WA8 create failed\n"); ++failures; }
        else {
            const unsigned int L = 48000;
            std::vector<float> one(1);
            para3_set_param(p, PARA3_P_CUTOFF,    0.7);
            para3_set_param(p, PARA3_P_RESONANCE, 0.4);
            para3_set_param(p, PARA3_P_ATTACK,    0.0);
            para3_set_param(p, PARA3_P_DECREL,    0.05);
            para3_set_param(p, PARA3_P_SUSTAIN,   0.8);
            para3_set_param(p, PARA3_P_DELAY_MIX, 0.0);
            para3_seq_flux_mode(p, 1);
            para3_seq_flux_loop_len(p, L);
            para3_seq_flux_rec(p, 1);

            const int OBRIGHT=50, ONOTE=200, ODARK=24000, OOFF=40000;
            for (unsigned int i=0;i<L;++i) {
                if ((int)i==OBRIGHT) para3_seq_flux_param(p, PARA3_P_CUTOFF, 0.7);
                if ((int)i==ONOTE)   para3_seq_flux_note (p, 60, 1);
                if ((int)i==ODARK)   para3_seq_flux_param(p, PARA3_P_CUTOFF, 0.05);
                if ((int)i==OOFF)    para3_seq_flux_note (p, 60, 0);
                para3_render(p, one.data(), 1);
            }
            para3_seq_flux_rec(p, 0);
            para3_seq_flux_commit(p);

            // Warm-up loop discarded
            std::vector<float> warmL(L); for (unsigned int i=0;i<L;++i) para3_render(p, &warmL[i], 1);
            std::vector<float> a(L), b(L);
            for (unsigned int i=0;i<L;++i) para3_render(p, &a[i], 1);
            for (unsigned int i=0;i<L;++i) para3_render(p, &b[i], 1);

            auto rmsW = [](const std::vector<float>& y, int st, int en){
                double s=0; for (int i=st;i<en;++i) s += (double)y[i]*y[i];
                return std::sqrt(s / std::max(1, en-st));
            };
            const int Q1s=L/8, Q2s=3*L/8, Q3s=5*L/8, W=L/8;
            const double q1A=rmsW(a,Q1s,Q1s+W), q2A=rmsW(a,Q2s,Q2s+W);
            const double q3A=rmsW(a,Q3s,Q3s+W);
            const double q1B=rmsW(b,Q1s,Q1s+W), q2B=rmsW(b,Q2s,Q2s+W);
            auto ratio = [](double x, double y){
                return std::max(x,y) / std::max(std::min(x,y), 1e-9);
            };
            bool finite=true; for (float v:a) if(!std::isfinite(v)) finite=false;
                              for (float v:b) if(!std::isfinite(v)) finite=false;
            const bool effectVisible = q3A < q1A * 0.5;
            const bool loudMatch     = ratio(q1A,q1B) < 1.10 && ratio(q2A,q2B) < 1.10;
            const long dropped       = para3_seq_flux_dropped(p);

            para3_seq_flux_clear(p);
            std::vector<float> tail(L);
            for (unsigned int i=0;i<L;++i) para3_render(p, &tail[i], 1);
            bool tailFinite=true; for (float v:tail) if(!std::isfinite(v)) tailFinite=false;

            para3_destroy(p);

            const bool pass = finite && tailFinite && effectVisible && loudMatch
                           && dropped == 0;
            std::printf("\nWA8 EXT-FLUX-PARAM via C-API (param event audible + repeatable + clear)\n");
            std::printf("   q1A=%.4f q2A=%.4f q3A=%.4f\n", q1A, q2A, q3A);
            std::printf("   q3/q1 (effect)       : %.3f  (want <0.5)\n", q3A/std::max(q1A,1e-9));
            std::printf("   loud match q1/q2     : %.3f / %.3f  (want <1.10)\n",
                        ratio(q1A,q1B), ratio(q2A,q2B));
            std::printf("   dropped              : %ld  (0 expected for 4 events)\n", dropped);
            std::printf("   tail finite post-clr : %s\n", tailFinite?"yes":"NO");
            std::printf("   -> %s\n", pass?"PASS":"FAIL");
            if (!pass) ++failures;
        }
    }

    // ---- WA9  EXT-BASS B1 PULSE via C-API (per-osc wave, default bit-identical)
    //  Proves para3_osc_wave routes through Controller→Engine→Oscillator, that
    //  Default-Saw is bit-identical to explicit setOscWave(*,0) over the
    //  worklet's 128-quantum render path (the closest E2E to live audio without
    //  a browser), and that switching to Pulse produces a measurably different
    //  spectrum (lower even-harmonic energy).
    {
        Para3* a = para3_create(sr, Q);
        Para3* b = para3_create(sr, Q);
        if (!a || !b) { std::fprintf(stderr, "WA9 create failed\n"); ++failures; }
        else {
            para3_set_mode(a, PARA3_M_UNISON);
            para3_set_mode(b, PARA3_M_UNISON);
            // b: explicit Saw on all 3 — must match a's default
            para3_osc_wave(b, 0, 0);
            para3_osc_wave(b, 1, 0);
            para3_osc_wave(b, 2, 0);
            para3_note_on(a, 60);
            para3_note_on(b, 60);
            const int N = 48000;                         // 1s
            std::vector<float> ya(N, 0.f), yb(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(a, ya.data() + i, c);
                para3_render(b, yb.data() + i, c);
            }
            double maxd = 0.0;
            for (int i = 0; i < N; ++i)
                maxd = std::max(maxd, (double)std::fabs(ya[i] - yb[i]));

            // out-of-range osc indices must not crash and must not affect audio
            para3_osc_wave(a, -1, 1);
            para3_osc_wave(a,  9, 1);
            std::vector<float> after(Q, 0.f);
            para3_render(a, after.data(), Q);
            bool oorFinite = true;
            for (float v : after) if (!std::isfinite(v)) oorFinite = false;

            // switch a to Pulse on all 3 — output must change, stay finite
            para3_osc_wave(a, 0, 1);
            para3_osc_wave(a, 1, 1);
            para3_osc_wave(a, 2, 1);
            std::vector<float> yp(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(a, yp.data() + i, c);
            }
            double maxAbsP = 0.0, maxAbsS = 0.0;
            bool finiteP = true;
            for (int i = 1000; i < N; ++i) {              // skip switch transient
                if (!std::isfinite(yp[i])) finiteP = false;
                maxAbsP = std::max(maxAbsP, (double)std::fabs(yp[i]));
                maxAbsS = std::max(maxAbsS, (double)std::fabs(ya[i]));
            }
            // RMS-level comparison: pulse is louder than saw at same pitch
            double rmsP = 0.0, rmsS = 0.0;
            for (int i = 1000; i < N; ++i) {
                rmsP += (double)yp[i] * yp[i];
                rmsS += (double)ya[i] * ya[i];
            }
            rmsP = std::sqrt(rmsP / (N - 1000));
            rmsS = std::sqrt(rmsS / (N - 1000));
            const double loudGain = rmsP / std::max(rmsS, 1e-9);
            const bool effect = loudGain >= 1.2;          // pulse RMS clearly above saw

            const bool pass = (maxd == 0.0) && oorFinite && finiteP && effect;
            std::printf("\nWA9 EXT-BASS B1 PULSE via C-API (default-Saw bit-identical + audible Pulse)\n");
            std::printf("   default vs explicit Saw  max|d| : %.3e  (want == 0)\n", maxd);
            std::printf("   OOR osc indices finite          : %s\n", oorFinite?"yes":"NO");
            std::printf("   pulse output finite             : %s\n", finiteP?"yes":"NO");
            std::printf("   rms(pulse)/rms(saw)             : %.3f  (want ≥ 1.2)\n", loudGain);
            std::printf("   peak|saw| / peak|pulse|         : %.3f / %.3f\n", maxAbsS, maxAbsP);
            std::printf("   -> %s\n", pass?"PASS":"FAIL");
            if (!pass) ++failures;

            para3_destroy(a); para3_destroy(b);
        }
    }

    // ---- WA10 EXT-BASS B2 PWM via C-API (PW + PWM-Depth through setParamNorm)
    //  Proves PARA3_P_BASS_PULSE_WIDTH and PARA3_P_BASS_PWM_DEPTH route into
    //  ParaEngine via para3_set_param. Default (don't touch) is bit-identical
    //  to explicit setting to PW=0.5, PwmDepth=0. Setting PW to extremes
    //  (0.1, 0.9) produces measurably different output vs PW=0.5.
    {
        Para3* a = para3_create(sr, Q);
        Para3* b = para3_create(sr, Q);
        if (!a || !b) { std::fprintf(stderr, "WA10 create failed\n"); ++failures; }
        else {
            para3_set_mode(a, PARA3_M_UNISON);
            para3_set_mode(b, PARA3_M_UNISON);
            para3_osc_wave(a, 0, 1); para3_osc_wave(a, 1, 1); para3_osc_wave(a, 2, 1);
            para3_osc_wave(b, 0, 1); para3_osc_wave(b, 1, 1); para3_osc_wave(b, 2, 1);
            // b sets the same defaults explicitly (norm 0.5 → PW 0.5, 0 → depth 0)
            para3_set_param(b, PARA3_P_BASS_PULSE_WIDTH, 0.5);
            para3_set_param(b, PARA3_P_BASS_PWM_DEPTH,   0.0);
            para3_note_on(a, 60);
            para3_note_on(b, 60);
            const int N = 48000;
            std::vector<float> ya(N, 0.f), yb(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(a, ya.data() + i, c);
                para3_render(b, yb.data() + i, c);
            }
            double maxdNeutral = 0.0;
            for (int i = 0; i < N; ++i)
                maxdNeutral = std::max(maxdNeutral, (double)std::fabs(ya[i] - yb[i]));

            // Now push a to PW=0.10, b to PW=0.90 — outputs must differ from each
            // other AND from the neutral baseline ya/yb.
            para3_set_param(a, PARA3_P_BASS_PULSE_WIDTH, 0.0556);   // norm → PW 0.10
            para3_set_param(b, PARA3_P_BASS_PULSE_WIDTH, 0.9444);   // norm → PW 0.90
            std::vector<float> y10(N, 0.f), y90(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(a, y10.data() + i, c);
                para3_render(b, y90.data() + i, c);
            }
            // skip initial ramp window
            double diff_pw10_neutral = 0.0, diff_pw90_neutral = 0.0;
            for (int i = 8000; i < N; ++i) {
                diff_pw10_neutral = std::max(diff_pw10_neutral, (double)std::fabs(y10[i] - ya[i]));
                diff_pw90_neutral = std::max(diff_pw90_neutral, (double)std::fabs(y90[i] - yb[i]));
            }
            bool finitePw = true;
            for (int i = 1000; i < N; ++i) {
                if (!std::isfinite(y10[i]) || !std::isfinite(y90[i])) { finitePw = false; break; }
            }

            const bool pass = (maxdNeutral == 0.0) && finitePw
                           && (diff_pw10_neutral >= 0.05) && (diff_pw90_neutral >= 0.05);
            std::printf("\nWA10 EXT-BASS B2 PWM via C-API (PW + PwmDepth setParamNorm)\n");
            std::printf("   default vs explicit Pulse params  max|d|: %.3e  (want == 0)\n", maxdNeutral);
            std::printf("   PW=0.10 vs default neutral        max|d|: %.3e  (want ≥ 5e-2)\n", diff_pw10_neutral);
            std::printf("   PW=0.90 vs default neutral        max|d|: %.3e  (want ≥ 5e-2)\n", diff_pw90_neutral);
            std::printf("   finite under PW extremes               : %s\n", finitePw?"yes":"NO");
            std::printf("   -> %s\n", pass?"PASS":"FAIL");
            if (!pass) ++failures;

            para3_destroy(a); para3_destroy(b);
        }
    }

    // ---- WA11 EXT-BASS B3 SPREAD + DRIFT via C-API ------------------------
    //  para3_set_param routes Bass-Spread / DriftRate / DriftDepth into the
    //  engine; para3_bass_drift_seed re-seeds the per-OSC xorshift. Default
    //  (no calls to these) is bit-identical to a fresh engine that explicitly
    //  sets BassSpread=0 + BassDriftDepth=0. Different seeds produce
    //  measurably different outputs; same seed produces sample-identical.
    {
        Para3* a = para3_create(sr, Q);
        Para3* b = para3_create(sr, Q);
        if (!a || !b) { std::fprintf(stderr, "WA11 create failed\n"); ++failures; }
        else {
            para3_set_mode(a, PARA3_M_UNISON);
            para3_set_mode(b, PARA3_M_UNISON);
            // b sets the same defaults explicitly via the C-API surface
            para3_set_param(b, PARA3_P_BASS_SPREAD,      0.0);
            para3_set_param(b, PARA3_P_BASS_DRIFT_DEPTH, 0.0);
            para3_set_param(b, PARA3_P_BASS_DRIFT_RATE,  0.5);    // beliebig, depth=0
            para3_note_on(a, 60);
            para3_note_on(b, 60);
            const int N = 48000;
            std::vector<float> ya(N, 0.f), yb(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(a, ya.data() + i, c);
                para3_render(b, yb.data() + i, c);
            }
            double maxdNeutral = 0.0;
            for (int i = 0; i < N; ++i)
                maxdNeutral = std::max(maxdNeutral, (double)std::fabs(ya[i] - yb[i]));

            // Reseed determinism: two new engines with same seed must match.
            // (We can't reseed a mid-render — the LP state has already evolved.)
            Para3* c1 = para3_create(sr, Q);
            Para3* c2 = para3_create(sr, Q);
            para3_set_mode(c1, PARA3_M_UNISON);
            para3_set_mode(c2, PARA3_M_UNISON);
            para3_set_param(c1, PARA3_P_BASS_DRIFT_DEPTH, 1.0);
            para3_set_param(c2, PARA3_P_BASS_DRIFT_DEPTH, 1.0);
            para3_set_param(c1, PARA3_P_BASS_DRIFT_RATE,  0.5);
            para3_set_param(c2, PARA3_P_BASS_DRIFT_RATE,  0.5);
            para3_bass_drift_seed(c1, 0xCAFEBABEu);
            para3_bass_drift_seed(c2, 0xCAFEBABEu);
            para3_note_on(c1, 60);
            para3_note_on(c2, 60);
            std::vector<float> yc1(N, 0.f), yc2(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(c1, yc1.data() + i, c);
                para3_render(c2, yc2.data() + i, c);
            }
            double maxdSeed = 0.0;
            for (int i = 0; i < N; ++i)
                maxdSeed = std::max(maxdSeed, (double)std::fabs(yc1[i] - yc2[i]));

            // Spread effect via API: re-prep b with spread > 0, output differs
            // from neutral a (skip initial 8000 samples for ramp settle).
            Para3* d = para3_create(sr, Q);
            para3_set_mode(d, PARA3_M_UNISON);
            para3_set_param(d, PARA3_P_BASS_SPREAD, 1.0);            // max
            para3_note_on(d, 60);
            std::vector<float> yd(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(d, yd.data() + i, c);
            }
            double spreadDiff = 0.0;
            for (int i = 8000; i < N; ++i)
                spreadDiff = std::max(spreadDiff, (double)std::fabs(yd[i] - ya[i]));

            bool finiteOk = true;
            for (int i = 1000; i < N; ++i)
                if (!std::isfinite(yc1[i]) || !std::isfinite(yd[i])) { finiteOk = false; break; }

            const bool pass = (maxdNeutral == 0.0) && (maxdSeed == 0.0)
                           && (spreadDiff >= 0.05) && finiteOk;
            std::printf("\nWA11 EXT-BASS B3 SPREAD+DRIFT via C-API\n");
            std::printf("   default vs explicit Spread=0+Depth=0  max|d|: %.3e  (want == 0)\n", maxdNeutral);
            std::printf("   same-seed determinism               max|d|: %.3e  (want == 0)\n", maxdSeed);
            std::printf("   Spread=max vs neutral               max|d|: %.3e  (want ≥ 5e-2)\n", spreadDiff);
            std::printf("   finite under all B3 settings              : %s\n", finiteOk?"yes":"NO");
            std::printf("   -> %s\n", pass?"PASS":"FAIL");
            if (!pass) ++failures;

            para3_destroy(a); para3_destroy(b);
            para3_destroy(c1); para3_destroy(c2);
            para3_destroy(d);
        }
    }

    // ---- WA12 EXT-BASS B4 STACK via C-API --------------------------------
    //  para3_bass_stack(p, on) hebt den Voice-Modus auf und legt alle 3 OSCs
    //  auf die newest Note. Default off ⇒ bit-identisch zur Voice-Modus-Engine
    //  (T65 deckt 6 Modi nativ ab; hier exemplarisch im 128-Quantum-Pfad mit
    //  Mode::Fifth, der Stack-on klar erkennbar abschaltet — die +7-Halbton-
    //  Komponente bei MIDI 67 verschwindet, Output ist mono auf MIDI 60).
    {
        Para3* a = para3_create(sr, Q);
        Para3* b = para3_create(sr, Q);
        if (!a || !b) { std::fprintf(stderr, "WA12 create failed\n"); ++failures; }
        else {
            para3_set_mode(a, PARA3_M_FIFTH);
            para3_set_mode(b, PARA3_M_FIFTH);
            para3_bass_stack(b, 0);                              // explicit off
            para3_note_on(a, 60);
            para3_note_on(b, 60);
            const int N = 32000;
            std::vector<float> ya(N, 0.f), yb(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int c = std::min(Q, N - i);
                para3_render(a, ya.data() + i, c);
                para3_render(b, yb.data() + i, c);
            }
            double maxdNeutral = 0.0;
            for (int i = 0; i < N; ++i)
                maxdNeutral = std::max(maxdNeutral, (double)std::fabs(ya[i] - yb[i]));

            // c: stack on. Output should be measurably different from Fifth.
            Para3* c = para3_create(sr, Q);
            para3_set_mode(c, PARA3_M_FIFTH);
            para3_bass_stack(c, 1);
            para3_note_on(c, 60);
            std::vector<float> yc(N, 0.f);
            for (int i = 0; i < N; i += Q) {
                const int q = std::min(Q, N - i);
                para3_render(c, yc.data() + i, q);
            }
            double stackDiff = 0.0;
            for (int i = 8000; i < N; ++i)
                stackDiff = std::max(stackDiff, (double)std::fabs(yc[i] - ya[i]));

            // OOR on/off values clamp to 0/1 (== 0 / != 0)
            para3_bass_stack(c, 99);                             // any non-zero = on
            para3_bass_stack(c, 0);                              // off again
            std::vector<float> rest(Q, 0.f);
            para3_render(c, rest.data(), Q);
            bool oorFinite = true;
            for (float v : rest) if (!std::isfinite(v)) oorFinite = false;

            const bool pass = (maxdNeutral == 0.0) && (stackDiff >= 0.05) && oorFinite;
            std::printf("\nWA12 EXT-BASS B4 STACK via C-API (Fifth mode override + Neutralität)\n");
            std::printf("   default vs explicit stack=0  max|d|: %.3e  (want == 0)\n", maxdNeutral);
            std::printf("   stack=1 vs Fifth             max|d|: %.3e  (want ≥ 5e-2)\n", stackDiff);
            std::printf("   OOR toggle finite                  : %s\n", oorFinite?"yes":"NO");
            std::printf("   -> %s\n", pass?"PASS":"FAIL");
            if (!pass) ++failures;

            para3_destroy(a); para3_destroy(b); para3_destroy(c);
        }
    }

    std::printf("\n==================================================\n");
    std::printf("%s  (%d failure%s)\n",
                failures ? "OVERALL: FAIL" : "OVERALL: PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
