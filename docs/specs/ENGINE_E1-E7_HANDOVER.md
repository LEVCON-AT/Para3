> ⚠ **HISTORISCHES DOKUMENT — nur E1–E7-Meilenstein.** Das Repo ist
> inzwischen deutlich weiter (EXT-ARP, EXT-FLUX u. a., ~30 Commits über
> E1–E7). Dies ist **nicht** der aktuelle Stand und die `engine-delivery/`-
> Dateien dürfen **NICHT** über das evolvierte Repo gelegt werden — das
> wäre ein Rückschritt. Source of Truth ist der aktuelle Repo-Stand.

# PARA·3 — Engine-Agent: KORG-Parity-Sprints E1–E7 — Übergabe

**Status (historisch): Meilenstein E1–E7 war fertig, gemessen, grün.**
Native Endregression *zum E1–E7-Zeitpunkt*: `offline_test` T1–T26,
`capi_test` WA1–WA6 (heute deutlich mehr — die aktuelle Repo-Suite zählt),
`scope_source_test`, `ring_test`, `audio_test`, `port_test` → **alle PASS,
0 Fehler, 0 Compilerwarnungen, kein `-ffast-math`.**

Dies ist die Engine-Seite. Frontend-Deltas + gemeinsamer E2E-Gate sind Claude
Codes Aufgabe (siehe `CLAUDE_FRONTEND_PARITY.md` / `CLAUDE_PARITY_E2E_GATE.md`
aus dem Spec-Paket) — diese Engine erfüllt deren Engine-seitige Voraussetzungen.

---

## 1. Was geliefert wird

```
changed/
  Para3Engine.hpp                 (+350 Zeilen: E1–E6 DSP, in-place)
  offline_test.cpp                (+883: Messblöcke T14–T26)
  wasm-bridge/para3_capi.h        (+30: neue IDs/Funktionen)
  wasm-bridge/para3_capi.cpp      (+33: Routing/Funktionen)
  wasm-bridge/capi_test.cpp       (+64: WA6 Voll-Sweep)
  wasm-bridge/build_wasm.sh       (+2: alle neuen Exporte)
  *.patch                         (unified diff Original→Endstand, auditierbar)
```

**Nicht angefasst** (verifiziert IDENTICAL gegen den hochgeladenen Stand):
`para3-ring.js`, `para3-port.js`, `para3-worklet.js`, `para3-audio.js`,
`scope_source_test.cpp`, `parity_native.cpp`, `parity_seq.h`, `sw.js`,
`manifest.webmanifest`, alle HTML/Brand. Kein Refactor, keine neue Engine-.cpp,
header-only — exakt nach Anhang A der DSP-Spec.

## 2. Wie anwenden

HISTORISCH (galt nur beim ursprünglichen E1–E7-Handover; HEUTE NICHT
MEHR ANWENDEN — das Repo ist weiter): Die 6 Dateien waren gezielt zu
übernehmen (das waren genau die
Dateien, die der Engine-Agent ändern darf). **Nicht** den ganzen Repo
überschreiben — Claude Codes Transport/Scope/PWA-Arbeit bleibt unberührt
(o.g. Dateien sind bitidentisch geblieben). Danach bauen wie gehabt:

```
g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o offline_test && ./offline_test
cd wasm-bridge && g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp capi_test.cpp -o capi_test && ./capi_test
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp scope_source_test.cpp -o scope && ./scope
node ring_test.mjs && node audio_test.mjs && node port_test.mjs
bash wasm-bridge/build_wasm.sh        # emscripten — auf deiner Seite
node wasm-bridge/wasm_parity.mjs       # WASM↔nativ-Δ — auf deiner Seite
```

**Ehrlicher Vorbehalt:** `wasm_parity.mjs` konnte ich nicht ausführen — in
meiner Sandbox ist kein `emcc`. Die native Suite (`parity_native.cpp` deckt
dieselbe Engine-Logik) ist vollständig grün. `build_wasm.sh` exportiert jetzt
**alle** neuen Symbole; das WASM↔nativ-Δ musst du beim Build verifizieren.

## 3. Sprints + Messung (alles gemessen, nichts behauptet)

Stärkster Anti-Blender-Beweis durchgängig: **Neutral-/Aus-Stellung ist
bitidentisch** zur unangetasteten Engine (`max|d| = 0.000e+00`).

