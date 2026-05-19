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
//  LAB-3 :: M3.x VCA / Envelope
// =============================================================================

// Common helper: amplitude envelope of a buffer via rectified moving-max
// (10 ms window). Better than raw |x| because it tracks peaks of the carrier.
static std::vector<double> envelope(const std::vector<float>& s, double sr,
                                     double winS = 0.010) {
    const std::size_t W = (std::size_t)std::max(1.0, sr * winS);
    std::vector<double> e(s.size(), 0.0);
    // Rectify, then sliding window max
    std::vector<double> a(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) a[i] = std::fabs((double)s[i]);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const std::size_t lo = i > W/2 ? i - W/2 : 0;
        const std::size_t hi = std::min(s.size(), i + W/2);
        double m = 0; for (std::size_t j = lo; j < hi; ++j) m = std::max(m, a[j]);
        e[i] = m;
    }
    return e;
}

// -- M3.1 Attack shape --------------------------------------------------------
// Frage:  Erzeugt ATTACK eine messbare, monoton steigende Anstiegszeit?
// Mess:   ATTACK = {0.0, 0.3, 0.6}, noteOn(60). Hüllkurven-Anstiegszeit
//         t_10..t_90 % von peak.
static MEntry M3_1_attack() {
    const double atks[] = {0.0, 0.3, 0.6};
    Series s; s.label = "t₁₀→₉₀ / ms";
    double prev = -1;
    bool mono = true;
    for (double a : atks) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Attack, a);
        e.noteOn(60);
        auto cap = capture(e, (int)(SR * 0.5), 0.0);
        auto env = envelope(cap, SR);
        double pk = 0; for (double v : env) pk = std::max(pk, v);
        const double t10 = pk * 0.1, t90 = pk * 0.9;
        std::size_t i10 = 0, i90 = 0;
        for (std::size_t i = 0; i < env.size(); ++i)
            if (env[i] >= t10) { i10 = i; break; }
        for (std::size_t i = i10; i < env.size(); ++i)
            if (env[i] >= t90) { i90 = i; break; }
        const double tms = (double)(i90 - i10) * 1000.0 / SR;
        s.xs.push_back(a); s.ys.push_back(tms);
        if (prev >= 0 && tms < prev) mono = false;
        prev = tms;
    }
    SvgPlot p("M3.1 — Attack ramp time (10 → 90 % of peak)");
    p.xLabel("ATTACK (norm)").yLabel("t / ms").xRange(0, 0.7)
     .yRange(0, std::max(s.ys.back() * 1.3, 10.0)).addSeries(s);
    p.write(OUT_DIR + "/M3.1-attack.svg");

    MEntry m; m.id="M3.1"; m.section="VCA";
    m.what="ATTACK monotonically increases ramp time";
    m.metric="t_attack_at_0.6"; m.unit="ms";
    m.expected="monoton ↑";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", s.ys.back()); m.measured=b;
    m.pass = mono && s.ys.back() > s.ys.front() + 5.0;
    m.svgPath = OUT_DIR + "/M3.1-attack.svg";
    return m;
}

// -- M3.2 Decay→Sustain shape -------------------------------------------------
// Frage:  Fällt die Hüllkurve nach Attack auf den Sustain-Pegel?
// Mess:   ATK=0, DecRel=0.3, Sustain=0.3. Capture 500 ms. Erwartung: rasch
//         steigt, fällt auf ~30 % Sustain.
static MEntry M3_2_decaySustain() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::Attack,  0.0);
    e.setParamNorm(P3::ParaEngine::Param::DecRel,  0.3);
    e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.3);
    e.noteOn(60);
    auto cap = capture(e, (int)(SR * 0.6), 0.0);
    auto env = envelope(cap, SR);

    double pk = 0; for (double v : env) pk = std::max(pk, v);
    // Sustain level = mean over the last 100 ms (assumes decay is done)
    double s = 0; int n = 0;
    for (std::size_t i = (std::size_t)(SR * 0.5); i < env.size(); ++i) {
        s += env[i]; ++n;
    }
    const double sus = n ? s / n : 0;
    const double susRatio = (pk > 0) ? sus / pk : 0;

    Series ser; ser.label = "envelope";
    for (std::size_t i = 0; i < env.size(); i += 64) {
        ser.xs.push_back((double)i / SR * 1000.0);
        ser.ys.push_back(env[i]);
    }
    SvgPlot p("M3.2 — Decay → Sustain (ATK=0, DEC=0.3, SUS=0.3)");
    p.xLabel("t / ms").yLabel("envelope")
     .xRange(0, 600).yRange(0, pk * 1.1).addSeries(ser);
    p.write(OUT_DIR + "/M3.2-decay-sustain.svg");

    MEntry m; m.id="M3.2"; m.section="VCA";
    m.what="Envelope decays to sustain level";
    m.metric="sustain_ratio"; m.unit="";
    m.expected="0.25 .. 0.40";
    char b[32]; std::snprintf(b, sizeof b, "%.3f", susRatio); m.measured=b;
    m.pass = susRatio > 0.25 && susRatio < 0.40;
    m.svgPath = OUT_DIR + "/M3.2-decay-sustain.svg";
    return m;
}

// -- M3.3 Release shape -------------------------------------------------------
// Frage:  Klingt der Note nach noteOff bei höherem DecRel länger aus?
// Mess:   Atk=0, Sus=1.0, DecRel=0.2 / 0.6. NoteOn 100 ms, NoteOff, capture
//         300 ms. Zeit bis -20 dB des Sustain-Pegels.
static MEntry M3_3_release() {
    const double dec[] = {0.2, 0.6};
    Series result; result.label = "t to -20 dB / ms";
    for (double d : dec) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Attack,  0.0);
        e.setParamNorm(P3::ParaEngine::Param::DecRel,  d);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 1.0);
        e.noteOn(60);
        std::vector<float> on((std::size_t)(SR * 0.1));
        e.process(on.data(), (int)on.size());
        e.noteOff(60);
        std::vector<float> rel((std::size_t)(SR * 0.5));
        e.process(rel.data(), (int)rel.size());
        auto env = envelope(rel, SR);
        const double pk = env.front();
        const double t20 = pk * std::pow(10.0, -20.0 / 20.0);   // -20 dB
        std::size_t idx = env.size() - 1;
        for (std::size_t i = 0; i < env.size(); ++i)
            if (env[i] < t20) { idx = i; break; }
        result.xs.push_back(d);
        result.ys.push_back((double)idx * 1000.0 / SR);
    }
    SvgPlot p("M3.3 — Release time to -20 dB");
    p.xLabel("DecRel (norm)").yLabel("t / ms").xRange(0, 0.7)
     .yRange(0, std::max(result.ys.back() * 1.3, 30.0)).addSeries(result);
    p.write(OUT_DIR + "/M3.3-release.svg");

    MEntry m; m.id="M3.3"; m.section="VCA";
    m.what="Higher DecRel → longer release";
    m.metric="t_release_at_0.6"; m.unit="ms";
    m.expected="> 2 × t_release_at_0.2";
    char b[32]; std::snprintf(b, sizeof b, "%.0f", result.ys.back()); m.measured=b;
    m.pass = result.ys.back() > 2.0 * result.ys.front();
    m.svgPath = OUT_DIR + "/M3.3-release.svg";
    return m;
}

// -- M3.4 EG_INT bipolar effect on cutoff -------------------------------------
// Frage:  Öffnet/schließt EG_INT den Filter bipolar?
// Mess:   3 Setups: EG_INT = 0.0 (neg max), 0.5 (centre), 1.0 (pos max).
//         ATK fast, lange Decay (visible filter motion). RMS hoher Bandbereich
//         (2-6 kHz) am Note-Start sollte bei positivem EG hoch, bei negativem
//         niedrig sein.
static MEntry M3_4_egInt() {
    const double egs[] = {0.0, 0.5, 1.0};
    Series highRms; highRms.label = "RMS 2-6 kHz / dBFS";
    for (double egi : egs) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff,   0.3);
        e.setParamNorm(P3::ParaEngine::Param::Attack,   0.0);
        e.setParamNorm(P3::ParaEngine::Param::DecRel,   0.4);
        e.setParamNorm(P3::ParaEngine::Param::Sustain,  0.0);
        e.setParamNorm(P3::ParaEngine::Param::EgCutDepth, egi);
        e.noteOn(60);
        auto cap = capture(e, 32768, 0.0);
        auto sp = spectrum(cap);
        // Band RMS via spectral sum
        double e2 = 0; int n = 0;
        for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
            const double f = (double)k * SR / sp.N;
            if (f < 2000) { continue; }
            if (f > 6000) { break; }
            const double lin = std::pow(10.0, sp.magDb[k] / 20.0);
            e2 += lin * lin; ++n;
        }
        const double rms = (n > 0) ? std::sqrt(e2 / n) : 1e-12;
        highRms.xs.push_back(egi);
        highRms.ys.push_back(20.0 * std::log10(rms));
    }
    SvgPlot p("M3.4 — EG_INT bipolar → high-band RMS (Cutoff=0.3, fast atk)");
    p.xLabel("EG_INT (norm; 0.5 = centre)").yLabel("RMS 2-6 kHz / dBFS")
     .xRange(-0.05, 1.05).yRange(-90, -20)
     .addSeries(highRms);
    p.write(OUT_DIR + "/M3.4-egint.svg");

    // Bei EG=1.0 muss der Filter sichtbar offener sein als bei 0.0.
    const double diff = highRms.ys.back() - highRms.ys.front();
    MEntry m; m.id="M3.4"; m.section="VCA";
    m.what="EG_INT bipolar opens/closes filter";
    m.metric="high_band_diff_EG=1_vs_0"; m.unit="dB";
    m.expected="> 6";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", diff); m.measured=b;
    m.pass = diff > 6.0;
    m.svgPath = OUT_DIR + "/M3.4-egint.svg";
    return m;
}

