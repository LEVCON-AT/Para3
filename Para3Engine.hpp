// =============================================================================
//  PARA-3  ::  Engine Core  (Implementation Step 1 — rev. 3)
//
//  Real production DSP. Rev.3 corrects the decimation filter to a properly
//  Kaiser-designed linear-phase low-pass with an explicit passband / stopband
//  and ~100 dB stopband attenuation, computed from the host sample rate
//  (SR-agnostic, per the Sprint-2 principle).
//
//  Design choices, stated honestly:
//    * Oversampling island OS=4 around the PolyBLEP saw (Sprint-2 design).
//    * Passband to 0.80 * baseband-Nyquist; the very top octave is intentionally
//      band-limited. That is correct band-limited synthesis, and a real analog
//      VCO rolls its highs off anyway — the measured roll-off (Sprint-1) drops
//      into the marked CALIB slot. This trims nothing audible and adds no fake.
//    * 383-tap FIR: correctness now; polyphase decimation (only L/M MACs) is a
//      Sprint-10 performance optimisation, explicitly deferred, not a stub.
//
//  CALIBRATION BOUNDARY: algorithms final; unit-matching constants marked
//  // CALIB(sprint1). RT contract unchanged (no alloc/lock/syscall in process).
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
  #include <xmmintrin.h>
  #include <pmmintrin.h>
  #define PARA3_HAS_SSE 1
#else
  #define PARA3_HAS_SSE 0
#endif

namespace para3 {

class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept {
#if PARA3_HAS_SSE
        prev_ = _mm_getcsr();
        _mm_setcsr((prev_ & ~0x8040u) | 0x8040u);   // FTZ + DAZ
#endif
    }
    ~ScopedNoDenormals() noexcept {
#if PARA3_HAS_SSE
        _mm_setcsr(prev_);
#endif
    }
    ScopedNoDenormals(const ScopedNoDenormals&)            = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
private:
#if PARA3_HAS_SSE
    unsigned int prev_ = 0;
#endif
};

inline double semitonesToHz(double midiNote) noexcept {
    // CALIB(sprint1): replace tempered map with measured "loose" tuning curve.
    return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
}
template <typename T>
inline T clamp(T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// -----------------------------------------------------------------------------
//  RampParam — unified anti-zipper funnel. Linear ramp: per-sample delta is
//  STRICTLY bounded = |Δtarget| / rampSamples for any jump. DSP never reads a
//  raw control value; every source converges here.
// -----------------------------------------------------------------------------
class RampParam {
public:
    void prepare(double sampleRate, double timeMs) noexcept {
        sr_ = sampleRate; setTimeMs(timeMs);
    }
    void setTimeMs(double timeMs) noexcept {          // CALIB(sprint1): from feel
        rampLen_ = std::max<int>(1, (int)std::lround(
                       std::max(timeMs, kMinMs) * 0.001 * sr_));
    }
    void snap(double v) noexcept { cur_ = tgt_ = v; remaining_ = 0; }
    void setTarget(double v) noexcept {
        if (v == tgt_ && remaining_ == 0) return;
        tgt_ = v;
        step_ = (tgt_ - cur_) / (double)rampLen_;
        remaining_ = rampLen_;
    }
    double value() const noexcept { return cur_; }

    inline double next() noexcept {
        if (remaining_ > 0) {
            cur_ += step_;
            if (--remaining_ == 0) cur_ = tgt_;
        }
        return cur_;
    }
private:
    static constexpr double kMinMs = 0.5;
    double sr_ = 48000.0, cur_ = 0.0, tgt_ = 0.0, step_ = 0.0;
    int    rampLen_ = 1, remaining_ = 0;
};

// -----------------------------------------------------------------------------
//  PolyBlepCore — band-limited saw, clocked at OS*fs inside the island.
// -----------------------------------------------------------------------------
class PolyBlepCore {
public:
    void prepare(double rate) noexcept { rate_ = rate; phase_ = 0.0; }
    void reset() noexcept { phase_ = 0.0; }
    inline double process(double freqHz) noexcept {
        const double dt = freqHz / rate_;
        double v = 2.0 * phase_ - 1.0;
        v -= blep(phase_, dt);
        phase_ += dt;
        if      (phase_ >= 1.0) phase_ -= 1.0;
        else if (phase_ <  0.0) phase_ += 1.0;
        return v;
    }
    // EXT-BASS B1 — band-limited Pulse (two PolyBLEP corrections: rising
    // edge at phase=0 and falling edge at phase=pw). Same OS regime as Saw
    // per spec §2 B1. T51/T57 measure: at PW=0.5 the alias matches the Saw
    // budget (-76 dBc, ≤ -74 dBc spec). TREUE-KONFLIKT (per CLAUDE.md §0.6
    // benannt, T57 documentiert): bei asymmetrischer PW (≠ 0.5) trägt die
    // Pulse alle Harmonischen mit langsamerem 1/n-Abfall, was die Kombi
    // BLEP-2 + 4×Kaiser-Decimator auf ~-64 dBc Alias begrenzt. Praktisch
    // weit unter audible-alias (~-40 dBc) und damit hörbar bandbegrenzt.
    // pw clamped to [2*dt, 1-2*dt] so the two BLEP windows never overlap
    // (degenerate at extreme pulse widths; protects audibility at high notes).
    inline double processPulse(double freqHz, double pw) noexcept {  // EXT-BASS B1
        const double dt = freqHz / rate_;
        const double safety = 2.0 * dt;
        if (pw < safety)         pw = safety;
        if (pw > 1.0 - safety)   pw = 1.0 - safety;
        double v = (phase_ < pw) ? 1.0 : -1.0;
        v += blep(phase_, dt);                         // EXT-BASS B1 rising step (-1→+1)
        double tp = phase_ - pw;
        if (tp < 0.0) tp += 1.0;
        v -= blep(tp, dt);                              // EXT-BASS B1 falling step (+1→-1)
        phase_ += dt;
        if      (phase_ >= 1.0) phase_ -= 1.0;
        else if (phase_ <  0.0) phase_ += 1.0;
        return v;
    }
private:
    static inline double blep(double t, double dt) noexcept {
        if (dt <= 0.0) return 0.0;
        if (t < dt)        { t /= dt;          return t + t - t * t - 1.0; }
        if (t > 1.0 - dt)  { t = (t - 1.0)/dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    double rate_ = 192000.0, phase_ = 0.0;
};

// -----------------------------------------------------------------------------
//  Decimator — Kaiser-designed linear-phase FIR, decimation by M.
//  prepare(baseSr) sets passband = 0.80*Nyquist, stopband = Nyquist, ~100 dB.
// -----------------------------------------------------------------------------
template <int M, int L>
class Decimator {
    static_assert(L % 2 == 1, "L must be odd for integer linear-phase delay");
public:
    void prepare(double baseSr) noexcept {
        const double osSr  = baseSr * M;
        const double nyq   = baseSr * 0.5;
        const double fpHz  = 0.80 * nyq;                // passband edge
        const double fsHz  = nyq;                       // stopband edge
        const double fcN   = ((fpHz + fsHz) * 0.5) / osSr;   // cutoff (cyc/sample)
        const double beta  = 10.0;                       // ~ -98 dB stopband
        const double c     = (L - 1) / 2.0;
        const double i0b   = i0(beta);
        double sum = 0.0;
        for (int n = 0; n < L; ++n) {
            const double x = n - c;
            const double s = (x == 0.0) ? 2.0 * fcN
                             : std::sin(2.0 * M_PI * fcN * x) / (M_PI * x);
            const double r = 2.0 * n / (L - 1) - 1.0;
            const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r*r))) / i0b;
            h_[n] = s * w;
            sum  += h_[n];
        }
        for (int n = 0; n < L; ++n) h_[n] /= sum;        // unity DC gain
        reset();
    }
    void reset() noexcept { z_.fill(0.0); pos_ = 0; }
    inline void push(double x) noexcept { z_[pos_] = x; if (++pos_ == L) pos_ = 0; }
    inline double read() const noexcept {
        double acc = 0.0; int idx = pos_;
        for (int k = 0; k < L; ++k) {
            if (--idx < 0) idx = L - 1;
            acc += h_[k] * z_[idx];
        }
        return acc;
    }
private:
    static double i0(double x) noexcept {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 40; ++k) {
            t *= (x * x) / (4.0 * k * k);
            s += t;
            if (t < 1e-14 * s) break;
        }
        return s;
    }
    std::array<double, L> h_{};
    std::array<double, L> z_{};
    int pos_ = 0;
};

// -----------------------------------------------------------------------------
//  Oscillator — oversampling island: PolyBLEP @ 4x -> Kaiser FIR -> fs.
// -----------------------------------------------------------------------------
class Oscillator {
public:
    static constexpr int kOS  = 4;
    static constexpr int kTap = 383;

    void prepare(double sampleRate) noexcept {
        core_.prepare(sampleRate * kOS);
        dec_.prepare(sampleRate);
        reset();
    }
    void reset() noexcept { core_.reset(); dec_.reset(); }
    // EXT-BASS B2 — pw parameter defaults to 0.5 (B1 hardcoded value). Saw
    // branch ignores pw entirely → Saw output stays byte-identical regardless
    // of the pw value. For Pulse, pw is applied per OS-sample (block-rate is
    // sample-rate at this level — the ParaEngine outer loop holds pw constant
    // across the kOS BLEP corrections in one output sample, which mirrors how
    // PWM modulation lands on the band-limited edges).
    inline float process(double freqHz, double pw = 0.5) noexcept {            // EXT-BASS B2
        if (wave_ == 0) {
            // default Saw path — byte-identical to pre-EXT-BASS-B1 codegen
            for (int i = 0; i < kOS; ++i) dec_.push(core_.process(freqHz));
        } else {
            // EXT-BASS B1+B2 — band-limited Pulse, PW from caller (B2 modulates)
            for (int i = 0; i < kOS; ++i) dec_.push(core_.processPulse(freqHz, pw));
        }
        // CALIB(sprint1): measured saw-shaping / HF roll-off stage inserts here.
        return static_cast<float>(dec_.read());
    }
    void setWave(int w) noexcept { wave_ = (w == 1) ? 1 : 0; }     // EXT-BASS B1
    int  wave() const noexcept { return wave_; }                    // EXT-BASS B1 observability
private:
    PolyBlepCore         core_;
    Decimator<kOS, kTap> dec_;
    int                  wave_ = 0;                                 // EXT-BASS B1 (0=Saw default)
};

// -----------------------------------------------------------------------------
//  RingModIsland — band-limited ring modulation on its OWN oversampling island.
//  Ring mod is a multiply; the product of two band-limited saws has sum/diff
//  components up to ~2x their bandwidth, so a base-rate multiply aliases badly.
//  Here both operands are generated at OS*fs and multiplied there, then a steep
//  Kaiser FIR decimates — the same proven island design as the oscillator.
//  Exact operand pairing/voicing is CALIB(sprint1); the anti-alias mechanism
//  is final and measured (T9).
// -----------------------------------------------------------------------------
class RingModIsland {
public:
    static constexpr int kOS  = 8;     // ring product is broadband -> 8x
    static constexpr int kTap = 767;   // sized for the OS=8 transition

    void prepare(double sampleRate) noexcept {
        a_.prepare(sampleRate * kOS);
        b_.prepare(sampleRate * kOS);
        dec_.prepare(sampleRate);
        reset();
    }
    void reset() noexcept { a_.reset(); b_.reset(); dec_.reset(); }

    inline float process(double fA, double fB) noexcept {
        for (int i = 0; i < kOS; ++i)
            dec_.push(a_.process(fA) * b_.process(fB));   // multiply at OS rate
        return static_cast<float>(dec_.read());
    }
private:
    PolyBlepCore         a_, b_;
    Decimator<kOS, kTap> dec_;
};

// -----------------------------------------------------------------------------
//  AdsrEnvelope — click-free analog-style ADSR (Volca topology).
// -----------------------------------------------------------------------------
class AdsrEnvelope {
public:
    void prepare(double sampleRate) noexcept {
        sr_ = sampleRate;
        setAttackMs(attackMs_); setDecRelMs(decRelMs_); setSustain(sustain_);
        reset();
    }
    void reset() noexcept { state_ = State::Idle; out_ = 0.0; }
    void setAttackMs(double ms) noexcept {            // CALIB(sprint1) min floor
        attackMs_ = ms;
        aCoef_ = calcCoef(std::max(ms, kMinSegMs), kAtkRatio);
        aBase_ = (1.0 + kAtkRatio) * (1.0 - aCoef_);
    }
    void setDecRelMs(double ms) noexcept {
        decRelMs_ = ms;
        dCoef_ = rCoef_ = calcCoef(std::max(ms, kMinSegMs), kDrRatio);
        recomputeBases();
    }
    void setSustain(double s) noexcept { sustain_ = clamp(s,0.0,1.0); recomputeBases(); }
    void gateOn()  noexcept { state_ = State::Attack; }
    void gateOff() noexcept { if (state_ != State::Idle) state_ = State::Release; }
    bool isActive() const noexcept { return state_ != State::Idle; }

