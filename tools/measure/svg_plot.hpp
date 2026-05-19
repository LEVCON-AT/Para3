// =============================================================================
//  PARA-3 :: Mess-Tooling — minimaler SVG-Plotter (header-only)
//
//  Drei Plot-Typen:
//    Scope   — Zeitbereich (Samples → SVG-Path)
//    Spectrum — Magnitude (dB) vs Frequency (log Hz, halbseitiges FFT-Magnitude)
//    Line    — generischer XY-Linienzug (z. B. ADSR-Hüllkurve, Cutoff-Sweep)
//
//  Keine externen Abhängigkeiten. Schriftart = system-ui (Browser-Fallback).
//  SVG viewBox 0..width × 0..height. Y nach unten — alles intern bereits
//  gespiegelt, sodass größere Werte oben gezeichnet werden.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace para3 { namespace measure {

struct PlotStyle {
    int    width  = 900;
    int    height = 320;
    int    padL   = 70;        // axis margin
    int    padR   = 30;
    int    padT   = 36;        // title space
    int    padB   = 50;
    std::string bg     = "#0e0f12";
    std::string line   = "#22232a";
    std::string text   = "#bcc1cc";
    std::string accent = "#ff6a18";   // PARA·3 accent
    std::string accent2= "#7a3c12";   // accent-dim
    int    gridLines = 6;
};

// One series of XY samples with a colour. xs may be empty → equally spaced.
struct Series {
    std::string         label;
    std::vector<double> xs;        // empty → indices
    std::vector<double> ys;
    std::string         colour;    // CSS colour
    bool                fill = false;
};

class SvgPlot {
public:
    SvgPlot(const std::string& title, const PlotStyle& s = {})
        : title_(title), s_(s) {}

    SvgPlot& xLabel(const std::string& v)         { xLabel_ = v; return *this; }
    SvgPlot& yLabel(const std::string& v)         { yLabel_ = v; return *this; }
    SvgPlot& xRange(double a, double b)           { xa_=a; xb_=b; xAuto_=false; return *this; }
    SvgPlot& yRange(double a, double b)           { ya_=a; yb_=b; yAuto_=false; return *this; }
    SvgPlot& xLog(bool on = true)                 { xLog_ = on; return *this; }
    SvgPlot& addSeries(Series s)                  { series_.push_back(std::move(s)); return *this; }
    SvgPlot& annotate(double x, double y, const std::string& t)
                                                  { ann_.push_back({x,y,t}); return *this; }
    SvgPlot& note(const std::string& t)           { notes_.push_back(t); return *this; }