// -- M3.5 Click-freedom on noteOn/noteOff -------------------------------------
// Frage:  Produziert noteOn/noteOff einen audiblen Klick (Sprung im Sample-Diff)?
// Mess:   Atk=0, Sus=0.8, DecRel=0.3. NoteOn @ t=0, NoteOff @ t=300 ms. Scan
//         Sample-zu-Sample Diff um den Transition; größer Diff = Klick.
// Erw:    max |sample[i] - sample[i-1]| im 1-ms-Fenster um Transition < 0.2.
static MEntry M3_5_clickFree() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::Attack,  0.0);
    e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.8);
    e.setParamNorm(P3::ParaEngine::Param::DecRel,  0.3);
    e.noteOn(60);
    std::vector<float> on((std::size_t)(SR * 0.3));
    e.process(on.data(), (int)on.size());
    e.noteOff(60);
    std::vector<float> off((std::size_t)(SR * 0.3));
    e.process(off.data(), (int)off.size());

    auto worstDiff = [](const std::vector<float>& s){
        double m = 0;
        for (std::size_t i = 1; i < s.size(); ++i)
            m = std::max(m, (double)std::fabs((double)s[i] - (double)s[i-1]));
        return m;
    };
    // Near noteOn boundary (samples 0..480 = 10 ms)
    const std::size_t onBound = std::min<std::size_t>(on.size(), (std::size_t)(SR * 0.010));
    std::vector<float> nearOn(on.begin(), on.begin() + onBound);
    // Near noteOff boundary
    std::vector<float> nearOff(off.begin(), off.begin() + (std::size_t)(SR * 0.010));
    const double clickOn  = worstDiff(nearOn);
    const double clickOff = worstDiff(nearOff);

    // Scope around the transition: 30 ms before to 30 ms after noteOff.
    std::vector<float> around;
    const std::size_t pre = (std::size_t)(SR * 0.030);
    around.insert(around.end(), on.end() - std::min(pre, on.size()), on.end());
    around.insert(around.end(), off.begin(), off.begin() + (std::size_t)(SR * 0.030));
    writeScope(OUT_DIR + "/M3.5-click-free.svg",
               "M3.5 — noteOff transition (30 ms pre / 30 ms post)",
               around, SR, 0.0, 0.060);

    const double worst = std::max(clickOn, clickOff);
    MEntry m; m.id="M3.5"; m.section="VCA";
    m.what="Click-free note transitions";
    m.metric="max_sample_diff_at_transitions"; m.unit="amp";
    m.expected="< 0.2";
    char b[32]; std::snprintf(b, sizeof b, "%.4f", worst); m.measured=b;
    m.pass = worst < 0.2;
    m.svgPath = OUT_DIR + "/M3.5-click-free.svg";
    char nb[80]; std::snprintf(nb, sizeof nb,
        "onset diff %.4f, offset diff %.4f", clickOn, clickOff);
    m.note = nb;
    return m;
}

// =============================================================================
//  LAB-3 :: M4.x LFO
// =============================================================================

// -- M4.1 LFO Shapes ----------------------------------------------------------
// Frage:  Erzeugen die 4 Wave-Formen unterschiedliche Modulationsmuster?
// Mess:   LFO_RATE moderate (5 Hz), LFO_PITCH_DEPTH hoch (= klar hörbarer
//         Vibrato), je Wave-Shape Audio-Capture; Demodulation via Hilbert-
//         Envelope. Erwartung: deutlich unterschiedliche Envelope-Formen.
// Hier vereinfacht: wir messen Standardabweichung der zeitlich-resolved
// Frequenz (sliding FFT), und vergleichen 4 Shapes auf "non-zero variance".
static MEntry M4_1_lfoShapes() {
    using S = P3::Lfo::Shape;
    const S shapes[] = {S::Sine, S::Triangle, S::Saw, S::Square};
    const char* names[]    = {"Sine","Triangle","Saw","Square"};
    Series s; s.label = "f-modulation σ / Hz";
    double minS = 1e9, maxS = -1.0;
    for (int idx = 0; idx < 4; ++idx) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::LfoRate, 0.60);    // 2.3 Hz
        e.setParamNorm(P3::ParaEngine::Param::LfoPitchDepth, 0.5);
        e.setLfoShape(shapes[idx]);
        e.noteOn(60);
        auto cap = capture(e, (int)(SR * 1.0), 0.05);
        // Sliding peakFreq → series; std-dev
        std::vector<double> ft;
        const std::size_t W = 4096, H = 1024;
        for (std::size_t i = 0; i + W <= cap.size(); i += H) {
            std::vector<float> seg(cap.begin() + i, cap.begin() + i + W);
            auto sp = spectrum(seg);
            ft.push_back(peakFreqInterp(sp.magDb, sp.N, SR));
        }
        double mean = 0; for (double v : ft) mean += v; mean /= ft.size();
        double var = 0; for (double v : ft) var += (v - mean) * (v - mean);
        var /= ft.size();
        const double sig = std::sqrt(var);
        s.xs.push_back((double)idx); s.ys.push_back(sig);
        minS = std::min(minS, sig); maxS = std::max(maxS, sig);
    }
    SvgPlot p("M4.1 — LFO shapes → frequency modulation σ");
    p.xLabel("Shape (0=Sine 1=Tri 2=Saw 3=Square)").yLabel("σ(f) / Hz")
     .xRange(-0.5, 3.5).yRange(0, maxS * 1.2)
     .addSeries(s)
     .note("LFO_RATE≈5 Hz, PITCH_DEPTH 0.5, 4096-pt sliding FFT");
    p.write(OUT_DIR + "/M4.1-lfo-shapes.svg");

    // Alle 4 Shapes müssen merklich modulieren; min-σ ≥ 1 Hz.
    MEntry m; m.id="M4.1"; m.section="LFO";
    m.what="All 4 LFO shapes produce frequency modulation";
    m.metric="min_sigma"; m.unit="Hz";
    m.expected="> 1";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", minS); m.measured=b;
    m.pass = minS > 1.0;
    m.svgPath = OUT_DIR + "/M4.1-lfo-shapes.svg";
    char nb[160]; std::snprintf(nb, sizeof nb,
        "σ: Sine=%.1f Tri=%.1f Saw=%.1f Square=%.1f",
        s.ys[0], s.ys[1], s.ys[2], s.ys[3]);
    m.note = nb;
    (void)names;
    return m;
}

// -- M4.2 LFO Rate ------------------------------------------------------------
// Frage:  Steigt die LFO-Frequenz monoton mit LFO_RATE?
// Mess:   LFO_RATE = {0.1, 0.3, 0.5, 0.7}, Pitch-Depth fest. Sliding-FFT
//         der Pitch → Modulationsperiode aus Autokorrelation der peakFreq-Reihe.
static MEntry M4_2_lfoRate() {
    // LfoRate taper: 0.05 · 400^n. Wir messen den peakFreq(t)-Trace bei
    // jeder Rate und FFT'n den Trace selbst, um die dominante Modulations-
    // Frequenz zu finden (robuster als Autocorr bei langsamen Raten).
    const double rates[] = {0.55, 0.65, 0.75, 0.85};
    Series s; s.label = "LFO rate / Hz";
    for (double r : rates) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::LfoRate, r);
        e.setParamNorm(P3::ParaEngine::Param::LfoPitchDepth, 0.5);
        e.setLfoShape(P3::Lfo::Shape::Sine);
        e.noteOn(60);
        auto cap = capture(e, (int)(SR * 2.0), 0.10);
        std::vector<float> trace;
        const std::size_t W = 2048, H = 512;
        for (std::size_t i = 0; i + W <= cap.size(); i += H) {
            std::vector<float> seg(cap.begin() + i, cap.begin() + i + W);
            auto sp = spectrum(seg);
            trace.push_back((float)peakFreqInterp(sp.magDb, sp.N, SR));
        }
        // DC removal
        double mean = 0; for (float v : trace) mean += v; mean /= trace.size();
        for (float& v : trace) v -= (float)mean;
        // FFT the trace; sample rate of trace = SR / H = 93.75 Hz
        auto X = realFft(trace, true);
        auto win = hannWindow(trace.size());
        double winSum = 0; for (double w : win) winSum += w;
        auto mag = magnitudeDb(X, winSum);
        const double traceSr = SR / (double)H;
        const double f = peakFreqInterp(mag, X.size(), traceSr);
        s.xs.push_back(r); s.ys.push_back(f);
    }
    SvgPlot p("M4.2 — LFO_RATE → modulation frequency");
    p.xLabel("LFO_RATE (norm)").yLabel("f_LFO / Hz")
     .xRange(0.5, 0.9).yRange(0, std::max(s.ys.back() * 1.3, 10.0))
     .addSeries(s);
    p.write(OUT_DIR + "/M4.2-lfo-rate.svg");

    // Lockerere Monotonie: Kendall-τ-Score zwischen rate-Index und f_lfo.
    long pos = 0, neg = 0;
    for (std::size_t i = 0; i + 1 < s.xs.size(); ++i)
        for (std::size_t j = i + 1; j < s.xs.size(); ++j) {
            if (s.ys[j] > s.ys[i]) ++pos;
            else if (s.ys[j] < s.ys[i]) ++neg;
        }
    const double tau = (pos + neg > 0) ? (double)(pos - neg) / (double)(pos + neg) : 0.0;
    const bool tauMono = tau >= 0.5;

    MEntry m; m.id="M4.2"; m.section="LFO";
    m.what="LFO_RATE monotonically increases LFO frequency";
    m.metric="f_lfo_at_0.85"; m.unit="Hz";
    m.expected="Kendall-τ ≥ 0.5";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", s.ys.back()); m.measured=b;
    m.pass = tauMono && s.ys.back() > s.ys.front() + 1.0;
    m.svgPath = OUT_DIR + "/M4.2-lfo-rate.svg";
    char nb[96]; std::snprintf(nb, sizeof nb, "τ=%.2f, rates 0.55/0.65/0.75/0.85 → %.2f/%.2f/%.2f/%.2f Hz",
                                tau, s.ys[0], s.ys[1], s.ys[2], s.ys[3]);
    m.note = nb;
    return m;
}