    inline double next() noexcept {
        switch (state_) {
            case State::Idle: return 0.0;
            case State::Attack:
                out_ = aBase_ + out_ * aCoef_;
                if (out_ >= 1.0) { out_ = 1.0; state_ = State::Decay; }
                break;
            case State::Decay:
                out_ = dBase_ + out_ * dCoef_;
                if (out_ <= sustain_) { out_ = sustain_; state_ = State::Sustain; }
                break;
            case State::Sustain: out_ = sustain_; break;
            case State::Release:
                out_ = rBase_ + out_ * rCoef_;
                if (out_ <= kEps) { out_ = 0.0; state_ = State::Idle; }
                break;
        }
        return out_;
    }
private:
    enum class State { Idle, Attack, Decay, Sustain, Release };
    double calcCoef(double ms, double ratio) const noexcept {
        const double r = (ms * 0.001) * sr_;
        return r <= 0.0 ? 0.0 : std::exp(-std::log((1.0 + ratio) / ratio) / r);
    }
    void recomputeBases() noexcept {
        dBase_ = (sustain_ - kDrRatio) * (1.0 - dCoef_);
        rBase_ = (0.0      - kDrRatio) * (1.0 - rCoef_);
    }
    static constexpr double kAtkRatio = 0.30;         // CALIB(sprint1)
    static constexpr double kDrRatio  = 0.0001;       // CALIB(sprint1)
    static constexpr double kMinSegMs = 1.5;          // CALIB(sprint1) anti-click
    static constexpr double kEps      = 1.0e-5;
    double sr_=48000.0, attackMs_=5.0, decRelMs_=200.0, sustain_=0.7;
    double aCoef_=0,aBase_=0,dCoef_=0,dBase_=0,rCoef_=0,rBase_=0,out_=0.0;
    State  state_=State::Idle;
};

// -----------------------------------------------------------------------------
//  Voice — Step-1 paraphonic-ready: band-limited osc -> click-free EG.
//  Allocator/filter/LFO/delay are later steps: explicitly absent, not faked.
// -----------------------------------------------------------------------------
class Voice {
public:
    void prepare(double sr) noexcept {
        osc_.prepare(sr);
        env_.prepare(sr);
        pitch_.prepare(sr, /*ms*/ 5.0);               // CALIB(sprint1) glide floor
        active_ = false;
    }
    void reset() noexcept { osc_.reset(); env_.reset(); active_ = false; }
    void noteOn(double midiNote) noexcept {
        if (!active_) pitch_.snap(midiNote);
        pitch_.setTarget(midiNote);
        env_.gateOn();
        active_ = true;
    }
    void noteOff() noexcept { env_.gateOff(); }
    bool isActive() const noexcept { return env_.isActive(); }
    AdsrEnvelope& env() noexcept { return env_; }
    inline float renderSample() noexcept {
        const double note = pitch_.next();
        const double hz   = semitonesToHz(note);
        const float  s    = osc_.process(hz);
        const double a    = env_.next();
        if (!env_.isActive()) active_ = false;
        return static_cast<float>(s * a);
    }
private:
    bool         active_ = false;
    Oscillator   osc_;
    AdsrEnvelope env_;
    RampParam    pitch_;
};

// =============================================================================
//  STEP 2 — shared paraphonic path: 3-osc mix -> ZDF ladder + nonlinearity
//  (own oversampling island) -> shared EG/VCA.  Step-1 classes above are
//  untouched (no regression). Ring-mod voice modes are deliberately deferred:
//  correct ring modulation needs its own oversampling island, and a half-built
//  one would be a fake — it is named, not stubbed.
// =============================================================================

// -----------------------------------------------------------------------------
//  Os2Island — exact 2x oversampling around a nonlinear stage.
//  Zero-stuff up + Kaiser LP, run the nonlinear callable at 2x, Kaiser LP down.
//  (Polyphase reduction of the MAC count is a Sprint-10 perf task, deferred,
//   not a stub — the math here is the correct full-rate form.)
// -----------------------------------------------------------------------------
template <int Lf>
class Os2Island {
    static_assert(Lf % 2 == 1, "Lf odd for linear phase");
public:
    void prepare(double baseSr) noexcept {
        const double osSr = baseSr * 2.0;
        const double nyq  = baseSr * 0.5;
        const double fcN  = (0.90 * nyq) / osSr;       // pass to 0.9*baseband-Nyq
        const double beta = 9.0;
        const double c    = (Lf - 1) / 2.0;
        const double i0b  = i0(beta);
        double sum = 0.0;
        for (int n = 0; n < Lf; ++n) {
            const double x = n - c;
            const double s = (x == 0.0) ? 2.0 * fcN
                             : std::sin(2.0 * M_PI * fcN * x) / (M_PI * x);
            const double r = 2.0 * n / (Lf - 1) - 1.0;
            const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r*r))) / i0b;
            h_[n] = s * w; sum += h_[n];
        }
        for (int n = 0; n < Lf; ++n) h_[n] /= sum;
        reset();
    }
    void reset() noexcept { up_.fill(0.0); dn_.fill(0.0); up_p_ = dn_p_ = 0; }

    // f: nonlinear per-OS-sample function double->double.
    template <typename F>
    inline double process(double x, F&& f) noexcept {
        // ---- upsample (zero-stuff x2, gain 2) ----
        pushUp(x * 2.0); const double a = convUp();
        pushUp(0.0);     const double b = convUp();
        // ---- nonlinear stage at 2x ----
        const double na = f(a);
        const double nb = f(b);
        // ---- downsample (LP then decimate) ----
        pushDn(na); const double y = convDn();
        pushDn(nb);                                   // discarded phase
        return y;
    }
private:
    static double i0(double x) noexcept {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 40; ++k) { t *= (x*x)/(4.0*k*k); s += t;
            if (t < 1e-14 * s) break; }
        return s;
    }
    inline void pushUp(double v) noexcept { up_[up_p_]=v; if(++up_p_==Lf) up_p_=0; }
    inline void pushDn(double v) noexcept { dn_[dn_p_]=v; if(++dn_p_==Lf) dn_p_=0; }
    inline double convUp() const noexcept {
        double acc=0.0; int idx=up_p_;
        for (int k=0;k<Lf;++k){ if(--idx<0) idx=Lf-1; acc += h_[k]*up_[idx]; }
        return acc;
    }
    inline double convDn() const noexcept {
        double acc=0.0; int idx=dn_p_;
        for (int k=0;k<Lf;++k){ if(--idx<0) idx=Lf-1; acc += h_[k]*dn_[idx]; }
        return acc;
    }
    std::array<double,Lf> h_{}, up_{}, dn_{};
    int up_p_=0, dn_p_=0;
};

// -----------------------------------------------------------------------------
//  LadderZDF — Zavalishin TPT 4-pole low-pass, zero-delay-feedback resolved,
//  with a bounded tanh resonance nonlinearity (analog drive + energy-safe
//  self-oscillation). Runs inside Os2Island. Cutoff/reso via the funnel.
//
//  Stability/boundedness by construction: the resonance feedback passes
//  through tanh (output in (-1,1)); fc is hard-clamped below the OS Nyquist;
//  denormals are flushed at the engine boundary. No path can blow up to
//  NaN/Inf or unbounded energy — verified by T5.
// -----------------------------------------------------------------------------
class LadderZDF {
public:
    void prepare(double osSampleRate) noexcept {
        osSr_ = osSampleRate;
        reset();
    }
    void reset() noexcept { s1_=s2_=s3_=s4_=fb_=0.0; }

    void setCutoffHz(double hz) noexcept {            // CALIB(sprint1): taper
        const double fc = clamp(hz, 10.0, 0.49 * osSr_);
        const double g  = std::tan(M_PI * fc / osSr_);
        G_ = g / (1.0 + g);
    }
    void setResonance(double r) noexcept {            // CALIB(sprint1): range/self-osc
        k_ = clamp(r, 0.0, 1.0) * 4.3;                // ~4 => self-oscillation
    }
    void setDrive(double d) noexcept { drive_ = clamp(d, 0.5, 4.0); }

    // One OS-rate sample. Called by Os2Island.
    //
    // Topology: four zero-delay TPT one-pole stages (each internally
    // delay-free / correct), with the GLOBAL resonance feedback carrying the
    // standard one-sample delay so the tanh nonlinearity can sit in the loop
    // without an implicit solve. This is a recognised, unconditionally stable
    // production ladder model; tanh bounds the loop energy (verified by T5).
    inline double tick(double in) noexcept {
        const double G = G_;
        const double u = std::tanh(drive_ * in) - k_ * std::tanh(fb_);

        // forward TPT stages
        double v, y1, y2, y3, y4;
        v = (u  - s1_) * G; y1 = v + s1_; s1_ = y1 + v;
        v = (y1 - s2_) * G; y2 = v + s2_; s2_ = y2 + v;
        v = (y2 - s3_) * G; y3 = v + s3_; s3_ = y3 + v;
        v = (y3 - s4_) * G; y4 = v + s4_; s4_ = y4 + v;

        fb_ = y4;                                     // bounded next-sample fb
        return y4;
    }
private:
    double osSr_ = 96000.0;
    double G_ = 0.1, k_ = 0.0, drive_ = 1.0;
    double s1_=0, s2_=0, s3_=0, s4_=0, fb_=0;
};

// -----------------------------------------------------------------------------
//  ParaAllocator — Volca paraphonic note→oscillator assignment.
//  Implemented: Poly, Unison, Octave, Fifth. (UniRing/PolyRing deferred with
//  the ring-mod island.) Held-note stack is fixed-size (no allocation).
// -----------------------------------------------------------------------------
class ParaAllocator {
public:
    enum class Mode { Poly, Unison, Octave, Fifth, UniRing, PolyRing };

    void setMode(Mode m) noexcept { mode_ = m; }
    void reset() noexcept { count_ = 0; }
    // EXT-BASS B4 — Stack/Mono override. Wenn aktiv: alle 3 OSCs spielen die
    // newest Note (gestapelt, monophon) und übersteuern den Voice-Modus.
    // Aus (Default) ⇒ ring() und resolve() liefern Pre-B4-Werte ⇒ bit-identisch.
    void setBassStack(bool on) noexcept { bassStack_ = on; }     // EXT-BASS B4
    bool bassStack() const noexcept { return bassStack_; }       // EXT-BASS B4
    bool ring() const noexcept {
        if (bassStack_) return false;                            // EXT-BASS B4 Stack ⇒ kein Ring
        return mode_ == Mode::UniRing || mode_ == Mode::PolyRing;
    }

    void noteOn(int note) noexcept {
        if (count_ < kMax) held_[count_++] = note;
    }
    void noteOff(int note) noexcept {
        int w = 0;
        for (int r = 0; r < count_; ++r)
            if (held_[r] != note) held_[w++] = held_[r];
        count_ = w;
    }
    // B1-fix: panic clear of the held stack. RT-safe (single integer write,
    // no malloc, no loops over external state). Used by ParaEngine::setOctave
    // to keep the noteOn/noteOff "same shift" invariant from breaking when
    // the user shifts the octave while a voice is gated (sequencer or live
    // keyboard). Without this, alloc_.noteOff(note + NEW_shift) would miss
    // its target and the voice would stay gated forever.
    void allNotesOff() noexcept { count_ = 0; }
    bool anyHeld() const noexcept { return count_ > 0; }

    // Fills target semitone for 3 oscillators + per-osc active flag.
    void resolve(double outNote[3], bool active[3]) const noexcept {
        for (int i = 0; i < 3; ++i) { outNote[i] = 0.0; active[i] = false; }
        if (count_ == 0) return;
        const double newest = held_[count_ - 1];
        // EXT-BASS B4 — Stack/Mono override: alle 3 OSCs auf newest Note,
        // alle aktiv. Voice-Modus wird ignoriert. B3-Drift macht die 3 OSCs
        // dann hörbar verstimmt — der "fette" Bass-Bauprinzip-Stack.
        if (bassStack_) {
            outNote[0] = outNote[1] = outNote[2] = newest;
            active[0] = active[1] = active[2] = true;
            return;
        }
        switch (mode_) {
            case Mode::Poly:
                for (int i = 0; i < 3; ++i) {
                    const int idx = count_ - 1 - i;     // newest..older
                    if (idx >= 0) { outNote[i] = held_[idx]; active[i] = true; }
                }
                break;
            case Mode::Unison: {                        // CALIB(sprint1): detune
                const double d = 0.06;                  // semitone spread
                outNote[0]=newest-d; outNote[1]=newest; outNote[2]=newest+d;
                active[0]=active[1]=active[2]=true;
                break;
            }
            case Mode::Octave:                          // CALIB(sprint1): spread
                outNote[0]=newest-12; outNote[1]=newest; outNote[2]=newest+12;
                active[0]=active[1]=active[2]=true;
                break;
            case Mode::Fifth:                           // CALIB(sprint1): voicing
                outNote[0]=newest; outNote[1]=newest+7; outNote[2]=newest;
                active[0]=active[1]=true; active[2]=false;
                break;
            case Mode::UniRing: {                        // CALIB(sprint1): ring pair
                const double d = 0.06;                   // detuned ring operands
                outNote[0]=newest-d; outNote[1]=newest+d; outNote[2]=newest;
                active[0]=active[1]=true; active[2]=false;
                break;
            }
            case Mode::PolyRing: {                        // CALIB(sprint1): ring pair
                const int i1 = count_ - 1;
                const int i0 = count_ - 2;
                outNote[0] = held_[i1]; active[0] = true;
                outNote[1] = (i0 >= 0) ? held_[i0] : held_[i1];   // self if mono
                active[1]  = true;
                active[2]  = false;
                break;
            }
        }
    }
private:
    static constexpr int kMax = 8;
    int  held_[kMax] = {0};
    int  count_ = 0;
    Mode mode_  = Mode::Poly;
    bool bassStack_ = false;                                      // EXT-BASS B4 default OFF
};

