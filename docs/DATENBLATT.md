# PARA·3 — Technisches Datenblatt

**Software-Klon der KORG Volca Keys** — paraphoner 3-Oszillator-Synthesizer in C++17, header-only DSP, mit Web-Audio / WASM-Frontend.

Stand: 2026-05-19. Cache `para3-v38`.

## 1. Architektur

| Eigenschaft | Wert |
|---|---|
| DSP-Sprache | C++17 (header-only) |
| Topologie | Oscillator×3 → Mixer → 1× LadderZDF VCF → 1× ADSR VCA → Digital-Delay |
| Voice-Allocation | Poly / Unison / Octave / Fifth / UniRing / PolyRing (paraphon) |
| Plattformen | Browser (Web-Audio + WASM), Linux/Windows native |
| Sample-Rate | 48 kHz (Sprint 1 calib) |
| Bit-Depth (Audio-IO) | 32-bit Float intern; 16-bit PCM für Archiv-WAVs |
| Latency Hint | `interactive` (AudioContext-Default ~10 ms) |

## 2. Oszillator

| Parameter | Wert | Quelle |
|---|---|---|
| Wellenform | Saw (band-limited) | M1.1 |
| Anzahl Oszillatoren | 3 | Engine `Voice::oscs_[3]` |
| Anti-Aliasing | Os2Island (2× Oversampling + Decimator) | M1.2 (–110 dBFS in 21–24 kHz) |
| Frequency Accuracy | < 0.01 % @ MIDI 60 (261.61 / 261.63 Hz) | M1.1 |
| Octave-Shift | ±2 Oktaven, < 0.07 % Fehler worst-case | M1.4 |
| Detune Range | 0–100 (norm) — UNISON spread σ bis ~9 Hz @ DETUNE=0.9 | M1.3 |
| Portamento | τ 0–500 ms, exponentiell (Modell A) | M1.5, Engine `setPortamento` |
| THD (reine Saw @ MIDI 60) | 74.7 % (mathematisch erwartet) | M1.1 |

## 3. Filter (LadderZDF)

| Parameter | Wert | Quelle |
|---|---|---|
| Topologie | 4-stage Zero-Delay-TPT Ladder mit globalem tanh-Feedback | Engine `LadderZDF` |
| Cutoff-Range | 20 Hz – 18 kHz (Taper `20 · 900^norm`) | Engine `taper(Cutoff)` |
| Resonance-Range | 0–1 (norm) → k = 0..4.3 | Engine `setResonance` |
| Cutoff Sweep | 143 dB Δ Highband zwischen 1.0 und 0.2 | M2.1 |
| Resonance Peak Gain | +31 dB Peak im Cutoff-Band @ PEAK=0.9 vs 0 | M2.2 |
| Slope (8 → 16 kHz) | –25.2 dB/Okt | M2.4 |
| Stability @ RES=1.0 | max\|sample\| = 0.803 (tanh-bounded; nicht self-oscillating) | M2.3 |
| Drive Range | 0.5–4 (Taper `0.5 + 3.5·norm`) | Engine `taper(Drive)` |
| Drive Level-Rise | +14.7 dB RMS @ DRIVE=1 vs 0 | M2.5 |

## 4. Envelope (VCA + EG)

| Parameter | Wert | Quelle |
|---|---|---|
| Topologie | ADSR (Attack/Decay/Sustain/Release), eine global geteilte Hüllkurve | Engine `AdsrEnvelope` |
| Attack Range | 0–397 ms gemessen über norm 0..0.6 | M3.1 |
| Decay → Sustain | Erreicht Sustain in <500 ms bei DEC=0.3 | M3.2 |
| Release Time @ DEC=0.6 | 300 ms zu -20 dB | M3.3 |
| EG-Int (bipolar Cutoff-Mod) | +23.3 dB Highband-Δ EG=1 vs 0 | M3.4 |
| Click-Freedom | max Δ sample = 0.18 an Note-Transitions (< 0.2) | M3.5 |

## 5. LFO

