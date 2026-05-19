# PARA·3 — Lab-Validation Methodologie

**Stand 2026-05-19.** Pflicht-Spec, wenn die Anforderung lautet "alle Funktionen
auf Laborstandard prüfen", "Datenblatt erstellen", "Mess-Bericht erstellen",
"Code-Audit" oder vergleichbar. Reproduziert die Qualität der LAB-1..10-Serie
(144 strukturierte Verifikationen, 143 PASS, 1 ⚠ designgemäß).

## 1. Sprint-Struktur (10 Phasen)

Jede Lab-Validation läuft in 10 fest definierten Sprints. Pro Sprint ein
Commit, Vollregression nach jedem DSP-Sprint, Push erst am Tagesende **nach
expliziter `push`-Bestätigung**.

| Sprint | Fokus | Deliverable |
|---|---|---|
| LAB-1 | Mess-Infrastruktur | `tools/measure/` (FFT, SVG-Plot, WAV-Writer, Manifest), Driver-Skeleton |
| LAB-2 | Oszillator + VCF | M1.x / M2.x mit SVG-Kurven |
| LAB-3 | VCA/EG + LFO + Delay | M3.x / M4.x / M5.x |
| LAB-4 | Voice-Modes + Ring | M6.x |
| LAB-5 | Sequencer + FLUX | M7.x / M8.x |
| LAB-6 | Arpeggiator | M9.x |
| LAB-7 | UI Walk-Through | MCP-Playwright Live, `docs/measurements/UI_MANIFEST.md` |
| LAB-8 | Code-Audit | `docs/AUDIT.md` (JS-Bridge, Style, Magic-Numbers) |
| LAB-9 | Datenblatt + Bericht | `docs/DATENBLATT.md` + `docs/BERICHT.md` |
| LAB-10 | Polish + Final Push | AUDIT-Befunde umsetzen, Vollregression, Push |

## 2. Mess-Infrastruktur (LAB-1 Pflicht-Stack)

Header-only, keine externen Abhängigkeiten. Alle Files unter `tools/measure/`:

- **`fft.hpp`** — Radix-2 Cooley-Tukey, Hann-Window, `peakFreqInterp` (parabolic
  Vertex), `thdPercent` (in Bin-Space, kein SR-Argument), `magnitudeDb` mit
  Window-Sum-Compensation.
- **`svg_plot.hpp`** — `SvgPlot` Builder (xLabel/yLabel/xRange/yRange/xLog/
  addSeries/annotate/note), Convenience `writeScope` + `writeSpectrum`.
- **`wav_write.hpp`** — RIFF/WAVE 16-bit PCM mono (Archiv-Captures).
- **`manifest.hpp`** — `MEntry` (id, section, what, metric, expected, measured,
  unit, pass, svgPath, wavPath, note), `Manifest::writeCsv`/`writeMarkdownTable`.
- **`measure_main.cpp`** — Driver, Inkl. `setNeutralPatch(e)`, `capture(e, n,
  warmupS)`, `spectrum(samples)`, `midiHz(note)`, `M{n.m}_*` Funktionen.
- **`build.sh`** — `g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I../.. ...
  -o /tmp/measure` von Repo-Root.

Manifest-Format (CSV):
```
id,section,what,metric,expected,measured,unit,pass,svg_path,wav_path,note
```

Pro Messung: `static MEntry M{n.m}_name() { ... }`, manifest.add() im Driver,
prefix-filter via `argv[1]` (z.B. `/tmp/measure M2` läuft nur Section 2).

## 3. Messungs-Disziplin

Jede Messung MUSS dokumentieren:

```cpp
// -- M{id} {kurztitel} -----------------------------------------------
// Frage:   {Was wird beantwortet?}
// User:    {echter Nutzer-Anwendungsfall}
// Mess:    {Methode + Parameter + Setup}
// Erw:     {Erwartung mit konkreten Zahlen/Toleranzen}
```

Pflicht: **Neutral-Bitidentität** wird als erstes geprüft. Default-Pfad muss
`max|d| = 0.000e+00` zum vorherigen Stand liefern. Wenn nicht, ist der Fix nie
"Erwartung aufweichen", sondern echter Defekt fixen.