    bool write(const std::string& path) const {
        // Auto-range if needed
        double xa = xa_, xb = xb_, ya = ya_, yb = yb_;
        if (xAuto_ || yAuto_) {
            bool any = false;
            for (const auto& ser : series_) {
                for (std::size_t i = 0; i < ser.ys.size(); ++i) {
                    const double x = ser.xs.empty() ? (double)i : ser.xs[i];
                    const double y = ser.ys[i];
                    if (!any) { xa = xb = x; ya = yb = y; any = true; continue; }
                    xa = std::min(xa, x); xb = std::max(xb, x);
                    ya = std::min(ya, y); yb = std::max(yb, y);
                }
            }
            if (!any) { xa = 0; xb = 1; ya = 0; yb = 1; }
            if (xa == xb) xb = xa + 1.0;
            if (ya == yb) yb = ya + 1.0;
        }

        const int W = s_.width, H = s_.height;
        const int x0 = s_.padL,        y0 = s_.padT;
        const int x1 = W - s_.padR,    y1 = H - s_.padB;
        const auto xMap = [&](double x){
            if (xLog_) {
                const double la = std::log10(std::max(xa, 1e-12));
                const double lb = std::log10(std::max(xb, 1e-12));
                const double lx = std::log10(std::max(x,  1e-12));
                return x0 + (lx - la) / (lb - la) * (x1 - x0);
            }
            return x0 + (x - xa) / (xb - xa) * (x1 - x0);
        };
        const auto yMap = [&](double y){
            return y1 - (y - ya) / (yb - ya) * (y1 - y0);
        };

        std::ostringstream o;
        o.precision(3);
        o << std::fixed;
        o << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " << W << ' ' << H
          << "' font-family='system-ui,sans-serif' font-size='11'>";
        // background
        o << "<rect width='100%' height='100%' fill='" << s_.bg << "'/>";
        // title
        o << "<text x='" << s_.padL << "' y='" << s_.padT - 12 << "' fill='" << s_.text
          << "' font-size='13' font-weight='600'>" << esc(title_) << "</text>";

        // grid + axis ticks
        const int gridN = s_.gridLines;
        for (int i = 0; i <= gridN; ++i) {
            const double f = (double)i / gridN;
            const double y = ya + f * (yb - ya);
            const int yp = (int)yMap(y);
            o << "<line x1='" << x0 << "' y1='" << yp << "' x2='" << x1 << "' y2='" << yp
              << "' stroke='" << s_.line << "' stroke-width='1'/>";
            o << "<text x='" << x0 - 6 << "' y='" << yp + 3 << "' fill='" << s_.text
              << "' text-anchor='end'>" << fmtNum(y) << "</text>";
        }
        for (int i = 0; i <= gridN; ++i) {
            const double f = (double)i / gridN;
            double x;
            if (xLog_) {
                const double la = std::log10(std::max(xa, 1e-12));
                const double lb = std::log10(std::max(xb, 1e-12));
                x = std::pow(10.0, la + f * (lb - la));
            } else x = xa + f * (xb - xa);
            const int xp = (int)xMap(x);
            o << "<line x1='" << xp << "' y1='" << y0 << "' x2='" << xp << "' y2='" << y1
              << "' stroke='" << s_.line << "' stroke-width='1'/>";
            o << "<text x='" << xp << "' y='" << y1 + 14 << "' fill='" << s_.text
              << "' text-anchor='middle'>" << fmtNum(x) << "</text>";
        }

        // axis labels
        if (!xLabel_.empty())
            o << "<text x='" << (x0 + x1) / 2 << "' y='" << H - 12 << "' fill='" << s_.text
              << "' text-anchor='middle'>" << esc(xLabel_) << "</text>";
        if (!yLabel_.empty())
            o << "<text x='12' y='" << (y0 + y1) / 2 << "' fill='" << s_.text
              << "' transform='rotate(-90 12 " << (y0 + y1) / 2 << ")' text-anchor='middle'>"
              << esc(yLabel_) << "</text>";

        // series
        for (std::size_t si = 0; si < series_.size(); ++si) {
            const auto& ser = series_[si];
            const std::string col = ser.colour.empty()
                ? (si == 0 ? s_.accent : pickColour(si)) : ser.colour;
            o << "<path d='";
            bool first = true;
            for (std::size_t i = 0; i < ser.ys.size(); ++i) {
                const double x = ser.xs.empty() ? (double)i : ser.xs[i];
                if (x < xa || x > xb) continue;
                const double y = std::clamp(ser.ys[i], ya, yb);
                o << (first ? 'M' : 'L') << ' ' << xMap(x) << ' ' << yMap(y) << ' ';
                first = false;
            }
            o << "' fill='" << (ser.fill ? col : "none") << "' fill-opacity='"
              << (ser.fill ? 0.25 : 0) << "' stroke='" << col
              << "' stroke-width='1.6'/>";
            // legend
            o << "<rect x='" << x1 - 110 << "' y='" << y0 + 6 + (int)si * 16
              << "' width='10' height='10' fill='" << col << "'/>";
            o << "<text x='" << x1 - 96 << "' y='" << y0 + 15 + (int)si * 16
              << "' fill='" << s_.text << "'>" << esc(ser.label) << "</text>";
        }

        // annotations
        for (const auto& a : ann_) {
            const int ax = (int)xMap(a.x), ay = (int)yMap(a.y);
            o << "<circle cx='" << ax << "' cy='" << ay << "' r='3' fill='"
              << s_.accent << "'/>";
            o << "<text x='" << ax + 6 << "' y='" << ay - 6 << "' fill='" << s_.accent
              << "'>" << esc(a.label) << "</text>";
        }

        // notes (bottom-left, before x-label)
        for (std::size_t i = 0; i < notes_.size(); ++i) {
            o << "<text x='" << x0 << "' y='" << y1 + 28 + (int)i * 14
              << "' fill='" << s_.text << "' font-size='10' opacity='0.85'>"
              << esc(notes_[i]) << "</text>";
        }

        o << "</svg>";

        std::ofstream f(path);
        if (!f) return false;
        f << o.str();
        return f.good();
    }

private:
    struct Ann { double x, y; std::string label; };
    std::string         title_;
    std::string         xLabel_, yLabel_;
    double              xa_=0, xb_=0, ya_=0, yb_=0;
    bool                xAuto_=true, yAuto_=true, xLog_=false;
    std::vector<Series> series_;
    std::vector<Ann>    ann_;
    std::vector<std::string> notes_;
    PlotStyle           s_;

