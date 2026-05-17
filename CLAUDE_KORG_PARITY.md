# CLAUDE_KORG_PARITY.md — Restliche Volca-Keys-Funktionen ergaenzen

Auftrag: die unter „Abweichungen" benannten fehlenden KORG-Volca-Keys-
Funktionen nachruesten — Industriestandard, Top-Level, jede Aussage
**gemessen**. **Keine Fakes, keine Blender, keine Helfer-Abkuerzungen.**
Jeder Sprint endet erst, wenn sein Abnahmekriterium *ausgefuehrt* gruen
ist und die volle Regression (Engine 13/13, capi, ring, audio_test,
0 Warnungen) unveraendert haelt. Bei Konflikt mit Treue: Konflikt
**benennen und messen**, nicht stillschweigend glaetten.

Reihenfolge ist Abhaengigkeitsreihenfolge. Sprints E1–E7 sind Code +
Messung. **Sprint E8 (Hardware-Kalibrierung) macht der Auftraggeber** am
Schluss gegen sein echtes Geraet.

Die hinzugefuegte **Swing-Funktion bleibt**, bleibt aber im UI/Doc klar
als „Erweiterung, nicht Original" gekennzeichnet.

---

## Universelle Gates (gelten in JEDEM Sprint, zusaetzlich zu den
## sprint-spezifischen Kriterien)

- **Kein Klick/Knack:** jede neue Wert-/Zustandsaenderung laeuft durch den
  bestehenden Anti-Zipper-Trichter bzw. die klickfreie Gate-Logik.
  Messung: Block-Energie-/Diskontinuitaetstest wie T2/T4, < gemessene
  Klick-Schwelle.
- **Endlich & definiert:** kein NaN/Inf, kein stiller Ausgang wo Signal
  erwartet wird (Methodik WA5).
- **Kein Trichter-Bypass:** jeder neue Parameter geht durch
  `setParamNorm`/Protokoll/Ring/Worklet — eine Steuerquelle. Kein
  Direktzugriff aus der UI auf die Engine.
- **Bit-exakter Roundtrip** wo Aufnahme/Wiedergabe existiert (Methodik
  T12, Reproduktion 0,0000).
- **Stabilitaet im Worst Case:** neuer Parameter auf Maximum + bestehende
  Worst-Case-Kombi (max Q/Drive) bleibt energiebeschraenkt (Methodik T5).
- **Keine Regression:** komplette bestehende Suite erneut gruen,
  0 Compilerwarnungen, kein `-ffast-math`.
- **Anti-Blender:** ein nicht verdrahteter/dekorativer Regler kann das
  gemessene Signal nicht aendern — das ist der Pruefstein jedes Features.

---

## SPRINT E1 — Modulationspfade vervollstaendigen (EG INT, LFO Trigger Sync)

**Ziel.** Zwei fehlende Modulationswege in bestehende Komponenten.

**E1.1 EG INT (Huellkurve → Cutoff).**
- Architektur: die bereits vorhandene, klickfreie geteilte EG liefert
  zusaetzlich einen skalierten Beitrag auf die Filter-Cutoff-Summe
  (Cutoff = Trichter + LFO·Tiefe + **EG·EgInt**). Neuer Param `EgCutDepth`,
  **bipolar** (Invertierung moeglich, wie am Original), Taper als
  CALIB(sprint1)-Platzhalter, Wert durch den Trichter.
- Messung: kurzzeit-spektraler Schwerpunkt folgt der ADSR-Form der EG
  (Korrelation ≥ Schwelle); EgInt=0 → Cutoff statisch == reiner Trichter
  (bit-identisch); negatives EgInt invertiert die Bewegung; klickfrei;
  EgInt max + Resonanz max bleibt stabil (T5-Methodik).