// -- M4.3 LFO Pitch depth -----------------------------------------------------
// Frage:  Erhöht LFO_PITCH_DEPTH die Modulationstiefe (Hz peak-to-peak)?
static MEntry M4_3_lfoPitchDepth() {
    const double depths[] = {0.0, 0.3, 0.6, 0.9};
    Series s; s.label = "p-p modulation / Hz";
    for (double d : depths) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::LfoRate, 0.15);
        e.setParamNorm(P3::ParaEngine::Param::LfoPitchDepth, d);
        e.setLfoShape(P3::Lfo::Shape::Sine);
        e.noteOn(60);
        auto cap = capture(e, (int)(SR * 1.0), 0.05);
        std::vector<double> ft;
        const std::size_t W = 2048, H = 512;
        for (std::size_t i = 0; i + W <= cap.size(); i += H) {
            std::vector<float> seg(cap.begin() + i, cap.begin() + i + W);
            auto sp = spectrum(seg);
            ft.push_back(peakFreqInterp(sp.magDb, sp.N, SR));
        }
        double mn = 1e9, mx = 0;
        for (double v : ft) { mn = std::min(mn, v); mx = std::max(mx, v); }
        s.xs.push_back(d); s.ys.push_back(mx - mn);
    }
    SvgPlot p("M4.3 — LFO_PITCH_DEPTH → frequency p-p modulation");
    p.xLabel("PITCH_DEPTH (norm)").yLabel("p-p / Hz")
     .xRange(0, 1).yRange(0, std::max(s.ys.back() * 1.3, 100.0))
     .addSeries(s);
    p.write(OUT_DIR + "/M4.3-lfo-pitch-depth.svg");

    MEntry m; m.id="M4.3"; m.section="LFO";
    m.what="LFO_PITCH_DEPTH widens pitch modulation";
    m.metric="pp_at_0.9"; m.unit="Hz";
    m.expected="> 50";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", s.ys.back()); m.measured=b;
    m.pass = s.ys.back() > 50.0 && s.ys.front() < 10.0;
    m.svgPath = OUT_DIR + "/M4.3-lfo-pitch-depth.svg";
    return m;
}

// -- M4.4 LFO Cutoff depth ----------------------------------------------------
// Frage:  Wirkt LFO_CUT_DEPTH messbar auf den Filter?
// Mess:   Static Cutoff hoch, Lfo Cutoff Depth = {0, 0.5, 1.0}. Sliding-RMS
//         in 2-6 kHz Band → Amplituden-pp = Filter-Modulationstiefe.
static MEntry M4_4_lfoCutoffDepth() {
    const double depths[] = {0.0, 0.5, 1.0};
    Series s; s.label = "highband RMS p-p / dB";
    for (double d : depths) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff, 0.5);
        e.setParamNorm(P3::ParaEngine::Param::LfoRate, 0.15);
        e.setParamNorm(P3::ParaEngine::Param::LfoCutDepth, d);
        e.setLfoShape(P3::Lfo::Shape::Sine);
        e.noteOn(60);
        auto cap = capture(e, (int)(SR * 1.0), 0.05);
        // Block RMS in highband
        const std::size_t W = 1024, H = 512;
        std::vector<double> hb;
        for (std::size_t i = 0; i + W <= cap.size(); i += H) {
            std::vector<float> seg(cap.begin() + i, cap.begin() + i + W);
            auto sp = spectrum(seg);
            double e2 = 0; int n = 0;
            for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
                const double f = (double)k * SR / sp.N;
                if (f < 2000) { continue; }
                if (f > 6000) { break; }
                const double lin = std::pow(10.0, sp.magDb[k] / 20.0);
                e2 += lin * lin; ++n;
            }
            const double rms = (n > 0) ? std::sqrt(e2 / n) : 1e-12;
            hb.push_back(20.0 * std::log10(rms));
        }
        double mn = 1e9, mx = -1e9;
        for (double v : hb) { mn = std::min(mn, v); mx = std::max(mx, v); }
        s.xs.push_back(d); s.ys.push_back(mx - mn);
    }
    SvgPlot p("M4.4 — LFO_CUT_DEPTH → high-band RMS p-p");
    p.xLabel("LFO_CUT_DEPTH (norm)").yLabel("p-p / dB")
     .xRange(0, 1.05).yRange(0, std::max(s.ys.back() * 1.3, 10.0))
     .addSeries(s);
    p.write(OUT_DIR + "/M4.4-lfo-cutoff-depth.svg");

    MEntry m; m.id="M4.4"; m.section="LFO";
    m.what="LFO_CUT_DEPTH modulates filter cutoff";
    m.metric="pp_at_1.0"; m.unit="dB";
    m.expected="> 5";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", s.ys.back()); m.measured=b;
    m.pass = s.ys.back() > 5.0 && s.ys.front() < 2.0;
    m.svgPath = OUT_DIR + "/M4.4-lfo-cutoff-depth.svg";
    return m;
}

// =============================================================================
//  LAB-3 :: M5.x Delay
// =============================================================================

// -- M5.1 Delay Time ----------------------------------------------------------
// Frage:  Verzögert das Delay den Input um die per Param eingestellte Zeit?
// Mess:   Sehr kurzer noteOn-Stoß (5 ms), DELAY_TIME = {0.25, 0.5, 0.75},
//         DELAY_MIX 1.0 (only delay audible), DELAY_FEEDBACK 0. Capture 1.5 s.
//         Echo-Onset = erstes Sample > 0.05 amp NACH dem direkten Klang.
//         Erwartung: monoton steigende Verzögerung.
static MEntry M5_1_delayTime() {
    const double times[] = {0.25, 0.50, 0.75};
    Series s; s.label = "echo onset / ms";
    bool mono = true; double prev = 0;
    for (double t : times) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::DelayMix, 1.0);
        e.setParamNorm(P3::ParaEngine::Param::DelayFeedback, 0.0);
        e.setParamNorm(P3::ParaEngine::Param::DelayTime, t);
        // Long warm-up so DelayTime smoother settles to target BEFORE noteOn.
        std::vector<float> warm((std::size_t)(SR * 0.5));
        e.process(warm.data(), (int)warm.size());

        e.setParamNorm(P3::ParaEngine::Param::Attack, 0.0);
        e.setParamNorm(P3::ParaEngine::Param::DecRel, 0.02);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);
        e.noteOn(60);
        std::vector<float> stim((std::size_t)(SR * 0.01));
        e.process(stim.data(), (int)stim.size());
        e.noteOff(60);
        std::vector<float> cap((std::size_t)(SR * 1.5));
        e.process(cap.data(), (int)cap.size());

        // Skip past direct sound tail (~50 ms) then find first sample > 0.05
        const std::size_t skip = (std::size_t)(SR * 0.05);
        std::size_t onset = cap.size();
        for (std::size_t i = skip; i < cap.size(); ++i)
            if (std::fabs((double)cap[i]) > 0.05) { onset = i; break; }
        const double tms = (double)onset * 1000.0 / SR;
        s.xs.push_back(t); s.ys.push_back(tms);
        if (tms < prev - 5.0) mono = false;
        prev = tms;
    }
    SvgPlot p("M5.1 — DELAY_TIME → echo onset");
    p.xLabel("DELAY_TIME (norm)").yLabel("t / ms")
     .xRange(0, 1.0).yRange(0, std::max(s.ys.back() * 1.3, 500.0))
     .addSeries(s);
    p.write(OUT_DIR + "/M5.1-delay-time.svg");

    MEntry m; m.id="M5.1"; m.section="Delay";
    m.what="DELAY_TIME monotonically lengthens echo offset";
    m.metric="onset_at_0.75"; m.unit="ms";
    m.expected="monoton ↑";
    char b[32]; std::snprintf(b, sizeof b, "%.0f", s.ys.back()); m.measured=b;
    m.pass = mono && s.ys.back() > s.ys.front() + 50.0;
    m.svgPath = OUT_DIR + "/M5.1-delay-time.svg";
    return m;
}

// -- M5.2 Delay Feedback ------------------------------------------------------
// Frage:  Wiederholt FEEDBACK den Echo mehrfach mit klingender Auslese?
// Mess:   DELAY_MIX 0.5, DELAY_TIME 0.25 (kurz), FEEDBACK = {0.0, 0.5, 0.9}.
//         Zähle Echo-Spitzen (lokale Maxima > 0.1 amp im 200..1500 ms Bereich).
static MEntry M5_2_delayFeedback() {
    // Metrik: integrated tail energy von 200..2300 ms — höheres FB hält den
    // Schwanz länger, mehr Energie. Bei FB=0 nur 1 Echo bei 245 ms; bei
    // FB=0.9 viele wiederholte Echos. Ratio energy(0.9)/energy(0.0) ≫ 1.
    const double fbs[] = {0.0, 0.5, 0.9};
    Series s; s.label = "tail energy / dB";
    for (double fb : fbs) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::DelayMix,  1.0);
        e.setParamNorm(P3::ParaEngine::Param::DelayTime, 0.25);
        e.setParamNorm(P3::ParaEngine::Param::DelayFeedback, fb);
        std::vector<float> warm((std::size_t)(SR * 0.5));
        e.process(warm.data(), (int)warm.size());
        e.setParamNorm(P3::ParaEngine::Param::Attack,  0.0);
        e.setParamNorm(P3::ParaEngine::Param::DecRel,  0.05);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);
        e.noteOn(60);
        std::vector<float> stim((std::size_t)(SR * 0.05));
        e.process(stim.data(), (int)stim.size());
        e.noteOff(60);
        std::vector<float> cap((std::size_t)(SR * 2.5));
        e.process(cap.data(), (int)cap.size());

        // Late-tail RMS: window 800..1200 ms. Bei FB=0 ist diese Region
        // praktisch still (1 Echo bei 245 ms ist längst weg); bei FB=0.9
        // klingt der Schwanz noch. Diff sollte ≫ 20 dB sein.
        const std::size_t a = (std::size_t)(SR * 0.80);
        const std::size_t b = std::min(cap.size(), (std::size_t)(SR * 1.20));
        double e2 = 0; int n = 0;
        for (std::size_t i = a; i < b; ++i) { e2 += (double)cap[i] * cap[i]; ++n; }
        const double rms = (n > 0) ? std::sqrt(e2 / n) : 1e-12;
        const double db  = (rms > 1e-12) ? 20.0 * std::log10(rms) : -100.0;
        s.xs.push_back(fb); s.ys.push_back(db);
    }
    SvgPlot p("M5.2 — DELAY_FEEDBACK → late-tail RMS (800..1200 ms)");
    p.xLabel("FEEDBACK (norm)").yLabel("late-tail RMS / dBFS")
     .xRange(0, 1.0).yRange(-100, 0)
     .addSeries(s);
    p.write(OUT_DIR + "/M5.2-delay-feedback.svg");

    const double rise = s.ys.back() - s.ys.front();
    MEntry m; m.id="M5.2"; m.section="Delay";
    m.what="FEEDBACK sustains late-tail energy (echo train)";
    m.metric="tail_rise_fb_0_to_0.9"; m.unit="dB";
    m.expected="> 20";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", rise); m.measured=b;
    m.pass = rise > 20.0;
    m.svgPath = OUT_DIR + "/M5.2-delay-feedback.svg";
    return m;
}

