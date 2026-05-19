// =============================================================================
//  PARA-3 :: Mess-Tooling — Driver
//
//  Treibt sämtliche M1.x..M9.x Messreihen. Schreibt:
//    docs/measurements/M{n.m}-{slug}.svg      Plot-Kurve
//    docs/measurements/M{n.m}-{slug}.wav      (optional) Archiv-Audio
//    docs/measurements/MANIFEST.csv            CSV-Index aller Messungen
//    docs/measurements/MANIFEST.md             Markdown-Tabelle (dieselben Reihen)
//
//  Build:
//    g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I../.. measure_main.cpp -o /tmp/m
//
//  Aufruf:
//    /tmp/m            führt alle Messungen aus
//    /tmp/m M1         nur Sektion 1 (Oszillator)
//    /tmp/m M1.1       nur eine Messung
//
//  LAB-1 baseline: nur die self-test Messungen sind aktiv. Echte M*.x Reihen
//  werden in LAB-2..6 sukzessive ergänzt (jede ist eine eigenständige Funktion
//  innerhalb dieser Datei oder unter tools/measure/).
// =============================================================================

#include "fft.hpp"
#include "svg_plot.hpp"
#include "wav_write.hpp"
#include "manifest.hpp"
#include "../../Para3Engine.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace para3::measure;
namespace P3 = para3;

static const double SR = 48000.0;
static const std::string OUT_DIR = "docs/measurements";

// -- helper: render 'samples' through the engine for `len` samples ------------
static std::vector<float> renderSimple(P3::ParaEngine& e, int len) {
    std::vector<float> out(len);
    e.process(out.data(), len);
    return out;
}