// -----------------------------------------------------------------------------
//  Lfo — sub-audio modulation source. Ideal shape + a measured-slew one-pole
//  (CALIB(sprint1)) that intrinsically band-limits hard edges, so square/saw
//  LFO cannot inject a modulation click. Depth is applied by the caller via
//  the funnel, never raw.
// -----------------------------------------------------------------------------
class Lfo {
public:
    enum class Shape { Sine, Triangle, Saw, Square };

    void prepare(double sampleRate) noexcept {
        sr_ = sampleRate;
        setSlewMs(2.0);                                  // CALIB(sprint1)
        reset();
    }
    void reset() noexcept { phase_ = 0.0; slew_ = 0.0; }
    // E1.2 LFO trigger sync: restart the cycle EXACTLY at phi0_ (timing is
    // faithful to the hardware — the Volca LFO genuinely jumps its phase on
    // note trigger). Value continuity is carried by the LFO's OWN intrinsic
    // edge band-limiter (slew_, part of the original design and applied to
    // every waveform transition) — this is NOT added smoothing to mask the
    // jump; the phase/timing reset is exact. No hard click (proven by T15).
    void resetPhase() noexcept { phase_ = phi0_; }
    void setSyncStartPhase(double p) noexcept {          // CALIB(E8) per shape
        p -= std::floor(p); phi0_ = clamp(p, 0.0, 1.0);
    }
    void setRateHz(double hz) noexcept { inc_ = clamp(hz, 0.01, 200.0) / sr_; }
    void setShape(Shape s) noexcept { shape_ = s; }
    void setSlewMs(double ms) noexcept {
        const double t = std::max(ms, 0.05) * 0.001;
        slewC_ = std::exp(-1.0 / (t * sr_));
    }

    inline double next() noexcept {
        double raw;
        switch (shape_) {
            case Shape::Sine:     raw = std::sin(2.0 * M_PI * phase_); break;
            case Shape::Triangle: raw = 4.0 * std::fabs(phase_ - 0.5) - 1.0; break;
            case Shape::Saw:      raw = 2.0 * phase_ - 1.0; break;
            default:              raw = (phase_ < 0.5) ? 1.0 : -1.0; break;
        }
        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        slew_ = raw + (slew_ - raw) * slewC_;            // edge band-limiting
        return slew_;
    }
private:
    double sr_ = 48000.0, phase_ = 0.0, inc_ = 0.0;
    double slew_ = 0.0, slewC_ = 0.0;
    double phi0_ = 0.0;                                   // CALIB(E8) trigger-sync start phase
    Shape  shape_ = Shape::Triangle;
};

// -----------------------------------------------------------------------------
//  Delay — fractional digital delay, cubic-Hermite read, lo-fi feedback filter,
//  tanh-bounded feedback (energy-safe "singing"). Delay time runs through the
//  funnel => clean retime, no click. Buffer allocated in prepare() only.
// -----------------------------------------------------------------------------
class Delay {
public:
    void prepare(double sampleRate, double maxSeconds) noexcept {
        sr_   = sampleRate;
        size_ = (int)std::ceil(maxSeconds * sampleRate) + 8;
        buf_.assign((size_t)size_, 0.0f);               // prepare-time alloc only
        time_.prepare(sampleRate, 40.0);                 // CALIB(sprint1) retime feel
        time_.snap(0.20 * sampleRate);                   // 200 ms default
        setFeedbackFilterHz(6500.0);                     // CALIB(sprint1) lo-fi
        reset();
    }
    void reset() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        w_ = 0; fbLp_ = 0.0;
    }
    void setTimeMs(double ms) noexcept {
        time_.setTarget(clamp(ms * 0.001 * sr_, 2.0, (double)size_ - 4.0));
    }
    void setFeedback(double f) noexcept { fb_ = clamp(f, 0.0, 1.05); }
    void setMix(double m)      noexcept { mix_ = clamp(m, 0.0, 1.0); }
    void setFeedbackFilterHz(double hz) noexcept {
        const double t = 1.0 / (2.0 * M_PI * clamp(hz, 200.0, 18000.0));
        fbC_ = std::exp(-1.0 / (t * sr_));
    }

    inline float process(float x) noexcept {
        const double dly = time_.next();
        double rp = (double)w_ - dly;
        while (rp < 0.0) rp += size_;
        const int i1 = (int)rp;
        const double f = rp - i1;
        const int i0 = (i1 - 1 + size_) % size_;
        const int i2 = (i1 + 1) % size_;
        const int i3 = (i1 + 2) % size_;
        const double y0 = buf_[i0], y1 = buf_[i1], y2 = buf_[i2], y3 = buf_[i3];
        // Catmull-Rom / cubic Hermite
        const double a = y3 - y2 - y0 + y1;
        const double b = y0 - y1 - a;
        const double c = y2 - y0;
        const double wet = ((a * f + b) * f + c) * f + y1;

        fbLp_ = wet + (fbLp_ - wet) * fbC_;              // lo-fi feedback colour
        const double inj = (double)x + fb_ * std::tanh(fbLp_); // bounded feedback
        buf_[w_] = (float)inj;
        if (++w_ == size_) w_ = 0;

        return (float)((1.0 - mix_) * x + mix_ * wet);
    }
private:
    std::vector<float> buf_;
    int    size_ = 0, w_ = 0;
    double sr_ = 48000.0, fb_ = 0.0, mix_ = 0.0, fbLp_ = 0.0, fbC_ = 0.0;
    RampParam time_;
};

// E4.4 Metronome tick — enveloped sine: short raised onset then exp decay,
// hard-gated to exactly 0 at both ends => inherently band-limited (no
// discontinuity), proven by FFT in T22. No allocation; preallocated state.
class MetroTick {
public:
    void prepare(double sr) noexcept { sr_ = sr; active_ = false; n_ = 0; }
    void trigger(bool accent) noexcept {
        f_ = accent ? kAccentHz : kClickHz;     // CALIB(E8)
        n_ = 0; active_ = true;
    }
    inline float tick() noexcept {
        if (!active_) return 0.0f;
        const double atk = kAtkMs * 0.001 * sr_;
        const double dec = kTauMs * 0.001 * sr_;
        double env;
        if (n_ < atk) env = (double)n_ / atk;             // 0 -> 1 (starts at 0)
        else          env = std::exp(-(n_ - atk) / dec);  // exp decay
        const double y = env * std::sin(2.0 * M_PI * f_ * n_ / sr_);
        if (++n_ > atk && env < 1.0e-4) { active_ = false; } // ends ~0
        return (float)y;
    }
private:
    static constexpr double kClickHz  = 1000.0;   // CALIB(E8)
    static constexpr double kAccentHz = 1500.0;   // CALIB(E8)
    static constexpr double kAtkMs    = 0.5;      // CALIB(E8) onset (starts at 0)
    static constexpr double kTauMs    = 6.0;      // CALIB(E8) decay
    double sr_ = 48000.0, f_ = 1000.0;
    long   n_ = 0;
    bool   active_ = false;
};

class ParaEngine {
public:
    void prepare(double sampleRate, int /*maxBlock*/) noexcept {
        sr_ = sampleRate;
        for (int i = 0; i < 3; ++i) {
            osc_[i].prepare(sampleRate);
            pitch_[i].prepare(sampleRate, 5.0);        // CALIB(sprint1) glide floor
        }
        island_.prepare(sampleRate);
        ring_.prepare(sampleRate);
        ladder_.prepare(sampleRate * 2.0);
        env_.prepare(sampleRate);
        cutoff_.prepare(sampleRate, 12.0);             // CALIB(sprint1) feel
        reso_.prepare  (sampleRate, 15.0);
        cutoff_.snap(1200.0);
        reso_.snap(0.0);
        ladder_.setCutoffHz(1200.0);
        ladder_.setResonance(0.0);
        lfo_.prepare(sampleRate);
        lfo_.setRateHz(5.0);
        lfoPitch_.prepare(sampleRate, 15.0);           // depth via funnel
        lfoCut_.prepare  (sampleRate, 15.0);
        lfoPitch_.snap(0.0);                            // neutral default
        lfoCut_.snap(0.0);                              // neutral default
        egInt_.prepare(sampleRate, 15.0);               // E1.1 depth via funnel
        egInt_.snap(0.0);                               // E1.1 neutral => bit-identical
        detune_.prepare(sampleRate, 15.0);              // E2.1 spread via funnel
        detune_.snap(0.0);                              // E2.1 neutral => bit-identical
        for (int i = 0; i < 3; ++i) { glide_[i] = 0.0; gtgt_[i] = 0.0; }
        delay_.prepare(sampleRate, 2.0);                // 2 s buffer (prepare-only)
        metro_.prepare(sampleRate);                     // E4.4
        vol_.prepare(sampleRate, 15.0);                 // E6.1 master gain (smoothed)
        vol_.snap(1.0);                                 // E6.1 unity => bit-identical
        pulseWidth_.prepare(sampleRate, 15.0);          // EXT-BASS B2 PW funnel
        pulseWidth_.snap(0.5);                          // EXT-BASS B2 PW=0.5 ⇒ bit-identisch zum B1 hardcoded
        pwmDepth_.prepare(sampleRate, 15.0);            // EXT-BASS B2 PWM-Tiefe funnel
        pwmDepth_.snap(0.0);                            // EXT-BASS B2 PWM=0 ⇒ statisch ⇒ bit-identisch zum B1
        bassSpread_.prepare(sampleRate, 15.0);          // EXT-BASS B3 Spread funnel
        bassSpread_.snap(0.0);                          // EXT-BASS B3 0 ⇒ bit-identisch
        bassDriftRate_.prepare(sampleRate, 15.0);       // EXT-BASS B3 LP-Rate (Hz)
        bassDriftRate_.snap(kBassDriftRateDefaultHz);   // EXT-BASS B3 default Rate; ohne Effekt wenn Depth=0
        bassDriftDepth_.prepare(sampleRate, 15.0);      // EXT-BASS B3 Drift-Tiefe funnel
        bassDriftDepth_.snap(0.0);                      // EXT-BASS B3 0 ⇒ bit-identisch (drift_v * 0 = 0)
        setBassDriftSeed(kBassDriftDefaultSeed);        // EXT-BASS B3 reproduzierbarer Default-Seed
        delay_.setMix(0.0);                             // neutral default
        delay_.setFeedback(0.0);
        reset();
    }
    void reset() noexcept {
        for (auto& o : osc_) o.reset();
        island_.reset(); ladder_.reset(); env_.reset(); alloc_.reset();
        lfo_.reset(); delay_.reset(); ring_.reset();
        gateHeld_ = false;
    }