    static std::string esc(const std::string& s) {
        std::string r; r.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&':  r += "&amp;"; break;
                case '<':  r += "&lt;";  break;
                case '>':  r += "&gt;";  break;
                case '\'': r += "&apos;"; break;
                case '"':  r += "&quot;"; break;
                default:   r += c;
            }
        }
        return r;
    }
    static std::string fmtNum(double v) {
        char buf[32];
        if (std::fabs(v) >= 1000.0)      std::snprintf(buf, sizeof buf, "%.0f", v);
        else if (std::fabs(v) >= 10.0)   std::snprintf(buf, sizeof buf, "%.1f", v);
        else                              std::snprintf(buf, sizeof buf, "%.2f", v);
        return buf;
    }
    static std::string pickColour(std::size_t i) {
        // distinct colours for additional series (post-accent)
        static const char* pal[] = {
            "#8ab4ff", "#71d99a", "#e9c46a", "#c084fc",
            "#ec4899", "#06b6d4", "#84cc16", "#f97316",
        };
        return pal[i % (sizeof(pal) / sizeof(pal[0]))];
    }
};

// (local copy of binFreq to avoid pulling fft.hpp into the type signature)
inline double binFreqHelper(std::size_t k, std::size_t N, double sampleRate) noexcept {
    return (double)k * sampleRate / (double)N;
}

// Convenience builders.
inline bool writeScope(const std::string& path, const std::string& title,
                        const std::vector<float>& samples, double sampleRate,
                        double tStart = 0.0, double tEnd = 0.0) {
    if (tEnd <= tStart) tEnd = (double)samples.size() / sampleRate;
    const std::size_t a = (std::size_t)(tStart * sampleRate);
    const std::size_t b = std::min(samples.size(), (std::size_t)(tEnd * sampleRate));
    Series sr; sr.label = "out";
    sr.xs.reserve(b - a); sr.ys.reserve(b - a);
    for (std::size_t i = a; i < b; ++i) {
        sr.xs.push_back((double)i / sampleRate * 1000.0);   // ms
        sr.ys.push_back((double)samples[i]);
    }
    SvgPlot p(title);
    p.xLabel("t / ms").yLabel("Amplitude")
     .xRange(tStart * 1000.0, tEnd * 1000.0)
     .yRange(-1.05, 1.05)
     .addSeries(sr);
    return p.write(path);
}

inline bool writeSpectrum(const std::string& path, const std::string& title,
                           const std::vector<double>& magDb,
                           std::size_t fftN, double sampleRate,
                           double fMin = 20.0, double fMax = 20000.0,
                           double dbMin = -120.0, double dbMax = 6.0) {
    Series sr; sr.label = "|X(f)|";
    sr.xs.reserve(magDb.size()); sr.ys.reserve(magDb.size());
    for (std::size_t k = 1; k < magDb.size(); ++k) {
        const double f = binFreqHelper(k, fftN, sampleRate);
        if (f < fMin) continue;
        if (f > fMax) break;
        sr.xs.push_back(f);
        sr.ys.push_back(magDb[k]);
    }
    SvgPlot p(title);
    p.xLabel("Frequency / Hz").yLabel("Magnitude / dBFS")
     .xLog(true).xRange(fMin, fMax).yRange(dbMin, dbMax)
     .addSeries(sr);
    return p.write(path);
}

}} // namespace para3::measure
