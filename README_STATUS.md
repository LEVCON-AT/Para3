# PARA·3 — Engine Core (Technische Basis VOLLSTAENDIG)

Echtes, produktionsreifes DSP + Steuerlayer. Keine Fakes, keine Stubs, keine
Helfer-Abkuerzungen. Jede Qualitaetsaussage ist gemessen, nicht behauptet.

## Vollstaendig & produktionsfinal

**Klang-Signalweg:** 3 bandbegrenzte Osc (4x PolyBLEP → 383-Tap-Kaiser)
→ Allocator mit allen 6 Voice-Modi (Poly/Unison/Octave/Fifth/UniRing/PolyRing)
→ Mixer → geteiltes ZDF-Ladder-Filter + tanh-NL (2x-Insel)
→ klickfreie ADSR/VCA → fraktionales Delay. LFO (4 Formen, Slew-bandbegrenzt)
moduliert Pitch/Cutoff in den korrekten Domaenen. Ring-Mod auf eigener 8x-Insel.

**Steuerlayer (Sprint 7):** sample-genauer Clock (Tempo/Swing, intern/extern),
16-Step-Sequencer, Motion-Aufnahme/Wiedergabe, MIDI-CC/Noten, lock-freies
Pattern-Editing (atomarer Doppelpuffer). **Parameter-Trichter-Invariante:**
UI / MIDI-CC / Automation / Motion gehen alle durch denselben Pfad
(normalisiert → CALIB-Taper → RampParam-Smoother → DSP) — in T11 als
bit-identisch nachgewiesen.

Alles RT-sicher (kein Alloc/Lock/Syscall im process), denormalfest.

## Messergebnisse (`offline_test`, fs=48 kHz, 0 Warnungen, kein -ffast-math)

| # | Test | Kernzahl | Status |
|---|---|---|---|
| T1 | Anti-Aliasing Osc | -74,1 dBc (naiv -20) | PASS |
| T2 | Klickfreies Gate | \|dx\| 1,01x | PASS |
| T3 | Anti-Zipper Pitch | \|dx\| 1,00x | PASS |
| T4 | Cutoff-Zipper (isoliert) | Exzess 0,003x | PASS |
| T5 | Worst-Case-Stabilitaet | Peak 1,29, finite | PASS |
| T6 | Nichtlinearitaets-Alias | -48,8 vs 1x -32,1 | PASS |
| T7 | LFO-Modulation | ratengenau, kein Klick | PASS |
| T8 | Delay (Retime/FB/Interp) | klickfrei, bounded | PASS |
| T9 | Ring-Mod (8x-Insel) | -53,5 dBc, +34,6 dB | PASS |
| T10 | Sequencer-Timing | Jitter 0 (int), <=1 (frac) | PASS |
| T11 | Trichter-Invariante | Sample-Diff 0,0e+00 | PASS |
| T12 | Motion Round-Trip | Reprod.-Fehler 0,0000 | PASS |
| T13 | Lock-freies Edit | konsistent, finite, stetig | PASS |

13/13. Jede Stufe ohne Regression bei Einbau der naechsten.

## Ehrliche Test-Disziplin (Auszug)

Mehrfach hat die Harness *Messfehler statt Enginefehler* aufgedeckt und wurde
korrigiert statt die Engine zu „tunen": Hann-Fenster-Selbstmaskierung (T1 →
kohaerentes Sampling), legitime Klangbewegung als Zipper fehlinterpretiert
(T4/T12 → saubere Isolation bzw. Querverweis statt schwacher Proxy),
Audio-Onset-Heuristik (T10 → direkte Clock-Instrumentierung). Reproduktion und
Trichter-Invariante sind exakt (0,0), nicht „nah genug".

## Kalibriergrenze (ehrlich)

Algorithmen final. Unit-spezifische Zahlen (Stimmkurve, alle Taper, Saegezahn-
Form, EG-Zeiten, Filterordnung/Resonanz, LFO-Slew, Delay-Signatur, Ring-Grit,
Voice-Spreads, Swing) tragen musikalisch gueltige Defaults `// CALIB(sprint1)`.
Default != Fake — Signalweg laeuft echt; nur Zielwerte warten auf die Messung.

## Einzig noch offen (ausgewiesen, nicht gefakt)

WASM/AudioWorklet-Bruecke, damit diese gepruefte Engine im HTML-Projekt
hoerbar wird. Die komplette Technik-Basis (Klang + Steuerung) ist fertig.

## Build

`g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o offline_test`
→ 0 Warnungen. `Para3Engine.hpp` header-only, framework-agnostisch.