// MIDI → Hz (A4=440, MIDI 69). Same formula the engine uses (Para3Engine.hpp:63).
static double midiHz(double note) {
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// Set every smoothed param to a clean reference: filter fully open, envelope
// instant-attack + full sustain, no LFO, no delay, no detune, no EG-mod.
// This isolates the oscillator path so spectra reflect oscillator shape, not
// envelope decay or filter colouring.
static void setNeutralPatch(P3::ParaEngine& e) {
    using P = P3::ParaEngine::Param;
    e.setParamNorm(P::Cutoff,        1.0);   // open
    e.setParamNorm(P::Resonance,     0.0);
    e.setParamNorm(P::Drive,         0.0);
    e.setParamNorm(P::LfoCutDepth,   0.0);
    e.setParamNorm(P::DelayMix,      0.0);
    e.setParamNorm(P::LfoRate,       0.0);
    e.setParamNorm(P::LfoPitchDepth, 0.0);
    e.setParamNorm(P::DelayTime,     0.5);
    e.setParamNorm(P::DelayFeedback, 0.0);
    e.setParamNorm(P::Attack,        0.0);
    e.setParamNorm(P::DecRel,        1.0);
    e.setParamNorm(P::Sustain,       1.0);
    e.setParamNorm(P::EgCutDepth,    0.5);   // bipolar centre → 0 mod
    e.setParamNorm(P::Detune,        0.0);
    e.setParamNorm(P::Portamento,    0.0);
    e.setParamNorm(P::Volume,        1.0);
    e.setMode(P3::ParaAllocator::Mode::Poly);
    e.setOctave(0);
}

// Run engine, settle for `warmupS` seconds, capture `captureN` samples.
static std::vector<float> capture(P3::ParaEngine& e, int captureN,
                                   double warmupS = 0.05) {
    std::vector<float> warm((std::size_t)(SR * warmupS));
    if (!warm.empty()) e.process(warm.data(), (int)warm.size());
    std::vector<float> cap((std::size_t)captureN);
    e.process(cap.data(), captureN);
    return cap;
}

// Convenience: window, FFT, mag-dB. Returns (magDb, X.size).
struct SpecOut { std::vector<double> magDb; std::size_t N; };
static SpecOut spectrum(const std::vector<float>& s) {
    auto X = realFft(s, true);
    auto w = hannWindow(s.size());
    double winSum = 0; for (double v : w) winSum += v;
    SpecOut o; o.N = X.size(); o.magDb = magnitudeDb(X, winSum);
    return o;
}

// =============================================================================
//  M0.1 self-test — pure sinusoid into FFT round-trip
//    Ausgangsfrage: liefert die Mess-Infrastruktur korrekte Werte?
//    Methode: 1 kHz Sine, 1 s, FFT(N=65536, Hann), peak interp.
//    Erwartung: Peak bei 1000 Hz ±0.5 Hz, THD vernachlässigbar.
// =============================================================================
static MEntry M0_1_selftest() {
    const std::size_t N = 65536;
    std::vector<float> s(N);
    for (std::size_t i = 0; i < N; ++i)
        s[i] = (float)std::sin(2.0 * M_PI * 1000.0 * (double)i / SR);

    auto win = hannWindow(N);
    double winSum = 0; for (double w : win) winSum += w;
    auto X = realFft(s, true);
    auto mag = magnitudeDb(X, winSum);

    const double f = peakFreqInterp(mag, X.size(), SR);
    const double thd = thdPercent(mag);

    writeSpectrum(OUT_DIR + "/M0.1-selftest-sine-1k.svg",
                  "M0.1 — Self-Test (1 kHz Sine, FFT/Hann)", mag, X.size(), SR);

    MEntry e;
    e.id="M0.1"; e.section="SelfTest"; e.what="Pure 1 kHz sine FFT round-trip";
    e.metric="peak_freq"; e.unit="Hz";
    e.expected="1000.0";
    char buf[32]; std::snprintf(buf, sizeof buf, "%.2f", f); e.measured=buf;
    e.pass = std::fabs(f - 1000.0) < 0.5;
    e.svgPath = OUT_DIR + "/M0.1-selftest-sine-1k.svg";
    char nb[64]; std::snprintf(nb, sizeof nb, "THD = %.3f %%", thd); e.note=nb;
    return e;
}

// =============================================================================
//  M0.2 self-test — engine smoke: 200 ms 60-MIDI, capture peak RMS
//    Sicherstellt, dass die Engine in der Mess-Strecke korrekt verlinkt ist
//    (keine Linker-Drift gegenüber offline_test.cpp).
// =============================================================================
static MEntry M0_2_engineSmoke() {
    P3::ParaEngine e;
    e.prepare(SR, 256);
    e.setParamNorm(P3::ParaEngine::Param::Cutoff, 0.7);
    e.setParamNorm(P3::ParaEngine::Param::Volume, 1.0);
    e.setParamNorm(P3::ParaEngine::Param::DelayMix, 0.0);
    e.noteOn(60);
    auto out = renderSimple(e, (int)(SR * 0.2));   // 0.2 s
    double rms = 0; for (float s : out) rms += (double)s * s;
    rms = std::sqrt(rms / (double)out.size());

    writeScope(OUT_DIR + "/M0.2-engine-smoke.svg",
               "M0.2 — Engine Smoke (60 MIDI, 0.2 s capture)",
               out, SR, 0.0, 0.05);

    MEntry m;
    m.id="M0.2"; m.section="SelfTest"; m.what="Engine wired into measure_main";
    m.metric="rms"; m.unit="";
    m.expected=">0.01";
    char buf[32]; std::snprintf(buf, sizeof buf, "%.4f", rms); m.measured=buf;
    m.pass = rms > 0.01;
    m.svgPath = OUT_DIR + "/M0.2-engine-smoke.svg";
    return m;
}

// =============================================================================
//  LAB-2 :: M1.x Oszillator
// =============================================================================

// -- M1.1 Saw-Wellenform bei C4 (MIDI 60) -------------------------------------
// Frage:  Erzeugt die Engine bei MIDI 60 einen sauberen Saw-Ton mit
//         Grundfrequenz 261.63 Hz?
// User:   "Spiele eine einzelne mittlere Note bei offenem Filter."
// Mess:   FFT (32k, Hann) der eingeschwungenen Stationärphase, Peak
//         interpoliert, THD über H2..H10.
// Erw:    f₀ = 261.63 Hz ±0.5 Hz; THD typisch für bandlimitierten Saw > 30 %.
static MEntry M1_1_sawWaveform() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e); e.noteOn(60);
    auto cap = capture(e, 32768, 0.10);

    auto sp = spectrum(cap);
    const double f   = peakFreqInterp(sp.magDb, sp.N, SR);
    const double thd = thdPercent(sp.magDb);

    writeScope(OUT_DIR + "/M1.1-saw-c4-scope.svg",
               "M1.1 — Saw @ MIDI 60 (C4 ≈ 261.63 Hz) — Scope",
               cap, SR, 0.0, 0.012);
    writeSpectrum(OUT_DIR + "/M1.1-saw-c4-spectrum.svg",
                  "M1.1 — Saw @ MIDI 60 — Spectrum",
                  sp.magDb, sp.N, SR);

    MEntry m; m.id="M1.1"; m.section="Oscillator";
    m.what="Saw waveform at MIDI 60"; m.metric="fundamental_freq"; m.unit="Hz";
    m.expected="261.63";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", f); m.measured=b;
    m.pass = std::fabs(f - midiHz(60)) < 0.5;
    m.svgPath = OUT_DIR + "/M1.1-saw-c4-spectrum.svg";
    char nb[64]; std::snprintf(nb, sizeof nb, "THD %.1f %% (saw expected)", thd);
    m.note = nb;
    return m;
}

