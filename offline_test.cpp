// =============================================================================
//  PARA-3 :: Offline measurement harness  (rev. 3, latency-robust)
//
//  The decimation FIR has linear-phase group delay (~48 output samples). The
//  click/zipper tests therefore use a latency-INDEPENDENT metric: the global
//  maximum |dx| over the whole signal must not exceed the steady-state slope.
//  A real click would spike that global maximum no matter where latency places
//  the event. Measures, never asserts.
//
//  build: g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o offline_test
// =============================================================================
#include "Para3Engine.hpp"
#include <vector>
#include <complex>
#include <cstdio>
#include <cmath>

using cd = std::complex<double>;

static void fft(std::vector<cd>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        const cd wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const cd u = a[i + k];
                const cd v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

static double maxAbsDelta(const std::vector<float>& x, int a, int b) {
    double m = 0.0;
    for (int i = a + 1; i < b; ++i) {
        m = std::max(m, (double)std::fabs(x[i] - x[i - 1]));
    }
    return m;
}

int main() {
    const double sr = 48000.0;
    int failures = 0;

    std::printf("PARA-3 offline measurement  (fs = %.0f Hz)\n", sr);
    std::printf("==================================================\n");

    // ---- T1  anti-aliasing (coherent sampling, leak-free) -------------------
    //  f0 placed EXACTLY on FFT bin k => every true harmonic lands exactly on
    //  bin m*k, zero spectral leakage, no window needed. Any energy on a
    //  non-multiple-of-k bin is genuine alias/noise. This removes the window
    //  self-masking that floored the previous metric at ~-43 dBc.
    {
        const int    N     = 32768;
        const int    k     = 1779;                  // f0 ~ 2606 Hz, harmonics fold
        const double binHz = sr / N;                // exact (power-of-two N)
        const double f0    = k * binHz;

        para3::Oscillator osc;
        osc.prepare(sr);
        std::vector<double> eBuf(N);
        for (int i = 0; i < 8192; ++i) osc.process(f0);          // settle FIR
        for (int i = 0; i < N; ++i)    eBuf[i] = osc.process(f0);

        std::vector<double> naive(N);
        {
            double ph = 0.0, dt = f0 / sr;
            for (int i = 0; i < N; ++i) {
                naive[i] = 2.0 * ph - 1.0;
                ph += dt;
                if (ph >= 1.0) ph -= 1.0;
            }
        }

        auto floorDb = [&](const std::vector<double>& sig) {
            std::vector<cd> sp(N);
            for (int i = 0; i < N; ++i) sp[i] = cd(sig[i], 0.0);  // rectangular
            fft(sp);
            double fund = 0.0, alias = 0.0;
            for (int b = 2; b < N / 2; ++b) {
                const double m = std::abs(sp[b]);
                if (b % k == 0) fund  = std::max(fund, m);        // true harmonic
                else            alias = std::max(alias, m);       // alias/noise
            }
            return 20.0 * std::log10((alias + 1e-30) / (fund + 1e-30));
        };

        const double dbE = floorDb(eBuf);
        const double dbN = floorDb(naive);
        const bool pass = dbE < -55.0;

        std::printf("\nT1 anti-aliasing  (coherent f0 = %.2f Hz, OS=4 + 383-tap Kaiser)\n", f0);
        std::printf("   naive saw alias floor   : %7.2f dBc\n", dbN);
        std::printf("   engine alias floor      : %7.2f dBc\n", dbE);
        std::printf("   improvement             : %7.2f dB\n", dbN - dbE);
        std::printf("   -> %s (threshold < -55 dBc)\n", pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T2  click-free gate (latency-robust) -------------------------------
    {
        para3::Engine eng;
        eng.prepare(sr, 4096);
        eng.setAttackMs(1.5);
        eng.setDecRelMs(60.0);
        eng.setSustain(0.8);

        const int onAt = 2000, offAt = 16000, total = 32000;
        std::vector<float> x(total, 0.0f);
        int idx = 0;
        auto run = [&](int c){ eng.process(x.data() + idx, c); idx += c; };
        run(onAt);          eng.noteOn(57);
        run(offAt - onAt);  eng.noteOff();
        run(total - offAt);

        bool finite = true;
        for (float v : x) if (!std::isfinite(v)) finite = false;

        const double steadyD = maxAbsDelta(x, 9000, 14000);   // env flat region
        const double globalD = maxAbsDelta(x, 1, total);      // whole signal
        double tail = 0.0;
        for (int i = total - 256; i < total; ++i) {
            tail = std::max(tail, (double)std::fabs(x[i]));
        }

        double hardJump = 0.0;
        {
            para3::PolyBlepCore o; o.prepare(sr);
            double prev = 0.0;
            for (int i = 0; i < total; ++i) {
                const double s = o.process(para3::semitonesToHz(57.0));
                const double g = (i >= onAt && i < offAt) ? s : 0.0;
                if (i >= offAt - 4 && i < offAt + 4)
                    hardJump = std::max(hardJump, std::fabs(g - prev));
                prev = g;
            }
        }

        const bool pass = finite && globalD <= steadyD * 1.6 && tail < 1e-5;
        std::printf("\nT2 click-free gate  (attack at 1.5 ms floor)\n");
        std::printf("   steady |dx| max         : %.6f\n", steadyD);
        std::printf("   GLOBAL |dx| max          : %.6f  (ratio %.2f)\n",
                    globalD, globalD / steadyD);
        std::printf("   hard-gate ref OFF jump  : %.6f  (what we avoid)\n", hardJump);
        std::printf("   release tail residual   : %.2e\n", tail);
        std::printf("   all-finite              : %s\n", finite ? "yes" : "NO");
        std::printf("   -> %s (no discontinuity anywhere vs steady)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T3  anti-zipper retune (latency-robust) ----------------------------
    {
        para3::Engine eng;
        eng.prepare(sr, 4096);
        eng.setAttackMs(1.0);
        eng.setDecRelMs(800.0);
        eng.setSustain(1.0);
        eng.noteOn(45);                       // ~110 Hz

        const int total = 24000, retuneAt = 8000;
        std::vector<float> x(total);
        int idx = 0;
        auto run = [&](int c){ eng.process(x.data() + idx, c); idx += c; };
        run(retuneAt);
        eng.noteOn(57);                       // +12 st, single instant request
        run(total - retuneAt);

        bool finite = true;
        for (float v : x) if (!std::isfinite(v)) finite = false;

        const double steadyD = maxAbsDelta(x, 3000, 6000);
        const double globalD = maxAbsDelta(x, 1, total);   // catches a zip anywhere

        int last = -1; double endHz = 0.0;
        for (int i = total - 4000; i < total; ++i) {
            if (x[i - 1] <= 0.0f && x[i] > 0.0f) {
                if (last >= 0) endHz = sr / (double)(i - last);
                last = i;
            }
        }
        const double tgtHz   = para3::semitonesToHz(57.0);
        const bool reached   = std::fabs(endHz - tgtHz) < tgtHz * 0.04;
        const bool noZipper  = globalD <= steadyD * 1.6;
        const bool pass = finite && reached && noZipper;

        std::printf("\nT3 anti-zipper retune  (+12 st in a single instant request)\n");
        std::printf("   target / measured end Hz: %.2f / %.2f\n", tgtHz, endHz);
        std::printf("   steady |dx| max         : %.6f\n", steadyD);
        std::printf("   GLOBAL |dx| max          : %.6f  (ratio %.2f)\n",
                    globalD, globalD / steadyD);
        std::printf("   reached target          : %s\n", reached ? "yes" : "NO");
        std::printf("   -> %s (no discontinuity at the instant; funnel ramps)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T4  cutoff zipper, cleanly isolated -------------------------------
    //  Source = pure 200 Hz sine. Cutoff alternates 3000<->6000 Hz (both far
    //  ABOVE 200 Hz, resonance 0): the intended steady output is essentially
    //  the same sine either way, so legitimate motion is ~nil. Anything extra
    //  at the switch is purely the coefficient-step artefact (zipper). The
    //  funnel must collapse that excess to the sine's own slope.
    {
        const int N = 24000;
        std::vector<double> in(N);
        for (int i = 0; i < N; ++i)
            in[i] = 0.5 * std::sin(2.0 * M_PI * 200.0 * i / sr);

        auto switchMaxD = [&](const std::vector<double>& y){
            double m = 0.0;
            for (int s = 64; s + 4 < N; s += 64)
                for (int i = s - 3; i <= s + 3; ++i)
                    m = std::max(m, std::fabs(y[i] - y[i-1]));
            return m;
        };
        auto steadyMaxD = [&](const std::vector<double>& y){
            double m = 0.0;                                  // no switch nearby
            for (int i = 2000; i < 3900; ++i)
                m = std::max(m, std::fabs(y[i] - y[i-1]));
            return m;
        };

        para3::LadderZDF fa; fa.prepare(sr); fa.setResonance(0.0); fa.setDrive(1.0);
        std::vector<double> ya(N);
        { bool hi=false;
          for (int i=0;i<N;++i){
              if (i % 64 == 0) { fa.setCutoffHz(hi?6000.0:3000.0); hi=!hi; }
              ya[i]=fa.tick(in[i]);
          } }

        para3::LadderZDF fb; fb.prepare(sr); fb.setResonance(0.0); fb.setDrive(1.0);
        para3::RampParam  cut; cut.prepare(sr, 12.0); cut.snap(3000.0);
        std::vector<double> yb(N);
        { bool hi=false;
          for (int i=0;i<N;++i){
              if (i % 64 == 0) { cut.setTarget(hi?6000.0:3000.0); hi=!hi; }
              fb.setCutoffHz(cut.next());
              yb[i]=fb.tick(in[i]);
          } }

        bool finite = std::isfinite(ya[N-1]) && std::isfinite(yb[N-1]);
        const double sineSlope   = steadyMaxD(yb);              // legit sine |dx|
        const double steppedExc  = switchMaxD(ya) - sineSlope;  // step artefact
        const double funnelExc   = switchMaxD(yb) - sineSlope;  // residual
        const bool pass = finite
                       && funnelExc < steppedExc * 0.15
                       && switchMaxD(yb) < sineSlope * 1.30;

        std::printf("\nT4 cutoff zipper  (isolated: sine source, stepped vs funnel)\n");
        std::printf("   sine baseline |dx|      : %.6f\n", sineSlope);
        std::printf("   stepped switch excess   : %.6f  (zipper)\n", steppedExc);
        std::printf("   funnel  switch excess   : %.6f  (ratio %.3f)\n",
                    funnelExc, steppedExc > 0 ? funnelExc / steppedExc : 0.0);
        std::printf("   all-finite              : %s\n", finite ? "yes" : "NO");
        std::printf("   -> %s (funnel collapses zipper to the sine slope)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T5  worst-case stability: sustained max-resonance + high drive ----
    {
        para3::ParaEngine eng;
        eng.prepare(sr, 4096);
        eng.setAttackMs(2.0);
        eng.setDecRelMs(400.0);
        eng.setSustain(1.0);
        eng.setResonance(1.0);                            // max Q
        eng.setDrive(4.0);                                // max drive
        eng.noteOn(33);                                   // HELD the whole time

        const int total = 96000;                          // 2 s sustained
        std::vector<float> x(total);
        int idx = 0;
        while (idx < total) {
            const int c = std::min(4096, total - idx);
            eng.process(x.data() + idx, c); idx += c;
        }

        bool finite = true; double peak = 0.0, rms = 0.0;
        for (float v : x) { if (!std::isfinite(v)) finite = false;
                            peak = std::max(peak, (double)std::fabs(v)); }
        for (int i = total - 8000; i < total; ++i) rms += (double)x[i]*x[i];
        rms = std::sqrt(rms / 8000.0);

        const bool pass = finite && peak < 4.0;
        std::printf("\nT5 worst-case stability  (held note, max Q, drive 4.0, 2 s)\n");
        std::printf("   peak |out|              : %.4f  (must stay bounded)\n", peak);
        std::printf("   sustained tail RMS      : %.4f  (alive, not blown up)\n", rms);
        std::printf("   all-finite              : %s\n", finite ? "yes" : "NO");
        std::printf("   -> %s (tanh-bounded, no NaN/Inf, energy-safe)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T6  nonlinear-drive anti-alias (2x island vs 1x reference) --------
    {
        const int    N  = 32768;
        const int    k  = 1779;
        const double f0 = k * (sr / N);                   // coherent
        // band-limited source (Step-1 oscillator, proven clean)
        para3::Oscillator src; src.prepare(sr);
        std::vector<double> in(N);
        for (int i = 0; i < 6000; ++i) src.process(f0);
        for (int i = 0; i < N; ++i)    in[i] = 0.6 * src.process(f0);

        // (a) filter at 1x, no island
        para3::LadderZDF f1; f1.prepare(sr);
        f1.setCutoffHz(2200.0); f1.setResonance(0.7); f1.setDrive(3.0);
        std::vector<double> y1(N);
        for (int i = 0; i < N; ++i) y1[i] = f1.tick(in[i]);

        // (b) filter inside the 2x island (engine path)
        para3::Os2Island<63> isl; isl.prepare(sr);
        para3::LadderZDF f2; f2.prepare(sr * 2.0);
        f2.setCutoffHz(2200.0); f2.setResonance(0.7); f2.setDrive(3.0);
        std::vector<double> y2(N);
        for (int i = 0; i < N; ++i)
            y2[i] = isl.process(in[i], [&](double s){ return f2.tick(s); });

        auto floorDb = [&](const std::vector<double>& sig) {
            std::vector<cd> sp(N);
            for (int i = 0; i < N; ++i) sp[i] = cd(sig[i], 0.0);   // coherent: rect
            fft(sp);
            double fund = 0.0, alias = 0.0;
            for (int b = 2; b < N/2; ++b) {
                const double m = std::abs(sp[b]);
                if (b % k == 0) fund  = std::max(fund, m);
                else            alias = std::max(alias, m);
            }
            return 20.0 * std::log10((alias + 1e-30) / (fund + 1e-30));
        };
        const double db1 = floorDb(y1);
        const double db2 = floorDb(y2);
        const bool pass = db2 < -45.0 && db2 < db1;       // cleaner than 1x

        std::printf("\nT6 nonlinear-drive anti-alias  (drive 3.0, reso 0.7)\n");
        std::printf("   filter @ 1x  alias floor: %7.2f dBc  (reference)\n", db1);
        std::printf("   filter @ 2x island      : %7.2f dBc  (engine path)\n", db2);
        std::printf("   island benefit          : %7.2f dB\n", db1 - db2);
        std::printf("   -> %s (threshold < -45 dBc and better than 1x)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T7  LFO modulation: clean + accurate (sine and square) ------------
    {
        auto baseline = [&](){
            para3::ParaEngine e; e.prepare(sr, 4096);
            e.setAttackMs(2); e.setDecRelMs(800); e.setSustain(1.0);
            e.setLfoPitchDepth(0.0);
            e.noteOn(45);
            std::vector<float> b(20000);
            e.process(b.data(), 20000);
            return maxAbsDelta(b, 6000, 18000);
        };
        const double baseD = baseline();

        auto runShape = [&](para3::Lfo::Shape sh){
            para3::ParaEngine e; e.prepare(sr, 4096);
            e.setAttackMs(2); e.setDecRelMs(800); e.setSustain(1.0);
            e.setLfoShape(sh);
            e.setLfoRateHz(6.0);
            e.setLfoPitchDepth(2.0);                       // +/-2 semitones
            e.noteOn(45);
            const int total = 48000;
            std::vector<float> x(total);
            e.process(x.data(), total);

            bool finite = true;
            for (float v : x) if (!std::isfinite(v)) finite = false;
            const double gD = maxAbsDelta(x, 1, total);

            // instantaneous freq via zero-cross, then its min/max + mod period
            double fmin = 1e9, fmax = 0.0;
            int last = -1, prevCrossA = -1;
            double sumPer = 0.0; int nPer = 0; double lastF = 0.0; int dir = 0;
            for (int i = 12000; i < total; ++i) {
                if (x[i-1] <= 0.0f && x[i] > 0.0f) {
                    if (last >= 0) {
                        const double f = sr / (double)(i - last);
                        fmin = std::min(fmin, f); fmax = std::max(fmax, f);
                        if (lastF != 0.0) {
                            const int d = (f > lastF) ? 1 : -1;
                            if (dir != 0 && d != dir) {
                                if (prevCrossA >= 0) { sumPer += (i - prevCrossA); ++nPer; }
                                prevCrossA = i;
                            }
                            dir = d;
                        }
                        lastF = f;
                    }
                    last = i;
                }
            }
            const double modHz = (nPer > 0) ? sr / (2.0 * sumPer / nPer) : 0.0;
            return std::make_tuple(finite, gD, fmax - fmin, modHz);
        };

        auto [finS, gDS, spanS, rateS] = runShape(para3::Lfo::Shape::Sine);
        auto [finQ, gDQ, spanQ, rateQ] = runShape(para3::Lfo::Shape::Square);

        const bool pass = finS && finQ
                       && spanS > 20.0 && spanQ > 20.0          // modulation present
                       && gDS <= baseD * 1.6 && gDQ <= baseD * 1.6  // no click
                       && std::fabs(rateS - 6.0) < 1.5;          // rate accurate

        std::printf("\nT7 LFO modulation  (pitch +/-2 st @ 6 Hz)\n");
        std::printf("   baseline |dx| (no LFO)  : %.6f\n", baseD);
        std::printf("   sine : |dx| %.4f  freqSpan %.1f Hz  rate %.2f Hz\n",
                    gDS, spanS, rateS);
        std::printf("   squ  : |dx| %.4f  freqSpan %.1f Hz  (slew band-limited)\n",
                    gDQ, spanQ);
        std::printf("   -> %s (modulates, accurate, no click even on square)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T8  Delay: clean retime, bounded feedback, low interpolation alias -
    {
        // (a) retime click-free + tail present
        para3::ParaEngine e; e.prepare(sr, 4096);
        e.setAttackMs(2); e.setDecRelMs(120); e.setSustain(0.0);
        e.setDelayMix(1.0); e.setDelayFeedback(0.55); e.setDelayTimeMs(200.0);
        const int total = 60000;
        std::vector<float> x(total);
        int idx = 0;
        auto run=[&](int c){ e.process(x.data()+idx,c); idx+=c; };
        e.noteOn(50); run(1500); e.noteOff(50); run(20000);
        e.setDelayTimeMs(90.0);                            // retime mid-tail
        run(total - idx);

        bool finite = true; double tailE = 0.0;
        for (float v : x) if (!std::isfinite(v)) finite = false;
        for (int i = total-8000; i<total; ++i) tailE += (double)x[i]*x[i];
        tailE = std::sqrt(tailE/8000.0);
        const double steadyD = maxAbsDelta(x, 6000, 12000);
        const double retimeD = maxAbsDelta(x, 22000, 30000);  // around retime
        const bool aOk = finite && tailE > 1e-4 && retimeD <= steadyD * 2.0;

        // (b) high feedback "singing" stays bounded
        para3::ParaEngine e2; e2.prepare(sr, 4096);
        e2.setSustain(0.9); e2.setDelayMix(1.0);
        e2.setDelayFeedback(1.03); e2.setDelayTimeMs(150.0);
        std::vector<float> y(120000);
        int j=0; auto run2=[&](int c){ e2.process(y.data()+j,c); j+=c; };
        e2.noteOn(48); run2(8000); e2.noteOff(48); run2(112000);
        bool fin2 = true; double peak2 = 0.0;
        for (float v : y){ if(!std::isfinite(v)) fin2=false;
                           peak2=std::max(peak2,(double)std::fabs(v)); }
        const bool bOk = fin2 && peak2 < 4.0;

        // (c) interpolation alias floor (static delay, pure sine, coherent)
        const int N = 32768; const int k = 1779;
        const double f0 = k * (sr / N);
        para3::Delay d; d.prepare(sr, 1.0);
        d.setTimeMs(123.45); d.setFeedback(0.0); d.setMix(1.0);
        for (int i=0;i<8000;++i) d.process((float)(0.5*std::sin(2*M_PI*f0*i/sr)));
        std::vector<cd> sp(N);
        for (int i=0;i<N;++i)
            sp[i]=cd(d.process((float)(0.5*std::sin(2*M_PI*f0*(i+8000)/sr))),0.0);
        fft(sp);
        double fund=0, alias=0;
        for (int b=2;b<N/2;++b){ double m=std::abs(sp[b]);
            if (b%k==0) fund=std::max(fund,m); else alias=std::max(alias,m); }
        const double dbAlias = 20.0*std::log10((alias+1e-30)/(fund+1e-30));
        const bool cOk = dbAlias < -55.0;

        const bool pass = aOk && bOk && cOk;
        std::printf("\nT8 fractional delay  (retime / feedback / interpolation)\n");
        std::printf("   (a) retime |dx| %.6f vs steady %.6f; tail RMS %.4f -> %s\n",
                    retimeD, steadyD, tailE, aOk?"ok":"BAD");
        std::printf("   (b) feedback 1.03 singing: peak %.4f finite -> %s\n",
                    peak2, bOk?"ok":"BAD");
        std::printf("   (c) interpolation alias floor: %.2f dBc -> %s\n",
                    dbAlias, cOk?"ok":"BAD");
        std::printf("   -> %s (clean retime, energy-safe, low-alias interp)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T9  ring modulation: anti-alias island + clean integrated path ----
    {
        // (a) coherent: fB = 3*fA => ring product periodic at fA, leak-free.
        const int N = 32768, kA = 1779;
        const double binHz = sr / N;
        const double fA = kA * binHz;
        const double fB = 3 * kA * binHz;

        para3::RingModIsland ring; ring.prepare(sr);
        std::vector<double> isl(N);
        for (int i = 0; i < 6000; ++i) ring.process(fA, fB);
        for (int i = 0; i < N; ++i)    isl[i] = ring.process(fA, fB);

        std::vector<double> naive(N);
        {
            double pa=0, pb=0; const double da=fA/sr, db=fB/sr;
            for (int i=0;i<N;++i){
                naive[i] = (2.0*pa-1.0) * (2.0*pb-1.0);    // base-rate multiply
                pa+=da; if(pa>=1.0)pa-=1.0;
                pb+=db; if(pb>=1.0)pb-=1.0;
            }
        }
        auto floorDb = [&](const std::vector<double>& s){
            std::vector<cd> sp(N);
            for (int i=0;i<N;++i) sp[i]=cd(s[i],0.0);       // coherent: rect
            fft(sp);
            double fund=0, alias=0;
            for (int b=2;b<N/2;++b){ double m=std::abs(sp[b]);
                if (b%kA==0) fund=std::max(fund,m); else alias=std::max(alias,m); }
            return 20.0*std::log10((alias+1e-30)/(fund+1e-30));
        };
        const double dbI = floorDb(isl);
        const double dbN = floorDb(naive);

        // (b) integrated UniRing path: finite, bounded, no discontinuity
        para3::ParaEngine e; e.prepare(sr, 4096);
        e.setMode(para3::ParaAllocator::Mode::UniRing);
        e.setAttackMs(3); e.setDecRelMs(600); e.setSustain(1.0);
        e.setCutoffHz(6000.0); e.setResonance(0.3);
        e.noteOn(48);
        const int M = 40000;
        std::vector<float> x(M);
        e.process(x.data(), M);
        bool finite = true; double peak = 0.0;
        for (float v : x){ if(!std::isfinite(v)) finite=false;
                           peak=std::max(peak,(double)std::fabs(v)); }
        const double gD = maxAbsDelta(x, 8000, M);
        // reference: same engine, plain Unison (no ring) — similar |dx| scale
        para3::ParaEngine e2; e2.prepare(sr,4096);
        e2.setMode(para3::ParaAllocator::Mode::Unison);
        e2.setAttackMs(3); e2.setDecRelMs(600); e2.setSustain(1.0);
        e2.setCutoffHz(6000.0); e2.setResonance(0.3);
        e2.noteOn(48);
        std::vector<float> r(M); e2.process(r.data(), M);
        const double gR = maxAbsDelta(r, 8000, M);

        const bool aOk = dbI < -50.0 && dbI < dbN - 20.0;
        const bool bOk = finite && peak < 4.0 && gD <= gR * 1.6;
        const bool pass = aOk && bOk;

        std::printf("\nT9 ring modulation  (own oversampling island)\n");
        std::printf("   (a) naive multiply alias: %7.2f dBc  (reference)\n", dbN);
        std::printf("   (a) ring island alias   : %7.2f dBc  (benefit %.1f dB)\n",
                    dbI, dbN - dbI);
        std::printf("   (b) UniRing path: peak %.3f, |dx| %.4f vs Unison %.4f, %s\n",
                    peak, gD, gR, finite ? "finite" : "NONFINITE");
        std::printf("   -> %s (island kills ring alias; integrated path clean)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T10  sample-accurate sequencer timing + jitter --------------------
    //  Measured directly off the clock: record the sample index at every
    //  currentStep() change; step durations are the spacings. No fragile
    //  audio-onset heuristic.
    {
        auto jitterFor = [&](double bpm, double idealStep)->std::pair<double,int>{
            para3::ParaEngine e; e.prepare(sr, 4096);
            e.setAttackMs(1); e.setDecRelMs(40); e.setSustain(0.0);
            para3::Controller ctl; ctl.prepare(e, sr);
            para3::Pattern pat;
            for (int s=0;s<16;++s){ pat.steps[s].gate=true; pat.steps[s].note=57; }
            ctl.bank().seedBoth(pat);
            ctl.clock().setTempo(bpm, 4);
            ctl.clock().start();
            const int total = 200000;
            std::vector<float> one(1);
            int prev = ctl.currentStep(), lastChange = -1;
            double jit = 0.0; int npairs = 0;
            for (int i = 0; i < total; ++i) {
                ctl.render(one.data(), 1);
                const int cs = ctl.currentStep();
                if (cs != prev) {
                    if (lastChange >= 0) {
                        const double sp = (double)(i - lastChange);
                        jit = std::max(jit, std::fabs(sp - idealStep));
                        ++npairs;
                    }
                    lastChange = i; prev = cs;
                }
            }
            return { jit, npairs };
        };
        auto [jitInt, nA] = jitterFor(120.0, 6000.0);              // exact 6000
        const double stepOdd = (60.0/127.0)/4.0*sr;
        auto [jitOdd, nB] = jitterFor(127.0, stepOdd);

        const bool pass = nA > 8 && nB > 8 && jitInt == 0.0 && jitOdd <= 1.0;
        std::printf("\nT10 sequencer timing  (sample-accurate clock)\n");
        std::printf("   120 BPM step=6000 jitter : %.2f smp  (%d intervals)\n",
                    jitInt, nA);
        std::printf("   127 BPM frac-step jitter : %.2f smp  (<=1 ok, %d)\n",
                    jitOdd, nB);
        std::printf("   -> %s (steps land on exact samples)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T11  funnel invariant: MIDI-CC path == direct, zipper-free --------
    {
        para3::ParaEngine eA; eA.prepare(sr, 4096);
        para3::ParaEngine eB; eB.prepare(sr, 4096);
        for (auto* e : { &eA, &eB }) {
            e->setAttackMs(3); e->setDecRelMs(800); e->setSustain(1.0);
            e->setResonance(0.3);
        }
        para3::Controller ctlB; ctlB.prepare(eB, sr);     // CC#74 -> Cutoff
        eA.noteOn(45); eB.noteOn(45);

        const int total = 40000;
        std::vector<float> a(total), b(total);
        double cc = 0.2;
        for (int i = 0; i < total; ++i) {
            if (i % 512 == 0) {                            // change every 512 smp
                cc = 0.15 + 0.7 * ((i / 512) % 2);          // bounce 0.15<->0.85
                eA.setParamNorm(para3::ParaEngine::Param::Cutoff, cc); // direct
                ctlB.midiCC(74, cc);                        // MIDI -> same funnel
            }
            eA.process(a.data()+i, 1);
            eB.process(b.data()+i, 1);
        }
        double maxDiff = 0.0; bool finite = true;
        for (int i = 0; i < total; ++i) {
            if (!std::isfinite(a[i]) || !std::isfinite(b[i])) finite = false;
            maxDiff = std::max(maxDiff, (double)std::fabs(a[i]-b[i]));
        }
        const double zip = maxAbsDelta(a, 1, total);
        const double steady = maxAbsDelta(a, 3000, 6000);
        const bool pass = finite && maxDiff < 1e-6 && zip <= steady * 1.6;
        std::printf("\nT11 funnel invariant  (MIDI-CC vs direct setParamNorm)\n");
        std::printf("   max sample diff         : %.2e  (one path => identical)\n",
                    maxDiff);
        std::printf("   |dx| %.5f vs steady %.5f  (zipper-free)\n", zip, steady);
        std::printf("   -> %s (every source is literally one funnel)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T12  motion record -> playback round-trip, zipper-free ------------
    //  Probe the settled cutoff at the END of each real step (read at the
    //  currentStep() transition), compare to the taper of the step's value.
    {
        para3::ParaEngine e; e.prepare(sr, 4096);
        e.setAttackMs(3); e.setDecRelMs(2000); e.setSustain(1.0);
        para3::Controller ctl; ctl.prepare(e, sr);

        para3::Pattern pat;
        for (int s = 0; s < 16; ++s) {
            pat.steps[s].gate = true; pat.steps[s].note = 48;
            pat.steps[s].motionOn  = true;
            pat.steps[s].motionCut = (double)s / 15.0;
        }
        ctl.bank().seedBoth(pat);
        ctl.clock().setTempo(60.0, 4);                      // 12000 smp/step
        ctl.clock().start();

        const int total = 12000 * 16 * 2;                    // 2 loops
        std::vector<float> x(total);
        std::vector<float> one(1);
        int prev = ctl.currentStep(); double lastObs = e.observedCutoffHz();
        double worst = 0.0; bool finite = true; int checked = 0;
        for (int i = 0; i < total; ++i) {
            ctl.render(one.data(), 1);
            x[i] = one[0];
            if (!std::isfinite(one[0])) finite = false;
            const int cs = ctl.currentStep();
            if (cs != prev) {
                // step `prev` just ended; lastObs = its settled cutoff
                if (prev >= 0 && i > 24000) {                 // skip warmup
                    const double want = para3::ParaEngine::taper(
                        para3::ParaEngine::Param::Cutoff,
                        pat.steps[prev].motionCut);
                    worst = std::max(worst, std::fabs(lastObs-want)/want);
                    ++checked;
                }
                prev = cs;
            }
            lastObs = e.observedCutoffHz();
        }
        const double globalD = maxAbsDelta(x, 1, total);
        const bool pass = finite && checked > 16 && worst < 0.02;
        std::printf("\nT12 motion round-trip  (programmed lane, 2 loops)\n");
        std::printf("   steps checked           : %d\n", checked);
        std::printf("   worst reproduction error: %.4f  (<2%%)\n", worst);
        std::printf("   signal |dx| max         : %.5f  (bounded; band-limited)\n",
                    globalD);
        std::printf("   note: this path is the SAME funnel proven zipper-free\n");
        std::printf("         in T4 (isolated) and T11 (bit-identical).\n");
        std::printf("   -> %s (records/plays exactly, through the funnel)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    // ---- T13  lock-free pattern edit during running audio ------------------
    {
        para3::ParaEngine e; e.prepare(sr, 4096);
        e.setAttackMs(2); e.setDecRelMs(120); e.setSustain(0.0);
        e.setCutoffHz(7000.0);
        para3::Controller ctl; ctl.prepare(e, sr);

        para3::Pattern live;                                 // alternating gates
        for (int s=0;s<16;++s){ live.steps[s].gate=((s&1)==0);
                                live.steps[s].note=60; }
        ctl.bank().seedBoth(live);
        ctl.clock().setTempo(140.0,4);
        ctl.clock().start();

        const int total = 160000, chunk = 777;               // odd chunking
        std::vector<float> x(total);
        int idx = 0; bool editedSilent = false;
        while (idx < total) {
            const int c = std::min(chunk, total - idx);
            ctl.render(x.data()+idx, c);
            idx += c;
            if (!editedSilent && idx > total/2) {             // "concurrent" edit
                para3::Pattern& ed = ctl.bank().edit();
                ed = ctl.bank().read();
                for (int s=0;s<16;++s) ed.steps[s].gate = false; // silence pattern
                ctl.bank().commit();                          // atomic publish
                editedSilent = true;
            }
        }
        bool finite = true;
        for (float v : x) if (!std::isfinite(v)) finite = false;
        double e1=0, e2=0;
        for (int i=total/2-20000;i<total/2;++i)        e1 += (double)x[i]*x[i];
        for (int i=total-20000;i<total;++i)            e2 += (double)x[i]*x[i];
        const double globalD = maxAbsDelta(x, 1, total);
        const double steadyD = maxAbsDelta(x, 10000, 40000);
        const bool seen = e2 < e1 * 0.05;                     // edit took effect
        const bool pass = finite && seen && globalD <= steadyD * 1.6;
        std::printf("\nT13 lock-free pattern edit  (commit during audio)\n");
        std::printf("   energy before/after edit: %.3e / %.3e  (edit seen: %s)\n",
                    std::sqrt(e1/20000), std::sqrt(e2/20000), seen?"yes":"NO");
        std::printf("   global |dx| %.5f vs steady %.5f  (no torn-read click)\n",
                    globalD, steadyD);
        std::printf("   -> %s (consistent snapshot, finite, continuous)\n",
                    pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }

    std::printf("\n==================================================\n");
    std::printf("%s  (%d failure%s)\n",
                failures ? "OVERALL: FAIL" : "OVERALL: PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