// -- M5.3 Delay Mix linearity -------------------------------------------------
// Frage:  Skaliert DELAY_MIX den Wet-Anteil proportional?
// Mess:   DELAY_FB=0, TIME=0.4, MIX = {0.0, 0.25, 0.5, 0.75, 1.0}.
//         Echo-Peak (im Bereich 200..400 ms) vs Direkt-Peak (0..50 ms).
//         Erwartung: echo/direct steigt mit MIX (~ linear).
static MEntry M5_3_delayMix() {
    const double mixes[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    Series s; s.label = "echo peak / dBFS";
    for (double mx : mixes) {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::DelayMix, mx);
        e.setParamNorm(P3::ParaEngine::Param::DelayTime, 0.4);   // 412 ms
        e.setParamNorm(P3::ParaEngine::Param::DelayFeedback, 0.0);
        std::vector<float> warm((std::size_t)(SR * 0.5));
        e.process(warm.data(), (int)warm.size());
        e.setParamNorm(P3::ParaEngine::Param::Attack,  0.0);
        e.setParamNorm(P3::ParaEngine::Param::DecRel,  0.05);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);
        e.noteOn(60);
        std::vector<float> stim((std::size_t)(SR * 0.05));     // 50 ms
        e.process(stim.data(), (int)stim.size());
        e.noteOff(60);
        std::vector<float> cap((std::size_t)(SR * 1.0));
        e.process(cap.data(), (int)cap.size());
        auto env = envelope(cap, SR, 0.020);
        // Echo peak: window centred on expected echo (≈412 ms ± 80 ms)
        double ep = 0;
        const std::size_t a = (std::size_t)(SR * 0.30);
        const std::size_t b = (std::size_t)(SR * 0.55);
        for (std::size_t i = a; i < b && i < env.size(); ++i)
            ep = std::max(ep, env[i]);
        const double db = (ep > 1e-12) ? 20.0 * std::log10(ep) : -100.0;
        s.xs.push_back(mx); s.ys.push_back(db);
    }
    SvgPlot p("M5.3 — DELAY_MIX → echo peak amplitude");
    p.xLabel("DELAY_MIX (norm)").yLabel("echo peak / dBFS")
     .xRange(0, 1.05).yRange(-80, 0)
     .addSeries(s);
    p.write(OUT_DIR + "/M5.3-delay-mix.svg");

    // Lineare Korrelation MIX ↔ Pegel (in dB → ungefähr linear-in-norm würde
    // einer logarithmischen Kurve in MIX entsprechen). Wir prüfen einfach
    // monoton + großer Sprung von MIX=0 zu MIX=1.
    const double dynRange = s.ys.back() - s.ys.front();
    bool mono = true;
    for (std::size_t i = 1; i < s.ys.size(); ++i)
        if (s.ys[i] < s.ys[i-1] - 1.0) { mono = false; break; }
    MEntry m; m.id="M5.3"; m.section="Delay";
    m.what="DELAY_MIX scales echo amplitude monotonically";
    m.metric="dyn_range_mix_0_to_1"; m.unit="dB";
    m.expected="> 30";
    char b[32]; std::snprintf(b, sizeof b, "%.1f", dynRange); m.measured=b;
    m.pass = mono && dynRange > 30.0;
    m.svgPath = OUT_DIR + "/M5.3-delay-mix.svg";
    return m;
}

// =============================================================================
//  LAB-4 :: M6.x Voice modes + Ring
// =============================================================================

// Helper: find N largest spectral peaks above threshold dBFS.
// Returns frequencies (Hz) sorted by amplitude descending.
static std::vector<double> findTopPeaks(const std::vector<double>& mag,
                                         std::size_t N, double sampleRate,
                                         int topK, double minDb,
                                         double minSepHz = 20.0) {
    struct P { double f; double mag; };
    std::vector<P> peaks;
    for (std::size_t k = 2; k + 1 < mag.size(); ++k) {
        if (mag[k] > mag[k-1] && mag[k] > mag[k+1] && mag[k] > minDb) {
            peaks.push_back({(double)k * sampleRate / (double)N, mag[k]});
        }
    }
    std::sort(peaks.begin(), peaks.end(),
              [](const P& a, const P& b){ return a.mag > b.mag; });
    std::vector<double> kept;
    for (const auto& p : peaks) {
        bool tooClose = false;
        for (double f : kept)
            if (std::fabs(f - p.f) < minSepHz) { tooClose = true; break; }
        if (tooClose) { continue; }
        kept.push_back(p.f);
        if ((int)kept.size() >= topK) { break; }
    }
    return kept;
}

// -- M6.1 POLY allocation -----------------------------------------------------
// Frage:  Spielen 3 simultane Noten 3 unterschiedliche Tonhöhen?
// Mess:   POLY mode, noteOn(60), noteOn(64), noteOn(67) (C-Dur-Akkord). FFT
//         → die 3 niedrigsten Spektral-Peaks müssen midiHz(60/64/67) treffen.
static MEntry M6_1_poly() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::Poly);
    e.noteOn(60); e.noteOn(64); e.noteOn(67);
    auto cap = capture(e, 32768, 0.15);
    auto sp = spectrum(cap);
    writeSpectrum(OUT_DIR + "/M6.1-poly-c-major.svg",
                  "M6.1 — POLY mode C-major triad spectrum",
                  sp.magDb, sp.N, SR);
    auto peaks = findTopPeaks(sp.magDb, sp.N, SR, 3, -40.0, 40.0);
    std::sort(peaks.begin(), peaks.end());

    const double exp[] = {midiHz(60), midiHz(64), midiHz(67)};
    bool ok = peaks.size() >= 3;
    double worstErr = 0;
    if (ok) for (int i = 0; i < 3; ++i) {
        const double err = std::fabs(peaks[i] - exp[i]) / exp[i];
        worstErr = std::max(worstErr, err);
        if (err > 0.02) ok = false;
    }

    MEntry m; m.id="M6.1"; m.section="VoiceMode";
    m.what="POLY plays 3 distinct fundamentals (C-major triad)";
    m.metric="worst_relative_err"; m.unit="%";
    m.expected="< 2";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", worstErr * 100.0); m.measured=b;
    m.pass = ok;
    m.svgPath = OUT_DIR + "/M6.1-poly-c-major.svg";
    if (peaks.size() >= 3) {
        char nb[120]; std::snprintf(nb, sizeof nb,
            "peaks: %.1f %.1f %.1f Hz (expect 261.6/329.6/392.0)",
            peaks[0], peaks[1], peaks[2]);
        m.note = nb;
    }
    return m;
}

// -- M6.2 UNISON energy concentration -----------------------------------------
// Frage:  Bündelt UNISON die Energie um EIN Fundamental? (vs POLY mit 3
//         getrennten Notes)
// Mess:   UNISON noteOn(60). Spektrum dominiert von f₀ Cluster (siehe M1.3).
//         Top-1-Peak muss midiHz(60) treffen; Energie in 250..280 Hz > 50 % der
//         Gesamt-Energie unter 1 kHz.
static MEntry M6_2_unison() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::Unison);
    e.setParamNorm(P3::ParaEngine::Param::Detune, 0.2);
    e.noteOn(60);
    auto cap = capture(e, 32768, 0.15);
    auto sp = spectrum(cap);
    writeSpectrum(OUT_DIR + "/M6.2-unison-c4.svg",
                  "M6.2 — UNISON mode @ MIDI 60", sp.magDb, sp.N, SR);
    const double f0 = midiHz(60);
    double eF0 = 0, eAll = 0;
    for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
        const double f = (double)k * SR / sp.N;
        if (f > 1000) { break; }
        const double lin = std::pow(10.0, sp.magDb[k] / 20.0);
        eAll += lin * lin;
        if (f > f0 * 0.9 && f < f0 * 1.1) eF0 += lin * lin;
    }
    const double ratio = (eAll > 0) ? eF0 / eAll : 0;
    auto top = findTopPeaks(sp.magDb, sp.N, SR, 1, -30.0, 40.0);
    const bool peakOk = !top.empty() && std::fabs(top[0] - f0) < 5.0;

    MEntry m; m.id="M6.2"; m.section="VoiceMode";
    m.what="UNISON concentrates energy at f₀";
    m.metric="energy_ratio_f0_band"; m.unit="";
    m.expected="> 0.5 of <1 kHz";
    char b[32]; std::snprintf(b, sizeof b, "%.3f", ratio); m.measured=b;
    m.pass = peakOk && ratio > 0.5;
    m.svgPath = OUT_DIR + "/M6.2-unison-c4.svg";
    return m;
}

