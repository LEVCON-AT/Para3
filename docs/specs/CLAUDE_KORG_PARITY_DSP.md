# CLAUDE_KORG_PARITY_DSP.md — DSP-genaue Spezifikation (verbindlich)

Diese Datei pinnt die Algorithmik fuer `CLAUDE_KORG_PARITY.md` fest. Sie
laesst **keinen** Interpretationsspielraum. Wo eine Zahl fehlt, ist sie als
`CALIB(E8)` markiert — die **Struktur/Gleichung ist hier fix**, nur der
Messwert kommt in Sprint E8. „Ich hatte die Zahl nicht, also habe ich
geschaetzt" ist ein Fake und verboten: Platzhalterkonstante setzen, im Code
als `// CALIB(E8)` markieren, Algorithmus voll implementieren.

---

## 0 — PRODUKTIONS-VERTRAG: wie ein echtes DSP-Element angehaengt wird

Gilt fuer JEDES neue klang- oder modulationserzeugende Element (neuer
Oszillator-Beitrag, Metronom-Tick, EG→Cutoff-Pfad, LFO-Reset, Glide …):

1. **Definiert durch seine Differenzengleichung.** Kein „macht ungefaehr
   X". Die Update-Gleichung pro Sample steht unten. Implementiere genau die.
2. **Bandbegrenzt, wenn es Audio mit Unstetigkeiten erzeugt.** Jede
   Wellenform mit Sprung/Knick MUSS die **bereits vorhandene** Maschinerie
   nutzen: `PolyBlepCore`, `Decimator<M,L>` (Kaiser-FIR),
   `Os2Island`/Insel-Oversampling. **Kein** neuer naiver Generator.
   Referenz-Klassen existieren in `Para3Engine.hpp` — wiederverwenden,
   nicht nachbauen.
3. **Gemessen gegen Referenz.** Neues Audio-Element → kohaerent gesampelte
   FFT, Aliasing/Images unter der etablierten Latte (Anti-Alias bleibt auf
   **−74 dBc**-Niveau, nichtlinear ≤ gemessener Bestwert). Reine Animation/
   Naiv-Generator faellt hier zwingend durch.
4. **RT-rein.** Zustand bei Konstruktion vorallokiert. Im Render/`process()`
   keine Allokation, keine Locks, kein `new`, kein unbeschraenktes Branching.
5. **Nur durch den Trichter.** Integration ausschliesslich ueber
   `setParamNorm` / Protokoll / Ring/Port / Worklet-`dispatch`. Kein
   Direktpfad UI→Engine.
6. **Edge-Cases enumeriert & getestet.** Reset, Denormal, Loop-Wrap,
   gleichzeitige Events, Param=0-Identitaet. Jeder Punkt unten hat seine
   Liste; jeder Punkt braucht eine Assertion.

**Worked Example — der Metronom-Tick (zeigt den Standard):** Ein „Beep"
per Sample-Sprung oder unenveloptem Sinusburst ist ein Fake (aliast /
klickt). Produktionsspezifikation steht in §E4.4: enveloptes Tonpaket,
Huelle startet und endet exakt bei 0 (keine Trunkierungs-Unstetigkeit →
inhaerent bandbegrenzt, kein PolyBLEP noetig, aber **bewiesen** per FFT).
So sieht „knallhart echt" aus — auch fuer ein Tick.

---

## 1 — ANTI-UMWEG-REGELN FUER CLAUDE CODE

- **Ein Sprint = ein Arbeitsblock = ein Testlauf = ein Commit.** Sprints
  werden **nicht** zusammengefasst. E1 vor E2 vor … E7. E8 macht der
  Auftraggeber.
- **Kein Stub, kein TODO, kein „spaeter".** Ein Sprint gilt nur als fertig,
  wenn sein DSP voll implementiert und seine Messung *ausgefuehrt* gruen ist.
- **Keine Helfer-Abkuerzung**, die das gemessene Verhalten umgeht (z. B.
  Tick „fest verdrahtet" statt durch den Mixer; Motion „direkt" gesetzt
  statt durch den Trichter).
