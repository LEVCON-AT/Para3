// =============================================================================
//  PARA-3 :: Mess-Tooling — Radix-2 Cooley-Tukey FFT (header-only, pure C++)
//
//  Reines C++17, keine externen Abhängigkeiten. Größe N muss eine Potenz von 2
//  sein (assert). In-place auf std::vector<std::complex<double>>.
//
//  Wir nutzen das hier NICHT in der Engine (RT-Code) — nur in tools/measure/.
//  Daher dürfen wir std::vector, std::complex und std::polar verwenden.
// =============================================================================
#pragma once

#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace para3 { namespace measure {

inline bool isPow2(std::size_t n) noexcept { return n && ((n & (n - 1)) == 0); }

// Bit-reversal index for the radix-2 Cooley-Tukey reorder step.
inline std::size_t bitReverse(std::size_t x, int bits) noexcept {
    std::size_t r = 0;
    for (int i = 0; i < bits; ++i) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}

// In-place radix-2 DIT FFT. N=x.size() must be a power of two.
inline void fft(std::vector<std::complex<double>>& x) {
    const std::size_t N = x.size();
    assert(isPow2(N));
    int bits = 0; for (std::size_t t = N; t > 1; t >>= 1) ++bits;
    for (std::size_t i = 0; i < N; ++i) {
        std::size_t j = bitReverse(i, bits);
        if (j > i) std::swap(x[i], x[j]);
    }
    for (std::size_t s = 1; s <= (std::size_t)bits; ++s) {
        const std::size_t m  = std::size_t(1) << s;
        const std::size_t m2 = m >> 1;
        const std::complex<double> wm = std::polar(1.0, -2.0 * M_PI / (double)m);
        for (std::size_t k = 0; k < N; k += m) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < m2; ++j) {
                const std::complex<double> t = w * x[k + j + m2];
                const std::complex<double> u = x[k + j];
                x[k + j]      = u + t;
                x[k + j + m2] = u - t;
                w *= wm;
            }
        }
    }
}

// Hann window — reduces spectral leakage on non-periodic captures.
inline std::vector<double> hannWindow(std::size_t N) {
    std::vector<double> w(N);
    for (std::size_t i = 0; i < N; ++i)
        w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)(N - 1));
    return w;
}

// Real-input FFT helper: window + complex-extend + fft.
inline std::vector<std::complex<double>> realFft(const std::vector<float>& x,
                                                  bool window = true) {
    // Pad to next power of two
    std::size_t N = 1; while (N < x.size()) N <<= 1;
    std::vector<std::complex<double>> X(N, {0.0, 0.0});
    std::vector<double> w; if (window) w = hannWindow(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double s = (double)x[i] * (window ? w[i] : 1.0);
        X[i] = {s, 0.0};
    }
    fft(X);
    return X;
}

// Magnitude in dBFS (ref=1.0 → 0 dB). Returns N/2+1 bins (one-sided).
inline std::vector<double> magnitudeDb(const std::vector<std::complex<double>>& X,
                                        double winSum = 0.0) {
    const std::size_t N = X.size();
    std::vector<double> m(N / 2 + 1);
    // Hann window coherent gain = sum(w)/N ≈ 0.5. Compensate so a unity sine
    // peaks at 0 dB regardless of window.
    const double norm = (winSum > 0.0) ? (2.0 / winSum)
                                        : (2.0 / (double)N);
    for (std::size_t k = 0; k <= N / 2; ++k) {
        const double mag = std::abs(X[k]) * norm;
        m[k] = (mag > 1e-12) ? 20.0 * std::log10(mag) : -240.0;
    }
    return m;
}

// Frequency for bin k given sample rate.
inline double binFreq(std::size_t k, std::size_t N, double sampleRate) noexcept {
    return (double)k * sampleRate / (double)N;
}

// Index of peak magnitude in a half-spectrum (skip DC if requested).
inline std::size_t peakBin(const std::vector<double>& mag, std::size_t skipDc = 1) {
    std::size_t best = skipDc; double bv = mag[skipDc];
    for (std::size_t k = skipDc + 1; k < mag.size(); ++k)
        if (mag[k] > bv) { bv = mag[k]; best = k; }
    return best;
}

// Quadratic-interpolated peak frequency for sub-bin accuracy.
inline double peakFreqInterp(const std::vector<double>& mag,
                              std::size_t N, double sampleRate) {
    const std::size_t k = peakBin(mag);
    if (k == 0 || k + 1 >= mag.size()) return binFreq(k, N, sampleRate);
    const double a = mag[k - 1], b = mag[k], c = mag[k + 1];
    const double p = 0.5 * (a - c) / (a - 2.0 * b + c);   // parabolic vertex
    return binFreq(k, N, sampleRate) + p * (sampleRate / (double)N);
}

// Total harmonic distortion: ratio of 2..H harmonics to fundamental, in %.
// Picks the strongest bin as fundamental; sums RMS of harmonic peaks. The
// search is performed in bin space (the bin index of the n-th harmonic is
// n·k_fundamental), so sample rate and N are not needed.
inline double thdPercent(const std::vector<double>& magDb,
                          int maxHarmonics = 10) {
    const std::size_t kF = peakBin(magDb);
    const double fundDb = magDb[kF];
    const double fundLin = std::pow(10.0, fundDb / 20.0);
    double sumHarm2 = 0.0;
    for (int h = 2; h <= maxHarmonics; ++h) {
        const std::size_t kh = kF * (std::size_t)h;
        if (kh >= magDb.size()) break;
        // Look within ±2 bins for the nearest peak (sub-bin slop)
        double best = magDb[kh];
        for (std::size_t off = 1; off <= 2; ++off) {
            if (kh + off < magDb.size()) best = std::max(best, magDb[kh + off]);
            if (kh >= off)               best = std::max(best, magDb[kh - off]);
        }
        const double lin = std::pow(10.0, best / 20.0);
        sumHarm2 += lin * lin;
    }
    if (fundLin <= 0.0) return 0.0;
    return std::sqrt(sumHarm2) / fundLin * 100.0;
}

}} // namespace para3::measure
