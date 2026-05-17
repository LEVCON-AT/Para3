# CLAUDE_ENGINE_HANDOVER.md — Auftrag für den Engine-Agenten

**Zweck.** Diese Datei ist der vollständige, in sich geschlossene Auftrag für
einen separaten Agenten, der die **Engine-/DSP-/C-API-/native-Test-Seite** der
KORG-Parity umsetzt. Der Frontend-Agent (ich) macht das Gegenstück
(`para3-responsive.html` + JS-Bridge). Beide Agenten arbeiten gegen *dieselben*
Sprintnummern (E1–E7). E8 ist Hardware-Kalibrierung und nicht Teil der Software-
Arbeit (siehe Anhang C in `CLAUDE_KORG_PARITY_DSP.md`).

**Verbindliche Vorlagen (zwingend zuerst lesen):**
- `CLAUDE_KORG_PARITY.md` — Sprintplan, Akzeptanzkriterien je Sprint.
- `CLAUDE_KORG_PARITY_DSP.md` — exakte DSP-Gleichungen, Anhang A
  (Datei-Politik), Anhang B (Andock-Karte), Anhang C (Labor E8).
- `CLAUDE_FRONTEND_PARITY.md` — was das Frontend ruft (informativ für die
  C-API-/OP-Seite).
- `CLAUDE_BUILD.md` — Build/Test-Disziplin, „messen statt behaupten".

**Vertrag in zwei Sätzen.** Header-only Engine `Para3Engine.hpp`,
in-place erweitern, keine Parallelklasse, kein neues `.cpp`. Jeder Sprint
erfüllt seine DSP-Gleichung **und** seinen Test in `offline_test.cpp` (neue
Messung *ergänzen*, nicht streuen). Voll-Regression nach jedem Sprint, 0
Warnungen, kein `-ffast-math`. Ein roter Test stoppt.

**Sprint-Reihenfolge:** strikt E1 → E2 → … → E7. Pro Sprint ein
Arbeitsblock = ein Testlauf = ein Commit.

---

## 0 — Baseline (PASS, 17.05.2026)

Auf VPS (`/opt/synth/para3-engine`, Ubuntu 24.04, g++ 13, emsdk):

| Suite | Ergebnis |
|---|---|
| `offline_test` (Engine T1–T13) | **PASS** 0/13 Fehler, 0 Warnungen |
| `capi_test` (WA1–WA5) | **PASS**, 128-quanta == 1 call bit-identisch |
| `ring_test` (FIFO + Cross-Thread) | **PASS** |
| `audio_test` (76 + Backpressure + 500-Round-Stress) | **PASS** |
| `port_test` (Port == Ring bit-identisch, 1000-stress) | **PASS** |
| `scope_source_test` (Stille=0; A4=440.000 Hz, 0.00 cents) | **PASS** |
| `wasm_parity` | **max \|Δ\| = 0**, bit-identisch |

Build-Zeile: `g++ -O2 -std=c++17 -Wall -Wextra -msse2 …`.

Diese Baseline darf nach **keinem** Sprint regressieren.

---

## 1 — Schnittstellen-Vertrag (Frontend ↔ Engine)

Der Frontend-Agent ruft nur die unten definierten Methoden. Wer was darf:

### 1.1 Bestehende Param-IDs (`para3_capi.h`, mirror `ParaEngine::Param`)

```
0  PARA3_P_CUTOFF
1  PARA3_P_RESONANCE
2  PARA3_P_DRIVE
3  PARA3_P_LFO_CUT_DEPTH
4  PARA3_P_DELAY_MIX
5  PARA3_P_LFO_RATE
6  PARA3_P_LFO_PITCH_DEPTH
7  PARA3_P_DELAY_TIME
8  PARA3_P_DELAY_FEEDBACK
9  PARA3_P_ATTACK
10 PARA3_P_DECREL
11 PARA3_P_SUSTAIN
```

### 1.2 NEUE Param-IDs (zu ergänzen, in dieser Reihenfolge)

```
12 PARA3_P_EG_CUT_DEPTH        // E1.1, bipolar via taper, n=0.5 == Mitte (0)
13 PARA3_P_DETUNE               // E2.1, unipolar n∈[0,1]
14 PARA3_P_PORTAMENTO           // E2.2, unipolar n∈[0,1], 0=instant (Modell A)
15 PARA3_P_VOLUME               // E6, unipolar n∈[0,1], Master-Gain in Engine
```

**Konvention bipolar:** `n∈[0,1]`, `0.5` = Null. Engine-Taper rechnet
`bip = (n - 0.5) * 2 ∈ [-1, +1]` und multipliziert mit dem CALIB(E8)-Max.

### 1.3 NEUE C-API-Funktionen (in `para3_capi.h/.cpp` ergänzen)