- **Treue-Konflikt** (z. B. LFO-Phasensprung vs. klickfrei): benennen,
  beide Groessen messen, dokumentierte Entscheidung — nie stillschweigend
  glaetten.
- Nach jedem Sprint **volle Regression** (offline_test inkl. neuer Tests,
  capi_test, ring_test, audio_test, wasm_parity), 0 Warnungen, kein
  `-ffast-math`. Ein roter Test stoppt den Fortschritt.

---

## E1 — Modulationspfade

### E1.1 EG INT (EG → Cutoff)

Bestehende geteilte EG liefert pro Sample `eg ∈ [0,1]` (bereits klickfrei).
Cutoff-Steuerpfad wird erweitert auf:

```
fc = clamp( base_fc * 2^( lfo*lfoDepthOct + (eg - EG_PIVOT)*egIntOct ),
            FC_MIN, FC_MAX )
```

- `base_fc = taper(Cutoff, n)` (bestehend).
- `egIntOct` = neuer Param `EgCutDepth`, **bipolar**, Bereich
  `[-EGINT_OCT_MAX, +EGINT_OCT_MAX]`, `EGINT_OCT_MAX = CALIB(E8)`.
- `EG_PIVOT = CALIB(E8)` (Pivot, an dem die Modulation „ansetzt";
  Struktur fix: lineare Verschiebung des Exponenten).
- Berechnung in `double`, **an exakt der Stelle und mit derselben
  Granularitaet** wie die bestehende Cutoff-Koeffizienten-Aktualisierung
  (kein neuer Pfad, kein zweiter Glaetter). Die vorhandene
  Koeffizienten-/Anti-Zipper-Glaettung wirkt unveraendert → klickfrei.
- `FC_MIN/FC_MAX` = bestehende Clamps.

Edge: EgInt=0 → Term 0 → bit-identisch zum Ist-Cutoff (Assertion). Negatives
EgInt invertiert (Assertion). EgInt=±max + Resonanz=max → tanh-begrenzte
Rueckkopplung haelt beschraenkt (T5-Methodik am neuen Extrem).

Messung: STFT-Schwerpunkt(t) korreliert mit eg(t) (Korrelation ≥ 0,9 bei
egInt>0); Inversion bei egInt<0; Klicktest < Schwelle; Stabilitaet PASS.

### E1.2 LFO Trigger Sync

LFO hat Phasenakkumulator `phi ∈ [0,1)`. Bei `noteOn` mit aktivem Sync:

```
phi = PHI0          // PHI0 = CALIB(E8) je Wellenform; Struktur fix: harter Reset
```

**Wir glaetten den LFO NICHT** (das waere untreu — die Volca springt
echt). Die Stetigkeit kommt aus dem **bereits vorhandenen Glaetter am
Modulationsziel** (Pitch/Cutoff laufen ohnehin durch den Anti-Zipper-Pfad).
Ergebnis: hoerbarer LFO-Neustart **ohne** Sample-Sprung-Klick.

Edge (paraphon): ein **globaler** LFO (Volca-Architektur). Reset bei
**jedem** `noteOn`-Event, das ein Gate startet (nicht nur erste Note).
Genau so spezifiziert; exakte Phase je Form = E8.

Messung: phi==PHI0 bei jedem noteOn (aktiv); Klicktest bei max LFO-Tiefe
auf Pitch UND Cutoff < Schwelle; deaktiviert → phi frei laufend (nachweisbar
≠ PHI0 bei noteOn).

---

## E2 — Tonhoehe

### E2.1 DETUNE

Drei Oszillatoren existieren. Frequenzen:

```
cents = d * CENTS_MAX                 // d = Param [0,1], CENTS_MAX = CALIB(E8)
f1 = f_note * 2^( -K1 * cents / 1200 )
f2 = f_note
f3 = f_note * 2^( +K3 * cents / 1200 )
```

- Struktur fix: **symmetrische** Spreizung um die gespielte Note;
  `K1=K3=1` als Default (genaue Verteilung = CALIB(E8), Struktur bleibt).
- Frequenz geht in die **bestehende** Phaseninkrement-Aktualisierung der
  Oszillatoren (PolyBLEP/Oversampling unveraendert). **Kein neuer
  Oszillator** — nur die Frequenzeingaenge der vorhandenen werden moduliert.