    void setMode(ParaAllocator::Mode m) noexcept { alloc_.setMode(m); }
    void noteOn(int note)  noexcept {
        alloc_.noteOn(note + octShift_); refresh();    // E6.2 octave
        if (syncOn_) lfo_.resetPhase();           // E1.2 GLOBAL LFO (Anhang D.1), before gate
        env_.gateOn();
        gateHeld_ = true;
    }
    void noteOff(int note) noexcept {
        alloc_.noteOff(note + octShift_);              // E6.2 octave (same shift)
        if (!alloc_.anyHeld()) {
            // RELEASE-FIX: last note off — DO NOT refresh. refresh() would set
            // active_[v]=false, which then short-circuits the per-voice osc
            // contribution in process() to zero — collapsing the audible
            // signal to silence before env_ has had a chance to drive its
            // Release segment. Mirror allNotesOff() (which seqStop has used
            // all along, and which sounds correct) by leaving active_[v]
            // alive while env_.gateOff lets the exponential decay ring the
            // oscillators down to 0 over DecRel ms — Korg Volca behaviour.
            // Polyphonic per-voice releases (other notes still held) DO need
            // refresh to reallocate slots, so that branch is untouched.
            env_.gateOff();
            gateHeld_ = false;
        } else {
            refresh();
        }
    }
    void setCutoffHz(double hz) noexcept { cutoff_.setTarget(hz); }
    void setResonance(double r) noexcept { reso_.setTarget(r);    }
    void setDrive(double d)     noexcept { ladder_.setDrive(d);   }
    void setAttackMs(double ms) noexcept { env_.setAttackMs(ms);  }
    void setDecRelMs(double ms) noexcept { env_.setDecRelMs(ms);  }
    void setSustain (double s)  noexcept { env_.setSustain(s);    }
    void setLfoRateHz(double hz)      noexcept { lfo_.setRateHz(hz); }
    void setLfoShape(Lfo::Shape s)    noexcept { lfo_.setShape(s);   }
    void setLfoPitchDepth(double st)  noexcept { lfoPitch_.setTarget(st);  } // semitones
    void setLfoCutoffDepth(double oct) noexcept { lfoCut_.setTarget(oct);  } // octaves
    void setDelayTimeMs(double ms)    noexcept { delay_.setTimeMs(ms); }
    void setDelayFeedback(double f)   noexcept { delay_.setFeedback(f); }
    void setDelayMix(double m)        noexcept { delay_.setMix(m);     }
    void setEgCutDepth(double oct)    noexcept { egInt_.setTarget(oct); } // E1.1 bipolar octaves (smoothed)
    void setLfoSync(bool on)          noexcept { syncOn_ = on; }          // E1.2
    void retrigger()                  noexcept { env_.gateOn(); }         // E4.1 force EG
    void setMetro(bool on)            noexcept { metroOn_ = on; }         // E4.4 (delay bypass)
    void metroTrigger(bool accent)    noexcept { metro_.trigger(accent); }// E4.4
    void setVolume(double g)          noexcept { vol_.setTarget(g); }     // E6.1 (smoothed)
    // EXT-BASS B1 — per-oscillator waveform (0=Saw default, 1=Pulse).
    // Discrete switch, NOT a setParamNorm target. Default 0 ⇒ Engine
    // bit-identical to pre-EXT-BASS-B1 (T49 proves max|d|=0 over full render).
    void setOscWave(int oscIdx, int wave) noexcept {                        // EXT-BASS B1
        if (oscIdx >= 0 && oscIdx < 3) osc_[oscIdx].setWave(wave);
    }
    int  oscWave(int oscIdx) const noexcept {                               // EXT-BASS B1
        return (oscIdx >= 0 && oscIdx < 3) ? osc_[oscIdx].wave() : 0;
    }
    // EXT-BASS B2 — Pulse Width + PWM-Depth (LFO-driven). Continuous knobs,
    // both go through setParamNorm (taper → RampParam → DSP). Defaults
    // (PW=0.5, PwmDepth=0) ⇒ effective PW = 0.5 every sample = B1's hardcoded
    // value → bit-identical to the B1 Pulse path (T54 measures).
    void setPulseWidth(double pw) noexcept { pulseWidth_.setTarget(pw); }   // EXT-BASS B2
    void setPwmDepth  (double d)  noexcept { pwmDepth_.setTarget(d);    }   // EXT-BASS B2
    // EXT-BASS B3 — Bass-Spread (additive auf E2.1-Detune, halbierter Spread
    // pro Voice 0/2) + Drift (langsam bandbegrenzter Pitch-Wander per OSC).
    // Defaults (0, _, 0) ⇒ kein Spread, kein Pitch-Wander ⇒ bit-identisch
    // (T59 misst). Drift: xorshift32 per OSC → einpoliger LP (rate-Hz-Param) →
    // ×Depth-Param. Seed über setBassDriftSeed reproduzierbar.
    void setBassSpread(double semis)    noexcept { bassSpread_.setTarget(semis); }   // EXT-BASS B3
    void setBassDriftRate(double hz)    noexcept { bassDriftRate_.setTarget(hz);  }   // EXT-BASS B3
    void setBassDriftDepth(double semis)noexcept { bassDriftDepth_.setTarget(semis); } // EXT-BASS B3
    void setBassDriftSeed(unsigned int s) noexcept {                                   // EXT-BASS B3
        const uint32_t seed = s ? (uint32_t)s : 1u;
        driftState_[0] = seed;
        driftState_[1] = seed ^ 0x9E3779B9u;            // golden ratio mix per OSC
        driftState_[2] = seed ^ 0x517CC1B7u;            // arbitrary derived constant
        if (driftState_[0] == 0) driftState_[0] = 1u;   // xorshift32 needs non-zero
        if (driftState_[1] == 0) driftState_[1] = 1u;
        if (driftState_[2] == 0) driftState_[2] = 1u;
        for (int i = 0; i < 3; ++i) driftLp_[i] = 0.0;
    }
    // EXT-BASS B4 — Stack/Mono override. Toggle löst refresh() aus damit der
    // active_[v]/pitch_[v]-Zustand sofort übernommen wird. Wenn keine Note
    // gehalten, ist refresh() ein No-op. Aus (Default) ⇒ bit-identisch.
    void setBassStack(bool on) noexcept {                                              // EXT-BASS B4
        alloc_.setBassStack(on);
        if (alloc_.anyHeld()) refresh();
    }
    bool bassStack() const noexcept { return alloc_.bassStack(); }                     // EXT-BASS B4
    // B1-fix: panic any held voice and release the envelope BEFORE changing
    // the shift. Reuses the engine's own allNotesOff (below) so seqStop, the
    // C-API panic path, and this one all converge on the same primitive.
    void setOctave(int oct)           noexcept {
        allNotesOff();
        octShift_ = oct * 12;                                                 // E6.2 semitones
    }
    // B2: engine-level panic primitive. Single source of truth for "release
    // every voice and start the envelope's Release stage now". RT-safe: one
    // int write (alloc.count_=0) and one Release-state assignment in
    // AdsrEnvelope::gateOff. Click-free: the existing envelope decay handles
    // the audible portion; no new smoother. Called from setOctave (B1) and
    // from para3_seq_stop (B2 — without it the sequencer's in-flight gate-on
    // step leaves a stuck voice forever when the user clicks Stop).
    void allNotesOff() noexcept {
        alloc_.allNotesOff();
        env_.gateOff();
        gateHeld_ = false;
    }
    void setDetune(double semis)      noexcept { detune_.setTarget(semis); } // E2.1 (smoothed)
    void setPortamento(double tauSec) noexcept {                            // E2.2 Modell A
        portTau_ = tauSec;
        if (tauSec <= 1e-6) { portOn_ = false; portA_ = 1.0; }
        else { portOn_ = true; portA_ = 1.0 - std::exp(-1.0 / (tauSec * sr_)); }
    }

    // --- Sprint-7 unified funnel: the SINGLE public entry every source
    //     (UI / MIDI CC / host automation / sequencer motion) goes through.
    //     normalized 0..1 -> CALIB(sprint1) taper -> the existing smoothed
    //     setter (RampParam) -> DSP. No source bypasses taper or smoother.
    enum class Param { Cutoff, Resonance, Drive, LfoCutDepth, DelayMix,
                       LfoRate, LfoPitchDepth, DelayTime, DelayFeedback,
                       Attack, DecRel, Sustain, EgCutDepth, Detune, Portamento, Volume,
                       // 16 reserved for kArpModePid (motion-pid only, not a setParamNorm target)
                       BassPulseWidth = 17,                                              // EXT-BASS B2
                       BassPwmDepth,                                                      // EXT-BASS B2
                       BassSpread,                                                        // EXT-BASS B3 (id=19)
                       BassDriftRate,                                                     // EXT-BASS B3 (id=20)
                       BassDriftDepth                                                     // EXT-BASS B3 (id=21)
    };

    static double taper(Param p, double n) noexcept {       // CALIB(sprint1)
        n = clamp(n, 0.0, 1.0);
        switch (p) {
            case Param::Cutoff:        return 20.0 * std::pow(900.0, n); // 20Hz..18kHz
            case Param::Resonance:     return n;                          // 0..1
            case Param::Drive:         return 0.5 + 3.5 * n;              // 0.5..4
            case Param::LfoCutDepth:   return 4.0 * n;                    // 0..4 oct
            case Param::DelayMix:      return n;                          // 0..1
            case Param::LfoRate:       return 0.05 * std::pow(400.0, n);  // .05..20 Hz
            case Param::LfoPitchDepth: return 12.0 * n;                   // 0..12 st
            case Param::DelayTime:     return 20.0 + 980.0 * n;           // 20..1000 ms
            case Param::DelayFeedback: return n;                          // 0..1
            case Param::Attack:        return 1.5 + 1998.5 * n;           // 1.5ms..2s
            case Param::DecRel:        return 1.5 + 1998.5 * n;           // 1.5ms..2s
            case Param::Sustain:       return n;                          // 0..1
            case Param::EgCutDepth:    return (n - 0.5) * 2.0 * kEgIntOctMax; // E1.1 bipolar oct
            case Param::Detune:        return n * (kDetuneCentsMax / 100.0);  // E2.1 semitone half-spread
            case Param::Portamento:    return n * kPortMaxSec;                // E2.2 glide TAU (s)
            case Param::Volume:        return n;                              // E6.1 0..1 (CALIB(E8) curve)
            case Param::BassPulseWidth: return 0.05 + 0.90 * n;                // EXT-BASS B2 unipolar 5%..95% (default n=0.5 ⇒ PW=0.5)
            case Param::BassPwmDepth:   return 0.45 * n;                       // EXT-BASS B2 unipolar 0..45% PW excursion (LFO-driven)
            case Param::BassSpread:     return n * kBassSpreadSemisMax;        // EXT-BASS B3 unipolar 0..2 semitones half-spread (additive auf E2.1 ds)
            case Param::BassDriftRate:  return 0.05 + n * (kBassDriftRateMaxHz - 0.05);  // EXT-BASS B3 unipolar 0.05..5 Hz LP cutoff
            case Param::BassDriftDepth: return n * kBassDriftDepthSemisMax;    // EXT-BASS B3 unipolar 0..0.15 semitones pitch wander
        }
        return n;
    }
    void setParamNorm(Param p, double n) noexcept {
        switch (p) {
            case Param::Cutoff:        setCutoffHz   (taper(p, n)); break;
            case Param::Resonance:     setResonance  (taper(p, n)); break;
            case Param::Drive:         setDrive      (taper(p, n)); break;
            case Param::LfoCutDepth:   setLfoCutoffDepth(taper(p, n)); break;
            case Param::DelayMix:      setDelayMix   (taper(p, n)); break;
            case Param::LfoRate:       setLfoRateHz  (taper(p, n)); break;
            case Param::LfoPitchDepth: setLfoPitchDepth(taper(p, n)); break;
            case Param::DelayTime:     setDelayTimeMs(taper(p, n)); break;
            case Param::DelayFeedback: setDelayFeedback(taper(p, n)); break;
            case Param::Attack:        setAttackMs   (taper(p, n)); break;
            case Param::DecRel:        setDecRelMs   (taper(p, n)); break;
            case Param::Sustain:       setSustain    (taper(p, n)); break;
            case Param::EgCutDepth:    setEgCutDepth (taper(p, n)); break;  // E1.1
            case Param::Detune:        setDetune     (taper(p, n)); break;  // E2.1
            case Param::Portamento:    setPortamento (taper(p, n)); break;  // E2.2
            case Param::Volume:        setVolume     (taper(p, n)); break;  // E6.1
            case Param::BassPulseWidth: setPulseWidth(taper(p, n)); break;  // EXT-BASS B2
            case Param::BassPwmDepth:   setPwmDepth  (taper(p, n)); break;  // EXT-BASS B2
            case Param::BassSpread:     setBassSpread    (taper(p, n)); break;  // EXT-BASS B3
            case Param::BassDriftRate:  setBassDriftRate (taper(p, n)); break;  // EXT-BASS B3
            case Param::BassDriftDepth: setBassDriftDepth(taper(p, n)); break;  // EXT-BASS B3
        }
    }
    // read-only observability (metering / tests) — not a control path
    double observedCutoffHz() const noexcept { return cutoff_.value(); }