```c
// E1.2 LFO Trigger Sync (resetPhase bei noteOn, wenn aktiv)
void para3_set_lfo_sync   (Para3* p, int on);

// E3 Motion (ziel-parametrisch — Lane pro motion-fähigem PARA3_P_*)
// param_id MUSS motion-fähig sein. Resonance(1) und Tempo (separat) verweigern.
void para3_seq_motion_set         (Para3* p, int param_id, int step_idx,
                                   double value01);
void para3_seq_motion_lane_commit (Para3* p, int param_id,
                                   const double* values01_16);  // len=16
void para3_seq_motion_smooth      (Para3* p, int on);

// E4 Step Trigger / Tempo-Div / Active Step / Metronom
void para3_seq_step_trigger (Para3* p, int on);
void para3_seq_tempo_div    (Para3* p, int div);              // 1, 2, 4
void para3_seq_active_step  (Para3* p, int idx, int enabled);
void para3_seq_metronome    (Para3* p, int on);

// E5 Flux-Modus (Modus-Toggle; Flux-Recording läuft über vorhandene
// noteOn/noteOff im Flux-armed Zustand)
void para3_seq_flux_mode    (Para3* p, int on);

// E6 Octave-Offset (Halbtöne-Offset im Pitch-Pfad, +12*round(oct))
void para3_set_octave       (Para3* p, int oct);              // typischer Bereich -2..+2
```

**RT-Sicherheit:** Wie alle bestehenden Methoden: keine Allokation, keine
Locks, kein `new`/`malloc` im Aufruf, alle Buffer in `para3_create()`
vorallokiert. `seq_motion_lane_commit` kopiert genau 16 doubles in die
Back-Buffer-Lane, kein Allokat.

### 1.4 NEUE Ring-/Port-OP-Codes (zu ergänzen in `para3-ring.js` UND
`para3-worklet.js` UND `para3-port.js` — der Frontend-Agent macht das)

```
15 SET_LFO_SYNC          // i0 = on
16 SEQ_MOTION_SET        // i0 = paramId, i1 = stepIdx, d = value01
17 SEQ_MOTION_LANE       // i0 = paramId, i1 = startIdx, d = packed (s. unten)
                         // Variante: 16 einzelne SEQ_MOTION_SET, dann
                         // SEQ_MOTION_LANE_COMMIT als Sammeltrigger
18 SEQ_MOTION_COMMIT     // i0 = paramId
19 SEQ_MOTION_SMOOTH     // i0 = on
20 SEQ_STEP_TRIGGER      // i0 = on
21 SEQ_TEMPO_DIV         // i0 = div (1|2|4)
22 SEQ_ACTIVE_STEP       // i0 = idx, i1 = enabled
23 SEQ_METRONOME         // i0 = on
24 SEQ_FLUX_MODE         // i0 = on
25 SET_OCTAVE            // i0 = oct (signed -2..+2)
```

**Wichtig (E7 §):** Neue *Param*-IDs (E1.1, E2.1, E2.2, E6 VOL) brauchen
**keinen** neuen OP — `SET_PARAM` trägt id+double, beide Seiten dispatchen
bereits darüber. Neue *Funktionen* (Sync/Motion/Step-Trigger/etc.) brauchen
neue OPs.

**Single source of truth:** Wenn der Engine-Agent die OP-Numerierung
ändert, muss er mit dem Frontend-Agenten abstimmen. Default ist die Liste
oben. Worklet- und Port-Dispatch teilen **eine** Switch-Funktion
(Anti-Drift).

---

## 2 — Sprint E1: Modulationspfade

### E1.1 EG INT (EG → Cutoff)

**Spec:** `CLAUDE_KORG_PARITY_DSP.md` §E1.1, Anhang B Zeile „E1.1".
**Gleichung:**
```
fc = clamp( base_fc * 2^( lfo*lfoDepthOct + (eg - EG_PIVOT)*egIntOct ),
            FC_MIN, FC_MAX )
```

**Andock:** `ParaEngine` (Para3Engine.hpp ≈ Zeile 652) — die Cutoff-Summe
**unmittelbar vor** dem `LadderZDF`-Koeffizienten-Update. Bestehende
`AdsrEnvelope` (Zeile 244) wird **nur gelesen**, kein neuer EG. Der
vorhandene RampParam-Glätter auf dem Cutoff-Pfad wirkt unverändert.

**Implementierung:**
1. `ParaEngine::Param` (Zeile 712) um `EgCutDepth` erweitern (Position 12).
2. `setParamNorm` (Zeile 734) um den neuen Param erweitern.
3. **Bipolar-Taper:** `bip = (n - 0.5) * 2`, dann `egIntOct = bip *
   EGINT_OCT_MAX`. `EGINT_OCT_MAX = CALIB(E8)` als Konstante setzen, im
   Code als `// CALIB(E8)` markieren. Platzhalter z. B. `2.0` (Oktaven).
4. `EG_PIVOT = CALIB(E8)` als Konstante (Platzhalter z. B. `0.0`).
5. Cutoff-Summe ergänzen — keine zweite Glättung, gleicher Pfad.

**C-API:** `PARA3_P_EG_CUT_DEPTH = 12` in `para3_capi.h` ergänzen.
`para3_set_param` route die ID auf `Param::EgCutDepth`.

**Edge-Cases & Asserts:**
- `EgCutDepth=0.5` (= bip 0): Cutoff-Pfad bit-identisch zum Ist-Cutoff
  (Vor-E1 Snapshot vergleichen, max diff 0.000e+00).