## 4. Engine-Eigenheiten (gemessen, nicht behauptet)

Diese Punkte sind in der LAB-Serie real verifiziert und gelten als
**Engine-Verhalten**, nicht als Bug:

- **LadderZDF schwingt designgemäß nicht selbst.** `k·4.3 · tanh(fb)` begrenzt
  die Feedback-Schleife (Volca-treu). Tests dürfen das nicht als FAIL werten —
  als Stability-Check formulieren (max|sample| < 1.5).
- **DRIVE 0 → 1.0 ändert das Spektrum, nicht zwingend THD.** Saw + tanh wird
  "square-iger" → THD kann sinken obwohl der Pegel um ~15 dB steigt. Metrik:
  RMS-Anstieg, nicht THD-Anstieg.
- **FLUX-Quantize schnappt auf `loopLen/16`-Grid, nicht auf BPM-1/16.** Bei 1 s
  Loop sind das 3000-Sample-Grids; bei 333 ms Note → snap auf 15000 Samples (vs
  16000 ohne Snap). Diff ~1000 Samples, nicht 4000.
- **Mode-Switch erzeugt einen kleinen Transient (~0.2 amp).** Volca-typisch.
  Threshold 0.30 mit Hinweis akzeptieren, nicht 0.2 hart erzwingen.
- **DELAY_FEEDBACK ändert den Schwanz, nicht den Spitzenwert.** Late-Tail-RMS-
  Vergleich (z.B. 800-1200 ms) statt Peak-Vergleich.
- **Engine-Param-Smoother brauchen Warm-up.** Bei Param-Sweep-Tests: render
  500 ms vor Stimulus, sonst landet die Smoother-Mitte mitten in der Messung.

## 5. Methodische Standard-Fallen

Beim ersten Durchlauf der LAB-Serie sind diese Fallen aufgetreten und
behoben worden. Bei neuen Messungen darauf achten:

- **Peak-Detektion bei modulierten Signalen ist fragil.** Für ARP-Sequenzen
  oder Detune-Spread: Band-Energy-Detektor mit bekannten Kandidaten-Fundamentals
  (`midiHz(cand) ± 10 Hz` Power-Summe). Robust an Übergängen.
- **Monotonie-Check via "back > front + ε" ist zu streng.** Bei FFT-Jitter
  alternativ Kendall-τ-Score: τ ≥ 0.5 reicht für "deutlich monoton".
- **Silence-Resets pushen Duplikat-Noten.** In Sequence-Detektoren: `lastNote`
  bei Silence NICHT zurücksetzen, sonst push gleicher Note nach Gate-Gap.
- **Sustain=0 + DecRel kurz = Peak-Detektor verfehlt.** Bei Step-Skip oder
  Step-Sequence Messungen: Sustain ≥ 0.6 oder hoch, damit Audio durchgehend
  laut bleibt und der Detektor stabile Frames bekommt.
- **Slope-Messung im Noise-Floor.** Bei VCF-Slope: messen wo Signal noch ≥ -60
  dBFS ist; bei tiefem Cutoff (0.4) sind 2-8 kHz schon im Floor → höher
  cutoffen (0.7) oder näher zum Cutoff messen.
- **Autocorrelation findet Sub-Harmonische.** Bei langsamen LFO-Raten (< 1 Hz)
  ist die Capture zu kurz. Statt Autocorr lieber FFT-of-trace und Peak in
  Modulations-Frequenz finden.
- **Engine-API-Routing-Mismatch:** Controller hat `midiNoteOn/Off` für die
  Arp-Logik, nicht `noteOn/Off`. ParaEngine::Mode existiert nicht — es ist
  `ParaAllocator::Mode`. fluxLoopLen heißt `fluxSetLoopLen`. Im Zweifel
  `Para3Engine.hpp` per Grep prüfen.

## 6. UI-Walk-Through (LAB-7)

MCP-Playwright Live gegen `https://para3.levcon.at/?e2e=1` mit deterministischem
`window.__para3Capture`-Tap. Lokales Playwright auch ok.