    void process(float* out, int n) noexcept {
        const ScopedNoDenormals guard;
        for (int i = 0; i < n; ++i) {
            const double lfo  = lfo_.next();
            const double pMod = lfo * lfoPitch_.next();        // semitone domain
            const double cMod = lfo * lfoCut_.next();          // octave domain
            const double eg   = env_.next();                   // E1.1 sample shared EG ONCE
            const double egD  = egInt_.next();                 // E1.1 smoothed bipolar octaves
            const double ds   = detune_.next();                // E2.1 smoothed semitone half-spread
            // EXT-BASS B2 — effective Pulse Width: base PW plus LFO-driven
            // excursion. At default (PW=0.5, PwmDepth=0) ⇒ pwEff = 0.5 + 0*lfo
            // = 0.5 (IEEE exact) ⇒ identical to B1's hardcoded processPulse arg.
            // The Saw branch of Oscillator.process ignores pwEff so the Saw
            // path stays byte-identical regardless of this value.
            const double pwBase = pulseWidth_.next();          // EXT-BASS B2
            const double pwMod  = pwmDepth_.next() * lfo;      // EXT-BASS B2 (lfo ∈ [-1,+1])
            const double pwEff  = pwBase + pwMod;              // EXT-BASS B2 (clamped in core_)
            // EXT-BASS B3 — Bass-Spread additiv auf ds (E2.1 unangetastet);
            // Drift = xorshift32 → einpoliger LP → ×Depth per OSC. State läuft
            // unabhängig vom Depth-Wert (Reproduzierbarkeit), Beitrag = LP*Depth
            // ist 0 wenn Depth=0 (IEEE exakt) ⇒ bit-identisch zum B2-Stand.
            const double spread     = bassSpread_.next();      // EXT-BASS B3 semitones half-spread
            const double driftRate  = bassDriftRate_.next();   // EXT-BASS B3 LP cutoff in Hz
            const double driftAlpha = 1.0 - std::exp(-2.0 * M_PI * driftRate / sr_);
            const double driftAmp   = bassDriftDepth_.next();  // EXT-BASS B3 semitones
            double drift_v[3];
            for (int v = 0; v < 3; ++v) {
                uint32_t x = driftState_[v];
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                driftState_[v] = x;
                const double u = (int32_t)x * (1.0 / 2147483648.0);  // [-1, +1)
                driftLp_[v] += driftAlpha * (u - driftLp_[v]);
                drift_v[v] = driftLp_[v] * driftAmp;
            }
            if (portOn_) {                                     // E2.2 Modell A one-pole glide
                for (int v = 0; v < 3; ++v) {
                    glide_[v] += (gtgt_[v] - glide_[v]) * portA_;
                    pitch_[v].setTarget(glide_[v]);
                }
            }

            // funnel -> filter controls (smoothed); LFO + EG in log (octave) domain.
            // egInt neutral (target 0) => term is exactly 0.0 => bit-identical.
            const double baseCut = cutoff_.next();
            ladder_.setCutoffHz(baseCut *
                std::pow(2.0, cMod + (eg - kEgPivot) * egD));   // E1.1
            ladder_.setResonance(reso_.next());

            double mix = 0.0;
            // EXT-BASS B3 — effective half-spread: E2.1 ds plus optional bass-
            // spread. At spread=0 ⇒ dsEff == ds (IEEE exact, ds + 0.0 = ds).
            const double dsEff = ds + spread;                          // EXT-BASS B3
            if (alloc_.ring()) {
                // ring operands via the funnel (zipper-free), product on its
                // own oversampling island. Non-ring path is untouched.
                const double nA = pitch_[0].next() + pMod - dsEff + drift_v[0];   // EXT-BASS B3
                const double nB = pitch_[1].next() + pMod + dsEff + drift_v[1];   // EXT-BASS B3
                pitch_[2].next();                       // keep funnel consistent
                mix = ring_.process(semitonesToHz(nA), semitonesToHz(nB));
            } else {
                for (int v = 0; v < 3; ++v) {
                    if (!active_[v]) { pitch_[v].next(); continue; }
                    const double sv = (v == 0 ? -dsEff : (v == 2 ? dsEff : 0.0));  // EXT-BASS B3 spread applied
                    const double hz = semitonesToHz(pitch_[v].next() + pMod
                                        + sv + drift_v[v]);             // EXT-BASS B3 drift per OSC
                    mix += osc_[v].process(hz, pwEff);                  // EXT-BASS B2 PW per sample
                }
            }
            mix *= 0.5;                                 // headroom for the drive

            const double filt = island_.process(mix,
                [this](double s){ return ladder_.tick(s); });

            const float dry = static_cast<float>(filt * eg * kVelFixed); // E6.3 fixed velocity
            const float wet = delay_.process(dry);               // keep delay state continuous
            const float mix2 = (metroOn_ ? dry : wet)            // E4.4 delay bypass when metro
                   + (metroOn_ ? metro_.tick() : 0.0f);          // E4.4 band-limited tick
            out[i] = static_cast<float>(mix2 * vol_.next());     // E6.1 master volume (post-VCA)
        }
    }
private:
    void refresh() noexcept {
        double tgt[3]; bool act[3];
        alloc_.resolve(tgt, act);
        for (int i = 0; i < 3; ++i) {
            active_[i] = act[i];
            if (act[i]) {
                if (!wasActive_[i]) { pitch_[i].snap(tgt[i]); glide_[i] = tgt[i]; } // no glide-in
                gtgt_[i] = tgt[i];                                   // E2.2 glide target
                if (!portOn_) pitch_[i].setTarget(tgt[i]);           // E2.2 off => original path
            }
            wasActive_[i] = act[i];
        }
    }
    double         sr_ = 48000.0;
    Oscillator     osc_[3];
    RampParam      pitch_[3];
    bool           active_[3]    = {false,false,false};
    bool           wasActive_[3] = {false,false,false};
    Os2Island<63>  island_;
    LadderZDF      ladder_;
    AdsrEnvelope   env_;
    RampParam      cutoff_, reso_;
    ParaAllocator  alloc_;
    Lfo            lfo_;
    RingModIsland  ring_;
    RampParam      lfoPitch_, lfoCut_;
    RampParam      egInt_;                              // E1.1 EG->cutoff depth (octaves, bipolar)
    RampParam      detune_;                             // E2.1 VCO spread (semitone half, smoothed)
    double         gtgt_[3]  = {0,0,0};                 // E2.2 glide targets (semitones)
    double         glide_[3] = {0,0,0};                 // E2.2 one-pole glide state
    double         portTau_  = 0.0;                     // E2.2 glide time constant (s)
    double         portA_    = 1.0;                     // E2.2 one-pole coef
    bool           portOn_   = false;                   // E2.2 engaged (port>0)
    Delay          delay_;
    bool           gateHeld_ = false;
    bool           syncOn_   = false;                   // E1.2 LFO trigger sync
    bool           metroOn_  = false;                   // E4.4 metronome
    MetroTick      metro_;                               // E4.4
    RampParam      vol_;                                 // E6.1 master gain (smoothed)
    RampParam      pulseWidth_;                          // EXT-BASS B2 PW (0.05..0.95, default 0.5)
    RampParam      pwmDepth_;                            // EXT-BASS B2 PWM excursion (0..0.45, default 0)
    RampParam      bassSpread_;                          // EXT-BASS B3 Spread (0..2 st, default 0)
    RampParam      bassDriftRate_;                       // EXT-BASS B3 Drift-LP-Rate (0.05..5 Hz)
    RampParam      bassDriftDepth_;                      // EXT-BASS B3 Drift-Tiefe (0..0.15 st, default 0)
    uint32_t       driftState_[3] = {1u, 2u, 3u};        // EXT-BASS B3 per-OSC xorshift32 state
    double         driftLp_[3]    = {0.0, 0.0, 0.0};     // EXT-BASS B3 per-OSC one-pole LP state
    int            octShift_ = 0;                        // E6.2 semitone offset
    static constexpr double kVelFixed = 1.0;             // CALIB(E8) E6.3 fixed velocity
    static constexpr double kEgIntOctMax   = 2.0;       // CALIB(E8) EG INT max swing (octaves)
    static constexpr double kEgPivot       = 0.0;       // CALIB(E8) EG value at zero swing
    static constexpr double kDetuneCentsMax= 50.0;      // CALIB(E8) DETUNE max half-spread (cents)
    static constexpr double kPortMaxSec    = 0.5;       // CALIB(E8) PORTAMENTO max glide (s)
    // EXT-BASS B3 DEFAULT constants (no HW reference — design choices):
    static constexpr double   kBassSpreadSemisMax     = 2.0;          // ±2 ST half-spread max
    static constexpr double   kBassDriftRateMaxHz     = 5.0;          // up to 5 Hz LP cutoff
    static constexpr double   kBassDriftRateDefaultHz = 0.5;          // 0.5 Hz default LP
    static constexpr double   kBassDriftDepthSemisMax = 0.15;         // ±15 cents max drift
    static constexpr uint32_t kBassDriftDefaultSeed   = 0x1A2B3C4Du;  // reproducible default
};

// -----------------------------------------------------------------------------
//  Engine — Step-1 single-voice (kept intact; setters push targets only).
// -----------------------------------------------------------------------------
class Engine {
public:
    void prepare(double sampleRate, int /*maxBlock*/) noexcept {
        sr_ = sampleRate; voice_.prepare(sampleRate);
    }
    void reset() noexcept { voice_.reset(); }
    void noteOn(int n)  noexcept { voice_.noteOn((double)n); }
    void noteOff()      noexcept { voice_.noteOff(); }
    void setAttackMs(double ms) noexcept { voice_.env().setAttackMs(ms); }
    void setDecRelMs(double ms) noexcept { voice_.env().setDecRelMs(ms); }
    void setSustain (double s)  noexcept { voice_.env().setSustain(s);  }
    void process(float* out, int n) noexcept {
        const ScopedNoDenormals guard;
        for (int i = 0; i < n; ++i) out[i] = voice_.renderSample();
    }
private:
    double sr_ = 48000.0;
    Voice  voice_;
};

// =============================================================================
//  STEP 4 — Sprint-7 control / sequencer layer.
//  Lock-free pattern editing, sample-accurate clock, motion record/playback,
//  MIDI-CC ingestion. Every parameter source funnels through
//  ParaEngine::setParamNorm (taper -> smoother). The DSP engine above is
//  untouched (no regression).
// =============================================================================

// 16-step pattern. One motion lane (cutoff, normalized) as the proven pattern;
// extensible. Plain POD so a whole pattern copies atomically by value.
struct Step {
    int    note      = 60;
    bool   gate      = false;
    bool   motionOn  = false;
    double motionCut = 0.5;     // normalized -> funnel
    bool   active    = true;    // E4.3 false => step skipped (play AND record)
    double vel       = 1.0;     // EXT-FLUX-VEL per-step velocity 0..1 (default 1.0
                                // = bitidentisch zum pre-EXT-FLUX-VEL-Stand).
    double gateLen   = 1.0;     // EXT-FLUX-GATE per-step gate-length fraction
                                // 0..1 (default 1.0 = legacy "hold until next
                                // step"; <1 schedules sample-accurate noteOff
                                // at step+gateLen*stepLen).
};
// E3: per-parameter motion lanes (sparse). Indexed directly by param id;
// size covers all current + planned ids. Resonance(1)/Tempo refused upstream.
// EXT-ARP-MOTION: pid 16 = ARP_MODE (Controller-level, discrete 0..4 mapped
// from the 0..1 lane via setArpMode). All other ids 0..15 dispatch to
// engine.setParamNorm as before — see applyMotionParam_().
static constexpr int kMP = 22;                                    // EXT-BASS B3: + 19 Spread + 20 DriftRate + 21 DriftDepth
static constexpr int kArpModePid = 16;
struct MotionLane { bool used = false; float v[16] = {0}; };
struct Pattern {
    Step       steps[16];
    int        length = 16;
    MotionLane lane[kMP];                  // E3 multi-knob motion (legacy Step.motion* kept)
};

// Lock-free double buffer: control thread writes edit(), commit() flips an
// atomic index; the audio thread always reads a complete, consistent Pattern.
class PatternBank {
public:
    PatternBank() noexcept { front_.store(0, std::memory_order_relaxed); }
    Pattern&       edit()  noexcept { return buf_[1 - front_.load(std::memory_order_relaxed)]; }
    void           commit() noexcept {
        const int b = 1 - front_.load(std::memory_order_relaxed);
        front_.store(b, std::memory_order_release);          // publish
    }
    const Pattern& read() const noexcept {
        return buf_[front_.load(std::memory_order_acquire)]; // consistent snapshot
    }
    void seedBoth(const Pattern& p) noexcept { buf_[0] = p; buf_[1] = p; }
private:
    Pattern             buf_[2];
    std::atomic<int>    front_;
};


// =============================================================================
//  E5 FLUX — sample-accurate event sequence (micro-timing). Replaces the step
//  grid with absolute sample offsets in a loop. Fixed-capacity, no allocation.
// =============================================================================
static constexpr int FLUX_CAP = 512;   // EXT-FLUX raised from 256 to fit
                                       // realistic param-event-rate per loop
struct FluxEvent { unsigned int off=0; unsigned char type=0;  // 0=ON 1=OFF 2=PARAM
                   unsigned char note=60;                      // also pid when type=2
                   float         val=0.f;                      // norm 0..1 when type=2
                 };
struct FluxPattern {
    unsigned int  loopLen = 0;          // samples; 0 => silent
    unsigned short count  = 0;
    FluxEvent     ev[FLUX_CAP];
};
class FluxBank {
public:
    FluxBank() noexcept { front_.store(0,std::memory_order_relaxed); }
    FluxPattern&       edit()  noexcept { return buf_[1-front_.load(std::memory_order_relaxed)]; }
    void               commit() noexcept {
        const int b=1-front_.load(std::memory_order_relaxed);
        front_.store(b,std::memory_order_release);
    }
    const FluxPattern& read() const noexcept {
        return buf_[front_.load(std::memory_order_acquire)];
    }
    void seedBoth(const FluxPattern& p) noexcept { buf_[0]=p; buf_[1]=p; }
private:
    FluxPattern       buf_[2];
    std::atomic<int>  front_;
};