- `EgCutDepth<0.5`: Modulationsrichtung **invertiert** vs. `>0.5`.
- `EgCutDepth=0` oder `=1` (Extrem) + `Resonance=1` (max): tanh-Begrenzung
  hält Ausgangs-Peak finit; T5-Methodik.

**Messung (in `offline_test.cpp`, neuer Block `T14_EG_INT`):**
- Note On 60, EG mit kurzem Attack/Decay, EgCutDepth=0.8.
- STFT-Schwerpunkt `centroid(t)` korreliert mit `eg(t)`: Pearson r ≥ 0.9.
- Wiederholung mit EgCutDepth=0.2 (negative Richtung): Korrelation
  `r ≤ -0.9` (invertiert).
- Klicktest am Cutoff-Sprung: `|x[n]-x[n-1]| < CLICK_THRESH` (T2-
  Methodik).
- PASS-Kriterium ausgeben, Format wie bestehende Tests.

### E1.2 LFO Trigger Sync

**Spec:** §E1.2, Anhang B Zeile „E1.2".

**Andock:** `Lfo` (Para3Engine.hpp ≈ Zeile 555) — neue Methode
`resetPhase()` (setzt `phi = PHI0`, Default `0.0`, je Wellenform CALIB(E8)
markiert). Aufruf in `ParaEngine::noteOn` (Zeile 689) wenn Sync-Flag aktiv
(neues Flag-Bit in `ParaEngine`).

**Implementierung:**
1. `Lfo::resetPhase() noexcept`: setzt internen Phasenakku auf `PHI0`.
   `PHI0` als `// CALIB(E8)`-Konstante je Form (Sine/Tri/Saw/Square), 0.0
   als Default.
2. `ParaEngine::syncOn_` als `bool`, Default `false`.
3. `ParaEngine::noteOn`: `if (syncOn_) lfo_.resetPhase();` **vor**
   `env_.gateOn()`, damit der LFO-Beitrag im ersten Sample des Gates schon
   die Phase-0-Modulation hat.
4. **Wichtig (Treue-Konflikt benannt):** Der LFO selbst wird **nicht**
   geglättet (Volca springt echt). Stetigkeit am Modulationsziel kommt vom
   bestehenden Cutoff-/Pitch-Glätter — Klicktest weist nach, dass das reicht.

**C-API:** `para3_set_lfo_sync(p, on)` ruft `eng->setLfoSync(on)`.
Implementiere `ParaEngine::setLfoSync(bool on)`.

**Messung (`T15_LFO_SYNC`):**
- Aktiv: 2× NoteOn im Abstand X ms, LFO Rate moderat. Cutoff-Beitrag (oder
  Pitch-Beitrag bei `LFO_PITCH_DEPTH>0`) extrahieren. Korrelation der
  zwei Notenkurven (zeitlich aufeinander geschoben): ≥ 0.99 (Phase
  reproduzierbar).
- Inaktiv: gleiche Sequenz, Korrelation deutlich < 0.99 (freilaufend).
- Klicktest auf Pitch UND Cutoff bei max LFO-Tiefe < `CLICK_THRESH`.

---

## 3 — Sprint E2: Tonhöhe

### E2.1 DETUNE

**Spec:** §E2.1, Anhang B „E2.1".

**Andock:** `ParaAllocator` (Zeile 478) — bzw. die Stelle, an der die
Oszillator-Sollfrequenzen für die drei Oszillatoren je Voice gesetzt
werden. **Kein neuer Oszillator.** `Oscillator` (Zeile 188) und
`PolyBlepCore` (Zeile 107) unverändert.

**Gleichung:**
```
cents = d * CENTS_MAX                      // CENTS_MAX = CALIB(E8), default ≈ 50 cents
f1 = f_note * 2^( -K1 * cents / 1200 )
f2 = f_note
f3 = f_note * 2^( +K3 * cents / 1200 )
K1 = K3 = 1                                 // Default; Verteilung = CALIB(E8)
```

**Implementierung:**
1. `ParaEngine::Param::Detune` (Pos. 13), `setParamNorm` route.
2. Detune läuft durch denselben RampParam-Pitch-Trichter wie Notenpitch
   (zipperfrei).
3. In der Frequenz-Update-Stelle: drei Faktoren pro Voice, eingerechnet in
   die bestehende Phaseninkrement-Aktualisierung.

**C-API:** `PARA3_P_DETUNE = 13`.

**Edge-Cases:**
- `d=0`: alle drei Oszillator-Frequenzen *deckungsgleich* (Assertion,
  exakte Identität).
- `d=1, höchste Note`: Aliasing **bleibt unter −74 dBc** (T3-Methodik,
  Anti-Alias-Re-Messung).

**Messung (`T16_DETUNE`):**
- Note 60, d=0: Spektrum hat keinen Schwebungsanteil messbar
  (Auto-Korrelation Single-Peak).
- d steigend (0.25, 0.5, 0.75, 1.0): Schwebungsfrequenz steigt **monoton**.
- Note hoch (z. B. 96) + d=1: Aliasing-Suite weiterhin ≤ −74 dBc
  (T3-Methodik wiederverwenden).