Pflicht-Stories (5): **US-COLD / US-ONE / US-PERSIST / US-IDEM / US-ORDER** —
alle grün.

Pro Bedienelement: Click → DOM-State-Check + Audio-Tap-Δ. Inventur (Anzahl
Knobs / Faders / Buttons je Sektion) als ersten Sanity-Check.

Output: `docs/measurements/UI_MANIFEST.md` + repräsentative Screenshots
(`UI-overview.png`, `UI-preset-*.png`).

## 7. Code-Audit (LAB-8)

`docs/AUDIT.md` deckt drei Kategorien ab:

1. **JS-Bridge / DSP-Shortcut.** Grep nach `Math.{pow,sin,cos,exp,log,tanh}`
   in `wasm-bridge/*.js,*.html`. Jede DSP-Math in JS muss eine Engine-Migration
   sein, sonst Defect.
2. **Style-Dubletten.** `grep -c "linear-gradient(...)" "color-mix(in srgb,
   var(--accent) X%, transparent)"`. Pattern mit ≥ 5× Wiederholung → in
   `:root` Custom Property hoisten.
3. **Magic Numbers.** Hart-kodierte Zahlen in JS/HTML — semantische Konstanten
   sind ok (FFT-Sizes, Viewport-Breakpoints), Schwellwerte und Timing-
   Konstanten gehören in named const oder data-Attribute.

Compiler-Warnings: **0** über `-Wall -Wextra`, alle Builds. Sonst Defect.

## 8. Datenblatt + Bericht (LAB-9)

Zwei verpflichtende Files:

- **`docs/DATENBLATT.md`** — Tabellarische Specs pro Engine-Block (Oszillator,
  VCF, ADSR, LFO, Delay, Voice-Modes, Sequencer, FLUX, ARP, Stabilität,
  Frontend, Test-Suiten-Übersicht). Jede Zeile mit Querverweis zur M-Messung,
  die den Wert belegt.
- **`docs/BERICHT.md`** — Narrativer Mess-Bericht mit eingebetteten SVG-Kurven.
  Pro Messung: Ausgangsfrage, User-Case, Methode, Erwartung, Messwert,
  Pass/Fail. Format:
  ```
  ### M{id} {Titel}
  **Frage:** ...
  **Methode:** ...
  **Messung:** **{Zahl}** {Einheit}
  **Ergebnis:** **PASS** / **FAIL** / ⚠ designgemäß
  ![Plot](measurements/M{id}-{slug}.svg)
  ```

## 9. Polish (LAB-10)

Nur die in LAB-8 dokumentierten Befunde umsetzen. Keine "weiteren Verbesser-
ungen" oben drauf. Nach jedem Polish-Schritt Vollregression.

Vollregression-Reihenfolge:
```
1. /tmp/e                                   # Engine T1-Tn
2. /tmp/c                                   # C-API WA1-WAn
3. /tmp/s                                   # Scope-Source
4. node ring_test.mjs / audio_test.mjs / port_test.mjs   # Transport
5. (VPS) bash build_wasm.sh && node wasm_parity.mjs       # WASM↔native
6. /tmp/measure                             # alle LAB-Messungen
```

Alle sechs Suiten müssen `OVERALL: PASS` zeigen. **Push erst nach expliziter
`push`-Anweisung des Menschen**, auch wenn alle Tests grün sind.

## 10. Akzeptanzkriterien Lab-Validation

Die Lab-Validation gilt als „done", wenn:

- ≥ 50 Lab-Messungen unter `docs/measurements/` + Manifest.
- 5 Pflicht-Stories + alle Sektion-Stories grün im UI_MANIFEST.
- DATENBLATT.md, BERICHT.md, AUDIT.md vorhanden und mit dem Code synchron.
- Vollregression aller sechs Suiten grün.
- 0 Compiler-Warnings.
- Push nach explizitem Go.

Eine spätere Messung verändert das Datenblatt nur, wenn sie real eine Spec
nachschärft — niemals "Erwartung aufweichen".