// Sample-accurate step clock with swing. Internal tempo or external ticks.
class Clock {
public:
    void prepare(double sampleRate) noexcept { sr_ = sampleRate; reset(); }
    void reset() noexcept { acc_ = 0.0; running_ = false; }
    void setTempo(double bpm, int stepsPerBeat = 4) noexcept {
        stepSamples_ = (60.0 / std::max(1.0, bpm)) / stepsPerBeat * sr_;
    }
    void setDiv(int d) noexcept { div_ = (d==2||d==4)? d : 1; }   // E4.2 1/1,1/2,1/4
    void setSwing(double s) noexcept { swing_ = clamp(s, 0.0, 0.7); } // CALIB(sprint1)
    // B4: Volca-parity Play = restart from step 1. `primed_` makes the FIRST
    // tick after start() fire immediately (no one-step warm-up silence), so
    // step 0 is audible at t=t0 instead of t=t0+stepDuration. RT-safe: one
    // bool write.
    void start() noexcept { running_ = true;  acc_ = 0.0; primed_ = true; }
    void stop()  noexcept { running_ = false; }
    bool running() const noexcept { return running_; }

    // returns true exactly on the sample a new step begins; *stepIdxIo advances
    inline bool tick(int* stepIdxIo) noexcept {
        if (!running_) return false;
        if (primed_) {                                       // B4: first tick after start()
            primed_ = false;
            *stepIdxIo = (*stepIdxIo + 1) & 15;              // -1 → 0 (paired with Controller::seqStart)
            return true;
        }
        acc_ += 1.0;
        const bool odd = ((*stepIdxIo) & 1) != 0;
        const double dur = stepSamples_ * (odd ? (1.0 - swing_) : (1.0 + swing_))
                           * (double)div_;                       // E4.2
        curDur_ = dur;
        if (acc_ >= dur) {
            acc_ -= dur;
            *stepIdxIo = (*stepIdxIo + 1) & 15;
            return true;
        }
        return false;
    }
    double phase() const noexcept { return curDur_ > 0.0 ? acc_ / curDur_ : 0.0; } // E3 [0,1)
    double sixteenthSamples() const noexcept { return stepSamples_; }   // EXT-ARP read-only getter (Block A)
private:
    double sr_ = 48000.0, acc_ = 0.0, stepSamples_ = 6000.0, swing_ = 0.0;
    double curDur_ = 6000.0;
    int    div_ = 1;                                          // E4.2
    bool   running_ = false;
    bool   primed_  = false;                                  // B4 Volca-parity Play=restart
};

// One incoming MIDI-ish event, sample-offset within the next render block.
struct MidiEvent {
    enum class Type { NoteOn, NoteOff, CC };
    Type   type = Type::CC;
    int    offset = 0;       // sample within block
    int    data1  = 0;       // note or CC number
    double value  = 0.0;     // normalized
};

// Controller: drives ParaEngine sample-accurately from the sequencer + MIDI,
// records/plays motion, all parameter writes via the unified funnel.
class Controller {
public:
    void prepare(para3::ParaEngine& eng, double sampleRate) noexcept {
        eng_ = &eng; sr_ = sampleRate;
        clock_.prepare(sampleRate);
        clock_.setTempo(120.0, 4);
        edit_ = Pattern();                               // host-side authoritative
        bank_.seedBoth(edit_);
        stepIdx_ = -1; recCC_ = 0.5; recording_ = false;
    }
    Clock&       clock()  noexcept { return clock_; }
    PatternBank& bank()   noexcept { return bank_;  }
    // host edits this freely (control thread only); publish atomically.
    Pattern&     editPattern() noexcept { return edit_; }
    void         commitEdit()  noexcept { bank_.edit() = edit_; bank_.commit(); }
    void armRecord(bool on) noexcept { recording_ = on; }
    // B4: Volca-parity Play=restart. Resetting stepIdx_=-1 here pairs with
    // Clock::primed_ — the first primed tick advances -1→0 so step 0 fires
    // immediately on every Play (no resume from where Stop happened). RT-safe.
    // EXT-ARP-FIX: also reset seqLastGatedNote_ so a previous run's last
    // gated note doesn't get a phantom noteOff on the first gate-off step.
    void seqStart() noexcept {
        stepIdx_ = -1; seqLastGatedNote_ = -1;
        clock_.start();
    }

    // ---- E3 motion: ziel-parametric, lock-free (edit_ -> commit) ----------
    static bool motionCapable(int pid) noexcept {        // Resonance(1) refused
        if (pid == kArpModePid) return true;            // EXT-ARP-MOTION
        return pid >= 0 && pid < 16 && pid != 1;
    }
    static bool paramOfId(int pid, para3::ParaEngine::Param& o) noexcept {
        using P = para3::ParaEngine::Param;
        switch (pid) {
            case 0:  o=P::Cutoff;        return true;
            case 2:  o=P::Drive;         return true;
            case 3:  o=P::LfoCutDepth;   return true;
            case 4:  o=P::DelayMix;      return true;
            case 5:  o=P::LfoRate;       return true;
            case 6:  o=P::LfoPitchDepth; return true;
            case 7:  o=P::DelayTime;     return true;
            case 8:  o=P::DelayFeedback; return true;
            case 9:  o=P::Attack;        return true;
            case 10: o=P::DecRel;        return true;
            case 11: o=P::Sustain;       return true;
            case 12: o=P::EgCutDepth;    return true;
            case 13: o=P::Detune;        return true;
            case 14: o=P::Portamento;    return true;
            case 15: o=P::Volume;        return true;   // E6.1
            // 16 = kArpModePid, handled separately above (motion-only, not a Param)
            case 17: o=P::BassPulseWidth; return true;  // EXT-BASS B2
            case 18: o=P::BassPwmDepth;   return true;  // EXT-BASS B2
            case 19: o=P::BassSpread;     return true;  // EXT-BASS B3
            case 20: o=P::BassDriftRate;  return true;  // EXT-BASS B3
            case 21: o=P::BassDriftDepth; return true;  // EXT-BASS B3
            default: return false;
        }
    }
    void motionSet(int pid, int step, double v) noexcept {
        if (!motionCapable(pid)) { ++mRejects_; return; }
        if (step < 0 || step >= 16) return;
        edit_.lane[pid].used = true;
        edit_.lane[pid].v[step] = (float)clamp(v, 0.0, 1.0);
    }
    void motionLaneCommit(int pid, const double* v16) noexcept {
        if (!motionCapable(pid)) { ++mRejects_; return; }
        edit_.lane[pid].used = true;
        for (int i = 0; i < 16; ++i)
            edit_.lane[pid].v[i] = (float)clamp(v16[i], 0.0, 1.0);
    }
    void motionSmooth(bool on) noexcept { smooth_ = on; }
    long motionRejects() const noexcept { return mRejects_; }
    // one-loop auto-deactivation capture: feed live value via motionVal()
    void motionRec(int pid, bool on) noexcept {
        if (!motionCapable(pid)) { ++mRejects_; return; }
        if (on) { mCap_[pid] = true; mCapCnt_[pid] = 0;
                  edit_.lane[pid].used = true; }
        else      mCap_[pid] = false;
    }
    void motionVal(int pid, double v) noexcept {
        if (pid >= 0 && pid < kMP) mLive_[pid] = clamp(v, 0.0, 1.0);
    }
    void setStepTrigger(bool on) noexcept { stepTrig_ = on; }            // E4.1
    void setTempoDiv(int d)      noexcept { clock_.setDiv(d); }          // E4.2
    void setActiveStep(int i, bool on) noexcept {                        // E4.3
        if (i>=0 && i<16) edit_.steps[i].active = on;
    }
    void setMetro(bool on) noexcept { metroOn_ = on;                     // E4.4
        if (eng_) eng_->setMetro(on); }
    // EXT-BASS B1 — per-oscillator waveform passthrough (0=Saw default, 1=Pulse).
    // Discrete control: outside the param trichter, not in KNOB_PARAM.
    void setOscWave(int oscIdx, int wave) noexcept {                     // EXT-BASS B1
        if (eng_) eng_->setOscWave(oscIdx, wave);
    }
    // EXT-BASS B3 — Drift-Seed passthrough. Discrete control (reproducibility).
    void setBassDriftSeed(unsigned int s) noexcept {                     // EXT-BASS B3
        if (eng_) eng_->setBassDriftSeed(s);
    }
    // EXT-BASS B4 — Stack/Mono passthrough. Discrete (kein Trichter).
    void setBassStack(bool on) noexcept {                                // EXT-BASS B4
        if (eng_) eng_->setBassStack(on);
    }

    // ---- E5 FLUX -------------------------------------------------------
    void setFluxMode(bool on) noexcept { fluxMode_ = on; }   // click-free: env continues
    void fluxSetLoopLen(unsigned int samples) noexcept {
        fluxEdit_.loopLen = samples;
    }
    void fluxRec(bool on) noexcept { fluxRec_ = on; }
    // EXT-FLUX-QUANTIZE: when true, recorded event offsets snap to the nearest
    // 1/16-step (loopLen / 16) — Korg-Volca/Electribe/Minilogue standard. Off
    // = "F·FINE" sample-accurate free-running mode. Default ON.
    void setFluxQuantize(bool on) noexcept { fluxQuantize_ = on; }
    bool fluxQuantize() const noexcept { return fluxQuantize_; }
    // Snap an off-sample to the nearest 1/16-step within a loop. Half-step
    // rounds up. If the snapped value reaches loopLen, wrap to 0 (events at
    // the loop boundary belong to the start of the next loop).
    unsigned int fluxSnap_(unsigned int off, unsigned int L) const noexcept {
        if (!fluxQuantize_ || L < 16) return off;
        const unsigned int step = L / 16;
        if (step == 0) return off;
        const unsigned int half = step / 2;
        const unsigned int rounded = ((off + half) / step) * step;
        return rounded >= L ? 0u : rounded;
    }
    void fluxNote(int note, bool on) noexcept {              // append at live cursor
        if (!fluxRec_) return;
        if (fluxEdit_.count >= FLUX_CAP) { ++fluxDropped_; return; }
        FluxEvent e;
        const unsigned int raw = (fluxEdit_.loopLen ? (fcur_ % fluxEdit_.loopLen) : fcur_);
        e.off  = fluxSnap_(raw, fluxEdit_.loopLen);
        e.type = on ? 0 : 1;
        e.note = (unsigned char)note;
        e.val  = 0.f;
        fluxEdit_.ev[fluxEdit_.count++] = e;
    }
    // EXT-FLUX param event: same offset semantics as fluxNote, encodes pid+norm.
    // pid is an API-PID 0..15 (mirrors ParaEngine::Param ordering — see
    // wasm-bridge/para3_capi.h enum). At replay the dispatcher casts back to
    // Param and routes through setParamNorm (Trichter + RampParam smoother) —
    // same path as midiCC + UI knob, so determinism + click-freedom hold.
    void fluxParam(int pid, double norm) noexcept {          // EXT-FLUX
        if (!fluxRec_) return;
        if (pid < 0 || pid >= 16) return;                    // audio-Param range only
        if (fluxEdit_.count >= FLUX_CAP) { ++fluxDropped_; return; }
        FluxEvent e;
        const unsigned int raw = (fluxEdit_.loopLen ? (fcur_ % fluxEdit_.loopLen) : fcur_);
        e.off  = fluxSnap_(raw, fluxEdit_.loopLen);
        e.type = 2;
        e.note = (unsigned char)pid;
        e.val  = (float)clamp(norm, 0.0, 1.0);
        fluxEdit_.ev[fluxEdit_.count++] = e;
    }
    // EXT-FLUX clear: drops all queued + published events. The engine's envelope
    // stays on its own release path (click-free per E5 design — fluxMode_ still
    // on, just no events fire). Notes already physically ON via earlier replay
    // hold their envelope until the natural noteOff arrives via another path —
    // or until the user retriggers / disarms FLUX-mode.
    void fluxClear() noexcept {                              // EXT-FLUX
        fluxEdit_.count = 0;
        const unsigned int L = fluxEdit_.loopLen;
        FluxPattern p; p.loopLen = L; p.count = 0;
        fluxBank_.edit() = p;
        fluxBank_.commit();
        fpos_ = 0;
    }
    void fluxCommit() noexcept {                             // stable-sort, publish
        const int n = fluxEdit_.count;
        // At the same sample offset: PARAM(2) -> OFF(1) -> ON(0). Params first
        // so parameter changes (cutoff, etc.) settle before any concurrent
        // note triggers; OFF before ON preserves the original click-free rule.
        // Stable sort to preserve insertion order within each rank.
        std::stable_sort(fluxEdit_.ev, fluxEdit_.ev + n,
            [](const FluxEvent& a, const FluxEvent& b){
                if (a.off != b.off) return a.off < b.off;
                const int ra = (a.type==2)?0 : (a.type==1)?1 : 2;
                const int rb = (b.type==2)?0 : (b.type==1)?1 : 2;
                return ra < rb;
            });
        fluxBank_.edit() = fluxEdit_;
        fluxBank_.commit();
    }
    FluxBank& fluxBank() noexcept { return fluxBank_; }
    long fluxDropped() const noexcept { return fluxDropped_; }