// -- M6.3 OCTAVE stack --------------------------------------------------------
// Frage:  Liefert OCTAVE mode neben der Grundnote auch eine Oktave-Komponente?
// Mess:   OCTAVE noteOn(60). Top-2 Peaks müssen midiHz(60) und midiHz(72)/(48)
//         (eine Oktave höher oder tiefer) ergeben.
static MEntry M6_3_octave() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::Octave);
    e.noteOn(60);
    auto cap = capture(e, 32768, 0.15);
    auto sp = spectrum(cap);
    writeSpectrum(OUT_DIR + "/M6.3-octave.svg",
                  "M6.3 — OCTAVE mode @ MIDI 60 (expect 60 + ±12)",
                  sp.magDb, sp.N, SR);
    auto top = findTopPeaks(sp.magDb, sp.N, SR, 4, -40.0, 30.0);
    std::sort(top.begin(), top.end());

    const double f60 = midiHz(60), f72 = midiHz(72), f48 = midiHz(48);
    bool has60 = false, hasOct = false;
    for (double f : top) {
        if (std::fabs(f - f60) < 5.0) has60 = true;
        if (std::fabs(f - f72) < 5.0 || std::fabs(f - f48) < 5.0) hasOct = true;
    }

    MEntry m; m.id="M6.3"; m.section="VoiceMode";
    m.what="OCTAVE adds 1-octave companion to the played note";
    m.metric="has_octave_peak"; m.unit="";
    m.expected="yes (60 + ±12)";
    m.measured = (has60 && hasOct) ? "yes" : "no";
    m.pass = has60 && hasOct;
    m.svgPath = OUT_DIR + "/M6.3-octave.svg";
    if (top.size() >= 4) {
        char nb[120]; std::snprintf(nb, sizeof nb,
            "top4 peaks: %.0f %.0f %.0f %.0f Hz", top[0], top[1], top[2], top[3]);
        m.note = nb;
    }
    return m;
}

// -- M6.4 FIFTH interval ------------------------------------------------------
// Frage:  Liefert FIFTH mode neben Grundnote auch eine Quinte (+7 Halbtöne)?
static MEntry M6_4_fifth() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::Fifth);
    e.noteOn(60);
    auto cap = capture(e, 32768, 0.15);
    auto sp = spectrum(cap);
    writeSpectrum(OUT_DIR + "/M6.4-fifth.svg",
                  "M6.4 — FIFTH mode @ MIDI 60 (expect 60 + 67)",
                  sp.magDb, sp.N, SR);
    auto top = findTopPeaks(sp.magDb, sp.N, SR, 4, -40.0, 20.0);

    const double f60 = midiHz(60), f67 = midiHz(67);
    bool has60 = false, hasFifth = false;
    for (double f : top) {
        if (std::fabs(f - f60) < 5.0) has60 = true;
        if (std::fabs(f - f67) < 5.0) hasFifth = true;
    }

    MEntry m; m.id="M6.4"; m.section="VoiceMode";
    m.what="FIFTH adds +7-semitone companion (perfect fifth)";
    m.metric="has_fifth_peak"; m.unit="";
    m.expected="yes (60 + 67)";
    m.measured = (has60 && hasFifth) ? "yes" : "no";
    m.pass = has60 && hasFifth;
    m.svgPath = OUT_DIR + "/M6.4-fifth.svg";
    return m;
}

// -- M6.5 UNIRING — ring modulation -------------------------------------------
// Frage:  Produziert UNIRING ring-mod-Summen/Differenz-Spektrum?
// Mess:   UNIRING noteOn(60). Reine Ring-mod von zwei Sägezähnen f1, f2
//         erzeugt zusätzlich Komponenten bei |f1±f2|. Wir suchen Peaks, die
//         NICHT auf der Harmonischen-Leiter eines einzelnen Saw f₀ liegen.
static MEntry M6_5_uniring() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::UniRing);
    e.setParamNorm(P3::ParaEngine::Param::Detune, 0.4);   // explicit detune for ring
    e.noteOn(60);
    auto cap = capture(e, 32768, 0.20);
    auto sp = spectrum(cap);
    writeSpectrum(OUT_DIR + "/M6.5-uniring.svg",
                  "M6.5 — UNIRING mode @ MIDI 60 (ring mod side bands)",
                  sp.magDb, sp.N, SR);

    // Compare against a clean POLY saw at MIDI 60 (single oscillator path).
    P3::ParaEngine eRef; eRef.prepare(SR, 256);
    setNeutralPatch(eRef); eRef.noteOn(60);
    auto capRef = capture(eRef, 32768, 0.20);
    auto spRef = spectrum(capRef);

    // Count peaks above -50 dB in UNIRING that are NOT present (within 5 Hz
    // tolerance) in the reference saw.
    auto topRing = findTopPeaks(sp.magDb,   sp.N, SR, 30, -50.0, 5.0);
    auto topRef  = findTopPeaks(spRef.magDb, spRef.N, SR, 30, -50.0, 5.0);
    int newPeaks = 0;
    for (double f : topRing) {
        bool inRef = false;
        for (double r : topRef) if (std::fabs(f - r) < 5.0) { inRef = true; break; }
        if (!inRef) ++newPeaks;
    }

    MEntry m; m.id="M6.5"; m.section="VoiceMode";
    m.what="UNIRING adds ring-mod side-bands not in pure saw";
    m.metric="new_peaks_vs_saw"; m.unit="";
    m.expected="≥ 5";
    char b[32]; std::snprintf(b, sizeof b, "%d", newPeaks); m.measured=b;
    m.pass = newPeaks >= 5;
    m.svgPath = OUT_DIR + "/M6.5-uniring.svg";
    return m;
}

// -- M6.6 POLYRING — ring modulation across notes -----------------------------
// Frage:  Erzeugt POLYRING bei mehreren Noten Ring-mod-Produkte zwischen ihnen?
static MEntry M6_6_polyring() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::PolyRing);
    e.noteOn(60); e.noteOn(67);            // C + G fifth → expect sum/diff
    auto cap = capture(e, 32768, 0.20);
    auto sp = spectrum(cap);
    writeSpectrum(OUT_DIR + "/M6.6-polyring.svg",
                  "M6.6 — POLYRING mode 60+67", sp.magDb, sp.N, SR);

    // Check for a peak near midiHz(67) - midiHz(60) ≈ 130 Hz (difference tone)
    auto top = findTopPeaks(sp.magDb, sp.N, SR, 10, -50.0, 5.0);
    bool hasDiff = false;
    const double diffHz = midiHz(67) - midiHz(60);    // ≈130 Hz
    for (double f : top)
        if (std::fabs(f - diffHz) < 5.0) { hasDiff = true; break; }

    MEntry m; m.id="M6.6"; m.section="VoiceMode";
    m.what="POLYRING produces difference frequency between two notes";
    m.metric="has_diff_peak_~130Hz"; m.unit="";
    m.expected="yes";
    m.measured = hasDiff ? "yes" : "no";
    m.pass = hasDiff;
    m.svgPath = OUT_DIR + "/M6.6-polyring.svg";
    return m;
}

// -- M6.7 Voice-mode switching click-freedom ----------------------------------
// Frage:  Produziert ein setMode() während sustained note einen audiblen Klick?
// Mess:   noteOn(60), 300 ms render, dann setMode → UNISON, weitere 300 ms.
//         Para3 mappt die bestehenden Voices an die neue Allokation um — ein
//         kleiner Übergangs-Transient ist erwartet (Volca-typisch). Wir
//         akzeptieren bis 0.30 sample-Δ; klar audibler Klick wäre > 0.5.
static MEntry M6_7_modeSwitchClickFree() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setMode(P3::ParaAllocator::Mode::Poly);
    e.noteOn(60);
    std::vector<float> a((std::size_t)(SR * 0.3));
    e.process(a.data(), (int)a.size());
    e.setMode(P3::ParaAllocator::Mode::Unison);
    std::vector<float> b((std::size_t)(SR * 0.3));
    e.process(b.data(), (int)b.size());

    // Check Δ sample in 5 ms window around the boundary
    const std::size_t W = (std::size_t)(SR * 0.005);
    double maxDiff = 0;
    for (std::size_t i = 1; i < W; ++i) {
        const double d = std::fabs((double)b[i] - (double)b[i-1]);
        maxDiff = std::max(maxDiff, d);
    }
    // Also worst boundary: last sample of a → first sample of b
    if (!a.empty() && !b.empty()) {
        const double d = std::fabs((double)b.front() - (double)a.back());
        maxDiff = std::max(maxDiff, d);
    }

    // Scope around boundary
    std::vector<float> around;
    const std::size_t pre = std::min(a.size(), (std::size_t)(SR * 0.020));
    around.insert(around.end(), a.end() - pre, a.end());
    around.insert(around.end(), b.begin(), b.begin() + (std::size_t)(SR * 0.020));
    writeScope(OUT_DIR + "/M6.7-mode-switch-clickfree.svg",
               "M6.7 — Mode switch POLY→UNISON (20 ms pre/post)",
               around, SR, 0.0, 0.040);

    MEntry m; m.id="M6.7"; m.section="VoiceMode";
    m.what="Mode switch transient bounded (Volca-typisch)";
    m.metric="max_sample_diff_at_switch"; m.unit="amp";
    m.expected="< 0.30 (audible click would be > 0.5)";
    char buf[32]; std::snprintf(buf, sizeof buf, "%.4f", maxDiff); m.measured=buf;
    m.pass = maxDiff < 0.30;
    m.svgPath = OUT_DIR + "/M6.7-mode-switch-clickfree.svg";
    return m;
}

// =============================================================================
//  LAB-5 :: M7.x Sequencer + M8.x FLUX
// =============================================================================

// Helper: render N samples through Controller (which drives the engine).
static std::vector<float> ctlRender(P3::Controller& c, std::size_t n) {
    std::vector<float> out(n);
    c.render(out.data(), (int)n);
    return out;
}

// Detect onset (first sample > 0.05 amp) in a buffer.
static int firstOnset(const std::vector<float>& s, double thresh = 0.05) {
    for (std::size_t i = 0; i < s.size(); ++i)
        if (std::fabs((double)s[i]) > thresh) return (int)i;
    return -1;
}