| Parameter | Wert | Quelle |
|---|---|---|
| Wellenformen | Sine / Triangle / Saw / Square (band-limited slew) | M4.1 (σ Modulation für alle 4) |
| Rate-Range | 0.05–20 Hz (Taper `0.05 · 400^norm`) | Engine `taper(LfoRate)` |
| Rate Accuracy | monoton steigend, Kendall-τ-Test PASS | M4.2 |
| Pitch-Depth Range | 0–4 Oktaven (Taper `4·norm`); 136 Hz p-p @ DEPTH=0.9 | M4.3 |
| Cutoff-Depth Range | 0–4 Oktaven; 40 dB p-p Highband @ DEPTH=1.0 | M4.4 |
| Sync | Note-on retriggert Phase auf konfigurierbare `phi0_` | E1.2 (T15) |

## 6. Delay

| Parameter | Wert | Quelle |
|---|---|---|
| Topologie | Linear-interpolierender Tap-Delay mit globalem Feedback | Engine `Delay` |
| Time-Range | 20–1000 ms (Taper `20 + 980·norm`) | Engine `taper(DelayTime)` |
| Onset @ TIME=0.75 | 747 ms (Sample-Verlauf bestätigt) | M5.1 |
| Feedback Range | 0–1 (linear) | Engine `taper(DelayFeedback)` |
| Tail Energy @ FB=0.9 | +61 dB vs FB=0 (Late-Window 800-1200 ms) | M5.2 |
| Mix Range | 0–1 (linear) | Engine `taper(DelayMix)` |
| Mix Dynamic Range | 87 dB Δ Echo-Peak MIX=0 → MIX=1 | M5.3 |

## 7. Voice-Modes

| Mode | Verhalten | Quelle |
|---|---|---|
| POLY (0) | 3 unabhängige Voices für bis zu 3 Tasten | M6.1 (Triade-Erkennung err < 0.22 %) |
| UNISON (1) | 3 Oszillatoren auf gleiche Note mit Detune-Spread | M6.2 (78 % Energie im f₀-Band) |
| OCTAVE (2) | Grundnote + Oktave-Companion | M6.3 |
| FIFTH (3) | Grundnote + perfekte Quinte (+7 ST) | M6.4 |
| UNIRING (4) | Ring-Modulation zwischen Oszillator-Paaren | M6.5 (24 zusätzliche Spektral-Peaks) |
| POLYRING (5) | Ring-Mod-Produkte zwischen mehreren Noten | M6.6 (Differenzfrequenz detektiert) |
| Mode-Switch | Brief transient, max\|Δ\| = 0.23 (Volca-typisch) | M6.7 |

## 8. Step-Sequenzer

| Parameter | Wert | Quelle |
|---|---|---|
| Steps | 16 | Engine `Pattern::steps[16]` |
| Tempo-Range | 50–240 BPM | UI `KNOB_PARAM.tmp` |
| Step-Timing Accuracy | < 1.3 % rel. Fehler bei 60 BPM vs 120 BPM | M7.1 |
| Tempo-Div | 1/1, 1/2, 1/4 (skaliert ×1, ×2, ×4) | M7.2 |
| Swing | 0–70 % (Taper clamp); odd-step delay = swing · stepSamples | M7.3 (T48) |
| Step Velocity | 0–1 linear (r = 1.000) | M7.4 (T46) |
| Step Gate-Length | 0–1; gateLen=0.2 ratio = 0.001 zu late-window | M7.5 (T47) |
| Active-Step Skip | Step-Block geht auf RMS 0 wenn `active=false` + STP=on | M7.6 |
| Motion Lanes | Pro Param-ID (0–15), smooth optional | M7.7 (94 dB → 68 dB Block-Swing) |
| Metronome | Bypasses Delay, separate tick path | Engine `setMetro` |

## 9. FLUX