- Klicktest beim d-Sprung < Schwelle.

### E2.2 PORTAMENTO

**Spec:** §E2.2, Modell A (Default).

**Andock:** `ParaEngine` Pitch-Pfad — Glide-Zustand pro Allokation,
Update **vor** der Phaseninkrement-Berechnung der bestehenden `Oscillator`.

**Gleichung (Modell A, Default):**
```
on noteOn:  p_target = note_semitones
per sample: p += (p_target - p) * a
            a = 1 - exp( -1 / (TAU * fs) )      // TAU aus Param via CALIB(E8)-Kurve
```
- `Portamento=0` ⇒ `TAU=0` ⇒ `a=1` ⇒ **instantan, bit-identisch** zum
  Ist-Zustand (Assertion).
- Modell B (konstante Rate) ist in §E2.2 voll spezifiziert — nicht jetzt
  implementieren, **aber kein Refactor** verlangt, wenn E8 doch Modell B
  zeigt: Funktion in eine schaltbare Code-Stelle setzen, Default A.

**Implementierung:**
1. `ParaEngine::Param::Portamento` (Pos. 14).
2. `p_state_` pro Voice/Allokation in `ParaAllocator` (oder einer
   gemeinsamen Stelle, an der pro Voice die Sollnote bekannt ist).
3. Glide-Update einmal pro Sample, vor `Oscillator::tick()`. Phaseninkrement
   nimmt `pow(2, (p - 69)/12) * 440 / fs` oder die schon existierende
   Note-zu-Increment-Map.
4. `TAU = portCurve(n)`, `portCurve` als `// CALIB(E8)`-markierte Funktion
   (Default: `TAU = n * 0.5` Sekunden).

**C-API:** `PARA3_P_PORTAMENTO = 14`.

**Edge-Cases:**
- `Portamento=0`: bit-identisch zur jetzigen Tonhöhe (Snapshot-Vergleich,
  max diff 0).
- Schneller Glide (z. B. Octave-Sprung in 50 ms) ⇒ Spektrum während Sweep
  *aliasingfrei* (PolyBLEP/Decimator tragen die Bandbegrenzung — Assertion).
- Erste Note nach `para3_reset()`: kein Glide (p initial = erste Note).

**Messung (`T17_PORTAMENTO`):**
- Note 60 → 72 mit `Portamento=0.5`: Pitch-Trajektorie (Auto-Korrelation
  über kurze Fenster) gleitet in *erwarteter* Zeit ±10% (Modell A).
- `Portamento=0`: Tonhöhe wechselt im 1. Sample der neuen Note (Differenz
  zum Bypass-Pfad max 0).
- Sweep-Spektrum: Aliasing ≤ −74 dBc.

---

## 4 — Sprint E3: Motion Sequence vollständig

**Spec:** §E3 komplett, Anhang B Zeile „E3".

### Datenstrukturen (in `Para3Engine.hpp` — bestehendes `Pattern`-Struct
erweitern, **kein** neues File)

```cpp
namespace para3 {

constexpr int NUM_MP = ...;   // Anzahl motion-fähiger Params, Compile-Zeit-Konstante

struct MotionLane {
    bool   used = false;
    float  v[16] = {0};
};

struct Pattern {
    // ... bestehende Felder (Step note/gate/motionCut) — Motion-cutoff-implizit
    // ENTFERNEN oder als legacy belassen (Frontend wird via seq_motion_*
    // ersetzen).
    MotionLane lane[NUM_MP];
    // ...
};
}
```

**Param→Lane-Index-Map (motion-fähig):** Alle PARA3_P_* AUSSER
`PARA3_P_RESONANCE` (1) und `PARA3_P_TEMPO`. Tempo ist kein PARA3_P_*-
Param (separater Pfad `para3_seq_set_tempo`), daher in der Lane-Map
ohnehin nicht vorhanden. Map ist Compile-Zeit (statisches Array).

### Aufnahme-Automat pro Param (im `Controller`)

```
IDLE → (Knopf p bewegt + Sequencer läuft + global Motion-REC armiert)
     → ARMED_CAPTURING:
          s0 = aktueller Step
          lane[p].used = true
          lane[p].v[aktStep] = aktWert
ARMED_CAPTURING:
   step-Grenze: lane[p].v[step] = letzter Wert von p
   bei erneutem Erreichen von s0 (eine volle Schleife) → IDLE
        (automatische Abschaltung diese Param-Lane)
```

Step-Grenzen aus dem bestehenden sample-genauen Step-Takt (`Clock`,
Zeile 882). Pro Param **unabhängig**.

### Wiedergabe

Pro Step-Grenze, für jede `used`-Lane:

```
SMOOTH aus:  am Step-Anfang  v = lane.v[step]      (stufig, aber klickfrei
                                                    durch RampParam)
SMOOTH an:   v(n) = lane.v[step]
              + (lane.v[(step+1) % len] - lane.v[step]) * (n / N)
             sample-genau, durch den Trichter (setParamNorm).
```

`N` = Samples pro Step.

### PEAK/TEMPO-Verweigerung