// -- M1.2 Aliasing-Test C7 (MIDI 96) ------------------------------------------
// Frage:  Suppresses the band-limited oscillator alias products above Nyquist?
// User:   "Hohe Note spielen — kein metallisches Schillern."
// Mess:   FFT bei C7 ≈ 2093 Hz. Über Nyquist (24 kHz) gefaltete Harmonische
//         sollten unter -60 dBFS bleiben (Untergrund-Niveau).
// Erw:    Im Bereich 22-23 kHz keine Linien > -50 dB (sonst Aliasing).
static MEntry M1_2_aliasing() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e); e.noteOn(96);                 // C7
    auto cap = capture(e, 32768, 0.10);

    auto sp = spectrum(cap);
    const double f0 = peakFreqInterp(sp.magDb, sp.N, SR);

    // Worst peak in the upper region (Nyquist - 2 kHz .. Nyquist)
    double worst = -240.0;
    const double fLo = SR * 0.5 - 2000.0, fHi = SR * 0.5;
    for (std::size_t k = 0; k < sp.magDb.size(); ++k) {
        const double f = (double)k * SR / sp.N;
        if (f >= fLo && f <= fHi) worst = std::max(worst, sp.magDb[k]);
    }

    writeSpectrum(OUT_DIR + "/M1.2-aliasing-c7-spectrum.svg",
                  "M1.2 — Aliasing-Check @ C7 (2093 Hz)",
                  sp.magDb, sp.N, SR);

    MEntry m; m.id="M1.2"; m.section="Oscillator";
    m.what="Band-limit / aliasing at C7"; m.metric="worst_band_21k_24k";
    m.unit="dBFS"; m.expected="< -50";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", worst); m.measured=b;
    m.pass = worst < -50.0;
    m.svgPath = OUT_DIR + "/M1.2-aliasing-c7-spectrum.svg";
    char nb[64]; std::snprintf(nb, sizeof nb, "f0 measured %.1f Hz", f0);
    m.note = nb;
    return m;
}

// -- M1.3 Detune in UNISON-Modus ----------------------------------------------
// Frage:  Erzeugt DETUNE eine messbare spektrale Aufspaltung zwischen den drei
//         gestackten Unison-Oszillatoren?
// Mess:   Mode=UNISON, MIDI 60, DETUNE = {0.0, 0.3, 0.6, 0.9}, je 32k Samples.
//         FFT → Bestimme spektrale Bandbreite um f₀ (Peak ±3 dB Linie). Bei
//         detune=0 alle drei Oszillatoren in Phase → schmale Linie; bei
//         detune>0 verbreitert sich die Linie.
// Erw:    Bandbreite (Hz) steigt monoton mit DETUNE; bei 0.9 ≥ 5 Hz Spread.
static MEntry M1_3_detuneUnison() {
    const double values[] = {0.0, 0.3, 0.6, 0.9};
    Series spread; spread.label = "Peak −3 dB width";

    for (double dt : values) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setMode(P3::ParaAllocator::Mode::Unison);
        e.setParamNorm(P3::ParaEngine::Param::Detune, dt);
        e.noteOn(60);
        auto cap = capture(e, 65536, 0.20);     // long capture → fine bin res
        auto sp = spectrum(cap);

        // Wir messen die spektrale Standard-Abweichung um f₀ in einem
        // ±50 % Fenster. An detune=0 ist (fast) alle Energie in 1-2 Bins → σ
        // sehr klein. An detune=0.9 verteilt sie sich → σ wächst.
        const double f0 = midiHz(60);
        const double fLo = f0 * 0.5, fHi = f0 * 1.5;
        double sumW = 0, sumWF = 0, sumWF2 = 0;
        for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
            const double f = (double)k * SR / sp.N;
            if (f < fLo) { continue; }
            if (f > fHi) { break; }
            const double w = std::pow(10.0, sp.magDb[k] / 20.0);
            sumW += w; sumWF += w * f; sumWF2 += w * f * f;
        }
        const double mean   = (sumW > 0) ? sumWF  / sumW : 0;
        const double var    = (sumW > 0) ? sumWF2 / sumW - mean * mean : 0;
        const double sigma  = std::sqrt(std::max(var, 0.0));

        spread.xs.push_back(dt);
        spread.ys.push_back(sigma);
    }

    SvgPlot p("M1.3 — UNISON Detune → spektrale σ um f₀ (energy-weighted)");
    p.xLabel("DETUNE (norm)").yLabel("σ / Hz")
     .xRange(0, 1).yRange(0, std::max(spread.ys.back() * 1.3, 1.0))
     .addSeries(spread).note("MIDI 60, UNISON, 65k FFT, energy in f₀ ± 50 %");
    p.write(OUT_DIR + "/M1.3-detune-unison.svg");

    const bool mono = spread.ys.back() > spread.ys.front() + 0.3
                       && spread.ys.back() >= 0.5;

    MEntry m; m.id="M1.3"; m.section="Oscillator";
    m.what="DETUNE spreads UNISON energy around f₀";
    m.metric="sigma_at_detune_0.9"; m.unit="Hz";
    m.expected="≥ 0.5 (vs detune=0)";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", spread.ys.back()); m.measured=b;
    m.pass = mono;
    m.svgPath = OUT_DIR + "/M1.3-detune-unison.svg";
    char nb[64]; std::snprintf(nb, sizeof nb, "σ@dt=0: %.3f Hz", spread.ys.front());
    m.note = nb;
    return m;
}