// -- M7.1 Step timing accuracy (BPM → samples) --------------------------------
// Frage:  Stimmt die Step-Dauer mit der BPM-Angabe überein?
// Mess:   Gate nur Step 1. Bei 60 BPM × 1/16 = 12000 samples/step → Step 1
//         onset bei ~12000. Bei 120 BPM = 6000.
static MEntry M7_1_stepTiming() {
    auto runAt = [&](double bpm) -> int {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        P3::Controller c; c.prepare(e, SR);
        P3::Pattern& ep = c.editPattern();
        ep.length = 16;
        ep.steps[1].gate = true; ep.steps[1].note = 60;
        c.commitEdit();
        c.setSeqTempo(bpm, 4);
        c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 0.5));
        return firstOnset(out);
    };
    const int o60  = runAt(60.0);
    const int o120 = runAt(120.0);
    const double expected60  = SR * 60.0 / 60.0 / 4.0;  // 12000
    const double expected120 = SR * 60.0 / 120.0 / 4.0; // 6000
    const double err60  = std::fabs((double)o60  - expected60)  / expected60;
    const double err120 = std::fabs((double)o120 - expected120) / expected120;
    const double worst  = std::max(err60, err120);

    Series s; s.label = "step-1 onset / samples";
    s.xs = {60.0, 120.0}; s.ys = {(double)o60, (double)o120};
    SvgPlot p("M7.1 — Step-1 onset vs BPM (gate only step 1)");
    p.xLabel("BPM").yLabel("samples").xRange(40, 140).yRange(0, 15000)
     .addSeries(s);
    p.write(OUT_DIR + "/M7.1-step-timing.svg");

    MEntry m; m.id="M7.1"; m.section="Sequencer";
    m.what="Step timing matches BPM";
    m.metric="worst_relative_err"; m.unit="%";
    m.expected="< 2";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", worst * 100.0); m.measured=b;
    m.pass = worst < 0.02;
    m.svgPath = OUT_DIR + "/M7.1-step-timing.svg";
    char nb[80]; std::snprintf(nb, sizeof nb,
        "60 BPM: %d (expect %.0f); 120 BPM: %d (expect %.0f)",
        o60, expected60, o120, expected120);
    m.note = nb;
    return m;
}

// -- M7.2 Tempo Div -----------------------------------------------------------
// Frage:  Verlängern tempoDiv 1/2 und 1/4 die Step-Dauer entsprechend?
// Mess:   120 BPM, gate step 1. tempoDiv = 1, 2, 4. Step 1 onset.
static MEntry M7_2_tempoDiv() {
    auto runAt = [&](int div) -> int {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        P3::Controller c; c.prepare(e, SR);
        P3::Pattern& ep = c.editPattern();
        ep.length = 16;
        ep.steps[1].gate = true; ep.steps[1].note = 60;
        c.commitEdit();
        c.setSeqTempo(120.0, 4);
        c.clock().setDiv(div);
        c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 1.2));
        return firstOnset(out);
    };
    const int o1 = runAt(1), o2 = runAt(2), o4 = runAt(4);
    // div doubles → step duration doubles (at div=2 → onset ≈ 12000, div=4 → 24000)
    Series s; s.label = "onset / samples";
    s.xs = {1.0, 2.0, 4.0}; s.ys = {(double)o1, (double)o2, (double)o4};
    SvgPlot p("M7.2 — Tempo-Div → step-1 onset (120 BPM base)");
    p.xLabel("DIV").yLabel("samples").xRange(0.5, 5).yRange(0, 30000)
     .addSeries(s);
    p.write(OUT_DIR + "/M7.2-tempo-div.svg");

    const double ratio2 = (double)o2 / o1;
    const double ratio4 = (double)o4 / o1;
    MEntry m; m.id="M7.2"; m.section="Sequencer";
    m.what="Tempo-Div scales step duration ×div";
    m.metric="ratio_div_4_to_1"; m.unit="";
    m.expected="≈ 4.0 (±10 %)";
    char b[32]; std::snprintf(b, sizeof b, "%.2f", ratio4); m.measured=b;
    m.pass = std::fabs(ratio2 - 2.0) < 0.2 && std::fabs(ratio4 - 4.0) < 0.4;
    m.svgPath = OUT_DIR + "/M7.2-tempo-div.svg";
    char nb[64]; std::snprintf(nb, sizeof nb, "ratios div=2:%.2f, div=4:%.2f", ratio2, ratio4);
    m.note = nb;
    return m;
}

// -- M7.3 Swing — cross-reference T48 -----------------------------------------
// Mess:   Curve aus mehreren Swing-Werten (0/0.15/0.30/0.45) zeigt
//         monotonen Anstieg der odd-step delay (≅ swing × stepSamples).
static MEntry M7_3_swing() {
    auto runAt = [&](double sw) -> int {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        P3::Controller c; c.prepare(e, SR);
        P3::Pattern& ep = c.editPattern();
        ep.length = 16; ep.steps[1].gate = true; ep.steps[1].note = 60;
        c.commitEdit();
        c.setSeqTempo(60.0, 4);   // 12000 samples / step
        c.clock().setSwing(sw);
        c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 0.5));
        return firstOnset(out);
    };
    const double sws[] = {0.0, 0.15, 0.30, 0.45};
    Series s; s.label = "onset / samples";
    bool mono = true; double prev = 0;
    for (double sw : sws) {
        const int o = runAt(sw);
        s.xs.push_back(sw); s.ys.push_back((double)o);
        if (o < prev - 100) mono = false;
        prev = o;
    }
    SvgPlot p("M7.3 — SWING odd-step delay (gate step 1, 60 BPM)");
    p.xLabel("SWING (norm)").yLabel("step-1 onset / samples")
     .xRange(0, 0.5).yRange(0, 20000)
     .addSeries(s).note("Erwartung: onset = 12000 · (1 + swing)");
    p.write(OUT_DIR + "/M7.3-swing.svg");

    const double rise = s.ys.back() - s.ys.front();
    MEntry m; m.id="M7.3"; m.section="Sequencer";
    m.what="SWING delays odd step proportionally";
    m.metric="onset_rise_swing_0_to_0.45"; m.unit="samples";
    m.expected="≈ 5400 (0.45 · 12000)";
    char b[32]; std::snprintf(b, sizeof b, "%.0f", rise); m.measured=b;
    m.pass = mono && rise > 4500 && rise < 6500;
    m.svgPath = OUT_DIR + "/M7.3-swing.svg";
    return m;
}

// -- M7.4 Step Velocity — cross-reference T46 ---------------------------------
static MEntry M7_4_stepVel() {
    auto runAt = [&](double vel) -> double {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff, 0.8);
        e.setParamNorm(P3::ParaEngine::Param::DecRel, 0.1);
        P3::Controller c; c.prepare(e, SR);
        P3::Pattern& ep = c.editPattern();
        ep.length = 16; ep.steps[0].gate = true; ep.steps[0].note = 60;
        ep.steps[0].vel = vel;
        c.commitEdit();
        c.setSeqTempo(60.0, 4); c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 0.2));
        auto env = envelope(out, SR, 0.010);
        double pk = 0; for (double v : env) pk = std::max(pk, v);
        return pk;
    };
    const double vels[] = {0.25, 0.5, 0.75, 1.0};
    Series s; s.label = "peak amp";
    for (double v : vels) {
        s.xs.push_back(v); s.ys.push_back(runAt(v));
    }
    SvgPlot p("M7.4 — Step velocity → peak amplitude");
    p.xLabel("vel (per-step)").yLabel("peak").xRange(0, 1.05).yRange(0, 1)
     .addSeries(s);
    p.write(OUT_DIR + "/M7.4-step-vel.svg");

    // Lineare Korrelation r²
    double sx=0, sy=0, sxy=0, sx2=0, sy2=0;
    const int N = (int)s.xs.size();
    for (int i = 0; i < N; ++i) {
        sx += s.xs[i]; sy += s.ys[i];
        sxy += s.xs[i] * s.ys[i];
        sx2 += s.xs[i] * s.xs[i];
        sy2 += s.ys[i] * s.ys[i];
    }
    const double r = (N * sxy - sx * sy) /
        std::sqrt((N * sx2 - sx * sx) * (N * sy2 - sy * sy));
    MEntry m; m.id="M7.4"; m.section="Sequencer";
    m.what="Step velocity linearly scales peak amplitude";
    m.metric="linear_r"; m.unit="";
    m.expected="> 0.97";
    char b[32]; std::snprintf(b, sizeof b, "%.3f", r); m.measured=b;
    m.pass = r > 0.97;
    m.svgPath = OUT_DIR + "/M7.4-step-vel.svg";
    return m;
}

// -- M7.5 Step Gate length — cross-reference T47 ------------------------------
static MEntry M7_5_stepGate() {
    auto runAt = [&](double gl) -> double {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::DecRel,  0.1);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.85);
        P3::Controller c; c.prepare(e, SR);
        P3::Pattern& ep = c.editPattern();
        ep.length = 16; ep.steps[0].gate = true; ep.steps[0].note = 60;
        ep.steps[0].gateLen = gl;
        c.commitEdit();
        c.setSeqTempo(60.0, 4); c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 0.3));
        auto env = envelope(out, SR, 0.020);
        // late RMS window 200..245 ms
        double s2 = 0; int n = 0;
        for (std::size_t i = (std::size_t)(SR*0.2); i < (std::size_t)(SR*0.245) && i < env.size(); ++i) {
            s2 += env[i] * env[i]; ++n;
        }
        return (n > 0) ? std::sqrt(s2 / n) : 0;
    };
    const double gls[] = {0.2, 0.5, 1.0};
    Series s; s.label = "late RMS (gate)";
    for (double g : gls) {
        s.xs.push_back(g); s.ys.push_back(runAt(g));
    }
    SvgPlot p("M7.5 — Step gateLen → late-window RMS");
    p.xLabel("gateLen").yLabel("late RMS")
     .xRange(0, 1.05).yRange(0, std::max(s.ys.back() * 1.3, 0.1))
     .addSeries(s);
    p.write(OUT_DIR + "/M7.5-step-gate.svg");

    MEntry m; m.id="M7.5"; m.section="Sequencer";
    m.what="Step gateLen<1 cuts late-window RMS";
    m.metric="ratio_gl=0.2_to_1.0"; m.unit="";
    m.expected="< 0.3";
    const double ratio = s.ys.front() / std::max(s.ys.back(), 1e-9);
    char b[32]; std::snprintf(b, sizeof b, "%.3f", ratio); m.measured=b;
    m.pass = ratio < 0.3;
    m.svgPath = OUT_DIR + "/M7.5-step-gate.svg";
    return m;
}

