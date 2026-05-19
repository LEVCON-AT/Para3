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

    // ---- LAB-2..6: M1.x..M9.x werden in den jeweiligen Sprints ergänzt ----

    manifest.writeCsv(OUT_DIR + "/MANIFEST.csv");
    manifest.writeMarkdownTable(OUT_DIR + "/MANIFEST.md",
                                 "PARA-3 Lab-Validation Manifest");

    std::printf("\n==== PARA-3 measure_main ====================================\n");
    std::printf("Measurements: %zu   Failures: %zu\n",
                manifest.size(), manifest.failures());
    std::printf("Wrote: %s/MANIFEST.{csv,md}\n", OUT_DIR.c_str());
    return manifest.failures() ? 1 : 0;
}