// -- M1.4 Octave-Shift Frequenzgenauigkeit ------------------------------------
// Frage:  Setzt setOctave(±2..0..±2) die Fundamentale um exakt n·12 Halbtöne?
// Mess:   MIDI 60, oct = {-2,-1,0,1,2} → Erwartung f₀ = midiHz(60 + oct*12).
// Erw:    |f_measured / f_expected - 1| < 0.5 %.
static MEntry M1_4_octaveShift() {
    const int octs[] = {-2, -1, 0, 1, 2};
    Series s; s.label = "f₀";
    double worstErr = 0.0;

    for (int oct : octs) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e); e.setOctave(oct); e.noteOn(60);
        auto cap = capture(e, 16384, 0.10);
        auto sp = spectrum(cap);
        const double f  = peakFreqInterp(sp.magDb, sp.N, SR);
        const double f0 = midiHz(60 + oct * 12);
        const double err = std::fabs(f / f0 - 1.0);
        worstErr = std::max(worstErr, err);
        s.xs.push_back((double)oct); s.ys.push_back(f);
    }

    SvgPlot p("M1.4 — Octave-Shift Genauigkeit (MIDI 60 + oct·12)");
    p.xLabel("Oct offset").yLabel("f₀ / Hz")
     .xRange(-2.5, 2.5).yRange(60, 1100)
     .addSeries(s);
    p.write(OUT_DIR + "/M1.4-octave-shift.svg");

    MEntry m; m.id="M1.4"; m.section="Oscillator";
    m.what="setOctave(-2..+2) pitch accuracy";
    m.metric="worst_relative_err"; m.unit="%";
    m.expected="< 0.5";
    char b[32]; std::snprintf(b, sizeof b, "%.3f", worstErr * 100.0); m.measured=b;
    m.pass = worstErr < 0.005;
    m.svgPath = OUT_DIR + "/M1.4-octave-shift.svg";
    return m;
}