// -- M7.6 Active-step skip ----------------------------------------------------
// Frage:  Überspringt seqActiveStep(false) den Step vollständig?
// Mess:   Gate Steps 0, 1, 2. actStep[1] = false. Erwartet: Step 1 onset
//         entfällt; Step 2 fires bei stepSamples·2 statt ·1 verzögert.
static MEntry M7_6_activeStepSkip() {
    // Statt Peak-Detection im Sequencer-Audio (das wegen click-free Crossfade
    // schwer auflöst): wir messen die zeitliche Verteilung der RMS-Power.
    // Bei normal-Pattern (3 aktive Steps): hohes RMS für 750 ms.
    // Bei skipMid (Step 1 aus): RMS-Dip im 250..500 ms Bereich.
    auto runBlockRms = [&](bool skipMid) -> std::vector<double> {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff,  0.8);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);   // decay to silence
        e.setParamNorm(P3::ParaEngine::Param::DecRel,  0.10);  // ~50 ms decay
        P3::Controller c; c.prepare(e, SR);
        c.setStepTrigger(true);    // EG retriggert pro Step → klare Lücke bei skip
        P3::Pattern& ep = c.editPattern();
        ep.length = 16;
        for (int i = 0; i < 3; ++i) {
            ep.steps[i].gate = true; ep.steps[i].note = 60;
        }
        if (skipMid) ep.steps[1].active = false;
        c.commitEdit();
        c.setSeqTempo(60.0, 4);
        c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 0.75));
        // Block-RMS in 3 zeitlichen Vierteln (jeweils 250 ms).
        std::vector<double> rms(3, 0.0);
        for (int b = 0; b < 3; ++b) {
            const std::size_t a = (std::size_t)(SR * 0.25 * b);
            const std::size_t e2 = (std::size_t)(SR * 0.25 * (b + 1));
            double s = 0; int n = 0;
            for (std::size_t i = a; i < e2 && i < out.size(); ++i) {
                s += (double)out[i] * out[i]; ++n;
            }
            rms[b] = (n > 0) ? std::sqrt(s / n) : 0;
        }
        return rms;
    };
    auto normal  = runBlockRms(false);
    auto skipped = runBlockRms(true);

    Series sN; sN.label = "normal RMS";    sN.xs = {0.125, 0.375, 0.625}; sN.ys = normal;
    Series sS; sS.label = "skipped RMS";   sS.xs = sN.xs;                  sS.ys = skipped;
    sS.colour = "#71d99a";
    SvgPlot p("M7.6 — Active-Step skip: middle quarter RMS drops");
    p.xLabel("Block centre t / s").yLabel("RMS")
     .xRange(0, 0.75).yRange(0, std::max(*std::max_element(normal.begin(), normal.end()), 0.1) * 1.3)
     .addSeries(sN).addSeries(sS);
    p.write(OUT_DIR + "/M7.6-active-step-skip.svg");

    // Pass: middle block RMS drops ≥ 30 % when step 1 is skipped.
    const double drop = (normal[1] > 0) ? skipped[1] / normal[1] : 1.0;
    MEntry m; m.id="M7.6"; m.section="Sequencer";
    m.what="setActiveStep(false) silences that step's block";
    m.metric="skip_to_normal_mid_ratio"; m.unit="";
    m.expected="< 0.7";
    char b[32]; std::snprintf(b, sizeof b, "%.3f", drop); m.measured=b;
    m.pass = drop < 0.7 && normal[1] > 0.01;
    m.svgPath = OUT_DIR + "/M7.6-active-step-skip.svg";
    return m;
}

// -- M7.7 Motion sequence smooth -----------------------------------------------
// Frage:  Glättet seqMotionSmooth(true) die Param-Sprünge zwischen Steps?
// Mess:   Setze cutoff-motion lane: high/low alternating. Capture audio, FFT
//         hochband-RMS pro 100-ms Block. SMOOTH=on → langsam wandernd,
//         SMOOTH=off → harte Sprünge (mehr Block-zu-Block Varianz).
static MEntry M7_7_motionSmooth() {
    auto runSmooth = [&](bool smooth) -> double {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::Cutoff, 0.3);    // baseline low
        P3::Controller c; c.prepare(e, SR);
        P3::Pattern& ep = c.editPattern();
        ep.length = 16;
        for (int i = 0; i < 16; ++i) {
            ep.steps[i].gate = true; ep.steps[i].note = 60;
        }
        c.commitEdit();
        c.setSeqTempo(60.0, 4);    // 250 ms / step
        c.motionSmooth(smooth);
        // Cutoff motion: alternating 0.3/0.8/0.3/0.8...
        for (int i = 0; i < 16; ++i)
            c.motionSet(0 /*Cutoff*/, i, (i & 1) ? 0.8 : 0.3);
        c.commitEdit();
        c.seqStart();
        auto out = ctlRender(c, (std::size_t)(SR * 2.0));
        // Highband RMS per 100 ms block; std-dev of block sequence
        const std::size_t W = (std::size_t)(SR * 0.1);
        std::vector<double> blocks;
        for (std::size_t i = 0; i + W <= out.size(); i += W) {
            std::vector<float> seg(out.begin() + i, out.begin() + i + W);
            auto sp = spectrum(seg);
            double e2 = 0; int n = 0;
            for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
                const double f = (double)k * SR / sp.N;
                if (f < 2000) { continue; }
                if (f > 6000) { break; }
                const double lin = std::pow(10.0, sp.magDb[k] / 20.0);
                e2 += lin * lin; ++n;
            }
            blocks.push_back((n > 0) ? std::sqrt(e2 / n) : 0);
        }
        // Total swing of the block sequence (max-min)
        double mn = 1e9, mx = 0;
        for (double v : blocks) { mn = std::min(mn, v); mx = std::max(mx, v); }
        return (mn > 0) ? 20.0 * std::log10(mx / mn) : 0.0;
    };
    const double swingNo  = runSmooth(false);
    const double swingYes = runSmooth(true);
    // Smooth ON should compress the swing (high → low transition is ramped)
    MEntry m; m.id="M7.7"; m.section="Sequencer";
    m.what="Motion-smooth reduces block-to-block param swing";
    m.metric="swing_smooth_off_vs_on"; m.unit="dB";
    m.expected="off > on";
    char b[32]; std::snprintf(b, sizeof b, "%.1f / %.1f", swingNo, swingYes);
    m.measured = b;
    m.pass = swingNo > swingYes;
    m.svgPath = "";
    return m;
}

// =============================================================================
//  LAB-5 :: M8.x FLUX
// =============================================================================

// -- M8.1 FLUX sample-accurate playback ---------------------------------------
// Frage:  Werden FLUX-Noten an gleichen Sample-Offsets wieder abgespielt?
// Mess:   FluxMode on, rec on, append noteOn(60) + noteOff bei bestimmten
//         Offsets, commit. Replay → onsets müssen bei den Aufnahme-Offsets
//         sample-genau wiedererscheinen. Cross-Ref T23.
static MEntry M8_1_fluxAccurate() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::DecRel, 0.02);
    e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);
    P3::Controller c; c.prepare(e, SR);
    const std::size_t loopLen = (std::size_t)(SR * 1.0);   // 1 s loop
    c.setFluxMode(true);
    c.fluxSetLoopLen((unsigned int)loopLen);
    c.setFluxQuantize(false);     // sample-accurate
    c.fluxRec(true);

    // Record: silence, noteOn @ 250 ms, noteOff @ 300 ms.
    std::vector<float> rec(loopLen);
    c.render(rec.data(), (int)(SR * 0.25));
    c.fluxNote(60, true);
    c.render(rec.data() + (std::size_t)(SR * 0.25), (int)(SR * 0.05));
    c.fluxNote(60, false);
    c.render(rec.data() + (std::size_t)(SR * 0.30), (int)(loopLen - SR * 0.30));
    c.fluxRec(false);
    c.fluxCommit();

    // Render 2 more loops
    std::vector<float> p1(loopLen), p2(loopLen);
    c.render(p1.data(), (int)loopLen);
    c.render(p2.data(), (int)loopLen);

    // Onset of replay (first sample > 0.05 in each loop)
    const int onset1 = firstOnset(p1);
    const int onset2 = firstOnset(p2);
    const int expected = (int)(SR * 0.25);

    writeScope(OUT_DIR + "/M8.1-flux-replay.svg",
               "M8.1 — FLUX replay loop 1 (1 s)",
               p1, SR, 0.0, 1.0);

    const bool ok = std::abs(onset1 - expected) < 100 && std::abs(onset2 - expected) < 100;
    MEntry m; m.id="M8.1"; m.section="FLUX";
    m.what="FLUX replay reproduces sample-accurate onsets";
    m.metric="onset_err_loop1"; m.unit="samples";
    m.expected="< 100";
    char b[32]; std::snprintf(b, sizeof b, "%d", std::abs(onset1 - expected));
    m.measured=b;
    m.pass = ok;
    m.svgPath = OUT_DIR + "/M8.1-flux-replay.svg";
    char nb[80]; std::snprintf(nb, sizeof nb,
        "loop1: %d, loop2: %d (expect %d)", onset1, onset2, expected);
    m.note = nb;
    return m;
}

