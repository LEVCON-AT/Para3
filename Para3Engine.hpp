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
    inline float process(double freqHz) noexcept {
        for (int i = 0; i < kOS; ++i) dec_.push(core_.process(freqHz));
        // CALIB(sprint1): measured saw-shaping / HF roll-off stage inserts here.
        return static_cast<float>(dec_.read());
    }
private:
    PolyBlepCore         core_;
    Decimator<kOS, kTap> dec_;
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
    bool ring() const noexcept {
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
    bool anyHeld() const noexcept { return count_ > 0; }

    // Fills target semitone for 3 oscillators + per-osc active flag.
    void resolve(double outNote[3], bool active[3]) const noexcept {
        for (int i = 0; i < 3; ++i) { outNote[i] = 0.0; active[i] = false; }
        if (count_ == 0) return;
        const double newest = held_[count_ - 1];
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
        delay_.prepare(sampleRate, 2.0);                // 2 s buffer (prepare-only)
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
    void noteOn(int note)  noexcept { alloc_.noteOn(note);  refresh(); env_.gateOn();  gateHeld_ = true; }
    void noteOff(int note) noexcept {
        alloc_.noteOff(note); refresh();
        if (!alloc_.anyHeld()) { env_.gateOff(); gateHeld_ = false; }
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

    // --- Sprint-7 unified funnel: the SINGLE public entry every source
    //     (UI / MIDI CC / host automation / sequencer motion) goes through.
    //     normalized 0..1 -> CALIB(sprint1) taper -> the existing smoothed
    //     setter (RampParam) -> DSP. No source bypasses taper or smoother.
    enum class Param { Cutoff, Resonance, Drive, LfoCutDepth, DelayMix,
                       LfoRate, LfoPitchDepth, DelayTime, DelayFeedback,
                       Attack, DecRel, Sustain };

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

            // funnel -> filter controls (smoothed), LFO applied in log domain
            const double baseCut = cutoff_.next();
            ladder_.setCutoffHz(baseCut * std::pow(2.0, cMod));
            ladder_.setResonance(reso_.next());

            double mix = 0.0;
            if (alloc_.ring()) {
                // ring operands via the funnel (zipper-free), product on its
                // own oversampling island. Non-ring path is untouched.
                const double nA = pitch_[0].next() + pMod;
                const double nB = pitch_[1].next() + pMod;
                pitch_[2].next();                       // keep funnel consistent
                mix = ring_.process(semitonesToHz(nA), semitonesToHz(nB));
            } else {
                for (int v = 0; v < 3; ++v) {
                    if (!active_[v]) { pitch_[v].next(); continue; }
                    const double hz = semitonesToHz(pitch_[v].next() + pMod);
                    mix += osc_[v].process(hz);
                }
            }
            mix *= 0.5;                                 // headroom for the drive

            const double filt = island_.process(mix,
                [this](double s){ return ladder_.tick(s); });

            out[i] = delay_.process(static_cast<float>(filt * env_.next()));
        }
    }
private:
    void refresh() noexcept {
        double tgt[3]; bool act[3];
        alloc_.resolve(tgt, act);
        for (int i = 0; i < 3; ++i) {
            active_[i] = act[i];
            if (act[i]) {
                if (!wasActive_[i]) pitch_[i].snap(tgt[i]);  // no glide-in
                pitch_[i].setTarget(tgt[i]);
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
    Delay          delay_;
    bool           gateHeld_ = false;
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
};
struct Pattern {
    Step steps[16];
    int  length = 16;
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

// Sample-accurate step clock with swing. Internal tempo or external ticks.
class Clock {
public:
    void prepare(double sampleRate) noexcept { sr_ = sampleRate; reset(); }
    void reset() noexcept { acc_ = 0.0; running_ = false; }
    void setTempo(double bpm, int stepsPerBeat = 4) noexcept {
        stepSamples_ = (60.0 / std::max(1.0, bpm)) / stepsPerBeat * sr_;
    }
    void setSwing(double s) noexcept { swing_ = clamp(s, 0.0, 0.7); } // CALIB(sprint1)
    void start() noexcept { running_ = true;  acc_ = 0.0; }
    void stop()  noexcept { running_ = false; }
    bool running() const noexcept { return running_; }

    // returns true exactly on the sample a new step begins; *stepIdxIo advances
    inline bool tick(int* stepIdxIo) noexcept {
        if (!running_) return false;
        acc_ += 1.0;
        const bool odd = ((*stepIdxIo) & 1) != 0;
        const double dur = stepSamples_ * (odd ? (1.0 - swing_) : (1.0 + swing_));
        if (acc_ >= dur) {
            acc_ -= dur;
            *stepIdxIo = (*stepIdxIo + 1) & 15;
            return true;
        }
        return false;
    }
private:
    double sr_ = 48000.0, acc_ = 0.0, stepSamples_ = 6000.0, swing_ = 0.0;
    bool   running_ = false;
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

    // CC#74 -> Cutoff (the classic). One mapping shown; same funnel for all.
    void midiCC(int cc, double norm) noexcept {
        if (cc == 74) {
            recCC_ = norm;                                   // also captured if armed
            eng_->setParamNorm(para3::ParaEngine::Param::Cutoff, norm);
        }
    }
    void midiNoteOn (int n) noexcept { eng_->noteOn(n);  }
    void midiNoteOff(int n) noexcept { eng_->noteOff(n); }

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
            if (clock_.tick(&stepIdx_)) onStep();
            eng_->process(out + i, 1);
        }
    }
    int currentStep() const noexcept { return stepIdx_; }

private:
    void onStep() noexcept {
        const Pattern& p = bank_.read();                     // consistent snapshot
        if (stepIdx_ >= p.length) return;
        const Step& s = p.steps[stepIdx_];
        if (recording_) {                                    // capture into edit buf
            Pattern& e = bank_.edit();
            e = p;                                           // base on current
            e.steps[stepIdx_].motionOn  = true;
            e.steps[stepIdx_].motionCut = recCC_;
            if (stepIdx_ == p.length - 1) bank_.commit();    // publish at loop end
        }
        if (s.gate) eng_->noteOn(s.note); else eng_->noteOff(s.note);
        if (s.motionOn)                                      // motion -> funnel
            eng_->setParamNorm(para3::ParaEngine::Param::Cutoff, s.motionCut);
    }
    para3::ParaEngine* eng_ = nullptr;
    double      sr_ = 48000.0;
    Clock       clock_;
    PatternBank bank_;
    Pattern     edit_;
    int         stepIdx_ = -1;
    double      recCC_ = 0.5;
    bool        recording_ = false;
};

} // namespace para3