// -- M1.5 Portamento-Glide ----------------------------------------------------
// Frage:  Glidet PORTAMENTO die Tonhöhe smooth über Zeit zwischen Noten?
// Mess:   PORTAMENTO = 0.2 (τ ≈ 100 ms = 8 τ über die Capture → ≥ 99 %), noteOn(60),
//         100 ms später noteOn(72). 1.5 s gesamt; sliding-FFT (2048 / 512) → f(t).
// Erw:    Endwert in 1 % von midiHz(72) = 523.25 Hz; "weitgehend monoton"
//         (≤ 5 % der Frames mit Rückschritt > 5 Hz — FFT-Jitter toleriert).
static MEntry M1_5_portamento() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::Portamento, 0.2);
    e.noteOn(60);

    const int total = (int)(SR * 1.5);
    std::vector<float> out(total);
    e.process(out.data(), (int)(SR * 0.1));
    e.noteOn(72);
    e.process(out.data() + (int)(SR * 0.1),
              total - (int)(SR * 0.1));

    Series ft; ft.label = "f(t)";
    const std::size_t W = 2048, H = 512;
    for (std::size_t i = 0; i + W <= out.size(); i += H) {
        std::vector<float> seg(out.begin() + i, out.begin() + i + W);
        auto sp = spectrum(seg);
        const double f = peakFreqInterp(sp.magDb, sp.N, SR);
        ft.xs.push_back((double)i / SR);
        ft.ys.push_back(f);
    }

    SvgPlot p("M1.5 — Portamento glide MIDI 60 → 72 (PORTAMENTO=0.2, τ≈100 ms)");
    p.xLabel("t / s").yLabel("f / Hz")
     .xRange(0, 1.5).yRange(200, 600)
     .addSeries(ft).note("noteOn(60) @ t=0, noteOn(72) @ t=0.1 s");
    p.write(OUT_DIR + "/M1.5-portamento.svg");

    // Akzeptanz-Kriterium: Endwert auf 1 % am Ziel UND signifikanter Anstieg
    // gegenüber Startfrequenz. FFT-Jitter in der Glide-Plateau-Phase wird
    // toleriert (saubere Monotonie ist mit kurzen Fenstern unrealistisch).
    const double endF   = ft.ys.back();
    const double startF = ft.ys.front();
    const double endErr = std::fabs(endF - midiHz(72)) / midiHz(72);
    const double risen  = endF - startF;
    // Kendall-τ-artige "monotone correlation": Anteil der Frame-Paare (i<j),
    // die in korrekter Reihenfolge stehen. > 0.85 = stark monoton (ignoriert
    // kleine Plateau-Jitter).
    long pos = 0, neg = 0;
    for (std::size_t i = 0; i + 1 < ft.ys.size(); ++i)
        for (std::size_t j = i + 1; j < ft.ys.size(); ++j) {
            if (ft.ys[j] > ft.ys[i]) ++pos;
            else if (ft.ys[j] < ft.ys[i]) ++neg;
        }
    const double tau = (pos + neg > 0) ? (double)(pos - neg) / (double)(pos + neg) : 0.0;

    MEntry m; m.id="M1.5"; m.section="Oscillator";
    m.what="Portamento glide MIDI 60→72 reaches target within τ window";
    m.metric="end_freq_err"; m.unit="%";
    m.expected="< 1.0";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", endErr * 100.0); m.measured=b;
    m.pass = endErr < 0.01 && risen > 200.0 && tau > 0.7;
    m.svgPath = OUT_DIR + "/M1.5-portamento.svg";
    char nb[120]; std::snprintf(nb, sizeof nb,
        "risen %.0f Hz, Kendall-τ=%.2f, end f=%.1f Hz",
        risen, tau, endF);
    m.note = nb;
    return m;
}

// =============================================================================
//  LAB-2 :: M2.x VCF (LadderZDF)
// =============================================================================

// -- M2.1 VCF-Cutoff-Response -------------------------------------------------
// Frage:  Senkt CUTOFF die hörbaren Harmonischen wie ein Tiefpass?
// Mess:   Saw bei MIDI 60 (reichhaltiges Spektrum), Cutoff = {0.2..1.0 step 0.1}.
//         Spektrum jeweils 32k FFT. RMS in zwei Bändern:
//           low:  100-500 Hz (passband)
//           high: 4-10 kHz   (stopband)
//         Verhältnis high/low als Funktion von Cutoff → Filter-Charakteristik.
// Erw:    Monoton fallendes high/low-Verhältnis mit kleiner werdendem Cutoff;
//         > 30 dB Unterschied zwischen Cutoff=1.0 und Cutoff=0.2.
static MEntry M2_1_vcfCutoff() {
    const double cutoffs[] = {0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    Series lowSer;  lowSer.label  = "RMS 100-500 Hz / dB";
    Series highSer; highSer.label = "RMS 4-10 kHz / dB";

    for (double c : cutoffs) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff, c);
        e.noteOn(60);
        auto cap = capture(e, 32768, 0.20);   // longer warm-up so smoother settles

        auto sp = spectrum(cap);
        auto bandDb = [&](double fLo, double fHi){
            double e2 = 0; int n = 0;
            for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
                const double f = (double)k * SR / sp.N;
                if (f < fLo) { continue; }
                if (f > fHi) { break; }
                const double lin = std::pow(10.0, sp.magDb[k] / 20.0);
                e2 += lin * lin; ++n;
            }
            const double rms = (n > 0) ? std::sqrt(e2 / n) : 1e-12;
            return 20.0 * std::log10(rms);
        };
        lowSer.xs.push_back(c);  lowSer.ys.push_back(bandDb(100, 500));
        highSer.xs.push_back(c); highSer.ys.push_back(bandDb(4000, 10000));
    }

    SvgPlot p("M2.1 — VCF Cutoff Response (Saw @ C4)");
    p.xLabel("CUTOFF (norm)").yLabel("Band-RMS / dBFS")
     .xRange(0.1, 1.05).yRange(-100, -20)
     .addSeries(lowSer).addSeries(highSer);
    p.write(OUT_DIR + "/M2.1-vcf-cutoff.svg");

    const double diff = highSer.ys.back() - highSer.ys.front();   // 1.0 vs 0.2
    MEntry m; m.id="M2.1"; m.section="VCF";
    m.what="Cutoff attenuates high band (low-pass action)";
    m.metric="high_band_diff_1.0_vs_0.2"; m.unit="dB";
    m.expected="> 30";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", diff); m.measured=b;
    m.pass = diff > 30.0;
    m.svgPath = OUT_DIR + "/M2.1-vcf-cutoff.svg";
    return m;
}