- `para3_seq_motion_set(p, PARA3_P_RESONANCE, ...)`: **ignoriert**, kein
  Effekt (hart, kein silent error — der Aufruf wird beobachtbar verworfen,
  z. B. via Zähler `engine->motionRejects_++`, im Test prüfbar).
- Tempo ist kein PARA3_P_*-Param, daher gar nicht ansprechbar.

### Lock-free Atomic

`Pattern`-Doppelpuffer aus `PatternBank` (Zeile 864). `seqCommit()`
swappt atomar (bestehend). Motion-Lane-Edits gehen in den `edit()`-Buffer,
`para3_seq_motion_lane_commit()` ist effektiv ein Schreib-+-implizit-Commit
für die ganze Lane; alternativ getrennt: 16× `seq_motion_set` + 1×
`seq_motion_commit(paramId)`.

**Empfehlung:** beide Varianten anbieten — `seq_motion_set` für Live-REC
(Step-für-Step), `seq_motion_lane_commit` für „lade Pattern komplett".

### Messung (`T18_MOTION`, ergänzt T12)

- Roundtrip pro motion-fähigem Param: programmierte Lane → 2 Loops
  abspielen → für jeden Step `worst reproduction error ≤ 0.0001`.
- Auto-Abschaltung sample-genau nach 1 Loop (T10-Methodik, Jitter 0).
- SMOOTH an: messbare lineare Rampe zwischen `v[i]` und `v[i+1]` über
  die Step-Dauer.
- SMOOTH aus: messbare Stufen (`|dx|` am Step-Anfang sichtbar, dazwischen
  ≈ 0 nach RampParam-Konvergenz).
- PEAK-Motion-Versuch: `motionRejects_` zählt; `lane[PARA3_P_RESONANCE]`
  bleibt `used=false`.
- ≥ 3 Lanes simultan (Cutoff, Drive, LfoRate) unabhängig exakt.
- Lock-free Edit während Audio (T13-Methodik) bleibt PASS.

---

## 5 — Sprint E4: Sequencer-Verhalten

### E4.1 STEP TRIGGER

**Andock:** `Controller` (Zeile 923) — Step-Logik um Trigger-Flag erweitern.

**Spec:** An jeder Step-Grenze EG-Gate-Retrigger über bestehende
`env_.gateOn()`-Logik. Kein neuer Attack-Pfad. Funktioniert auch bei
gehaltener/legato-gebundener Note.

**C-API:** `para3_seq_step_trigger(on)`.

**Messung (`T19_STEP_TRIG`):** Amplitudenhüllkurve zeigt Attack-Transiente
an jeder Step-Grenze (Anstiegszeit messbar = Attack-Parameter); aus →
unverändert (Amplitude stabil zwischen Steps). Klickfrei.

### E4.2 Tempo-Division

**Andock:** `Clock` (Zeile 882) — `samplesPerStep_eff = samplesPerStep_base
* DIV` als Integer-Rechnung. **Kein** Float-Drift.

**C-API:** `para3_seq_tempo_div(p, div)` mit `div ∈ {1, 2, 4}` (andere
Werte ignorieren).

**Messung (`T20_TEMPO_DIV`):** Step-Intervall exakt `samplesPerStep_base *
DIV`, Jitter 0 (T10-Methodik).

### E4.3 Active Step

**Andock:** `Controller` Step-Wiedergabe + Aufnahme.

**Spec:** Deaktivierter Step:
- Wiedergabe: **kein** `noteOn`/`noteOff`, **keine** Motion-Anwendung.
- Aufnahme (`armRecord`): der Step zählt im Lauf, schreibt aber keine
  Noten/Motion.
- Step zählt für die Loop-Position (also tickt der Sequencer normal weiter).

**C-API:** `para3_seq_active_step(p, idx, enabled)`. Default: alle aktiv
(=1).

**Messung (`T21_ACTIVE_STEP`):**
- Pattern mit Step 5 deaktiviert, in Wiedergabe: bei Step-5-Position
  Audio-Aktivität (Energy in kurzem Fenster) = `0` für die Note dieses
  Schritts (Note-Off-Schwanz vom vorigen Step ist OK; Test schaut auf das
  Attack-Transient-Fehlen).
- Bei Aufnahme: nach Lauf ist `lane[*].v[5]` unverändert.

### E4.4 Metronom (echter Generator nach §0)

**Andock:** Neuer kleiner enveloperter Generator als Member in
`ParaEngine` (z. B. `MetroTick`). **Summierung in der vorhandenen
Output-Stufe** (post-Mixer, pre-Output). Delay-Bypass-Flag im bestehenden
`Delay`-Signalweg.

**Gleichung:**
```
tick(n) = A(n) * sin(2π f_click n / fs)
A(n)    = exp( -n / (TAU_CLICK*fs) )
Fenster: Anstieg auf 0..A_max in ATTACK_SAMPLES (~0.5 ms),
         dann exp. Decay, hart bei A(n)<eps gefenstert (Energie endet ≈ 0)
f_click = F_CLICK_ACCENT (Beat 1) | F_CLICK (sonst)   // CALIB(E8)
TAU_CLICK = CALIB(E8)                                   // wenige ms
```
Hülle startet **bei 0** und endet **bei 0** → inhärent bandbegrenzt (kein
PolyBLEP nötig, aber FFT-bewiesen).