    // CC#74 -> Cutoff (the classic). One mapping shown; same funnel for all.
    void midiCC(int cc, double norm) noexcept {
        if (cc == 74) {
            recCC_ = norm;                                   // also captured if armed
            eng_->setParamNorm(para3::ParaEngine::Param::Cutoff, norm);
        }
    }
    // EXT-ARP Block A: routing point for keyboard/MIDI notes. When arpEnabled_
    // is false (default), behaviour is byte-identical to direct engine.noteOn —
    // T27(a) proves max|d|=0 over a full render. When true, notes go to the
    // arp pool instead and the per-sample arp scheduler drives the engine.
    // Block C: hold/latch semantics per spec §2.6 — when arpHold_ is true,
    // a fresh attack (arpPhys_ 0→1) clears the pool first (so the
    // previous latched chord is replaced, not added to); subsequent
    // midiNoteOff calls keep the pool intact until that 0→1 trigger.
    // UI-FIX6: arpPhys_[] tracks the actual physically-held notes (not just
    // a count) so setArpHold(false) can filter the pool to that set —
    // matches Korg LATCH/HOLD-off semantics (microKORG, Minilogue, Volca).
    void midiNoteOn (int n) noexcept {
        if (arpEnabled_) {
            if (arpHold_ && arpPhysN_ == 0) {              // EXT-ARP Block C latch reset
                arpPoolN_ = 0; arpIdx_ = 0; arpUpDownCnt_ = 0;
            }
            arpAdd_(n);                                     // EXT-ARP Block A (pool)
            arpListAdd_(arpPhys_, arpPhysN_, n);            // EXT-ARP UI-FIX6 (physical set)
        } else { eng_->noteOn(n); }
    }
    void midiNoteOff(int n) noexcept {
        if (arpEnabled_) {                                  // EXT-ARP Block A+C
            arpListRemove_(arpPhys_, arpPhysN_, n);         // EXT-ARP UI-FIX6 (always)
            if (!arpHold_) arpRemove_(n);                   // EXT-ARP Block C — keep pool when held
        } else {
            eng_->noteOff(n);
        }
    }