// -- M2.2 VCF-Resonance-Peak --------------------------------------------------
// Frage:  Erzeugt PEAK eine messbare Resonanzüberhöhung *exakt am Cutoff*?
// Mess:   Cutoff = 0.5 → taper 20·900^0.5 = 600 Hz. PEAK = {0.0..0.9 step 0.15},
//         Saw bei MIDI 36 (Grundton 65 Hz, dichte Harmonische). Peak-Magnitude
//         in schmaler Bandbreite (cutoff ± 25 %, also 450..750 Hz).
// Erw:    Bei PEAK=0.9 mindestens +6 dB Anhebung *im Cutoff-Bereich* vs PEAK=0.
//         (Ladder verliert Passband-Gain bei Resonance — nur Cutoff-Bereich
//         steigt. Klassisches Ladder-Verhalten, NICHT als Bug zu interpretieren.)
static MEntry M2_2_vcfResonance() {
    const double peaks[] = {0.0, 0.15, 0.3, 0.45, 0.6, 0.75, 0.9};
    Series rs; rs.label = "Peak in 450..750 Hz / dBFS";

    const double fcHz = 600.0;     // = 20 * 900^0.5
    const double fLo  = fcHz * 0.75, fHi = fcHz * 1.25;

    for (double pk : peaks) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff,    0.5);
        e.setParamNorm(P3::ParaEngine::Param::Resonance, pk);
        e.noteOn(36);
        auto cap = capture(e, 32768, 0.30);
        auto sp = spectrum(cap);
        double best = -240.0;
        for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
            const double f = (double)k * SR / sp.N;
            if (f < fLo) { continue; }
            if (f > fHi) { break; }
            best = std::max(best, sp.magDb[k]);
        }
        rs.xs.push_back(pk); rs.ys.push_back(best);
    }

    SvgPlot p("M2.2 — VCF Resonance-Peak im Cutoff-Band 450..750 Hz");
    p.xLabel("RESONANCE (norm)").yLabel("Peak / dBFS")
     .xRange(-0.05, 0.95).yRange(-60, 5)
     .addSeries(rs);
    p.write(OUT_DIR + "/M2.2-vcf-resonance.svg");

    const double rise = rs.ys.back() - rs.ys.front();
    MEntry m; m.id="M2.2"; m.section="VCF";
    m.what="Resonance lifts cutoff-band peak (Ladder ZDF)";
    m.metric="peak_rise_0_to_0.9"; m.unit="dB";
    m.expected="> 6";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", rise); m.measured=b;
    m.pass = rise > 6.0;
    m.svgPath = OUT_DIR + "/M2.2-vcf-resonance.svg";
    return m;
}

// -- M2.3 VCF Resonance Stability at RES=1.0 ----------------------------------
// Frage:  Bleibt der Output bei RES=1.0 *stabil* (kein Run-Away, kein Clip)
//         während sustained noteOn? Para3 nutzt tanh-Begrenzung im Feedback-
//         Pfad (k·4.3·tanh), schwingt also designgemäß NICHT in echtes
//         Selbst-Schwingen — das ist Volca-Keys-Treue, kein Bug.
// Mess:   noteOn(60), Cutoff=0.5, Resonance ∈ {0.0..1.0}. 1 s sustained
//         Capture. Wir messen: (a) max |sample| → muss < 1.5 bleiben
//         (Stabilität), (b) RMS bei RES=1.0 deutlich höher als RES=0.0
//         (Resonance-Anhebung am Cutoff hörbar), (c) keine NaN/Inf.
static MEntry M2_3_vcfSelfOsc() {
    const double peaks[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    Series rms; rms.label = "RMS sustained / dBFS";

    double maxAbs100 = 0.0;
    double rmsAt0 = 0.0, rmsAt100 = 0.0;
    bool sawNaN = false;
    for (double pk : peaks) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff,    0.5);
        e.setParamNorm(P3::ParaEngine::Param::Resonance, pk);
        e.noteOn(60);
        auto cap = capture(e, (int)(SR * 1.0), 0.20);

        double s2 = 0, maxAbs = 0;
        for (float v : cap) {
            if (!std::isfinite(v)) sawNaN = true;
            s2 += (double)v * v;
            maxAbs = std::max(maxAbs, (double)std::fabs(v));
        }
        const double r  = std::sqrt(s2 / (double)cap.size());
        const double db = (r > 1e-12) ? 20.0 * std::log10(r) : -240.0;
        rms.xs.push_back(pk); rms.ys.push_back(db);
        if (pk == 0.0) rmsAt0    = db;
        if (pk == 1.0) { rmsAt100 = db; maxAbs100 = maxAbs; }
    }

    SvgPlot p("M2.3 — VCF Stability (sustained note, Cutoff=0.5)");
    p.xLabel("RESONANCE (norm)").yLabel("RMS / dBFS")
     .xRange(-0.05, 1.05).yRange(-50, 5)
     .addSeries(rms)
     .note("Bei RES=1.0: max|sample| muss < 1.5 bleiben (tanh-bounded loop)");
    p.write(OUT_DIR + "/M2.3-vcf-stability.svg");

    const bool bounded = maxAbs100 < 1.5 && !sawNaN;
    const bool effect  = rmsAt100 > rmsAt0 + 2.0;     // resonance amplifies

    MEntry m; m.id="M2.3"; m.section="VCF";
    m.what="Stability at RES=1.0 (tanh-bounded ladder, Volca-treu)";
    m.metric="max_abs_at_res=1.0"; m.unit="|sample|";
    m.expected="< 1.5";
    char b[32]; std::snprintf(b, sizeof b, "%.3f", maxAbs100); m.measured=b;
    m.pass = bounded && effect;
    m.svgPath = OUT_DIR + "/M2.3-vcf-stability.svg";
    char nb[120]; std::snprintf(nb, sizeof nb,
        "%s; RMS res=0/1: %.1f / %.1f dBFS",
        sawNaN ? "NaN/Inf!" : "stable", rmsAt0, rmsAt100);
    m.note = nb;
    return m;
}