**E1.2 LFO Trigger Sync.**
- Architektur: bei `noteOn` und aktivem Trigger-Sync wird die LFO-Phase
  auf den definierten Startpunkt gesetzt. **Ehrlicher Konflikt:** ein
  harter Phasensprung kann in der modulierten Groesse eine Diskontinuitaet
  erzeugen. Loesung ohne Blender: der resultierende Modulations-Offset
  laeuft durch den bestehenden Anti-Zipper-Pfad (schnell, aber nicht
  instantan) — der LFO „startet hoerbar neu", erzeugt aber **keinen harten
  Klick**. Der Kompromiss wird dokumentiert und gemessen, nicht versteckt.
- Messung: LFO-Phase == Start bei jedem `noteOn` (aktiv); Klicktest bei
  hoher LFO-Tiefe auf Pitch und Cutoff < Schwelle; deaktiviert →
  frei laufende Phase (Phase ≠ Start nachweisbar).

**Definition of Done E1:** beide Messungen PASS, Regression intakt.

---

## SPRINT E2 — Tonhoehen-Verhalten (DETUNE, PORTAMENTO)

**Ziel.** Die zwei praegenden Pitch-Domain-Funktionen — bandbegrenzt
bleiben ist Pflicht.

**E2.1 DETUNE (VCO-Spreizung).**
- Architektur: Live-Parameter, der OSC 2/3 gegen OSC 1 um ±Cents
  (CALIB-Skala) verstimmt, durch den Anti-Zipper-Pitch-Pfad. Wirkt in
  allen Voice-Modi (insb. Unison/Poly — der typische dicke Keys-Charakter).