**Implementierung:**
1. `MetroTick { phase_, env_, attackCounter_, decayCounter_, ... }` —
   keine Allokation, vorallokiert.
2. `MetroTick::trigger(bool accent)`: Reset Phase + Envelope.
3. `MetroTick::tick(double fs) → float`: ein Sample.
4. `ParaEngine::renderBlock`: nach Mixer/Delay/Filter:
   ```
   sample = mixerOut + (metroOn_ ? tick.tick(fs) : 0);
   ```
   wobei `mixerOut` bei `metroOn_` das Delay-Bypass-Variant verwendet.
5. Delay-Bypass: `Delay::setBypass(bool)`. Pre-Output addition unchanged.

**Trigger-Zeitpunkt:** `Controller`-Step-Grenze ruft
`metro_.trigger(stepIdx % 4 == 0)` bei aktivem Metronom (Beat 1 =
Akzent — Volca-typisch Quartelschläge auf Steps 0/4/8/12).

**C-API:** `para3_seq_metronome(p, on)`.

**Messung (`T22_METRONOME`):**
- Isolierter Tick (Note off, kein Sequencer-Pattern, Metro on): FFT zeigt
  Energie konzentriert um `F_CLICK`, **Aliasing-Bänder** (Spiegel oberhalb
  Nyquist abklappen) < `-74 dBc` (Bewertung wie T3-Methodik).
- Onset/Offset-Energietest: `|x[0]| < 1e-6` und `|x[N-1]| < 1e-6`
  (keine Trunkierungs-Unstetigkeit).
- Metro on + Sequencer mit gefülltem Delay-Pattern: Delay-Wiederholungen
  nachweislich **unterdrückt** (Energie nach den Steps signifikant
  niedriger als ohne Metro, oder Delay-Bypass-Flag in der Engine-State
  nachweislich gesetzt).
- Ticks an korrekten Beat-Positionen (Jitter 0, T10).

---

## 6 — Sprint E5: FLUX-Modus

**Spec:** §E5 komplett, Anhang B „E5".

### Datenmodell (in `Para3Engine.hpp`, neue Structs **im Header**)

```cpp
namespace para3 {

constexpr int FLUX_CAP = 256;

struct FluxEvent {
    uint32_t off;     // Sample-Offset innerhalb des Loops
    uint8_t  type;    // 0 = ON, 1 = OFF
    uint8_t  note;
};

struct FluxPattern {
    uint32_t loopLen = 0;       // Samples; aus Tempo×Bars (Manual-Semantik)
    uint16_t count   = 0;
    FluxEvent ev[FLUX_CAP];
};

}
```

Doppelpuffer parallel zu `PatternBank` (gleicher Mechanismus). Neuer
`FluxBank`. `count > FLUX_CAP`: ältestes Event verwerfen, Zähler erhöhen
(**beobachtbar**, nicht still — z. B. `engine->fluxDropped_`).

### Aufnahme

Im Flux-Recording-Modus: jede `noteOn`/`noteOff` schreibt
`ev[count++] = {off = aktSamplePos mod loopLen, type, note}`. Bei
`seqCommit()` *stabil* nach `off` sortieren.

`loopLen` aus `Tempo × Bars` — bei E8 wird die exakte Volca-Regel zitiert;
Platzhalter: `loopLen = (60.0 / bpm) * 4.0 * fs` (1 Bar, 4 Beats).

### Wiedergabe (sample-genau)

```
Cursor c ∈ [0, loopLen)
für jeden process()-Block der Länge L:
    für jedes ev mit off ∈ [c, c+L):
        trigger an In-Block-Index (off - c)
    Wrap: c+L > loopLen → Rest ab 0 verarbeiten (Reihenfolge wahren)
    c = (c + L) mod loopLen
```

**Reihenfolge bei gleichem `off`:** **OFF vor ON** (saubere Gates).

### Moduswechsel

`para3_seq_flux_mode(on)`: schaltet nur, welcher Scheduler noteOn/off
erzeugt. Laufende Noten enden über bestehende klickfreie Gate-Logik (kein
harter Cut). Lock-free-Swap.

### Messung (`T23_FLUX`)

- Aufnahme einer unregelmäßig getimten Sequenz (5 Noten zu zufälligen
  ms-Zeitpunkten). Wiedergabe: jedes Event sample-genau zur Aufnahme
  reproduziert (Jitter 0).
- Loop-Wrap klickfrei (T2-Methodik am Wrap-Sample).
- Leeres Flux-Pattern → Stille (max|x| = 0).
- Zwei Events im selben Sample (`OFF` und `ON` zur gleichen Position):
  OFF wird zuerst angewendet (Assertion auf interne Reihenfolge oder
  beobachtbar via Voice-Allokation).

---

## 7 — Sprint E6: Signalweg-Angleichung

### VOLUME → Engine

**Andock:** `ParaEngine::renderBlock` — Master-Gain nach VCA, vor Ausgang.

