// =============================================================================
//  PARA-3 :: Mess-Tooling — RIFF/WAVE 16-bit PCM Writer (header-only)
//
//  Schreibt Mono- oder Stereo-Float-Samples nach 16-bit Little-Endian PCM.
//  Begrenzt auf -1..+1 (Clipping → ±32767). Reicht für Archiv-Captures der
//  Mess-Strecken; FFT-Analyse läuft auf den Float-Daten direkt, nicht aus
//  dem WAV gelesen.
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace para3 { namespace measure {

namespace detail {
    inline void w32(std::vector<unsigned char>& o, std::uint32_t v) {
        o.push_back((unsigned char)(v & 0xff));
        o.push_back((unsigned char)((v >>  8) & 0xff));
        o.push_back((unsigned char)((v >> 16) & 0xff));
        o.push_back((unsigned char)((v >> 24) & 0xff));
    }
    inline void w16(std::vector<unsigned char>& o, std::uint16_t v) {
        o.push_back((unsigned char)(v & 0xff));
        o.push_back((unsigned char)((v >> 8) & 0xff));
    }
    inline void wStr(std::vector<unsigned char>& o, const char* s) {
        while (*s) o.push_back((unsigned char)*s++);
    }
}

// Mono float samples → 16-bit PCM WAV.
inline bool writeWavMono(const std::string& path,
                          const std::vector<float>& samples,
                          double sampleRate) {
    using namespace detail;
    const std::uint32_t sr = (std::uint32_t)sampleRate;
    const std::uint16_t ch = 1, bps = 16;
    const std::uint32_t dataBytes = (std::uint32_t)samples.size() * 2u;
    std::vector<unsigned char> o; o.reserve(44 + dataBytes);
    wStr(o, "RIFF"); w32(o, 36 + dataBytes); wStr(o, "WAVE");
    wStr(o, "fmt "); w32(o, 16); w16(o, 1); w16(o, ch); w32(o, sr);
    w32(o, sr * ch * (bps / 8)); w16(o, ch * (bps / 8)); w16(o, bps);
    wStr(o, "data"); w32(o, dataBytes);
    for (float s : samples) {
        const float c = std::max(-1.0f, std::min(1.0f, s));
        const std::int16_t pcm = (std::int16_t)std::lrint((double)c * 32767.0);
        o.push_back((unsigned char)((std::uint16_t)pcm & 0xff));
        o.push_back((unsigned char)(((std::uint16_t)pcm >> 8) & 0xff));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)o.data(), (std::streamsize)o.size());
    return f.good();
}

}} // namespace para3::measure