| Sprint | Inhalt | Schlüsselmessung |
|---|---|---|
| **E1.1** EG INT | EG→Cutoff, bipolar, Log-Domain, geteilte EG nur gelesen | centroid~eg corr **+0.999 / −0.985**; neutral **0.000e+00**; kein Spike |
| **E1.2** LFO Sync | globaler LFO (Anhang D.1), Phase exakt resettet | Reproduzierbarkeit ON **+0.997** vs OFF **−0.560**; Onset-\|dx\| ~1.0× (kein Klick) |
| **E2.1** DETUNE | symmetrischer VCO-Spread, pro Sample geglättet | neutral **0.000e+00**; Schwebung **2.93→4.39→8.79 Hz**; Alias hochverstimmt **−76.1 dBc** |
| **E2.2** PORTAMENTO | Modell-A Ein-Pol-Glide; aus = Originalpfad exakt | neutral **0.000e+00**; 63%-Zeit **0.171 / 0.200 s**; Fast-Glide aliasfrei |
| **E3** Motion full | Multi-Lane je Param-ID, SMOOTH, 1-Loop-Auto-Off, PEAK-Verweigerung | Roundtrip **0.0000**; PEAK rejects **0→2**; SMOOTH off/on **0.0/2658 Hz**; Auto-Off **ja**; Multi-Lane **0.0000** |
| **E4.1** Step Trigger | EG-Retrigger je Step (Note gehalten) | EG-Onsets off/on **2 / 31** |
| **E4.2** Tempo-Div | 1/1, 1/2, 1/4 | Schritt-Samples **6000/12000/24000**, Jitter **0** |
| **E4.3** Active Step | deaktivierter Step strukturell übersprungen | RMS aktiv/inaktiv **2.68e-2 / 0.000e+00**; Sequencer zählt weiter 16 |
| **E4.4** Metronom | bandbegrenzter Tick, Delay-Bypass | Tick lo/hi **107 / 0.066**; Onset/Offset **~0**; Delay-Tail off/on **4.04e-2 / 0.000e+00** |
| **E5** FLUX | sample-genaue Event-Sequenz | Jitter **0** (Loop exakt); OFF-vor-ON **ja**; leer = Stille; Loop-Wrap klickfrei; Overflow-Drop beobachtbar (**144**) |
| **E6.1** Volume | Master-Gain post-VCA | unity **0.000e+00**; 0.5 → **−6.021 dB**; 0 = stumm |
| **E6.2** Octave | Semitone-Shift in noteOn/Off | oct0 **0.000e+00**; f(+1)/f(0) **1.9778**; Alias +2 **−87.6 dBc** |
| **E6.3** Fixed Vel | deterministische Velocity (kVelFixed) | Peaks identisch, Spread **0.00e+00** |
| **E7** Integration | alle C-API-Exporte, Voll-Sweep | WA6 PASS: finite, rejects≥2, dropped>0 |

## 4. CALIB(E8) — Platzhalter (Algorithmus voll implementiert, NUR die Zahl offen)

E8 (Hardware-Kalibrierung gegen die echte Volca) ist **dein** Job, nicht in
Software „nach Gehör". Die Konstanten sind klar beschriftet:

| Konstante | Default | Bedeutung |
|---|---|---|
| `kEgIntOctMax` | 2.0 | EG-INT max Cutoff-Hub (Oktaven) |
| `kEgPivot` | 0.0 | EG-Wert bei Null-Hub |
| `Lfo::phi0_` | 0.0 | LFO-Trigger-Startphase (je Form, `setSyncStartPhase`) |
| `kDetuneCentsMax` | 50.0 | DETUNE max Halb-Spread (Cents) |
| `kPortMaxSec` | 0.5 | PORTAMENTO max Glide (s) |
| `Param::Volume` Taper | linear `n` | Volume-Kurve |
| `kVelFixed` | 1.0 | feste Velocity |
| `MetroTick` k* | 1000/1500 Hz, 0.5/6.0 ms | Click/Accent-Freq, Onset/Decay |

Vorgehen E8 siehe Anhang C der DSP-Spec (Messkette zuerst validieren,
MIDI-CC-Wiederholbarkeit, ≥3 Takes, Unsicherheit, A/B-Δ).

## 5. Bewusst benannte Treue-Konflikte (nicht stillgeglättet)

- **E1.2 LFO-Sync:** die Phase wird **exakt** auf `phi0_` zurückgesetzt
  (Timing originalgetreu — die Volca springt hart). Die Wert-Stetigkeit trägt
  der LFO-**eigene**, schon im Original vorhandene Kantenglätter (`slew_`),
  der ohnehin jede Wellenform-Transition bandbegrenzt — **kein** neu
  drangeklebter Glätter, kein harter Klick. Gemessen in T15.
- **E2.2 Portamento aus:** kein Ersatzpfad — exakt der Original-Pitchpfad
  (`refresh()` unverändert), bitidentisch belegt.
- **E4.4 Metronom:** Delay wird bei Metronom umgangen (Volca-Delay dann
  unbrauchbar), aber `delay_.process` läuft weiter → Zustand stetig, **kein**
  Klick beim Umschalten. Gemessen in T22.

## 6. Neue C-API-Oberfläche (alle in build_wasm.sh exportiert)

Param-IDs: `PARA3_P_EG_CUT_DEPTH=12` (bipolar, 0.5=Mitte), `_DETUNE=13`,
`_PORTAMENTO=14`, `_VOLUME=15` (1.0=unity) — alle über `para3_set_param`.

Funktionen: `para3_set_lfo_sync`, `para3_set_octave`,
`para3_seq_motion_set/_lane_commit/_smooth/_rec/_val/_rejects`,
`para3_seq_step_trigger/_tempo_div/_active_step/_metronome`,
`para3_seq_flux_mode/_loop_len/_rec/_note/_commit/_dropped`.

## 7. Nächster Schritt (für dich / Claude Code)

Engine-Seite E1–E7 ist abgeschlossen und grün. Pro
`CLAUDE_PARITY_E2E_GATE.md` ist jeder Sprint erst gemeinsam „done", wenn
Claude Codes Frontend-Deltas (Knob-Aktivierung egi/det/por, neue Toggles
LFO-Sync/StepTrig/TempoDiv/ActiveStep/Metro/Flux/SMOOTH, ziel-parametrische
`paint`/`flushPattern`) **und** der gemeinsame E2E-Test grün sind. Diese
Engine liefert dafür die gemessene, bitidentisch-neutrale Basis.