- Messung: Schwebungsfrequenz zwischen Partialgruppen (FFT)
  steigt monoton mit Detune; **Anti-Aliasing bei max Detune erneut
  gemessen** (muss auf dem −74-dBc-Niveau bleiben, nicht nur „klingt
  ok"); Aenderung klickfrei; Detune=0 → exakt deckungsgleiche Oszillatoren.

**E2.2 PORTAMENTO (Glide).**
- Architektur: Ziel-Pitch bei `noteOn`, aktuelle Pitch slewt mit
  Zeitkonstante (Param `Portamento`, CALIB-Kurve) zum Ziel. Time=0 →
  instantan == bisheriges Verhalten (bit-identisch). Glide-Modell
  (immer-an vs. legato) nach Manual; exakte Kurve = Sprint E8.
- Messung: Momentanfrequenz (Phasen-/Nulldurchgang) gleitet in der
  gesetzten Zeit vom alten zum neuen Ton; **Spektrum waehrend eines
  schnellen Glides gemessen** → kein Aliasing (Bandbegrenzung haelt waehrend
  des Sweeps); Time=0 bit-identisch zum Ist-Zustand.

**Definition of Done E2:** beide Messungen PASS inkl. Anti-Alias-Re-Messung,
Regression intakt.

---

## SPRINT E3 — Motion Sequence vollstaendig (4 Abweichungen schliessen)

**Ziel.** Vom Cutoff-only-Modell zum originalgetreuen Verhalten.

**E3.1 Multi-Regler-Lanes.** Pro motion-faehigem Parameter eine eigene
16-Step-Lane, sparse allokiert (nur bei Nutzung). Daten fliessen durch
denselben Trichter (kein Bypass).

**E3.2 Ein-Schleifen-Auto-Abschaltung.** Per-Parameter-Zustandsautomat:
Reglerbewegung waehrend Record → Aufnahme genau eine volle Schleife ab
dem aktuellen Step, dann automatisches Disarm dieses Parameters
(Original-Verhalten laut Manual).

**E3.3 SMOOTH an/aus (global).** SMOOTH an → Lane-Werte werden ueber die
Step-Dauer interpoliert (kontinuierliche Rampe). SMOOTH aus → stufiger
Wert am Step-Anfang, aber durch den klickfreien Trichter (stufige Kontur,
kein Zipper).

**E3.4 PEAK/TEMPO-Ausschluss.** PEAK (Resonanz) und TEMPO sind nicht
motion-aufnahmefaehig (Original). Hart erzwingen.

- Messung: je Parameter Roundtrip 0,0000 (T12-Methodik); Auto-Abschaltung
  exakt nach einer Schleife (Sample-Genauigkeit, T10-Methodik); SMOOTH an
  messbar kontinuierliche Rampe vs. aus messbar Stufen; PEAK/TEMPO
  verweigern Aufnahme nachweislich; mehrere Lanes gleichzeitig unabhaengig
  und exakt; lock-freier Pattern-Tausch (T13-Methodik) weiter gueltig.

**Definition of Done E3:** alle obigen Messungen PASS; Liste C im
UI-Runbook entsprechend verkleinert (Motion-Eintraege entfallen).

---

## SPRINT E4 — Sequencer-Verhalten (Step Trigger, Tempo-Division, Active
## Step exakt, Metronom)

**E4.1 STEP TRIGGER.** Toggle: an → EG-Retrigger an jeder Step-Grenze
(auch bei gehaltenen/gebundenen Noten), ueber die klickfreie Gate-Logik.
Messung: Amplituden-Huellkurve zeigt Attack-Transiente an jeder
Step-Grenze; aus → unveraendert; klickfrei.

**E4.2 Tempo-Division (1/1, 1/2, 1/4).** Ganzzahliger Teiler auf den
sample-genauen Step-Takt. Messung: Step-Intervall skaliert exakt ×2/×4,
Jitter weiterhin 0, bit-identisch zur Soll-Rechnung.

**E4.3 Active Step exakt.** Deaktivierte Steps werden bei **Wiedergabe
und Aufnahme** uebersprungen (Manual-Semantik). Messung: deaktivierter
Step erzeugt weder Note noch Motion-Anwendung, in beiden Modi.

**E4.4 Metronom.** Click auf Beats; bei aktivem Metronom **Delay
deaktiviert** (Original). Messung: Click an korrekten Positionen; Delay
nachweislich umgangen waehrend aktiv; klickfrei; aus → unveraendert.

**Definition of Done E4:** vier Messungen PASS, Regression intakt.

---

## SPRINT E5 — FLUX-Modus (nicht-quantisierte Loops)

**Ziel.** Eigenes, ereignisbasiertes Pattern-Subsystem parallel zum
16-Step-Modell.

**Architektur.** Statt 16 quantisierter Steps eine Liste zeit-gestempelter
Ereignisse (Note on/off mit Sample-Offset in der Loop-Laenge). Wiedergabe
spielt Ereignisse am exakten Sample-Offset, nahtlos geloopt. Lock-freier
Doppelpuffer wie beim Step-Pattern. Umschaltung Step↔Flux ohne Klick.

**Messung.** Eine Folge unregelmaessig getimter Ereignisse aufnehmen,
wiedergeben → Zeitstempel sample-genau reproduziert (Jitter 0,
T10-Methodik); Loop-Grenze ohne Klick (T2-Methodik); Roundtrip exakt;
Moduswechsel klickfrei.

**Definition of Done E5:** Messungen PASS, Regression intakt.

---

## SPRINT E6 — Signalweg-Angleichung (VOLUME, OCTAVE, Velocity)

**Ziel.** Funktionale Treue im Signalweg statt host-seitiger Behelfe.

- **VOLUME** als Engine-Master-Gain (nach VCA, vor Ausgang), durch den
  Trichter. Messung: linear/au. CALIB-Kurve, klickfrei, Bypass des
  host-`GainNode` entfernt.
- **OCTAVE** als Engine-Pitch-Offset-Parameter (statt host-Transpose).
  Messung: exakte Oktav-Verschiebung in der Engine; bandbegrenzt.
- **Tastatur-Velocity.** Das Original ist fix-velocity. Engine modelliert
  einen festen Anschlagspegel (exakter Pegel = Sprint E8). Messung:
  gespielte Note immer auf definiertem Pegel, unabhaengig vom UI-Event.

**Definition of Done E6:** Messungen PASS; Liste C entsprechend
verkleinert; Regression intakt.

---

## SPRINT E7 — Integration & Voll-Regression

**Ziel.** Alle neuen Parameter durch C-API + Ring-Protokoll + Worklet +
UI fuehren; eine Steuerquelle; Liste C minimal.

- C-API: neue `PARA3_P_*`/Funktionen; Ring-OPs falls noetig; Worklet-
  Dispatch erweitert (gemeinsame `dispatch`-Funktion, kein Logik-Drift
  zwischen Ring- und Port-Transport).
- UI: `para3-responsive.html` an die neuen Parameter binden (Tabelle
  pflegen). Liste C sollte fast leer sein; was bleibt, ehrlich begruenden.
- **Voll-Regression ausgefuehrt:** `offline_test` (alle bestehenden +
  alle neuen Tests aus E1–E6) `OVERALL PASS`; `capi_test`, `ring_test`,
  `audio_test` PASS; 0 Warnungen; `wasm_parity` (kompiliertes WASM ==
  nativ, Sample-fuer-Sample) PASS; UI-Funktionstest (siehe separates
  Runbook) fuer jeden neuen Regler messbar wirksam, fuer Rest-Liste-C
  nachweislich inert.

**Definition of Done E7:** alle Suites gruen, ein einziger
konsolidierter Testlauf dokumentiert.

---

## SPRINT E8 — Hardware-Kalibrierung (AUFTRAGGEBER, am Schluss)

**Ziel.** Alle `// CALIB(sprint1)`-Platzhalter durch gegen das echte
KORG Volca Keys **gemessene** Werte ersetzen. Erst hiernach gilt
„klanglich exakte Kopie".

**Zu messende Groessen (Minimalliste):**
- Filter-Cutoff in Hz an definierten Reglerstellungen; Resonanz/
  Selbstoszillations-Schwelle; Filter-Nichtlinearitaet/Drive-Kurve.
- EG-Zeiten (Attack/Decay-Release) bei Min/Max; Sustain-Kennlinie.
- EG-INT-Skala (bipolar) in Cutoff-Hub.
- LFO-Ratenbereich (Hz), LFO-Pitch-Tiefe (Cents/Halbtoene), LFO-Cutoff-
  Tiefe (Oktaven), pro LFO-Form.
- Delay-Zeitbereich (ms) und Feedback-/Mix-Charakter.
- DETUNE-Cents-Bereich; PORTAMENTO-Zeitbereich + Kurvenform.
- Fixer Velocity-/Anschlagspegel; Master-Volume-Kurve.
- Ringmod-Charakter (Trraegerverhaeltnis/Spektrum) je Ring-Modus.
- VCO-Verstimmung/Drift-Charakter (fuer optionales Analog-Verhalten).

**Verfahren.** Definiertes Testsignal/Notenmuster auf echtem Geraet,
Aufnahme, Analyse mit derselben FFT-/Zeitmethodik wie `offline_test`;
Werte in `Para3Engine.hpp` eintragen; **komplette Suite (alle Tests aus
E1–E7) erneut gruen stellen**. Werte „nach Gehoer" sind **kein** Messwert
und nicht zulaessig.

**Definition of Done E8:** alle CALIB-Werte ersetzt, volle Suite gruen,
A/B gegen Hardware dokumentiert.

---

## Was nach E1–E8 noch bewusst NICHT Original ist

- **Swing** bleibt als gekennzeichnete Erweiterung (nicht auf der echten
  Keys) — Entscheidung des Auftraggebers, ehrlich im UI/Doc markiert.
- Analog-Drift/Warmlaufen nur falls in E8 explizit als Ziel gemessen;
  sonst bewusst weggelassen und so benannt.

## Eiserne Regeln

- Jede Behauptung = ausgefuehrter Test mit Kriterium. Kein „nah genug".
- Ein nicht verdrahteter Regler darf das gemessene Signal nicht aendern.
- Treue-Konflikte werden benannt und gemessen, nie stillschweigend
  geglaettet.
- Nach jedem Sprint volle Regression; ein roter Test stoppt den Fortschritt.
