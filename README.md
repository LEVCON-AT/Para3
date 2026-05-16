# PARA·3 — Paket

Sound-genauer, produktionsreifer Software-Synth (Volca-Keys-Klasse),
header-only C++ DSP + verifizierte WASM/AudioWorklet-Brücke. Offline am Handy
als installierbare PWA spielbar.

**Live:** https://para3.levcon.at/
**Doku (lang):** https://para3.levcon.at/documentation
**Repo:** https://github.com/LEVCON-AT/Para3

## Schnellüberblick

- **DSP-Engine** (`Para3Engine.hpp`, header-only C++17): drei bandbegrenzte
  Oszillatoren (4× PolyBLEP → 383-Tap-Kaiser), ZDF-Ladder-Filter +
  tanh-NL (2×-Insel), ADSR/VCA, fraktionales Delay, LFO (4 Shapes),
  Ringmod (8×-Insel), 16-Step-Sequencer + Motion-Aufnahme.
- **C-API-Brücke** (`wasm-bridge/para3_capi.*`): RT-sichere `extern "C"`
  Oberfläche; nativ und in WASM bit-identisch (Parity-Test).
- **AudioWorklet-Brücke**: lock-freier SPSC-Ring über SharedArrayBuffer
  (Desktop, COOP/COEP) bzw. postMessage (Offline-PWA).
- **PWA-Hülle**: Manifest + Service-Worker, installierbar; läuft offline.
- **Read-only Oszilloskop** über AnalyserNode am realen Audio-Graph —
  Stille = flache Linie, kein synthetischer Ersatz.

## Für Claude Code: HIER STARTEN

**Lies und befolge `CLAUDE_BUILD.md`** — das ist das verbindliche
Ausführungs-Runbook (Phasen, messbare Abnahmekriterien, „messen statt
behaupten"-Disziplin). Ziel des Auftraggebers: **PARA·3 offline auf dem
Handy spielbar** (PWA).

## Ergebnisse (Stand grün)

Alle Test-Suites bestanden, 0 Compilerwarnungen, kein `-ffast-math`:

| Suite | Beweist | Ergebnis |
|---|---|---|
| `offline_test` | Engine: Anti-Aliasing −74 dBc, klickfrei, zipper-frei, Sequencer-Jitter 0, Motion-Roundtrip 0.0000, lock-freies Pattern-Edit | **13/13 PASS** |
| `capi_test` | 128-Quanten == ein Call bit-identisch, Trichter durch C-Grenze, 10 s Stress | **PASS** (WA1–WA5) |
| `ring_test` | FIFO ohne Verlust/Duplikat, Double-Roundtrip bit-exakt, realer Cross-Thread-Worker | **PASS** |
| `audio_test` | UI → Ring → Worklet-Decode bit-exakt; Backpressure ohne Block | **PASS** (76-Op + Stress) |
| `wasm_parity` | WASM-Output == nativer C-API-Output, Sample für Sample | **max \|Δ\| = 0** (bit-identisch) |
| `scope_source_test` | Stille = 0; NoteOn 69 → 440.000 Hz auf dem Engine-Puffer | **PASS** (0 Cent Fehler) |

## Inhalt

- `Para3Engine.hpp` — DSP-Engine (header-only, 13/13 Messungen).
- `offline_test.cpp` — Engine-Messsuite (Beweis der Klangqualität).
- `README_STATUS.md` — Stand & Messergebnisse der Engine.
- `wasm-bridge/` — C-API, lock-freier Ring, AudioWorklet, Build-Skript,
  Server, Steuer-Glue, Tests, Integrationsanleitung, PWA-Hülle (Manifest +
  Service-Worker), das gestaltete UI (`para3-responsive.html`), Brand-Assets.
- `brand/` — SVG-Logo, Marke, Maskable-Icon, Preview-Seite.
- `wasm-bridge/documentation.html` — die ausführliche Entstehungs-Doku
  (gleicher Inhalt wie `/documentation` live).
- `CLAUDE_BUILD.md` — **das Build-Runbook. Zuerst lesen.**
- `CLAUDE_INTEGRATE_UI.md` — den gestalteten Entwurf
  (`wasm-bridge/para3-responsive.html`) an die Engine binden. Nach Phase
  A/B lesen.
- `CLAUDE_SCOPE_AND_LAYOUT.md` — Mobile-Layout vollständig erreichbar
  machen + den Oszilloskop als **echtes, gemessenes** Signal (kein Fake).
  Nach der UI-Anbindung lesen.

## Reihenfolge

1. **Sanity-Tests** aus `CLAUDE_BUILD.md` (müssen grün sein, 0 Warnungen).
2. **Phase A**: WASM bauen + Gleichwertigkeit messen
   (`wasm_parity.mjs` ≤ 1e-6).
3. **Phase B**: Offline-Handy (SAB-freier Steuerpfad + PWA) — das Kernziel.
4. **Phase C**: optional, Sprint-1-Hardware-Kalibrierung (nur für
   1:1-Klangkopie eines bestimmten Volca-Keys-Geräts).
5. **UI-Bindung** nach `CLAUDE_INTEGRATE_UI.md`.
6. **Layout-Fit + echter Oszilloskop** nach `CLAUDE_SCOPE_AND_LAYOUT.md`.

## Ausführliche Entstehungs-Dokumentation

Eine längere, eigenständig lesbare Doku zur Methodik („messen statt
behaupten"), den verwendeten Werkzeugen, den Ergebnissen pro Suite, den
ehrlichen Grenzen und einer Schritt-für-Schritt-Reproduktion lebt unter
**[`wasm-bridge/documentation.html`](wasm-bridge/documentation.html)** —
serverseitig erreichbar als **https://para3.levcon.at/documentation**.

## Selbst reproduzieren

```bash
git clone https://github.com/LEVCON-AT/Para3
cd Para3

# 1 — Sanity (muss grün sein, 0 Warnungen)
g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o /tmp/e && /tmp/e
cd wasm-bridge
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp capi_test.cpp -o /tmp/c && /tmp/c
node ring_test.mjs
node audio_test.mjs
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp scope_source_test.cpp -o /tmp/s && /tmp/s

# 2 — WASM bauen (emsdk aktiv)
bash build_wasm.sh ..

# 3 — Parity: WASM == native
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp parity_native.cpp -o /tmp/p && /tmp/p > parity_native.f32
node wasm_parity.mjs

# 4 — Lokal servieren (COOP/COEP)
node serve.mjs . 8080
```

Ehrliche Grenzen stehen in `CLAUDE_BUILD.md` und `README_STATUS.md` —
nichts hier ist gefakt; jede Qualitätsaussage hat einen ausführbaren Test.
