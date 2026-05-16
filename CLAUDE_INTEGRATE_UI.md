# CLAUDE_INTEGRATE_UI.md — den schoenen Entwurf anbinden

Kontext: Der minimale `index.html` war nur eine Harness. Die fertige,
gestaltete Oberflaeche ist **`wasm-bridge/para3-responsive.html`** (Desktop
+ Mobile, zwei Themes, Knobs, Sequencer, Motion-Lane, Keyboard) — bislang
look&feel ohne Audio. Sie ist jetzt im Paket.

Was bereits erledigt & hier regressionsgeprueft ist (nicht erneut tun):
Der Parameter-Trichter wurde auf das **volle Panel** erweitert
(`Para3Engine::Param` + `taper` + `setParamNorm`, C-API `PARA3_P_*`,
`para3_set_lfo_shape`, Ring-OP `SET_LFO_SHAPE`, Worklet-Dispatch,
`para3-audio.js` `PARAM`/`LFO_SHAPE`). Engine 13/13, capi, ring, audio_test
weiterhin `OVERALL: PASS`, 0 Warnungen.

Deine Aufgabe: die DOM-Controls von `para3-responsive.html` an
`Para3Controls` binden, **ohne** den Trichter zu umgehen, **ohne** etwas zu
faken. Inerte Controls (siehe Liste C) bleiben sichtbar, aber ohne
Engine-Wirkung — nicht vortaeuschen.

## A) Start-Geste + Audio

Mobile/iOS verlangen eine User-Geste. Fuege ein einmaliges Overlay
("TAP TO START") ueber `#app` ein, das bei erstem Tap:
```js
import { Para3Audio, PARAM, MODE, LFO_SHAPE } from './para3-audio.js';
const audio = new Para3Audio();
await audio.start({ wasmUrl:'./para3.wasm', transport:'auto' }); // SAB or postMessage
await audio.resume();
```
`transport:'auto'` waehlt SAB nur bei `crossOriginIsolated`, sonst
PortTransport (Phase B aus CLAUDE_BUILD.md — fuer Offline-PWA Pflicht).
Master-Lautstaerke als `GainNode` zwischen WorkletNode und destination
(VOLUME-Knopf, siehe Liste B) — bewusst host-seitig, nicht im C-API-Trichter.

## B) Mapping — diese Controls bekommen einen ECHTEN Engine-Weg

Knob-`data-k` → Aufruf. Knobs liefern `data-min..data-max`; normalisiere
`n=(val-min)/(max-min)` und rufe `audio.controls.setParam(PARAM.X, n)`.

| UI (`data-k` / Element) | Aufruf |
|---|---|
| `cut` CUTOFF | `setParam(PARAM.CUTOFF, n)` |
| `pk` PEAK | `setParam(PARAM.RESONANCE, n)` |
| `lrt` LFO RATE | `setParam(PARAM.LFO_RATE, n)` |
| `lpi` PITCH INT | `setParam(PARAM.LFO_PITCH_DEPTH, n)` |
| `lci` CUTOFF INT | `setParam(PARAM.LFO_CUT_DEPTH, n)` |
| `dt` DLY TIME | `setParam(PARAM.DELAY_TIME, n)` |
| `df` FEEDBACK | `setParam(PARAM.DELAY_FEEDBACK, n)` |
| EG-Fader `F` Attack | `setParam(PARAM.ATTACK, n)` |
| EG-Fader `F` Decay/Release | `setParam(PARAM.DECREL, n)` |
| EG-Fader `F` Sustain | `setParam(PARAM.SUSTAIN, n)` |
| `tmp` TEMPO | `seqTempo(val)` (BPM direkt, **nicht** setParam) |
| `vol` VOLUME | host-`GainNode.gain` = `n` (nicht C-API) |
| `oct` OCTAVE | host-seitig: MIDI-Note + `Math.round(val)*12` vor `noteOn` |
| Voice-Grid (`#voice`) Index | `setMode(MODE.{POLY,UNISON,OCTAVE,FIFTH,UNIRING,POLYRING}[ix])` |
| Wave (`#wave`) lw 0/1/2 | `setLfoShape([LFO_SHAPE.TRIANGLE,SAW,SQUARE][lw])` |
| Keyboard `.wk/.bk` (`data-n`) | Notenname→MIDI (C4=60 Basis) + oct-Offset; `noteOn`/`noteOff` |
| `#play` ▶ | toggle `seqStart()` / `seqStop()` |
| `#clr` CLR | alle Steps aus: `seqStep(i,note,false,…)` ×16 + `seqCommit()` |
| 16 Steps `#steps` (`onSteps[i]`) | bei Toggle: ganzes Pattern setzen + `seqCommit()` |
| Motion-Lane `#lane` (`motion[i]` 0..100) | je Step `seqStep(i,note,gate,true,motion[i]/100)` + `seqCommit()` |
| `#mArm` MOTION REC | `seqArmRecord(on)` |