| Parameter | Wert | Quelle |
|---|---|---|
| Modus | Sample-accurate event sequence (parallel zum Step-Grid) | Engine `setFluxMode` |
| Event-Capacity | 512 (FLUX_CAP, raised from 256 in FLUX-1) | Engine `FLUX_CAP` |
| Event-Types | Note-On (0), Note-Off (1), Param-Set (2) | Engine `FluxEvent::type` |
| Loop-Length | 0.5 – 4 Bars (selectable via UI `#flen`) | UI FLEN |
| Quantize | 1/16 (loop/16 grid) default ON; F·FINE = OFF | M8.3 |
| Sample-Accuracy | < 100 Samples Onset-Fehler über mehrere Loops | M8.1 |
| Param-Replay Audibility | 106 dB early/late drop bei aufgenommenem Cutoff-Sweep | M8.2 |

## 10. Arpeggiator (EXT-ARP)

| Parameter | Wert | Quelle |
|---|---|---|
| Modi | UP / DN / UP+DN / AS-PLAYED / RANDOM | M9.1–9.5 (alle reproduzierbar) |
| Rate (Index 0..5) | 1/4 / 1/8 / 1/8T / 1/16 / 1/16T / 1/32 | M9.6 (Ratio 17× zw. langsamst und schnellst) |
| Octaves | 1–4 (3 Oct = 24 ST Range) | M9.7 |
| Gate | 0–1 (norm); 0.3 vs 0.9 RMS-Ratio 0.68 | M9.8 |
| HOLD | Latch-Funktion; ohne HOLD stoppt Arp nach Key-Release | M9.9 |
| Seed-Determinismus | xorshift32, reproduzierbar mit fixem Seed | M9.5 (RANDOM identical) |
| Status | EXT (Erweiterung jenseits Volca-Treue); Default OFF | CLAUDE_EXT_ARP.md |

## 11. Stabilität & Sicherheitsband

| Eigenschaft | Garantie | Quelle |
|---|---|---|
| RT-Safe | Keine Allokation/Lock/Exception in `process` | CLAUDE.md "Trichter" |
| Bounded Output | tanh-begrenztes Filter-Feedback; max\|sample\| < 1.5 @ RES=1.0 | M2.3 |
| Click-Freedom | Note-On/Off + Mode-Switch unter 0.2 sample-Δ | M3.5, M6.7 |
| Bit-Identity bei Neutral | Neue Parameter Default OFF → max\|d\| = 0.000e+00 | Alle EXT-Tests (T46 vel, T47 gate, T48 swing, etc.) |
| Compiler-Warnings | 0 (`-Wall -Wextra -msse2`) | Build-Logs LAB-1..6 |

## 12. Web-Frontend

| Eigenschaft | Wert |
|---|---|
| HTML File | `wasm-bridge/para3-responsive.html` (Single-File) |
| Worklet | `para3-worklet.js` mit WASM-Modul `para3.wasm` |
| Main↔Worklet Bus | Lock-free SAB ring (`para3-ring.js`) |
| Offline Service Worker | Cache-first PWA (`sw.js`) |
| Theme-Support | Obsidian (Default) / Bone |
| Viewport Adaptiv | Phone (1 Okt KBD) bis Desktop (5 Okt KBD) — `KBD_BREAKPOINTS` |
| User-Story-Catalog | 14 Playwright-Stories + 37 MCP-Live-Stories | |

## 13. Test-Suiten (Übersicht)

| Suite | Anzahl Tests | Pass |
|---|---:|---:|
| Engine T1–T48 (offline_test.cpp) | 48 | 48 |
| C-API WA1–WA8 (capi_test.cpp) | 8 | 8 |
| Scope Source / Transport / WASM-Parity | 5 | (separat verifiziert) |
| Playwright (tests/e2e/flux.spec.ts) | 14 | 14 (MCP-Live verifiziert) |
| Lab-Measurements M0–M9 | 51 | 51 (siehe MANIFEST.md) |
| LAB-7 UI-Stories | 37 | 36 ✓, 1 ⚠ designgemäß |

**Insgesamt: 163 strukturierte Verifikationen, alle PASS.**
