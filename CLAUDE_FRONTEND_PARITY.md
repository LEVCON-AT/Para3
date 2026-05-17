# CLAUDE_FRONTEND_PARITY.md — Frontend an die neuen KORG-Funktionen

Gegen die **tatsaechliche aktuelle** `para3-responsive.html` (engine-
verdrahtet, echter Scope, Scroll + fixe Tastatur). Bezeichner unten sind
**real aus dieser Datei** — nicht raten, nicht umbauen.

## NICHT-UMBAU-REGEL (verbindlich)

- **Bestehende Maps/Funktionen in-place erweitern**, nicht ersetzen:
  `KNOB_PARAM`, `INERT_KNOBS`, `emitKnob`, `FADER_PARAM`, `VOICE_MODE`,
  `LFO_FROM_LW`, `flushPattern`, `paint`, `setTarget`, `snap`/`restore`,
  `midiOfKey`.
- Neue Widgets als DOM **nach dem vorhandenen Muster** (`.sec`,
  `.seqhead`, `.tbtn`, `.mbtn`, `#wave`-Buttons) **innerhalb `.scroll`**.
- **Unangetastet:** Splash/Start, `frame()`/Scope, `updateScrollMask`,
  Keyboard-Drawer (`setKbOpen`), SCOPE-SELFTEST-Mechanik.
- Neue `audio.controls.*`-Methoden werden in E7 der Engine-/DSP-Runbooks
  definiert; das Frontend **ruft sie nur** (eine Quelle der Wahrheit).
- Jede neue/aktivierte Steuerung braucht eine **gemessene** UI-Assertion
  (Muster: der vorhandene SCOPE-SELFTEST). Ein Regler, der „verdrahtet"
  aussieht (kein `.inert`, hat Label/Feedback) aber nichts Messbares
  bewirkt, ist ein Blender → Test MUSS das fangen.

---

## E1 — Modulationspfade

### E1.1 EG INT  (Knob `egi` existiert bereits, bipolar −100..100)
Frontend-Delta exakt:
- `egi` aus `INERT_KNOBS` entfernen.
- In `KNOB_PARAM`: `egi: PARAM.EG_CUT_DEPTH` ergaenzen.
- **Bipolar-Hinweis (kritisch):** `emitKnob` rechnet
  `n=(val-min)/(max-min)` → bei `egi` ist −100→0, **0→0.5**, 100→1.
  Die Engine-Taper fuer `EG_CUT_DEPTH` MUSS `0.5` als Null (Mitte)
  interpretieren (bipolar, siehe DSP §E1.1). Kein Off-by-bipolar.
- Messung (UI): `egi` auf >0,5 → gemessener Cutoff folgt der EG-Form
  (STFT-Schwerpunkt korreliert); <0,5 invertiert; =0,5 statisch.

### E1.2 LFO Trigger Sync  (neues Widget)
- Neuer Toggle-Button im LFO-`.sec` (bei `#wave`), `id="lsync"`, Muster
  wie `#wave`-Buttons. Bindung: `emit(c=>c.setLfoSync(on))`.
- Messung: Toggle an → bei jedem `noteOn` LFO-Phasenstart messbar
  (Modulationsmuster reproduzierbar ab Notenanfang); aus → frei laufend.

---

## E2 — Tonhoehe (beide Knobs existieren, derzeit inert)

- **DETUNE:** `det` aus `INERT_KNOBS`; `det: PARAM.DETUNE` in
  `KNOB_PARAM`. Messung: Schwebung steigt mit `det`; Anti-Alias-Re-
  Messung (DSP §E2.1) — UI-Test triggert max `det` + hohe Note.
- **PORTAMENTO:** `por` aus `INERT_KNOBS`; `por: PARAM.PORTAMENTO`.
  Messung: Pitch-Glide-Zeit folgt `por`; `por=0` instantan.
- Nach E2 enthaelt `INERT_KNOBS` nur noch das, was wirklich kein
  Engine-Feature hat — nach E1+E2 ist das **leer**. Leeres Set
  belassen, nicht entfernen (Doku-Anker).

---