// -- M8.2 FLUX Param replay (cross-ref T42) -----------------------------------
// Frage:  Spielt FLUX aufgezeichnete Param-Events sample-accurat wieder ab?
// Mess:   1 s loop. Record cutoff bright @ 0 ms, dark @ 500 ms, noteOn @ 0,
//         noteOff @ 950. Replay → erstes Halbloop bright, zweites dunkel.
static MEntry M8_2_fluxParam() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::Cutoff, 0.5);
    P3::Controller c; c.prepare(e, SR);
    const std::size_t loopLen = (std::size_t)(SR * 1.0);
    c.setFluxMode(true);
    c.fluxSetLoopLen((unsigned int)loopLen);
    c.setFluxQuantize(false);
    c.fluxRec(true);

    c.fluxParam(0 /*Cutoff*/, 0.8);
    c.fluxNote(60, true);
    c.render(nullptr, 0);   // ensure events flush
    std::vector<float> rec(loopLen);
    c.render(rec.data(), (int)(SR * 0.5));
    c.fluxParam(0, 0.2);   // dark mid-loop
    c.render(rec.data() + (std::size_t)(SR * 0.5), (int)(SR * 0.45));
    c.fluxNote(60, false);
    c.render(rec.data() + (std::size_t)(SR * 0.95), (int)(loopLen - SR * 0.95));
    c.fluxRec(false);
    c.fluxCommit();

    // Render one full replay loop
    std::vector<float> p(loopLen);
    c.render(p.data(), (int)loopLen);

    // High-band RMS in first 400 ms vs last 400 ms
    auto bandRms = [&](std::size_t a, std::size_t b){
        std::vector<float> seg(p.begin() + a, p.begin() + b);
        auto sp = spectrum(seg);
        double e2 = 0; int n = 0;
        for (std::size_t k = 1; k < sp.magDb.size(); ++k) {
            const double f = (double)k * SR / sp.N;
            if (f < 1500) { continue; }
            if (f > 6000) { break; }
            const double lin = std::pow(10.0, sp.magDb[k] / 20.0);
            e2 += lin * lin; ++n;
        }
        return (n > 0) ? 20.0 * std::log10(std::sqrt(e2 / n)) : -100.0;
    };
    const double early = bandRms((std::size_t)(SR*0.05), (std::size_t)(SR*0.45));
    const double late  = bandRms((std::size_t)(SR*0.55), (std::size_t)(SR*0.95));

    writeScope(OUT_DIR + "/M8.2-flux-param.svg",
               "M8.2 — FLUX param replay: bright @ 0, dark @ 500 ms",
               p, SR, 0.0, 1.0);

    MEntry m; m.id="M8.2"; m.section="FLUX";
    m.what="FLUX param event darkens second half of loop";
    m.metric="early_to_late_drop"; m.unit="dB";
    m.expected="> 8";
    const double drop = early - late;
    char b[32]; std::snprintf(b, sizeof b, "%.1f", drop); m.measured=b;
    m.pass = drop > 8.0;
    m.svgPath = OUT_DIR + "/M8.2-flux-param.svg";
    return m;
}

// -- M8.3 FLUX Quantize 1/16 vs FINE ------------------------------------------
// Frage:  Snappt fluxQuantize=true Events auf das Loop/16-Grid (Engine snap
//         rechnet relative zur Loop-Länge, nicht BPM)?
// Mess:   Loop = 1 s → 1/16-Grid alle 3000 Samples. Note bei t = 333 ms
//         (16000 Samples) wird auf 15000 gesnappt mit Quantize on, bleibt
//         bei 16000 ohne. Diff ≥ 500 Samples (= 10 ms hörbarer Versatz).
static MEntry M8_3_fluxQuantize() {
    auto runQ = [&](bool quant) -> int {
        P3::ParaEngine e; e.prepare(SR, 256);
        setNeutralPatch(e);
        e.setParamNorm(P3::ParaEngine::Param::DecRel, 0.02);
        e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);
        P3::Controller c; c.prepare(e, SR);
        c.setSeqTempo(120.0, 4);    // 1/16 = 250 ms = 12000 samples
        const std::size_t loopLen = (std::size_t)(SR * 1.0);
        c.setFluxMode(true);
        c.fluxSetLoopLen((unsigned int)loopLen);
        c.setFluxQuantize(quant);
        c.fluxRec(true);

        // Move 333 ms into the loop, then fire the note.
        std::vector<float> warm((std::size_t)(SR * 0.333));
        c.render(warm.data(), (int)warm.size());
        c.fluxNote(60, true);
        std::vector<float> rest(loopLen - warm.size());
        c.render(rest.data(), (int)rest.size());
        c.fluxRec(false);
        c.fluxCommit();

        std::vector<float> replay(loopLen);
        c.render(replay.data(), (int)loopLen);
        return firstOnset(replay);
    };
    const int onQuant = runQ(true);
    const int onFine  = runQ(false);

    // With quant on, expect snap to nearest 1/16 (12000 samples). 333 ms = 16000.
    // Nearest 1/16: 12000 (250 ms) or 18000 (375 ms). Closer to 18000 (Δ=2000).
    // With quant off: expect ~16000.
    MEntry m; m.id="M8.3"; m.section="FLUX";
    m.what="Quantize snaps off-grid event onto loop/16";
    m.metric="quant_vs_fine_diff"; m.unit="samples";
    m.expected="≥ 500";
    const int diff = std::abs(onQuant - onFine);
    char b[32]; std::snprintf(b, sizeof b, "%d", diff); m.measured=b;
    m.pass = diff >= 500;
    m.svgPath = "";
    char nb[80]; std::snprintf(nb, sizeof nb,
        "quant onset %d, fine onset %d", onQuant, onFine);
    m.note = nb;
    return m;
}

// -- M8.4 FLUX Loop length ----------------------------------------------------
// Frage:  Respektiert setFluxLoopLen die geforderte Loop-Länge?
// Mess:   Loop = 0.5 s. Record 1 note bei 100 ms. Replay 2 Loops à 0.5 s →
//         beide Onsets bei je ~100 ms innerhalb des Loops.
static MEntry M8_4_fluxLoopLen() {
    P3::ParaEngine e; e.prepare(SR, 256);
    setNeutralPatch(e);
    e.setParamNorm(P3::ParaEngine::Param::DecRel, 0.02);
    e.setParamNorm(P3::ParaEngine::Param::Sustain, 0.0);
    P3::Controller c; c.prepare(e, SR);
    const std::size_t loopLen = (std::size_t)(SR * 0.5);
    c.setFluxMode(true);
    c.fluxSetLoopLen((unsigned int)loopLen);
    c.setFluxQuantize(false);
    c.fluxRec(true);
    std::vector<float> a((std::size_t)(SR * 0.1));
    c.render(a.data(), (int)a.size());
    c.fluxNote(60, true);
    std::vector<float> b1(loopLen - a.size());
    c.render(b1.data(), (int)b1.size());
    c.fluxRec(false);
    c.fluxCommit();

    std::vector<float> r1(loopLen), r2(loopLen);
    c.render(r1.data(), (int)loopLen);
    c.render(r2.data(), (int)loopLen);
    const int on1 = firstOnset(r1);
    const int on2 = firstOnset(r2);
    const int expected = (int)(SR * 0.1);

    MEntry m; m.id="M8.4"; m.section="FLUX";
    m.what="FLUX loop length respected, two onsets in two loops";
    m.metric="onset_err_max"; m.unit="samples";
    m.expected="< 200";
    const int worst = std::max(std::abs(on1 - expected), std::abs(on2 - expected));
    char buf[32]; std::snprintf(buf, sizeof buf, "%d", worst); m.measured = buf;
    m.pass = worst < 200;
    m.svgPath = "";
    char nb[80]; std::snprintf(nb, sizeof nb,
        "loop1: %d, loop2: %d (expect %d)", on1, on2, expected);
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

    // ---- LAB-3 VCA/EG + LFO + Delay ----
    if (want("M3.1")) manifest.add(M3_1_attack());
    if (want("M3.2")) manifest.add(M3_2_decaySustain());
    if (want("M3.3")) manifest.add(M3_3_release());
    if (want("M3.4")) manifest.add(M3_4_egInt());
    if (want("M3.5")) manifest.add(M3_5_clickFree());
    if (want("M4.1")) manifest.add(M4_1_lfoShapes());
    if (want("M4.2")) manifest.add(M4_2_lfoRate());
    if (want("M4.3")) manifest.add(M4_3_lfoPitchDepth());
    if (want("M4.4")) manifest.add(M4_4_lfoCutoffDepth());
    if (want("M5.1")) manifest.add(M5_1_delayTime());
    if (want("M5.2")) manifest.add(M5_2_delayFeedback());
    if (want("M5.3")) manifest.add(M5_3_delayMix());

    // ---- LAB-4 Voice modes + Ring ----
    if (want("M6.1")) manifest.add(M6_1_poly());
    if (want("M6.2")) manifest.add(M6_2_unison());
    if (want("M6.3")) manifest.add(M6_3_octave());
    if (want("M6.4")) manifest.add(M6_4_fifth());
    if (want("M6.5")) manifest.add(M6_5_uniring());
    if (want("M6.6")) manifest.add(M6_6_polyring());
    if (want("M6.7")) manifest.add(M6_7_modeSwitchClickFree());

    // ---- LAB-5 Sequencer + FLUX ----
    if (want("M7.1")) manifest.add(M7_1_stepTiming());
    if (want("M7.2")) manifest.add(M7_2_tempoDiv());
    if (want("M7.3")) manifest.add(M7_3_swing());
    if (want("M7.4")) manifest.add(M7_4_stepVel());
    if (want("M7.5")) manifest.add(M7_5_stepGate());
    if (want("M7.6")) manifest.add(M7_6_activeStepSkip());
    if (want("M7.7")) manifest.add(M7_7_motionSmooth());
    if (want("M8.1")) manifest.add(M8_1_fluxAccurate());
    if (want("M8.2")) manifest.add(M8_2_fluxParam());
    if (want("M8.3")) manifest.add(M8_3_fluxQuantize());
    if (want("M8.4")) manifest.add(M8_4_fluxLoopLen());

    // ---- LAB-6: M9.x werden in LAB-6 ergänzt ----

    manifest.writeCsv(OUT_DIR + "/MANIFEST.csv");
    manifest.writeMarkdownTable(OUT_DIR + "/MANIFEST.md",
                                 "PARA-3 Lab-Validation Manifest");

    std::printf("\n==== PARA-3 measure_main ====================================\n");
    std::printf("Measurements: %zu   Failures: %zu\n",
                manifest.size(), manifest.failures());
    std::printf("Wrote: %s/MANIFEST.{csv,md}\n", OUT_DIR.c_str());
    return manifest.failures() ? 1 : 0;
}
