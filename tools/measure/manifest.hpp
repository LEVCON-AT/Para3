// =============================================================================
//  PARA-3 :: Mess-Tooling — CSV/Markdown Manifest-Writer (header-only)
//
//  Schreibt eine Zeile pro Messung in
//    docs/measurements/MANIFEST.csv
//
//  Spaltenformat:
//    id,section,what,metric,expected,measured,unit,pass,svg_path,wav_path,note
//
//  Felder mit Komma werden in Anführungszeichen geschrieben. Markdown-Variante
//  rendert dieselben Zeilen als Tabelle.
// =============================================================================
#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace para3 { namespace measure {

struct MEntry {
    std::string id;        // "M1.1"
    std::string section;   // "Oscillator"
    std::string what;      // human description
    std::string metric;    // "fundamental_freq"
    std::string expected;  // "261.63"
    std::string measured;  // "261.62"
    std::string unit;      // "Hz"
    bool        pass = true;
    std::string svgPath;   // relative to repo root
    std::string wavPath;
    std::string note;
};

class Manifest {
public:
    void add(MEntry e) { rows_.push_back(std::move(e)); }
    std::size_t size() const { return rows_.size(); }
    std::size_t failures() const {
        std::size_t n = 0; for (const auto& r : rows_) if (!r.pass) ++n; return n;
    }

    bool writeCsv(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;
        f << "id,section,what,metric,expected,measured,unit,pass,svg_path,wav_path,note\n";
        for (const auto& r : rows_) {
            f << esc(r.id) << ',' << esc(r.section) << ',' << esc(r.what) << ','
              << esc(r.metric) << ',' << esc(r.expected) << ',' << esc(r.measured)
              << ',' << esc(r.unit) << ',' << (r.pass ? "PASS" : "FAIL") << ','
              << esc(r.svgPath) << ',' << esc(r.wavPath) << ',' << esc(r.note) << '\n';
        }
        return f.good();
    }

    bool writeMarkdownTable(const std::string& path,
                             const std::string& title = "PARA-3 Mess-Manifest") const {
        std::ofstream f(path);
        if (!f) return false;
        f << "# " << title << "\n\n";
        f << "| ID | Section | What | Metric | Expected | Measured | Unit | Pass | Curve |\n";
        f << "|---|---|---|---|---:|---:|---|---|---|\n";
        for (const auto& r : rows_) {
            f << "| " << r.id << " | " << r.section << " | " << r.what << " | "
              << r.metric << " | " << r.expected << " | " << r.measured << " | "
              << r.unit << " | " << (r.pass ? "PASS" : "**FAIL**") << " | ";
            if (!r.svgPath.empty()) f << "[svg](../" << r.svgPath << ")";
            f << " |\n";
        }
        f << "\nTotal: " << rows_.size() << " — Failures: " << failures() << "\n";
        return f.good();
    }

private:
    std::vector<MEntry> rows_;
    static std::string esc(const std::string& s) {
        if (s.find(',') == std::string::npos && s.find('"') == std::string::npos)
            return s;
        std::string r = "\"";
        for (char c : s) { if (c == '"') r += '"'; r += c; }
        r += '"'; return r;
    }
};

}} // namespace para3::measure