Sequencer-Note pro Step: nimm eine feste Wurzel (z. B. MIDI 48) oder die
zuletzt gespielte Taste — dokumentiere die Wahl. Pattern-Aenderungen immer
ueber `seqStep(...)` ×n **dann** `seqCommit()` (atomar, lock-frei).

Bindepunkte in `para3-responsive.html` (vorhandene Handler erweitern, nicht
ersetzen): die Knob-`pointermove`/`R()`-Stelle, `voice.addEventListener`,
`wave.addEventListener`, die Step-Toggle-Stelle (`onSteps`), `laneEl`
paint, `kbd.addEventListener('pointerdown')` + pointerup, `#play`, `#clr`,
`#mArm`. Fuege je einen `audio.controls.*`-Aufruf hinzu. Eine zentrale
`emit()`-Hilfsfunktion ist sauberer als verstreute Aufrufe.

## C) NICHT faken — UI sichtbar, aber (noch) ohne Engine-Weg

Diese Controls haben **kein** echtes Engine-Feature. Lass sie inert
(reagieren visuell, aber **kein** `controls.*`-Aufruf), oder blende sie aus.
Niemals einen Ersatzweg vortaeuschen:

- `egi` EG INT — die Engine hat keine EG→Cutoff-Modulation (EG steuert nur
  die VCA). Echtes Feature, kein blosser Weg → ausstehend.
- `det` DETUNE — kein Live-Detune-Parameter (Unison-Spread ist konstant).
- `por` PORTAMENTO — Pitch-Glide-Zeit ist fix/CALIB, kein Live-Setter.
- Motion-Target ≠ CUTOFF — die Engine-Motion-Lane ist cutoff-only; der
  Zielwechsel in der UI darf nur CUTOFF wirklich modulieren. Andere Ziele:
  UI-Auswahl ok, aber kein Engine-Effekt → so dokumentieren.
- Patch SAVE/RECALL — bleibt UI-lokal (Snapshot der UI-Werte), das ist
  korrekt so; beim RECALL die wiederhergestellten Werte erneut durch
  `controls.*` an die Engine schicken.

Wenn du eines davon doch verdrahten willst, ist das ein **Engine-Feature**
(neuer DSP-Pfad + Messtest in `offline_test.cpp`, 13/13 muss gruen bleiben),
kein UI-Task. Dann erst Engine erweitern + messen, dann binden.

## D) Abnahme

1. Sanity unveraendert gruen (Engine 13/13, capi, ring, audio_test;
   0 Warnungen) — nach jeder Engine-Aenderung erneut.
2. `para3-responsive.html` als PWA-`start_url` (statt `index.html`);
   `index.html` nicht ausliefern.
3. Geraetetest (Pflicht, wie CLAUDE_BUILD.md B3): installierte PWA,
   **Flugmodus an**, Start-Tap → Ton; jeder Regler aus Liste B veraendert
   hoerbar den Klang; Sequencer/Motion laufen; **kein Knacksen**; nach
   Sperren/Entsperren wieder Ton; Konsole fehlerfrei.
4. Kein Aufruf umgeht den Trichter (eine Steuerquelle: UI→Ring/Port→C-API).
   Controls aus Liste C bleiben nachweislich inert.

Fertig, wenn 1–4 erfuellt sind und der schoene Entwurf offline am Handy
spielbar ist.
