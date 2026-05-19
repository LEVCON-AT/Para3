# PARA·3 Code-Audit (LAB-8)

Audit scope: `Para3Engine.hpp`, `wasm-bridge/*` (HTML + JS), `tools/measure/`.

## 1. JS-Bridge / DSP-Shortcut Audit

**Befund: clean.** Keine DSP-Berechnungen in JS, die in die Engine gehören würden.

| Stelle | Kategorie | Status |
|---|---|---|
| `midiOfKey()` (Z 1896-1909) | Keyboard-Layout-Mapping (DOM → MIDI) | ✓ UI-Math, nicht DSP. Octave-Shift wurde explizit zu `setOctave` engine-seitig migriert (E6.2 Kommentar). |
| `dominantHzAutocorr()` (Scope-Selftest) | Analyse only | ✓ Nicht im Audio-Pfad. `Math.log2` für cents-Anzeige, kein Audio-Output. |
| Tempo / Swing / FLUX-Events | Controller-Aufrufe | ✓ Alle durch `c.seqTempo`, `c.seqSwing`, `c.seqFluxNote` → C-API → Engine. Kein Bypass. |
| `c.setParam(pid, norm)` Aufrufe | Trichter | ✓ Alle UI-Knobs/Fader gehen über `setParam` → Engine `setParamNorm` (taper + RampParam smoother + DSP). Single funnel respektiert. |
| Step-Vel / Step-Gate (FLUX-4/5) | Per-Step Mod | ✓ Durch `c.seqStepVel/Gate` → C-API → Engine `editPattern().steps[i].vel/gateLen`. Kein DSP in JS. |
| Scope/E2E-Tap | Read-only Mirror | ✓ ScriptProcessor passt unverändert durch (test-only, hinter Flag). Bit-Identität bei Flag-OFF bestätigt. |

**Empfehlung: keine Änderung.**

## 2. CSS-Style-Dubletten

Eine moderate Dublette wurde gefunden:

**Pattern `linear-gradient(180deg,var(--accent2),var(--accent))`** erscheint 7× als "on-state" Hintergrund:

| Selector | Zeile |
|---|---:|
| `.pslot.active` | 83 |
| `.psave.arm` | 98 |
| `.vgrid button.on` | 206 |
| `.wave button.on` | 213 |
| `.tbtn.on` | 226 |
| `.mbtn.on` | 296 |
| `.wk.dn` | 318 |

**Empfehlung (LAB-10 Polish):**
```css
:root { --accent-grad: linear-gradient(180deg,var(--accent2),var(--accent)); }
/* dann überall durch var(--accent-grad) ersetzen */
```

`color-mix(in srgb,var(--accent) X%,transparent)` erscheint 22× mit variierenden Prozenten — token-basiert, kein Cleanup-Bedarf (Variation ist beabsichtigt).

## 3. Magic Numbers

**Befund: keine produktionskritischen.**

| Klasse | Beispiel | Bewertung |
|---|---|---|
| FFT-/Buffer-Sizes | `fftSize=2048`, `createScriptProcessor(2048,...)` | ✓ Web-Audio-Standard, semantisch |
| Viewport Breakpoints | 1024, 1280, 1680 | ✓ Bereits in `KBD_BREAKPOINTS` als Konstante |
| Test-Timeouts | `setTimeout(r, 200)`, `..., 400)` | ✓ In Test-Code, nicht im Produktions-Pfad |
| Smoother-Tau | `kPortMaxSec=0.5` etc. | ✓ Im Engine als `static constexpr`, `// CALIB(E8)` annotiert |
| Ring-Buffer Capacity | `ringCapacity=1024` | ✓ Default-Argument, dokumentiert in Comment |

**Empfehlung: keine Änderung.**

## 4. Spec-Drift

CLAUDE.md verweist auf `docs/specs/` Dateien. Diese sind beim aktuellen E1-E7 + EXT-ARP + FLUX-1..7 Stand gehalten. Eine **eigene Spec-Datei für FLUX-Serie** (analog zu `ENGINE_E1-E7_HANDOVER.md`) wäre wünschenswert, ist aber kein Code-Defect — als Doku-Aufgabe in LAB-9 als Punkt vorgemerkt.

## 5. Compiler-Warnungen

```
g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o /tmp/e
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp capi_test.cpp -o /tmp/c
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I. tools/measure/measure_main.cpp -o /tmp/measure
```

Alle drei Builds: **0 Warnungen.**

## Zusammenfassung

| Kategorie | Befund | Action |
|---|---|---|
| JS-Bridge DSP-Shortcut | keine gefunden | — |
| Style-Dublette | 1 (linear-gradient on-state, 7×) | LAB-10 Polish: `--accent-grad` Variable |
| Magic Numbers | alle semantisch / dokumentiert | — |
| Spec-Drift | FLUX-Series ohne dedizierte Spec | LAB-9 Doku-Punkt |
| Compiler Warnings | 0 | — |

Produktionsreife auf Code-Ebene: **bestätigt**, mit einem optionalen Style-Polish in LAB-10.