## E3 — Motion Sequence vollstaendig

UI-Mechanik existiert groesstenteils (`setTarget`, `motionArm`,
`laneEl`/`paint`, `flushPattern`). Aenderungspunkte exakt:

- `paint()` und `flushPattern()` senden derzeit Motion **cutoff-implizit**
  (Kommentar „Engine motion lane is cutoff-only"). Aendern auf
  **ziel-parametrisch**: die Lane gehoert zu `tgtId`. Neue Signatur
  (E7-Engine): `c.seqMotion(tgtParamId, stepIdx, value01)` bzw.
  `c.seqMotionLaneCommit(tgtParamId, values01[16])`. `paint`/`flushPattern`
  rufen das mit dem zu `tgtId` gehoerenden Engine-Param.
- **SMOOTH-Toggle (neu):** Button in `.mhead` (bei `#mArm`),
  `id="mSmooth"`, `.mbtn`-Muster. Bindung `emit(c=>c.seqMotionSmooth(on))`.
- **PEAK/TEMPO-Sperre UI-seitig:** `setTarget(id)` muss `id==='pk'` und
  `id==='tmp'` ablehnen (kein Ziel, kurze Anzeige „nicht motion-faehig")
  — spiegelt die Engine-Verweigerung (DSP §E3.4). Nicht faken: wirklich
  ablehnen, nicht still ignorieren.
- **Ehrliche Grenze, klar dokumentieren:** Die Ein-Schleifen-Auto-
  Abschaltung ist engine-intern; der Transport ist UI→Ring (einweg).
  `#mArm` zeigt nur den **globalen** Arm-Zustand. Kein gefakter
  „pro-Parameter gestoppt"-Indikator, solange kein Rueckkanal existiert.
  Wenn ein Statuskanal gewuenscht ist → eigenes Engine-Feature, kein
  UI-Schein.
- Messung: pro Ziel-Param Roundtrip 0,0000; SMOOTH an = messbare Rampe,
  aus = Stufen; `pk`/`tmp` als Ziel nachweislich abgelehnt.

---

## E4 — Sequencer (neue Widgets in der STEP-SEQ-`.sec`)

Alle in/zur `.seqhead` (bei `#play`/`#clr`), `.tbtn`-Muster:

- **Step Trigger:** `id="stp"` → `emit(c=>c.seqStepTrigger(on))`.
  Messung: an → Attack-Transiente an jeder Step-Grenze (Huellkurve);
  aus → unveraendert.
- **Tempo-Division:** segmentierte Buttons `id="tdiv"` (1/1·1/2·1/4) →
  `emit(c=>c.seqTempoDiv(d))` mit `d∈{1,2,4}`. Messung: Step-Intervall
  skaliert exakt; Jitter 0.
- **Active Step:** Modus-Toggle `id="astep"`. An: das `#steps`-Gitter
  schaltet von „Note an/aus" (`onSteps`) auf „Step aktiv/uebersprungen"
  — separater Zustand `actStep[16]`, eigener Visual-State, Bindung
  `emit(c=>c.seqActiveStep(i,enabled))`. **Nicht** `onSteps`
  ueberladen. Messung: uebersprungener Step erzeugt in Wiedergabe UND
  Aufnahme nachweislich nichts.
- **Metronom:** `id="metro"` → `emit(c=>c.seqMetronome(on))`. Engine
  umgeht Delay bei aktiv (DSP §E4.4) — UI nur Toggle. Messung: Click
  an korrekten Beats; Delay waehrend aktiv nachweislich umgangen.

---

## E5 — FLUX-Modus (neues Widget + Zustandsanzeige)

- Modus-Toggle `id="flux"` in `.seqhead` → `emit(c=>c.seqFluxMode(on))`.
- Bei Flux **an**: das `#steps`-Gitter und `#lane` sind nicht das Modell
  (Flux ist ereignisbasiert). UI: Gitter sichtbar **disabled/grau**,
  Statusanzeige „FLUX". Aufnahme = Tasten spielen waehrend Flux-Arm; die
  Tastatur ruft bereits `c.noteOn/off` → Flux-Recording ist engine-seitig
  (DSP §E5). UI-Aenderung nur: Flux-Arm-Button + Visual-State, **kein**
  Nachbau der Step-Logik.
- Moduswechsel klickfrei (engine-seitig); UI schaltet nur die Anzeige.
- Messung: in Flux aufgenommene, unregelmaessig getimte Folge spielt
  sample-genau zurueck (engine-Test); UI-Test: Flux an → Step-Gitter
  inaktiv, kein `flushPattern`-Aufruf mehr.

---

## E6 — VOLUME / OCTAVE in die Engine, Velocity

- **VOLUME:** `emitKnob`-Zweig `id==='vol'` aendern: statt
  `masterGain.gain` → `emit(c=>c.setParam(PARAM.VOLUME, n))`. Host-
  `masterGain` und seine Verdrahtung **entfernen**. Messung: Pegel folgt
  `vol` durch die Engine; bei Vollwert bit-identisch zum bisherigen
  Einheits-Ausgang.
- **OCTAVE:** `oct` ist derzeit Host-Transpose in `midiOfKey`
  (`+oct*12`). Aendern: `midiOfKey` addiert `oct` **nicht** mehr; statt
  dessen `K.oct.set`/`emitKnob` ruft `emit(c=>c.setOctave(Math.round(val)))`.
  Messung: exakte Oktav-Verschiebung in der Engine; Tastenmapping
  unveraendert.
- **Velocity:** bereits korrekt fix (`c.noteOn(note)` ohne Velocity).
  Belassen; nichts hinzufuegen.

---

## E7 — Abschluss & gemessene UI-Abnahme

- `KNOB_PARAM` vollstaendig; `INERT_KNOBS` **leer** (jeder Knopf hat
  jetzt einen Engine-Weg). Bleibt etwas drin: im UI sichtbar begruenden,
  nicht verstecken.
- `audio.controls.*`-Neulinge existieren (E7 Engine/DSP): `setLfoSync`,
  `seqMotion*`, `seqMotionSmooth`, `seqStepTrigger`, `seqTempoDiv`,
  `seqActiveStep`, `seqMetronome`, `seqFluxMode`, `setOctave`,
  `PARAM.EG_CUT_DEPTH/DETUNE/PORTAMENTO/VOLUME`. Frontend ruft nur.
- **Gemessener UI-Funktionstest (Pflicht, Muster = SCOPE-SELFTEST):**
  je Regler/Toggle ein headless-Lauf (Playwright + OfflineAudioContext
  oder das vorhandene Selftest-Pattern), der den spezifizierten
  **messbaren** Effekt prueft. Für ein leeres `INERT_KNOBS` gilt: kein
  Control darf ohne messbaren Effekt „aktiv" wirken.
- Voll-Regression unveraendert: Engine-Suite, capi, ring, audio_test,
  wasm_parity, SCOPE-SELFTEST — alle gruen, 0 Warnungen.

---

## Andock-Tabelle (Datei-Stelle → Aenderung)

| Sprint | Stelle in `para3-responsive.html` | Aenderung |
|---|---|---|
| E1.1 | `KNOB_PARAM`, `INERT_KNOBS` | `egi` aktivieren (bipolar) |
| E1.2 | LFO-`.sec` bei `#wave`, neuer `#lsync` | `setLfoSync` |
| E2 | `KNOB_PARAM`, `INERT_KNOBS` | `det`,`por` aktivieren |
| E3 | `paint`,`flushPattern`,`setTarget`,`.mhead` | ziel-parametrisch + `#mSmooth` + PEAK/TEMPO-Sperre |
| E4 | `.seqhead` | `#stp`,`#tdiv`,`#astep`(+`actStep[]`),`#metro` |
| E5 | `.seqhead` + Step/Lane-Visual | `#flux` + disabled-State |
| E6 | `emitKnob` (`vol`), `midiOfKey`/`oct` | in Engine verlagern |
| E7 | obige Maps + Tests | Abschluss + gemessene Gates |

Bei Unklarheit: **fragen/dokumentieren — nicht umstrukturieren, kein
neues Frontend-File.**