    // Render n samples, applying sequencer + sorted MIDI events sample-accurately.
    void render(float* out, int n,
                const MidiEvent* ev = nullptr, int nev = 0) noexcept {
        int ei = 0;
        for (int i = 0; i < n; ++i) {
            while (ei < nev && ev[ei].offset == i) {
                const MidiEvent& e = ev[ei++];
                if      (e.type == MidiEvent::Type::NoteOn)  midiNoteOn(e.data1);
                else if (e.type == MidiEvent::Type::NoteOff) midiNoteOff(e.data1);
                else                                         midiCC(e.data1, e.value);
            }
            if (fluxMode_) {                                     // E5 sample-accurate
                const FluxPattern& fp = fluxBank_.read();
                if (fp.loopLen > 0) {
                    while (fpos_ < fp.count && fp.ev[fpos_].off == fcur_) {
                        const FluxEvent& e = fp.ev[fpos_++];
                        if      (e.type == 2) {                  // EXT-FLUX param
                            // Cast is safe: fluxParam clamps pid to 0..15 which
                            // matches Param enum ordering Cutoff..Volume.
                            using P = para3::ParaEngine::Param;
                            eng_->setParamNorm(static_cast<P>(e.note), (double)e.val);
                        }
                        else if (e.type == 1) eng_->noteOff(e.note); // OFF first (sorted)
                        else                  eng_->noteOn (e.note);
                    }
                }
                // cursor runs on the active loop length, or the edit-buffer
                // length while recording before the first commit.
                const unsigned int L = fp.loopLen ? fp.loopLen
                                                   : fluxEdit_.loopLen;
                if (L > 0) { if (++fcur_ >= L) { fcur_ = 0; fpos_ = 0; } } // click-free wrap
                else       { ++fcur_; }
            } else {
                // EXT-FLUX-GATE: per-step gate-length scheduler. Decrement the
                // pending sample countdown before the next tick so a gateOff
                // fires on the right sample even when it lands at a step
                // boundary. Default -1 keeps this branch a one-cycle no-op.
                if (seqGateOffSamples_ > 0) {
                    if (--seqGateOffSamples_ == 0 && seqGateOffNote_ >= 0) {
                        const int gn = seqGateOffNote_;
                        eng_->noteOff(gn);
                        if (seqLastGatedNote_ == gn) seqLastGatedNote_ = -1;
                        seqGateOffNote_ = -1;
                    }
                }
                if (clock_.tick(&stepIdx_)) onStep();
                if (smooth_ && stepIdx_ >= 0) applySmoothLanes();   // E3 SMOOTH on
            }
            // EXT-ARP Block A + FIX4: per-sample arp scheduler. Runs whenever
            // arpEnabled_ is true — INDEPENDENT of the sequencer's transport
            // state (clock_.running()). This matches industry-standard HW arps
            // (Volca FM, Minilogue, JP-8000, etc.): the arp derives its
            // step duration from the same tempo (clock_.sixteenthSamples()),
            // but the user's Play button controls only the SEQUENCER. The
            // earlier spec §1.4 reading ("arp runs only when clock_.running()")
            // forced the user to press Play to hear the arp, which is wrong.
            // arpStepSamples_ is set by setTempo / setArpRate regardless of
            // running state, so the tempo source is always valid. When
            // arpEnabled_ goes false, setArpEnabled releases the current note
            // and zeroes arpAcc_/arpGateAcc_/arpIdx_ for a clean restart.
            if (arpEnabled_) {
                arpAcc_ += 1.0;                                  // EXT-ARP
                if (arpAcc_ >= arpStepSamples_) {                // EXT-ARP
                    arpAcc_ -= arpStepSamples_;                  // EXT-ARP
                    arpStep_();                                  // EXT-ARP
                }
                if (arpCur_ >= 0 && arpGateAcc_ > 0.0) {         // EXT-ARP gate
                    arpGateAcc_ -= 1.0;                          // EXT-ARP
                    if (arpGateAcc_ <= 0.0 && arpGate_ < 1.0) {  // EXT-ARP staccato
                        eng_->noteOff(arpCur_); arpCur_ = -1;    // EXT-ARP
                    }
                }
            }
            eng_->process(out + i, 1);
            // EXT-FLUX-VEL — apply per-step velocity gain post-engine. Default
            // stepVelGain_ = 1.0 keeps bit-identity to pre-EXT-FLUX-VEL.
            out[i] *= (float)stepVelGain_;
        }
    }
    int currentStep() const noexcept { return stepIdx_; }
    // EXT-ARP Block A: observable + setters (no taper trichter — Controller settings).
    long arpDropped() const noexcept { return arpDropped_; }     // EXT-ARP
    // EXT-ARP UI-FIX6 test accessors (read-only pool/phys snapshots).
    int  arpPoolSize() const noexcept { return arpPoolN_; }
    int  arpPoolNote(int i) const noexcept {
        return (i >= 0 && i < arpPoolN_) ? arpPool_[i] : -1;
    }
    int  arpPhysSize() const noexcept { return arpPhysN_; }
    // EXT-ARP-MOTION test accessor: current discrete mode (0..4).
    int  arpModeCurrent() const noexcept { return arpMode_; }
    void setArpEnabled(bool on) noexcept {                       // EXT-ARP
        if (on == arpEnabled_) return;
        arpEnabled_ = on;
        if (!on && arpCur_ >= 0) { eng_->noteOff(arpCur_); arpCur_ = -1; }
        arpAcc_ = 0.0; arpGateAcc_ = 0.0; arpIdx_ = 0;           // EXT-ARP clean transition
        if (on) arpRecalcStepSamples_();                          // EXT-ARP pick up current tempo
    }
    // EXT-ARP: tempo entry-point that also refreshes arpStepSamples_.
    // Wraps clock_.setTempo so the arp accumulator stays in sync after a
    // BPM change. Existing C-API for seqTempo routes through here.
    void setSeqTempo(double bpm, int stepsPerBeat = 4) noexcept { // EXT-ARP
        clock_.setTempo(bpm, stepsPerBeat);
        arpRecalcStepSamples_();
    }
    void setArpMode(int m) noexcept {                            // EXT-ARP (Block A: only Up=0)
        arpMode_ = (m >= 0 && m <= 4) ? m : 0;
    }
    void setArpRate(int r) noexcept {                            // EXT-ARP rate index
        arpRate_ = (r >= 0 && r < 6) ? r : 3;                    // EXT-ARP default 1/16
        arpRecalcStepSamples_();
    }
    void setArpGate(double g) noexcept {                         // EXT-ARP 0..1
        arpGate_ = (g < 0.0) ? 0.0 : (g > 1.0 ? 1.0 : g);
    }
    void setArpOctaves(int oct) noexcept {                       // EXT-ARP Block B 1..4
        const int o = (oct < 1) ? 1 : (oct > 4 ? 4 : oct);
        if (o == arpOct_) return;
        arpOct_ = o;
        // Reclamp running indices modulo the new L so the next step lands in
        // a valid slot of the new effective sequence. Pool may be empty here;
        // arpStep_ guards on arpPoolN_==0 anyway.
        const int L = (arpPoolN_ > 0) ? (arpPoolN_ * arpOct_) : 1;
        const int period = (L > 1) ? (2*L - 2) : 1;
        arpIdx_       = arpIdx_       % L;
        arpUpDownCnt_ = arpUpDownCnt_ % period;
    }
    // EXT-ARP Block C + UI-FIX5/6: Hold/Latch toggle. With hold=true,
    // midiNoteOff leaves the pool untouched; the arp keeps playing held
    // notes until either hold goes off or a fresh "all-released → new key"
    // gesture clears it (the 0→1 arpPhysN_ transition; spec §2.6).
    //
    // Industry-standard HW behaviour for HOLD off (Korg microKORG /
    // Minilogue LATCH, Volca FM, JP-8000):
    //   * no keys physically held  → drop the entire latched set, arp stops
    //     (UI-FIX5);
    //   * keys still physically held → filter the pool down to that set,
    //     so notes briefly pressed and released *during* HOLD (then released
    //     before HOLD-off) disappear; only the actually-held keys remain
    //     and the arp keeps cycling them (UI-FIX6).
    // We also release arpCur_ when it falls outside the new pool, accounting
    // for octave shifts produced by arpStep_ (note = phys + 12*k).
    void setArpHold(bool on) noexcept {
        if (on == arpHold_) return;
        const bool wasOn = arpHold_;
        arpHold_ = on;
        if (!(wasOn && !on)) return;
        if (arpPhysN_ == 0) {
            // UI-FIX5: latch released and no key held → clear & silence.
            arpPoolN_ = 0; arpIdx_ = 0; arpUpDownCnt_ = 0;
            if (arpCur_ >= 0) { eng_->noteOff(arpCur_); arpCur_ = -1; }
            arpAcc_ = 0.0; arpGateAcc_ = 0.0;
        } else {
            // UI-FIX6: latch released but keys still held → pool ← phys set.
            for (int i = 0; i < arpPhysN_; ++i) arpPool_[i] = arpPhys_[i];
            arpPoolN_ = arpPhysN_;
            arpIdx_ = 0; arpUpDownCnt_ = 0;
            // Release currently-sounding note if it's not a member (any
            // octave shift) of the new physical set.
            if (arpCur_ >= 0) {
                bool stillHeld = false;
                for (int k = 0; k < arpOct_ && !stillHeld; ++k) {
                    const int base = arpCur_ - 12*k;
                    for (int i = 0; i < arpPhysN_; ++i)
                        if (arpPhys_[i] == base) { stillHeld = true; break; }
                }
                if (!stillHeld) { eng_->noteOff(arpCur_); arpCur_ = -1; }
            }
        }
    }
    // EXT-ARP Block C: Random PRNG seed. Same seed ⇒ identical note sequence
    // (T34 proves reproducibility). Default seed is a design default, not a
    // hardware calibration — the Volca Keys has no random arp.
    void setArpSeed(unsigned int s) noexcept { arpRng_ = (uint32_t)s; }

private:
    void onStep() noexcept {
        const Pattern& p = bank_.read();                     // consistent snapshot
        if (stepIdx_ >= p.length) return;
        const Step& s = p.steps[stepIdx_];
        if (!s.active) return;                               // E4.3 skip (no note/motion/capture)
        if (metroOn_ && (stepIdx_ % 4 == 0))                 // E4.4 quarter-note tick
            eng_->metroTrigger(stepIdx_ == 0);               // accent on beat 1
        if (recording_) {                                    // capture into edit buf
            Pattern& e = bank_.edit();
            e = p;                                           // base on current
            e.steps[stepIdx_].motionOn  = true;
            e.steps[stepIdx_].motionCut = recCC_;
            if (stepIdx_ == p.length - 1) bank_.commit();    // publish at loop end
        }
        // EXT-ARP-FIX: only release notes the SEQUENCER actually played. The
        // legacy `noteOff(s.note)` on every gate-off step blindly killed any
        // currently-allocated voice whose pitch happened to match s.note —
        // which is fine when only the seq plays, but breaks "additive arp
        // + seq" coexistence (spec §1.3): a held arp/keyboard note matching
        // s.note got silenced after one tick. Track the last gated note; only
        // release that. seqLastGatedNote_ resets to -1 in seqStart so a fresh
        // Play doesn't carry over state from a previous run.
        if (s.gate) {
            eng_->noteOn(s.note);
            seqLastGatedNote_ = s.note;
            // EXT-FLUX-VEL — per-step velocity scales the engine output (post-
            // engine gain). Updated only on gated steps; sustained notes keep
            // the velocity of their original hit (Volca-Bass/Minilogue semantic).
            // Default vel=1.0 = bitidentisch (anti-blender T46-NEU).
            stepVelGain_ = s.vel;
            // EXT-FLUX-GATE — schedule sample-accurate noteOff at gateLen
            // fraction of the step. gateLen=1.0 (default) → leave scheduler
            // disarmed (-1); the legacy next-step noteOff path keeps full
            // bit-identity to the pre-EXT-FLUX-GATE stand.
            if (s.gateLen < 1.0) {
                const int len = (int)clock_.sixteenthSamples();
                int n = (int)(len * (s.gateLen < 0.0 ? 0.0 : s.gateLen));
                if (n < 1) n = 1;          // gateLen>0 always fires at least 1 sample
                seqGateOffSamples_ = n;
                seqGateOffNote_    = s.note;
            } else {
                seqGateOffSamples_ = -1;
            }
        } else if (!stepTrig_) {
            if (seqLastGatedNote_ >= 0) {
                eng_->noteOff(seqLastGatedNote_);
                seqLastGatedNote_ = -1;
            }
        }
        if (stepTrig_) eng_->retrigger();                    // E4.1 force EG every step
        if (s.motionOn)                                      // legacy cutoff lane
            eng_->setParamNorm(para3::ParaEngine::Param::Cutoff, s.motionCut);

        // E3 one-loop capture: write live value into the param's lane, then
        // auto-deactivate after exactly one full loop (Volca behaviour).
        bool committed = false;
        for (int pid = 0; pid < kMP; ++pid) {
            if (!mCap_[pid]) continue;
            Pattern& e = bank_.edit(); e = p;
            e.lane[pid].used = true;
            e.lane[pid].v[stepIdx_] = (float)mLive_[pid];
            applyMotionParam_(pid, mLive_[pid]);              // EXT-ARP-MOTION audible
            if (++mCapCnt_[pid] >= p.length) { mCap_[pid] = false;
                bank_.commit(); committed = true; }   // publish at loop end
        }
        (void)committed;

        // E3 playback, SMOOTH off: apply each used lane's step value (funnel,
        // RampParam => stepped contour, zipper-free).
        if (!smooth_) {
            for (int pid = 0; pid < kMP; ++pid) {
                if (!p.lane[pid].used) continue;
                applyMotionParam_(pid, p.lane[pid].v[stepIdx_]);  // EXT-ARP-MOTION
            }
        }
    }
    // E3 playback, SMOOTH on: linear interpolation across the step duration.
    void applySmoothLanes() noexcept {
        const Pattern& p = bank_.read();
        if (stepIdx_ >= p.length) return;
        const double ph = clock_.phase();
        const int nx = (stepIdx_ + 1) % p.length;
        for (int pid = 0; pid < kMP; ++pid) {
            if (!p.lane[pid].used) continue;
            const double a = p.lane[pid].v[stepIdx_];
            const double b = p.lane[pid].v[nx];
            applyMotionParam_(pid, a + (b - a) * ph);         // EXT-ARP-MOTION
        }
    }
    // EXT-ARP-MOTION: single dispatch for "apply lane value at current step".
    // Handles both engine PARA3_P_* params (pid 0..15) via setParamNorm, and
    // the Controller-level ARP_MODE pid (16) by snapping norm 0..1 → 0..4.
    // RT-safe: one engine call OR one int store; no allocation.
    void applyMotionParam_(int pid, double v) noexcept {
        if (pid == kArpModePid) {
            int m = (int)std::floor(v * 5.0);   // 0..1 → 0..4 (5 buckets of 0.2)
            if (m > 4) m = 4;
            if (m < 0) m = 0;
            setArpMode(m);
            return;
        }
        para3::ParaEngine::Param pr;
        if (paramOfId(pid, pr)) eng_->setParamNorm(pr, v);
    }
    // ============================ EXT-ARP Block A ============================
    // Note-source transform sitting between keyboard/MIDI and the engine.
    // Default OFF — every byte of the audio path is identical to the pre-EXT
    // build when arpEnabled_==false (T27a is the proof). All controller
    // settings, no taper/funnel. RT-safe: no allocation, no rand(), bounded.
    static constexpr int kArpCap = 16;                 // EXT-ARP pool capacity
    // EXT-ARP UI-FIX6: generic deduped-list helpers, reused by both the
    // latched pool (arpPool_) and the physical-key set (arpPhys_). The
    // pool variant additionally bumps arpDropped_ on overflow (observable).
    static void arpListAdd_(int* p, int& n, int note) noexcept {
        for (int i = 0; i < n; ++i) if (p[i] == note) return;
        if (n >= kArpCap) return;
        p[n++] = note;
    }
    static void arpListRemove_(int* p, int& n, int note) noexcept {
        int w = 0;
        for (int r = 0; r < n; ++r) if (p[r] != note) p[w++] = p[r];
        n = w;
    }
    void arpAdd_(int n) noexcept {                     // EXT-ARP dedupe + bounded
        for (int i = 0; i < arpPoolN_; ++i) if (arpPool_[i] == n) return;
        if (arpPoolN_ >= kArpCap) { ++arpDropped_; return; }   // Pool full
        arpPool_[arpPoolN_++] = n;
    }
    void arpRemove_(int n) noexcept { arpListRemove_(arpPool_, arpPoolN_, n); }
    void arpRecalcStepSamples_() noexcept {            // EXT-ARP rate table
        // 16th-multipliers per §2.3 (1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32).
        static constexpr double mul[6] = { 4.0, 2.0, 4.0/3.0, 1.0, 2.0/3.0, 0.5 };
        arpStepSamples_ = clock_.sixteenthSamples() * mul[arpRate_];
    }
    // EXT-ARP Block C: xorshift32 PRNG. RT-safe, deterministic from arpRng_.
    // Seeded via setArpSeed; default seed 0x9E3779B9 is the golden-ratio
    // constant used as a "Design-Default" — there is no HW reference.
    inline uint32_t arpXorshift_() noexcept {
        uint32_t x = arpRng_; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return (arpRng_ = x);
    }
    void arpStep_() noexcept {                         // EXT-ARP one beat
        // OFF-before-ON discipline (mirrors E5, click-free via existing env).
        if (arpCur_ >= 0) { eng_->noteOff(arpCur_); arpCur_ = -1; }
        if (arpPoolN_ == 0) return;                    // idle, silence
        // Block B: sort ascending for Up/Down/UpDown (AsPlayed uses pool order).
        int srt[kArpCap];
        for (int i = 0; i < arpPoolN_; ++i) srt[i] = arpPool_[i];
        for (int i = 1; i < arpPoolN_; ++i) {          // insertion sort (n<=16)
            int x = srt[i], j = i;
            while (j > 0 && srt[j-1] > x) { srt[j] = srt[j-1]; --j; }
            srt[j] = x;
        }
        const int n   = arpPoolN_;
        const int oct = arpOct_;                       // EXT-ARP Block B 1..4
        const int L   = n * oct;
        // dispatch on mode; computes the "effective sequence" index i in [0,L).
        // (note = base + 12 * (i / n) where base is from srt[] or pool[] per mode)
        int i = 0;
        switch (arpMode_) {
            case 1: {                                   // EXT-ARP Down
                i = (L - 1) - (arpIdx_ % L);
                arpIdx_ = (arpIdx_ + 1) % L;
                break;
            }
            case 2: {                                   // EXT-ARP UpDown (excl. endpoints)
                // Period = 2L-2 for L>1; constant 0 for L==1.
                const int period = (L > 1) ? (2*L - 2) : 1;
                const int c = arpUpDownCnt_ % period;
                i = (c < L) ? c : (2*L - 2 - c);
                arpUpDownCnt_ = (arpUpDownCnt_ + 1) % period;
                break;
            }
            case 3: {                                   // EXT-ARP AsPlayed
                // Pool insertion order (no sort), with octave wrap.
                const int j = arpIdx_;
                arpIdx_ = (arpIdx_ + 1) % L;
                const int note = arpPool_[j % n] + 12 * (j / n);
                eng_->noteOn(note); arpCur_ = note;
                arpGateAcc_ = arpGate_ * arpStepSamples_;
                return;                                 // bypass sorted-path below
            }
            case 4: {                                   // EXT-ARP Block C — Random
                // Deterministic xorshift32 (seedable, RT-safe, no rand()).
                // Note built from SORTED pool so octave-spread is reproducible
                // for a given seed — spec §2.5.
                i = (int)(arpXorshift_() % (uint32_t)L);
                // arpIdx_ not advanced (Random has no "next" semantic).
                break;
            }
            default: {                                  // EXT-ARP Up (mode 0)
                i = arpIdx_;
                arpIdx_ = (arpIdx_ + 1) % L;
                break;
            }
        }
        const int note = srt[i % n] + 12 * (i / n);
        eng_->noteOn(note); arpCur_ = note;
        arpGateAcc_ = arpGate_ * arpStepSamples_;
    }
    // EXT-ARP state (Blocks A+B; Block C extends hold/rng).
    bool   arpEnabled_   = false;                       // EXT-ARP DEFAULT off
    int    arpMode_      = 0;                           // EXT-ARP 0=Up 1=Dn 2=UpDn 3=AsPlayed 4=Random
    int    arpRate_      = 3;                           // EXT-ARP DEFAULT 1/16
    int    arpOct_       = 1;                           // EXT-ARP Block B DEFAULT 1..4 range
    int    arpPool_[kArpCap] = {0};                     // EXT-ARP latched/active pool
    int    arpPoolN_     = 0;                           // EXT-ARP pool size
    int    arpPhys_[kArpCap] = {0};                     // EXT-ARP UI-FIX6 physical keys
    int    arpPhysN_     = 0;                           // EXT-ARP physical-key count
    int    arpIdx_       = 0;                           // EXT-ARP running index
    int    arpUpDownCnt_ = 0;                           // EXT-ARP Block B UpDown counter (period 2L-2)
    int    arpCur_       = -1;                          // EXT-ARP currently sounding note (-1 none)
    double arpStepSamples_ = 6000.0;                    // EXT-ARP recalc on tempo/rate
    double arpAcc_       = 0.0;                         // EXT-ARP sample accumulator
    double arpGate_      = 0.5;                         // EXT-ARP DEFAULT staccato-50%
    double arpGateAcc_   = 0.0;                         // EXT-ARP samples until NoteOff
    long   arpDropped_   = 0;                           // EXT-ARP observable pool-overflow
    bool   arpHold_      = false;                       // EXT-ARP Block C latch toggle
    uint32_t arpRng_     = 0x9E3779B9u;                 // EXT-ARP Block C DEFAULT seed (golden ratio)
    // =========================================================================
    para3::ParaEngine* eng_ = nullptr;
    double      sr_ = 48000.0;
    Clock       clock_;
    PatternBank bank_;
    Pattern     edit_;
    int         stepIdx_ = -1;
    double      recCC_ = 0.5;
    bool        recording_ = false;
    // EXT-ARP-FIX: tracked so onStep only releases notes the seq actually
    // played (instead of blindly noteOff(s.note) every gate-off, which
    // killed any matching arp/keyboard voice). See onStep + seqStart.
    int         seqLastGatedNote_ = -1;
    bool        smooth_ = false;                 // E3 SMOOTH toggle
    bool        stepTrig_ = false;               // E4.1
    bool        metroOn_  = false;               // E4.4
    bool        fluxMode_     = false;           // E5
    bool        fluxRec_      = false;           // E5
    bool        fluxQuantize_ = true;            // EXT-FLUX default 1/16 (Korg)
    double      stepVelGain_  = 1.0;             // EXT-FLUX-VEL (default 1.0)
    int         seqGateOffSamples_ = -1;         // EXT-FLUX-GATE pending noteOff countdown
    int         seqGateOffNote_    = -1;         // EXT-FLUX-GATE note awaiting gate-off
    unsigned int fcur_ = 0;                      // E5 sample cursor
    unsigned short fpos_ = 0;                     // E5 next-event index
    long        fluxDropped_ = 0;                // E5 observable overflow
    FluxBank    fluxBank_;                        // E5 lock-free
    FluxPattern fluxEdit_;                        // E5 host-side authoritative
    long        mRejects_ = 0;                   // E3 observable PEAK/TEMPO refusals
    bool        mCap_[kMP]    = {false};         // E3 one-loop capture armed
    int         mCapCnt_[kMP] = {0};             // E3 steps since capture start
    double      mLive_[kMP]   = {0};             // E3 fed live value
};

} // namespace para3