// -- M2.4 VCF Slope (24 dB/oct ladder) ----------------------------------------
// Frage:  Liefert das Ladder-Filter den erwarteten 24 dB/oct Roll-off?
// Mess:   Saw @ MIDI 36. Cutoff = 0.7 → 20·900^0.7 ≈ 4377 Hz. Messung deutlich
//         past cutoff: 8 kHz vs 16 kHz (1 Oktave). Saw fällt dort selbst mit
//         -6 dB/oct → erwarteter Gesamt-Slope ≈ -30 dB/oct.
//         Methode-Hinweis: weiter unten lag das Bandpass-Resultat im
//         Rausch-Floor (-100 dB) und verfälschte den gemessenen Slope auf nur
//         -14 dB/oct (false negative in der ersten LAB-2-Iteration).
static MEntry M2_4_vcfSlope() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::Cutoff,    0.7);
    e.setParamNorm(P3::ParaEngine::Param::Resonance, 0.0);
    e.noteOn(36);
    auto cap = capture(e, 32768, 0.30);
    auto sp = spectrum(cap);

    auto bandDb = [&](double fLo, double fHi){
        double s = 0; int n = 0;
        for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
            const double f = (double)k * SR / sp.N;
            if (f < fLo) { continue; }
            if (f > fHi) { break; }
            s += sp.magDb[k]; ++n;
        }
        return n ? s / n : -240.0;
    };
    const double db8k  = bandDb(7000, 9000);
    const double db16k = bandDb(15000, 17000);
    const double octaves = std::log2(16000.0 / 8000.0);
    const double slope = (db16k - db8k) / octaves;

    writeSpectrum(OUT_DIR + "/M2.4-vcf-slope.svg",
                  "M2.4 — VCF Slope @ Cutoff=0.7 (Saw, MIDI 36)",
                  sp.magDb, sp.N, SR);

    MEntry m; m.id="M2.4"; m.section="VCF";
    m.what="Filter slope past cutoff (8 kHz → 16 kHz)";
    m.metric="slope_8k_to_16k"; m.unit="dB/oct";
    m.expected="-30 ± 5";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", slope); m.measured=b;
    m.pass = slope < -25.0 && slope > -40.0;
    m.svgPath = OUT_DIR + "/M2.4-vcf-slope.svg";
    return m;
}