- Detune-Knopf-Aenderung laeuft durch denselben RampParam-Pitch-Trichter
  wie Notenpitch (zipperfrei).

Edge: d=0 → f1=f2=f3 (deckungsgleich, Assertion). Wirkt in allen
Voice-Modi.

Messung (Pflicht, **Anti-Alias-Re-Messung**): Schwebungsfrequenz zwischen
Partialgruppen steigt monoton mit d; bei `d=1` und hoechster Note
kohaerent gesampelte FFT → Aliasing weiterhin ≤ −74 dBc (nicht „klingt
ok"); Aenderung klickfrei.

### E2.2 PORTAMENTO

Glide im **Pitch-Domain** (Halbtoene, perzeptiv linear = exponentiell in
Hz, wie Analog-VCO-Portamento). Pro Voice/Allokation ein Glide-Zustand
`p` (Halbtoene).

**Modell A (Spezifikation, Default): Ein-Pol, exponentiell.**
```
on noteOn:  p_target = note_semitones
per sample: p += (p_target - p) * a
            a = 1 - exp( -1 / (TAU * fs) )      // TAU aus Param, TAU = CALIB(E8)-Kurve
```
`Portamento`-Param → `TAU` via `CALIB(E8)`-Kurve. `Portamento=0` ⇒ `TAU=0`
⇒ `a=1` ⇒ instantan, **bit-identisch** zum Ist-Zustand (Assertion).

**Modell B (Fallback, ebenfalls hier voll spezifiziert):** falls die
E8-Hardware-Messung **konstante Glide-Rate** (linear) zeigt:
```
per sample: step = RATE / fs                    // RATE = CALIB(E8) Halbtoene/s
            p += clamp(p_target - p, -step, +step)
```
Claude Code implementiert **Modell A**. Modell B ist vollstaendig
beschrieben, damit bei E8 ein Umschalten **kein** Neuentwurf ist —
nicht improvisieren.

- `p` speist pro Sample das Phaseninkrement der (bandbegrenzten)
  Oszillatoren. Glide-Semantik: **immer** zwischen aufeinanderfolgenden
  Noten (nicht legato-gegated) — Volca-Verhalten; E8 bestaetigt.

Edge: Portamento=0 instantan; sehr schneller Glide → Spektrum waehrend des
Sweeps gemessen, **kein Aliasing** (Bandbegrenzung haelt). Erste Note nach
Reset: kein Glide (p initial = erste Note).

Messung: Momentanfrequenz gleitet in gesetzter Zeit (±Toleranz) gemaess
Modell A; Spektrum waehrend schnellem Glide aliasingfrei; time=0
bit-identisch.

---

## E3 — Motion Sequence vollstaendig

### Datenstrukturen (im bestehenden, lock-frei doppelgepufferten Pattern)

```
struct MotionLane { bool used; float v[16]; };
// Eine Lane pro motion-faehigem Parameter.
// MOTION-FAEHIG = alle Trichter-Parameter AUSSER Resonance(PEAK) und Tempo.
struct Pattern { /* bestehend: Noten/Gates */  MotionLane lane[NUM_MP]; ... };
```

`NUM_MP` fix zur Compile-Zeit (Anzahl motion-faehiger Params). Keine
Allokation zur Laufzeit. `used=false` ⇒ Lane traegt **nichts** bei.

### Aufnahme — Zustandsautomat pro Parameter p

```
IDLE → (Knopf p bewegt, Sequencer laeuft, global Motion-REC armiert)
     → ARMED_CAPTURING:
          s0 = aktueller Step
          lane[p].used = true
          lane[p].v[aktStep] = aktWert
ARMED_CAPTURING:
   an jeder Step-Grenze: lane[p].v[step] = letzter Wert von p
   wenn Sequencer wieder s0 erreicht (eine volle Schleife) → IDLE
        (automatische Abschaltung dieses Parameters — Volca-Verhalten)
```

Step-Grenzen aus dem **bestehenden sample-genauen** Step-Takt (kein neuer
Timer). Pro Parameter unabhaengig; mehrere Lanes simultan moeglich.

### Wiedergabe

Pro Step-Grenze, fuer jede `used`-Lane, ist die Motion-Beitragsquelle die
Lane. Anwendung **durch den Trichter** (kein Bypass):

- **SMOOTH aus:** am Step-Anfang `v = lane.v[step]` (stufige Kontur, aber
  durch klickfreien Trichter ⇒ zipperfrei).
- **SMOOTH an:** lineare Interpolation ueber die Step-Dauer N Samples:
  ```
  v(n) = lane.v[step] + (lane.v[(step+1) % len] - lane.v[step]) * (n / N)
  ```
  sample-genau, durch den Trichter.

### Interaktion Lane ↔ Live-Knopf (Volca-Semantik, fix spezifiziert)

- Motion an + `lane.used`: die **Lane gewinnt pro Step**; ein Live-Knopf
  ohne REC wird von der Motion ueberschrieben.
- `lane.used=false` oder Motion aus: der Live-Knopf-Wert gilt.
- Waehrend REC: der bewegte Knopf schreibt die Lane (siehe Automat).

### PEAK/TEMPO-Ausschluss

Parameter→Lane-Map hat **keinen** Eintrag fuer Resonance/Tempo. Eine
Motion-REC-Anforderung dafuer wird **ignoriert** (hart, kein Effekt).

Edge: Loop-Wrap bei SMOOTH an interpoliert zu `v[0]`; gleichzeitige Lanes
unabhaengig; `seqCommit()` swappt atomar (T13-Methodik gilt weiter).

Messung: pro Parameter Roundtrip **0,0000** (T12); Auto-Abschaltung exakt
nach einer Schleife (sample-genau, T10); SMOOTH an = messbare kontinuierl.
Rampe, aus = messbare Stufen; PEAK/TEMPO verweigern Aufnahme nachweislich;
≥3 Lanes simultan unabhaengig exakt; Lock-free-Swap PASS.

---

## E4 — Sequencer-Verhalten

### E4.1 STEP TRIGGER
Toggle. An: an **jeder** Step-Grenze ein EG-Gate-Retrigger (auch bei
gehaltener/gebundener Note), ueber die **bestehende klickfreie**
Gate-Logik (kein neuer Attack-Pfad). Messung: Amplitudenhuellkurve zeigt
Attack-Transiente an jeder Step-Grenze; aus → unveraendert; klickfrei.

### E4.2 Tempo-Division
Ganzzahliger Teiler `DIV ∈ {1,2,4}` auf die Samples-pro-Step:
```
samplesPerStep_eff = samplesPerStep_base * DIV
```
Integer-Rechnung, kein Rundungsdrift. Messung: Step-Intervall exakt ×2/×4,
Jitter 0, bit-identisch zur Soll-Rechnung.

### E4.3 Active Step (exakt)
Deaktivierter Step wird bei **Wiedergabe UND Aufnahme** uebersprungen
(Manual-Semantik): kein Notentrigger, keine Motion-Anwendung, der Step
zaehlt im Lauf aber erzeugt nichts. Messung: deaktivierter Step erzeugt in
beiden Modi nachweislich weder Note noch Motion.

### E4.4 Metronom (echter Generator — Standard nach §0)
Nicht-naiver Tick. Spezifikation:
```
tick(n) = A(n) * sin(2π f_click n / fs)
A(n)    = exp( -n / (TAU_CLICK*fs) ),  A(0)=0-Anstieg:  Fenster start@0, end@0
f_click = F_CLICK_ACCENT auf Beat 1, sonst F_CLICK   // CALIB(E8) Frequenzen
TAU_CLICK = CALIB(E8) (wenige ms)
```
- Huelle startet und endet **exakt bei 0** (kurzer Anstieg z. B. 0,5 ms,
  dann exp. Abfall, hart bei 0 gefenstert) → keine Trunkierungs-
  Unstetigkeit ⇒ inhaerent bandbegrenzt. **Beweis statt Annahme:** FFT
  des isolierten Ticks, Energie in Alias-Baendern < Schwelle.
- Tick wird **post-Mixer, pre-Output summiert** (durch den regulaeren
  Signalweg, nicht „daneben").
- Bei aktivem Metronom **Delay deaktiviert** (Manual). Messung: Delay
  nachweislich umgangen waehrend aktiv; Tick an korrekten Beats; klickfrei
  (Onset/Offset-Energietest); aus → unveraendert.

---

## E5 — FLUX-Modus

### Datenmodell (zweites Pattern, neben Step-Pattern, gleicher Doppelpuffer)
```
struct FluxEvent { uint32 off; uint8 type; uint8 note; }; // type: 0=ON 1=OFF
struct FluxPattern {
   uint32 loopLen;            // Samples; aus Tempo×Bars (Manual-Semantik)
   uint16 count;
   FluxEvent ev[FLUX_CAP];    // FLUX_CAP fix, vorallokiert (z.B. 256)
};
```
`count > FLUX_CAP` ⇒ aeltestes/zusaetzliches Event **verworfen + Zaehler**
(beobachtbar, **nicht still**). Events nach `off` sortiert bei Commit.

### Aufnahme
Jede gespielte Note on/off: `off = aktSamplePos mod loopLen`. Append; bei
`seqCommit()` stabil sortieren. `loopLen` fix aus Tempo/Bars (exakte
Volca-Regel zitiert; Struktur fix).

### Wiedergabe (sample-genau)
Cursor `c ∈ [0, loopLen)`. Pro `process()`-Block der Laenge L:
```
für jedes ev mit off ∈ [c, c+L):  trigger an In-Block-Index (off - c)
Wrap: wenn c+L > loopLen → Rest ab 0 weiterverarbeiten, Reihenfolge wahren
c = (c + L) mod loopLen
```
Diskrete Events ⇒ keine Interpolation. **Wrap klickfrei:** ON/OFF an der
Grenze in Reihenfolge ausfuehren; die bestehende klickfreie EG/Gate-Logik
traegt die Huelle (T2-Methodik am Wrap).

### Moduswechsel Step ↔ Flux
Wechselt nur, **welcher Scheduler** noteOn/off erzeugt. Laufende Noten:
sauber durch die bestehende klickfreie Gate-Logik (kein harter Cut) —
exakt spezifiziert; Volca-Verhalten zitiert. Lock-free-Doppelpuffer wie
Step-Pattern.

Edge: leeres Flux-Pattern → Stille (kein Default-Ton); Event genau bei
loopLen-1; mehrere Events im selben Sample (Reihenfolge: OFF vor ON).

Messung: unregelmaessig getimte Folge aufnehmen → jedes Event
sample-genau reproduziert (Jitter 0, T10); Loop-Wrap klickfrei (T2);
Roundtrip exakt; Moduswechsel klickfrei.

---

## E6 — Signalweg-Angleichung

- **VOLUME**: Engine-Master-Gain `g = CALIB(E8)-Kurve(vol)`, angewandt
  **nach VCA, vor Ausgang**, durch den Trichter. Host-`GainNode` entfernt.
  g=Voll → bit-identisch zum jetzigen Ausgang bei Einheitsgain.
- **OCTAVE**: Engine-Pitch-Offset `+12*round(oct)` Halbtoene als
  Parameter (statt Host-Transpose), in den Pitch-Pfad (bandbegrenzt).
- **Velocity**: Original ist fix. Engine setzt jeden Note-Anschlag auf
  `VEL_FIXED = CALIB(E8)`, **unabhaengig** vom UI-Event. Messung: Pegel
  konstant ueber verschiedene UI-Velocities.

---

## E7 — Integration & Voll-Regression

- Neue Params: `PARA3_P_*` ergaenzen; Ring-OPs nur falls noetig (SET_PARAM
  traegt id+double — neue ids brauchen **keinen** neuen OP). Worklet- und
  Port-Transport teilen **eine** `dispatch`-Funktion (kein Logik-Drift).
- `setParamNorm`/`taper` um alle neuen Params erweitern (CALIB(E8)-Taper
  als Platzhalter, Algorithmus fix).
- UI: jeden neuen Regler binden; Liste C schrumpft auf ~leer; Restliche
  ehrlich begruenden.
- **Ein konsolidierter Testlauf**, dokumentiert: offline_test (alle alten
  + alle neuen Assertions aus E1–E6) `OVERALL PASS`; capi_test, ring_test,
  audio_test, wasm_parity PASS; UI-Funktionstest je neuer Regler messbar
  wirksam, Rest-Liste-C nachweislich inert; 0 Warnungen.

---

## Mapping Platzhalter → Sprint E8

Jede `CALIB(E8)`-Konstante hier wird in E8 durch einen Hardware-Messwert
ersetzt; die **Gleichung daneben bleibt unveraendert**. Liste:
`EGINT_OCT_MAX, EG_PIVOT, PHI0[form], CENTS_MAX, K1,K3, TAU(Portamento-
Kurve) [bzw. RATE], F_CLICK, F_CLICK_ACCENT, TAU_CLICK, VOLUME-Kurve,
VEL_FIXED, loopLen-Regel, alle bestehenden Sprint-1-CALIB`.

Ein Wert „nach Gehoer" ist kein Messwert. Algorithmus immer voll
implementieren, Konstante markiert platzhaltern.

---

## ANHANG A — DATEI-POLITIK (verbindlich, kein Spielraum)

Die Engine ist **header-only** (`Para3Engine.hpp`). „Ein DSP-Element
anhaengen" = die **bestehenden Klassen in dieser Datei in-place
erweitern**. Verboten:

- **Kein** neues Engine-Quellfile (keine `oscillator.cpp`,
  `motion.cpp`, `flux.cpp` …). Es gibt keine parallele Oszillator-Unit,
  an die man „andockt" — der Oszillator lebt in `Para3Engine.hpp`.
- **Keine** Umstrukturierung/Aufspaltung des Headers. Keine Parallel-
  klasse „neben" einer bestehenden, wenn die bestehende erweitert werden
  kann (z. B. Detune geht in den vorhandenen Oszillator-Frequenzpfad,
  **nicht** in einen zweiten Oszillatortyp).
- **Kein** Duplizieren vorhandener DSP-Bausteine. PolyBLEP/Decimator/
  Inseln werden wiederverwendet, nicht neu geschrieben.

**Genau diese Dateien werden BEARBEITET:**
- `Para3Engine.hpp` — alle DSP-/Steuer-Erweiterungen (E1–E6).
- `para3_capi.h` / `para3_capi.cpp` — neue `PARA3_P_*`/Funktionen (E7).
- `para3-ring.js`, `para3-worklet.js`, `para3-audio.js` — Protokoll/
  Dispatch/PARAM (E7).
- `para3-responsive.html` — Bindung neuer Regler (E7).
- `offline_test.cpp` — neue Messungen werden **hier ergaenzt** (eine
  Test-Datei, keine Streuung).

**NEU erstellt werden nur:** `wasm_parity.mjs` (bereits in
`CLAUDE_BUILD.md` vorgesehen). Kein weiteres neues File ohne expliziten
Grund im jeweiligen Sprint.

## ANHANG B — ANDOCK-KARTE (welche bestehende Klasse/Methode)

Pro Sprint exakt der Eingriffspunkt in `Para3Engine.hpp`. Erweitern,
nicht ersetzen; bestehende Signatur/Verhalten bei Param=0 bit-identisch.

| Sprint | Andock-Punkt (bestehende Klasse → Stelle) |
|---|---|
| E1.1 EG INT | `ParaEngine`: die Cutoff-Summe **unmittelbar vor** dem `LadderZDF`-Koeffizienten-Update; `Param`/`taper`/`setParamNorm` ergaenzen. Geteilte `AdsrEnvelope` wird nur **gelesen**. |
| E1.2 LFO Sync | `Lfo`: neue `resetPhase()`-Methode; Aufruf im `noteOn`-Pfad von `ParaEngine`/`Controller`. |
| E2.1 DETUNE | `ParaAllocator`/`ParaEngine`: die Stelle, an der die Oszillator-Sollfrequenzen gesetzt werden — Faktor einrechnen. **Kein** neuer Oszillator. |
| E2.2 PORTAMENTO | `ParaEngine`: Pitch-Zustand pro Allokation; Glide-Update vor der Phaseninkrement-Berechnung der vorhandenen `Oscillator`. |
| E3 Motion | `Pattern`-Struct um `MotionLane[]` erweitern; Aufnahme/Wiedergabe-Automat im `Controller` (nutzt vorhandenen sample-genauen Step-Takt + `PatternBank`-Doppelpuffer). |
| E4.1/.2/.3 | `Controller`/`Clock`: Step-Trigger-Flag, Integer-Tempo-Teiler, Active-Step-Skip — in der bestehenden Step-Logik. |
| E4.4 Metronom | Kleiner envelopter Generator als Member in `ParaEngine`, Summierung **in der vorhandenen Output-Stufe** (post-Mixer); Delay-Bypass-Flag im bestehenden Signalweg. |
| E5 Flux | Neues `FluxPattern`-Struct **im Header**; zweiter Scheduler-Pfad im `Controller` neben `PatternBank` (gleicher Doppelpuffer-Mechanismus). Kein neues File. |
| E6 | `ParaEngine`: Master-Gain in der Output-Stufe; Octave-Offset im Pitch-Pfad; fixe Velocity im `noteOn`. |
| E7 | C-API + Bridge-JS + UI wie Anhang A; Tests in `offline_test.cpp`. |

Wenn ein Andock-Punkt unklar ist: **fragen/dokumentieren, nicht
umstrukturieren und nicht neues File anlegen.**

---

## ANHANG C — LABOR-MESSPROTOKOLL (Sprint E8, Auftraggeber)

Hier haengt die einzige echte Hardware an der Messkette. Alles danach
erbt diese Werte — eine schlampige Messung hier ist der ultimative
Blender. Reihenfolge ist Pflicht: **erst die Messkette validieren, dann
das Geraet messen.**

### C.0 — Messkette VOR der Korg validieren (nicht ueberspringen)

Ziel: wissen, was die Kette selbst verfaelscht, bevor man der Korg-
Antwort traut (dasselbe Prinzip wie der Hann-Fenster-Fehler ganz am
Anfang — nur in Hardware).

- **Loopback-Referenz:** ein digital exakt erzeugter Sinus (bekannte
  f, Amplitude) ueber dasselbe Interface ausgeben, physisch Out→In
  schleifen, mit **derselben FFT-/Zeitmethodik wie `offline_test`**
  zuruecklesen. Assertion: gemessene Frequenz < 50 ppm Abweichung,
  Amplitude < 0,1 dB, und der **Eigen-THD+N / Rauschboden der Kette**
  ist charakterisiert und protokolliert (das ist der Boden, gegen den
  Korg-Werte spaeter abgegrenzt werden).
- **Brumm/DC-Check:** Stille aufnehmen; Netzbrumm-Komponenten und
  DC-Offset unter Schwelle, sonst Erdung/Kabel fixen — **nicht**
  wegrechnen.
- **Aufnahmebedingungen protokollieren:** Interface-Modell, Sample-Rate
  ≥ 96 kHz (192 kHz wo Korg-Aliasing/Oberwellen gemessen werden),
  24 bit, Gain-Staging ohne Clipping (Referenzpegel z. B. −12 dBFS),
  kein Eingangs-Processing, Mono-Pfad. Bedingungen sind Teil des
  Messergebnisses.

### C.1 — Geraet (Korg) reproduzierbar einstellen

- **Aufwaermen + Auto-Tune** der Volca Keys vor pitch-kritischen
  Messungen (Analog driftet; sie stimmt sich selbst). Aufwaermzeit
  protokollieren.
- **Mess-Patch:** alle Regler auf dokumentierte Referenzstellungen, so
  dass der Parameter unter Test isoliert ist (Rest fix).
- **Parameter setzen ueber MIDI CC, nicht von Hand.** Die Volca Keys
  empfaengt CC — kleine Regler ohne Raster sind **nicht** reproduzierbar,
  CC-Werte schon. Nur wo es ausschliesslich Panel-Regler ohne CC gibt:
  markierte Referenzstellung **und erhoehte Unsicherheit dokumentieren**.
- **≥ 3 Takes** je Messpunkt; Mittelwert ± Streuung. Streuung > Toleranz
  → Ursache suchen (Aufwaermung, Reglerschlupf, Brumm), **nicht**
  glattmitteln.

### C.2 — Messmethode je Groesse (gleiche Methodik wie `offline_test`)

- **Cutoff (Hz vs CC):** feste helle Quelle, −3-dB-Ecke bzw.
  Spektralschwerpunkt vs. CC → Kennlinie. Fuellt `taper(Cutoff)`-CALIB.
- **Resonanz/Selbstoszillation:** PEAK erhoehen bis reine Eigen-
  schwingung einsetzt; Onset-CC + Resonanz-Gain-Kurve darunter.
- **Drive/Nichtlinearitaet:** bekannter Sinus, THD/Oberwellenstruktur
  vs. Drive.
- **EG-Zeiten:** Note gaten, Amplitudenhuellkurve; Attack (10–90 %),
  Decay/Release-Zeitkonstanten vs. CC.
- **EG INT:** Cutoff-Exkursion (Schwerpunkt-Trajektorie) vs. CC →
  `EGINT_OCT_MAX`, `EG_PIVOT`, **Vorzeichen/Bipolaritaet**.
- **LFO:** Rate (Hz) je Form vs. CC; Pitch-Tiefe (Cents) und
  Cutoff-Tiefe (Oktaven) aus Modulationshub. **Ehrlich:** die LFO-
  Startphase `PHI0` ist extern kaum sauber messbar — falls nicht
  zuverlaessig bestimmbar, als **schwach belegte Konstante** markieren
  (perzeptiv per Retrigger abgeglichen), **nicht** als Messwert
  ausgeben.
- **Delay:** Zeit (ms) vs. CC; Feedback-Abklingen + Bandbreite der
  Wiederholungen (Lo-Fi-Charakter). Nicht zusammen mit Metronom messen.
- **DETUNE:** Schwebungs-/Partialabstand vs. CC → `CENTS_MAX`, K1/K3.
- **PORTAMENTO:** Sprung zwischen zwei Noten, Pitch-Trajektorie messen
  → entscheidet **Modell A (Ein-Pol) vs. Modell B (konstante Rate)**
  aus §E2.2 und liefert die Zeitkonstante. Ergebnis explizit
  zurueckschreiben.
- **Fixe Velocity:** tatsaechlicher Ausgangspegel eines Tastendrucks →
  `VEL_FIXED`.
- **VOLUME:** Ausgangs-Gain vs. Reglerstellung → Kurve.
- **Ringmod:** Traegerverhaeltnis/Spektrum je Ring-Modus.
- **VCO-Drift (optional):** Pitch ueber Zeit/Temperatur — nur falls
  Analog-Verhalten Ziel ist; sonst explizit „out of scope".

### C.3 — Unsicherheit & Abnahme

- Jeder CALIB-Wert wird als **Mittel ± Unsicherheit** eingetragen
  (Takes-Streuung + charakterisierter Ketten-Fehler aus C.0), nicht als
  blanke Einzelzahl.
- Werte in `Para3Engine.hpp` eintragen → **komplette Suite (alle Tests
  E1–E7) erneut gruen**.
- **A/B-Beleg:** PARA·3 mit kalibrierten Konstanten gegen die
  Korg-Aufnahme desselben Patches/Sequenz rendern; gemessenes Delta
  (spektral/zeitlich) je Parameter **protokollieren**. Dieses Delta
  *ist* der Treue-Nachweis — berichtet, nicht behauptet.
- Verboten: ein Take, „sieht plausibel aus", „nach Gehoer". Jede solche
  Stelle ist ein Blender und macht den ganzen Klon-Anspruch ungueltig.

### C.4 — Reproduzierbarkeit

Vollstaendiges Protokoll (Interface, Raten, Pegel, CC-Werte, Takes,
Aufwaermzeit, Unsicherheiten) ablegen, sodass ein Dritter die Messung
nachstellen kann — dieselbe Reproduzierbarkeits-Anforderung wie fuer
den Software-Teil.