**Implementierung:**
1. `ParaEngine::Param::Volume` (Pos. 15).
2. `setParamNorm(Volume, n)` → `masterGain_ = volumeCurve(n)`,
   `volumeCurve` als `// CALIB(E8)`-markierte Funktion (Default: linear,
   `g = n`).
3. Im `renderBlock`: `out[i] *= masterGain_;` (am Ende der Mixer-Stufe,
   nach Delay/Filter, vor `out[]` Schreibung).

**C-API:** `PARA3_P_VOLUME = 15`.

**Messung (`T24_VOLUME`):**
- `Volume=1.0` (Vollwert): Engine-Ausgang **bit-identisch** zum
  Vor-E6-Snapshot bei `Volume=Vollwert` (max diff 0).
- `Volume=0.5`: Pegel exakt `volumeCurve(0.5)` × Pre-Gain-Pegel (Toleranz
  < 0.1 dB nach Anti-Zipper-Konvergenz).

### OCTAVE → Engine

**Andock:** Pitch-Pfad in `ParaEngine`/`ParaAllocator` — Halbtoner-Offset
`+12 * round(oct)` zur eingehenden MIDI-Note.

**Implementierung:**
1. `ParaEngine::setOctave(int oct) noexcept`: speichert `octOffset_`.
2. `ParaEngine::noteOn(int n)`: ruft `alloc_.noteOn(n + 12*octOffset_)`.
3. Bandbegrenzung: keine Änderung — PolyBLEP/Decimator wirken am
   resultierenden f_note.

**C-API:** `para3_set_octave(p, oct)`. Typischer Bereich `-2..+2`,
größere Werte clampen.

**Messung (`T25_OCTAVE`):**
- `setOctave(0)`: bit-identisch zum jetzigen Verhalten (max diff 0).
- `setOctave(+1)`, NoteOn 60: gemessene f0 = NoteOn-72-f0 (exakte
  Halbton-Verschiebung, ±0.01% wegen RampParam-Glättung).
- `setOctave(+2)`, höchste Note: Aliasing ≤ −74 dBc (T3-Re-Messung).

### Velocity fix

**Andock:** `ParaEngine::noteOn(int n)` — fixe Anschlagstärke setzen.

**Implementierung:** `VEL_FIXED = CALIB(E8)` als Konstante (Default `1.0`).
NoteOn ignoriert UI-Velocity (gibt es ohnehin nicht über C-API), setzt
intern `vel_ = VEL_FIXED`.

**Messung (`T26_VEL_FIXED`):** mehrere `noteOn(60)` hintereinander mit
identischem Setup → Ausgangs-Peak identisch (max Δ < `1e-6`).

---

## 8 — Sprint E7: Integration + Voll-Regression

### Aufgaben Engine-Seite

1. Alle neuen Param-IDs/Funktionen in `para3_capi.h/.cpp` finalisiert,
   `extern "C"`-Exports.
2. `build_wasm.sh`: `EXPORTED_FUNCTIONS` um alle neuen Symbole erweitern:
   ```
   _para3_set_lfo_sync, _para3_seq_motion_set,
   _para3_seq_motion_lane_commit, _para3_seq_motion_smooth,
   _para3_seq_step_trigger, _para3_seq_tempo_div, _para3_seq_active_step,
   _para3_seq_metronome, _para3_seq_flux_mode, _para3_set_octave
   ```
3. `wasm_parity.mjs`: WASM-Output gegen `parity_native.cpp` weiter
   bit-identisch (max |Δ| = 0). `parity_native.cpp` darf um neue
   Param-Sweeps erweitert werden, **muss** aber den gleichen
   deterministischen Plan wie das WASM-`parity_seq.h`-Skript abspielen.
4. `offline_test.cpp` enthält T1–T26 (T1–T13 bestehend, T14–T26 neu).
5. `capi_test.cpp` WA1–WA5 weiter PASS; falls neue C-API-Funktionen einen
   eigenen WA-Test brauchen (Empfehlung: WA6 = Param-Sweep aller neuen
   IDs), ergänzen.

### Akzeptanz (Voll-Regression)

```
g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o /tmp/e && /tmp/e
# → T1..T26 PASS, 0 failures, 0 warnings
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp capi_test.cpp -o /tmp/c && /tmp/c
# → WA1..WA5 (+WA6) PASS
node ring_test.mjs && node audio_test.mjs && node port_test.mjs
# → PASS
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp scope_source_test.cpp -o /tmp/s && /tmp/s
# → PASS (silence=0, A4=440.000Hz)
bash build_wasm.sh ..
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp parity_native.cpp -o /tmp/p && /tmp/p > parity_native.f32
node wasm_parity.mjs
# → max |Δ| = 0 (bit-identisch)
```

Alle PASS, 0 Warnungen, kein `-ffast-math`.

---

## 9 — Lieferung / Hand-off

**Pro Sprint (E1..E6):**
1. Engine-Änderungen in-place in `Para3Engine.hpp`.
2. C-API-Erweiterung in `para3_capi.h/.cpp`.
3. Neuer Test `T{N}_{NAME}` in `offline_test.cpp`.
4. Voll-Regression grün. Commit-Message: `Engine Ex.y: <Sprintname> +
   T{N} green`.