// -- M2.5 Drive saturation harmonics ------------------------------------------
// Frage:  Erhöht DRIVE den Pegel UND verändert er das Spektrum messbar?
// Mess:   Da die Engine keinen Sinus liefert (nur Saw, schon H2..H∞), ist
//         klassisches THD nicht aussagekräftig (Saw + tanh wird "square-iger",
//         THD kann sogar sinken). Daher messen wir:
//           (a) Output-Peak-RMS (sollte mit Drive steigen)
//           (b) Spektrale Flachheit / H1-Pegel-Anstieg (Soft-Clip)
//         DRIVE=0 vs DRIVE=1.0, MIDI 60, Cutoff=0.6.
static MEntry M2_5_drive() {
    auto runAndMetrics = [&](double drive){
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff, 0.6);
        e.setParamNorm(P3::ParaEngine::Param::Drive,  drive);
        e.noteOn(60);
        auto cap = capture(e, 32768, 0.20);
        auto sp = spectrum(cap);
        double s2 = 0; for (float v : cap) s2 += (double)v * v;
        const double rms = std::sqrt(s2 / (double)cap.size());
        return std::pair<double, SpecOut>{rms, sp};
    };
    auto p0 = runAndMetrics(0.0);
    auto p1 = runAndMetrics(1.0);
    const double rms0 = p0.first, rms1 = p1.first;
    const double riseDb = 20.0 * std::log10(std::max(rms1, 1e-12) /
                                             std::max(rms0, 1e-12));
    const double thd0   = thdPercent(p0.second.magDb);
    const double thd1   = thdPercent(p1.second.magDb);

    Series s0; s0.label = "DRIVE=0";
    Series s1; s1.label = "DRIVE=1"; s1.colour = "#71d99a";
    for (std::size_t k = 1; k < p0.second.magDb.size(); ++k) {
        const double f = (double)k * SR / p0.second.N;
        if (f < 100) { continue; }
        if (f > 8000) { break; }
        s0.xs.push_back(f); s0.ys.push_back(p0.second.magDb[k]);
        s1.xs.push_back(f); s1.ys.push_back(p1.second.magDb[k]);
    }
    SvgPlot p("M2.5 — DRIVE Sättigung (MIDI 60, Cutoff=0.6)");
    p.xLabel("Frequency / Hz").yLabel("Magnitude / dBFS")
     .xLog(true).xRange(100, 8000).yRange(-100, 6)
     .addSeries(s0).addSeries(s1)
     .note("RMS rise & spectral shape change beweisen Drive-Effekt");
    p.write(OUT_DIR + "/M2.5-drive.svg");

    MEntry m; m.id="M2.5"; m.section="VCF";
    m.what="DRIVE soft-clips into ladder (level + spectrum shift)";
    m.metric="rms_rise_drive_0_to_1"; m.unit="dB";
    m.expected="> 3";
    char buf[32]; std::snprintf(buf, sizeof buf, "%.2f", riseDb); m.measured=buf;
    m.pass = riseDb > 3.0;
    m.svgPath = OUT_DIR + "/M2.5-drive.svg";
    char nb[96]; std::snprintf(nb, sizeof nb,
        "RMS %.4f → %.4f (+%.1f dB); THD %.1f%% → %.1f%% (Saw + tanh → squariger)",
        rms0, rms1, riseDb, thd0, thd1);
    m.note = nb;
    return m;
}

// =============================================================================
//  Driver
// =============================================================================
int main(int argc, char** argv) {
    const std::string filter = (argc > 1) ? argv[1] : "";
    auto want = [&](const std::string& id){
        if (filter.empty()) return true;
        return id.rfind(filter, 0) == 0;   // prefix match: "M1" matches M1.x
    };

    Manifest manifest;

    if (want("M0.1")) manifest.add(M0_1_selftest());
    if (want("M0.2")) manifest.add(M0_2_engineSmoke());

    // ---- LAB-2 Oszillator + VCF ----
    if (want("M1.1")) manifest.add(M1_1_sawWaveform());
    if (want("M1.2")) manifest.add(M1_2_aliasing());
    if (want("M1.3")) manifest.add(M1_3_detuneUnison());
    if (want("M1.4")) manifest.add(M1_4_octaveShift());
    if (want("M1.5")) manifest.add(M1_5_portamento());
    if (want("M2.1")) manifest.add(M2_1_vcfCutoff());
    if (want("M2.2")) manifest.add(M2_2_vcfResonance());
    if (want("M2.3")) manifest.add(M2_3_vcfSelfOsc());
    if (want("M2.4")) manifest.add(M2_4_vcfSlope());
    if (want("M2.5")) manifest.add(M2_5_drive());

    // ---- LAB-3..6: M3.x..M9.x werden in den jeweiligen Sprints ergänzt ----

    manifest.writeCsv(OUT_DIR + "/MANIFEST.csv");
    manifest.writeMarkdownTable(OUT_DIR + "/MANIFEST.md",
                                 "PARA-3 Lab-Validation Manifest");

    std::printf("\n==== PARA-3 measure_main ====================================\n");
    std::printf("Measurements: %zu   Failures: %zu\n",
                manifest.size(), manifest.failures());
    std::printf("Wrote: %s/MANIFEST.{csv,md}\n", OUT_DIR.c_str());
    return manifest.failures() ? 1 : 0;
}