5. Status melden: welche `// CALIB(E8)`-Konstanten neu gesetzt (Default-
   Werte), Test-Output (Last lines).

**Nach E7:** Konsolidierter Status mit `// CALIB(E8)`-Liste — alle
Platzhalter, die der Auftraggeber in E8 ersetzen muss. Format:

```
CALIB(E8) Übersicht:
  Para3Engine.hpp:NNN  EGINT_OCT_MAX = 2.0  (oktaven, default)
  Para3Engine.hpp:NNN  EG_PIVOT     = 0.0
  Para3Engine.hpp:NNN  PHI0[Sine]   = 0.0
  ... usw.
```

---

## 10 — Was der Frontend-Agent (ich) macht — informativ

Damit es keine Doppelarbeit gibt:

- `para3-responsive.html`: alle Knob-/Toggle-Bindungen (E1–E6 UI-Teil
  aus `CLAUDE_FRONTEND_PARITY.md`).
- `para3-audio.js`: `PARAM.*` Konstanten ergänzen, `Para3Controls` um die
  neuen Methoden (`setLfoSync`, `seqMotion*`, `seqMotionSmooth`,
  `seqStepTrigger`, `seqTempoDiv`, `seqActiveStep`, `seqMetronome`,
  `seqFluxMode`, `setOctave`).
- `para3-ring.js` + `para3-port.js`: neue OP-Codes (15–25) in beiden
  Transporten.
- `para3-worklet.js`: Dispatch-Switch um die neuen OPs erweitern, ruft die
  neuen C-API-Exports.

**Synchronisation:** Sobald der Engine-Agent einen Sprint abgeschlossen +
WASM neu gebaut + Voll-Regression grün gemeldet hat, schließe ich den
Frontend-Anteil des entsprechenden Sprints ab und führe die UI-Abnahme
durch (Playwright/Selftest pro neuer Steuerung). Per-Sprint sequenziell
E1 → E7.

---

## 11 — Verbotenes / häufige Fallen

- **Kein** neues Engine-Quellfile, **kein** Refactor des Headers, **keine**
  Parallel-Oszillator-Klasse.
- **Kein** `-ffast-math`, keine Compile-Warnung.
- Algorithmus *immer voll* implementieren, auch wenn der CALIB(E8)-Wert
  Platzhalter ist. „Wert nach Gehör" ist ein Fake.
- Bipolar-Falle: bei bipolaren Params **immer** durch `bip = (n-0.5)*2`
  rechnen, nicht `n` direkt mit dem Max multiplizieren.
- Active Step: nicht `Step::gate` (oder Frontend `onSteps`) überladen —
  separater Zustand.
- Treue-Konflikt benennen, nicht still glätten (LFO-Sync ist ein Beispiel
  — der Sprung ist gewollt, der Glätter ist *am Ziel*, nicht am LFO).

---

## 12 — Andock-Referenz (Zeilennummern Stand 17.05.2026)

In `Para3Engine.hpp`:

| Klasse | Zeile | Sprint-Rolle |
|---|---|---|
| `RampParam` | 73 | wiederverwenden (Anti-Zipper-Glätter) |
| `PolyBlepCore` | 107 | wiederverwenden (Bandbegrenzung) |
| `Decimator` | 135 | wiederverwenden (Kaiser-FIR) |
| `Oscillator` | 188 | E2.1 Frequenz-Eingang, E2.2 Phaseninkrement |
| `RingModIsland` | 218 | unverändert |
| `AdsrEnvelope` | 244 | E1.1 nur LESEN |
| `Voice` | 309 | E1.2 LFO-Sync-Trigger im noteOn |
| `Os2Island` | 357 | unverändert |
| `LadderZDF` | 428 | E1.1 Koeff.-Update folgt der neuen Cutoff-Summe |
| `ParaAllocator` | 478 | E2.1 Detune-Faktoren, E2.2 Glide-Pitch |
| `Lfo` | 555 | E1.2 `resetPhase()` |
| `Delay` | 596 | E4.4 `setBypass(bool)` |
| `ParaEngine` | 652 | E1.1 Cutoff-Summe, E1.2 syncOn_, E4.4 MetroTick, E6 Master-Gain + Octave + VEL_FIXED |
| `Engine` | 821 | unverändert |
| `Pattern` | 857 | E3 `MotionLane lane[NUM_MP]` ergänzen |
| `PatternBank` | 864 | wiederverwenden (Doppelpuffer) |
| `Clock` | 882 | E4.2 Tempo-Div, E4.3 Active-Step-Skip |
| `Controller` | 923 | E3 Motion-Automat, E4.1 Step-Trigger, E5 Flux-Scheduler |

In `para3_capi.h/.cpp`: alle neuen Enums + Funktionen am Ende ergänzen,
Header-Reihenfolge unverändert, `extern "C"` beibehalten.

---

**Ende des Auftrags.** Bei unklarem Andock-Punkt: in
`CLAUDE_KORG_PARITY_DSP.md` Anhang B nachsehen, dort steht der maßgebliche
Eingriffspunkt. Bei echter Unklarheit: **fragen/dokumentieren — NICHT
umstrukturieren, KEIN neues File anlegen.**
