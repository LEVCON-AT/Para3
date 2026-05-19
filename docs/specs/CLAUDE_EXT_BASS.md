# CLAUDE_EXT_BASS — „Fetter" Volca-Bass-Charakter (Erweiterung, NICHT Parität)

> **Zuerst lesen.** Die echte Volca *Keys* ist kein Bass-Synth. Dieser Block
> bringt den **satten Charakter** der Volca *Bass* technologisch in PARA·3 —
> als bewusste, klar markierte **Erweiterung** (`// EXT-BASS`), **Default
> AUS**, und „AUS ⇒ Engine bitidentisch zum aktuellen Stand"
> (`max|d| = 0.000e+00`). Der getreue Keys-Kern bleibt unangetastet.
> Stil/Disziplin wie `CLAUDE_EXT_ARP.md`. Antworten an den Menschen: Deutsch.

Architektur-Wahrheit (am hochgeladenen Code verifiziert): PARA·3 ist **eine**
geteilte paraphone Stimme — `ParaEngine` hat `osc_[3] → ein ladder_ →
ein env_`. „Bass" ist **kein zweiter Klang**, sondern eine andere
*Konfiguration* dieser einen Stimme. Layering „fetter Bass-Loop + Keys live"
ist deshalb ein **App-/Host-Thema** (zwei Instanzen) — §6, nicht eine zweite
Stimme in der Engine. Flux/F-Rec bleibt ereignisbasiert (kein Audio-Loop) —
das ist hardware-treu zur echten Bass.

---

## §0 Grundregeln (verbindlich)

1. **Marker** `// EXT-BASS` an JEDER neuen Zeile (nie `// E1..E7`).
2. **Default AUS.** Solange aus, ist jeder Pfad exakt der bisherige Code.
   Haupt-Akzeptanz: „aus ⇒ bitidentisch" (`max|d|=0`) je Block messbar.
3. **Datei-Politik.** Nur: `Para3Engine.hpp`, `wasm-bridge/para3_capi.{h,cpp}`,
   `offline_test.cpp`, `wasm-bridge/capi_test.cpp`,
   `wasm-bridge/build_wasm.sh` (nur Exporte), das EXT-BASS-Frontend-Panel
   (siehe §5, im Rahmen von `CLAUDE_FRONTEND_PARITY.md`),
   `tests/e2e/*` (Playwright), `wasm-bridge/ui_test.mjs` nur für §7-Fixes,
   sowie dieser Spec. **Kein** neues Engine-`.cpp`, **kein** Refactor,
   header-only. Das bandlimitierte Erweitern von `PolyBlepCore`/`Oscillator`
   in `Para3Engine.hpp` ist erlaubt — Saw-Pfad muss bitidentisch bleiben.
4. **RT-safe.** Keine Allokation/Lock/Exception/Syscall in `render`.
   Determinismus: jeder Zufall (Drift) über seedbaren xorshift, kein `rand()`.
5. **Durch den Trichter.** Kontinuierliche Klangparameter (PW, PWM-Tiefe,
   Bass-Spread, Drift-Tiefe/-Rate, Sub-Level) sind echte
   `setParamNorm`-Parameter (Taper → RampParam, neutral gesnappt). Diskrete
   Moduswahl (Waveform Saw/Pulse, Stack, Bass-Master) sind
   Controller-Settings mit eigenen Hooks (wie Tempo/Arp), NICHT im
   Param-Trichter, NICHT in `KNOB_PARAM`/`INERT_KNOBS`.
6. **Klickfrei & bandbegrenzt.** Pulse/PWM/Sub müssen bandlimitiert sein
   (PolyBLEP-Disziplin), Alias gemessen ≤ −74 dBc (Methodik T1). Treue-
   Konflikte benennen, nicht stillglätten.
7. **0 Compilerwarnungen, kein `-ffast-math`.**
8. **Ein Block = ein Test = ein Commit.** Volle Regression grün vor dem
   nächsten Block (siehe §9).
9. **`// EXT-BASS DEFAULT`-Konstanten** sind Design-Defaults (kein HW-Vorbild
   für einen *Keys*-Bass) — ausdrücklich **nicht** `CALIB(E8)`.
10. **Andock-Anker re-verifizieren.** Zeilennummern unten gelten für den
    hochgeladenen Eval-Stand; vor dem Editieren im aktuellen Repo prüfen
    (Anhang-B-Disziplin). Test-Indizes (`T<n>`)/Param-IDs NICHT raten:
    aktuell höchsten `T`-Index bzw. nächste freie `Param`-Id + `paramOfId`/
    `kArpModePid`-Map im Repo ermitteln und kollisionsfrei anhängen.

---

## §1 Woher der „satte" Klang kommt (Bausteine)

**Vorbild = Volca Bass (strukturell), Treuebeweis = bewusst offen.** Die
Bausteine modellieren das *Synthese-Modell* der Volca Bass (3 VCOs,
Saw/Pulse, Sub, bass-typischer Filtereinsatz, EG-Hub) anhand öffentlich
dokumentierten Verhaltens. Es gibt aktuell **kein** gemessenes Bass-
Hardware-Target (Entscheidung des Menschen: „nein/später"). Darum sind alle
Zielwerte `// EXT-BASS DEFAULT` (Design), **nicht** `CALIB(BASS)`. Was für
einen späteren echten Treuebeweis fehlt, steht explizit in §8b — diese
Lücke ist zu benennen, nicht zu kaschieren.

Messbar fundiert (siehe §3). Fünf Bausteine, einzeln zuschaltbar, plus der
schon vorhandene perkussive Sweet-Spot **EG-INT (E1.1)** — den nur stärker
einsetzen, nicht neu bauen.

- **B1 Pulse/Square-Wellenform** — anderer Obertongehalt als Saw (Material
  für Filterdruck). Pro Oszillator wählbar.
- **B2 Pulsweite + PWM** — Pulsbreite + modulierbare Breite (lebendige,
  „atmende" Fülle).
- **B3 Bass-Spread + Drift** — stärkerer VCO-Stimmversatz (Schwebung) plus
  langsamer analoger Pitch-Wander (Imperfektion, die ein Sample nie hat).
- **B4 Stack/Mono-Allocator** — 3 Oszillatoren monophon auf eine Note
  gestapelt (statt paraphoner Verteilung) = Bass-Bauprinzip.
- **B5 Sub-Oszillator** — eine (zwei) Oktave(n) tiefer, durch den geteilten
  Filter/VCA geformt → fundamentale Tiefe.

---

## §1b Öffentlich belegte Volca-Bass-Referenz (gegen DIESES messen)

Damit das Prinzip „gemessen, nicht behauptet" erhalten bleibt, wird gegen
**öffentlich dokumentierte, zitierbare** Volca-Bass-Daten gemessen — nicht
gegen Schätzungen. Drei-Stufen-Markierung, je Konstante/Akzeptanz eindeutig:

- `// EXT-BASS REF` — durch öffentliche Quelle belegt (KORG-Spezifikation /
  Vintage Synth Explorer / MusicRadar). Akzeptanz = messbarer Korridor
  gegen den belegten Wert; Quelle im Kommentar nennen.
- `// EXT-BASS DEFAULT` — bewusste Design-Wahl **ohne** Volca-Bass-Beleg
  (Erweiterung über das Vorbild hinaus). Nicht als „bass-treu" bezeichnen.
- `CALIB(BASS)` — bräuchte echte Hardware-Messung; **bleibt offen** (§8b),
  wird NICHT erfunden.

Belegte Referenz (Quellen siehe §10):

| Aspekt | Öffentlich belegter Wert | Stufe |
|---|---|---|
| Topologie | 3 VCO → 1 VCF → 1 VCA, + 1 LFO, 1 EG (deckt sich mit PARA·3-Einzelstimme) | REF |
| VCO-Wellen | je VCO **Saw** oder **Square** | REF (B1) |
| Detune-Mapping | fein bis ±50 Cent (Viertelton), darüber Halbtonschritte bis ±1 Oktave; Tonumfang > 6 Oktaven | REF (B3-Spread) |
| VCO-Gruppen | (a) 3 unabhängig, (b) 2+1, (c) alle 3 unisono/gestapelt; Akkord-Bsp. 0/+3/+7 | REF (B4) |
| VCF | resonanter LP, **12 dB/Oct**, MiniKORG-700S-abgeleitet, self-oscillation bei hohem Peak; kein Cutoff-Keytracking; stumm wenn Osz gemutet | REF (Filtereinsatz) |
| Peak/Resonanz | sauberer Peak → bei höherer Stellung musikalische Übersteuerung, bass-getrimmt | REF |
| EG | ADR: Attack, Decay/Release (kombiniert), Sustain **on/off ohne Pegel**; EG→Cutoff via „EG Int"; EG→Amp on/off ohne Tiefe; sonst Amp = Gate on/off | REF |
| LFO | Wellen **Triangle/Square**; Ziele Amp/Pitch/Cutoff (komb.); bis Audio-Rate | REF |
| Slide | TB-303-artig, **pro Step**, editierbar | REF |
| Self-Tuning | Oszillatoren autostimmen (gegen Drift) | REF |
| Accent | **kein** Accent vorhanden | REF |
| Steuersignale | digital erzeugt, 10-bit-DAC (PARA·3 ist ohnehin digital — kein Analog-Claim nötig) | REF (Kontext) |

**Ehrliche Konsequenz für unsere Bausteine:**
- B1 (Square) = REF — exakt belegt. B4 (Stack=„alle unisono", auch „2+1")
  = REF — die Gruppenmodi sind dokumentiert.
- B3-Spread Detune-**Mapping** = REF (≤50 Cent → Halbton → ±1 Oktave).
- VCF-Verhalten (12 dB/Oct, Self-Osc, bass-getrimmte Übersteuerung) = REF
  als messbare Korridore.
- **NICHT vom Volca Bass belegt** (= bleiben `// EXT-BASS DEFAULT`,
  ausdrücklich Erweiterung, nicht „bass-treu"): **B2 PWM/Pulse-Breite**
  (Spec kennt nur Saw/Square), **B5 Sub-Oszillator** (kein Sub dokumentiert),
  **B3-Drift** (das echte Gerät *entfernt* Drift via Self-Tuning — bewusster
  Charakter-Zusatz, Default 0).

---

## §2 DSP-exakt je Baustein (Andock-Stellen am Eval-Stand)

Bezug: `Oscillator`@188 (Saw-only via `PolyBlepCore`@107),
`ParaAllocator`@474 (`Mode`@481), `ParaEngine` Osz-Loop ≈ Z. 872–904
(`pMod`, `ds`=E2.1, `mix += osc_[v].process(hz)`), `Param`-Enum@819,
`taper`@~825, `setParamNorm`-Routing, `detune_`@947 (E2.1 — **unangetastet**).

### B1 Pulse-Wellenform
- `PolyBlepCore`/`Oscillator` um eine **bandlimitierte Pulse** erweitern
  (zwei PolyBLEP-Kanten bzw. Differenz zweier bandlimitierter Sägezähne mit
  Phasenversatz = Pulsbreite). Gleiches OS/Decimator-Regime wie der Saw.
- Pro-Osz Waveform-Wahl: Controller-Setting
  `setOscWave(int osc, int wave)` (0=Saw default, 1=Pulse). Saw = exakt der
  bisherige Codepfad ⇒ bitidentisch.

### B2 Pulsweite + PWM
- Param `BassPulseWidth` (unipolar, Taper auf z. B. 0.05..0.95, Default
  0.5), Param `BassPwmDepth` (unipolar, Default 0). PWM-Quelle: vorhandener
  LFO und/oder EG und/oder Drift (B3) — Quelle als Controller-Setting
  wählbar. Beide Kanten der Pulse bandlimitiert mit der modulierten Breite.
- Neutral: Waveform=Saw ⇒ PW/PWM ohne Wirkung; zusätzlich PWM-Tiefe 0 und
  PW 0.5 ⇒ auch bei Pulse statisch. „Aus ⇒ bitidentisch".

### B3 Bass-Spread + Drift
- **Spread:** eigener `RampParam bassSpread_` (EXT-BASS), additiv **auf**
  `ds` (E2.1) im Osz-Loop: `... + pMod ± (ds + bassSpread_.next())`.
  `detune_` (E2.1) bleibt unverändert (faithful-Regler bitidentisch).
  Param `BassSpread` (unipolar, Default 0).
- **Drift:** pro Osz ein langsam bandbegrenzter Zufalls-Pitch-Offset:
  seedbarer xorshift → Einpol-Tiefpass (Rate-Param) → ×Tiefe-Param,
  Summe in den `hz`-Term je Osz. Params `BassDriftRate`, `BassDriftDepth`
  (Default 0 ⇒ bitidentisch). Seed `// EXT-BASS DEFAULT`, via C-API setzbar
  (Reproduzierbarkeit, Test).

### B4 Stack/Mono-Allocator
- `ParaAllocator::Mode` um `Stack` erweitern **oder** ein Override-Flag
  `setBassStack(bool)`. Bei aktiv: `resolve()` setzt für alle 3 Osz die
  *newest* Note (alle aktiv) — 3 gestapelte (durch B3 verstimmte) Osz, mono.
  Übersteuert den Voice-Modus, solange an. Aus ⇒ bestehende Modi exakt
  unverändert ⇒ bitidentisch.

### B5 Sub-Oszillator
- Zusätzlicher bandlimitierter Osz (Square oder Sine, `// EXT-BASS DEFAULT`)
  bei Note −12 (bzw. −24, wählbar), Pegel `RampParam bassSub_`.
  Summe **vor** dem Filter in `mix` (wird vom geteilten VCF/VCA geformt —
  hardware-treues „fundamentales" Verhalten). Param `BassSubLevel`
  (unipolar, Default 0 ⇒ `mix += 0.0` exakt ⇒ bitidentisch).
- Bandlimitierung des Sub gemessen (Alias ≤ −74 dBc) — tiefe Frequenz, aber
  Kanten müssen sauber sein.

### Master
- Controller-Setting `setBassMode(bool on)` + `bassPreset()` der eine
  kohärente „fette" Default-Konfiguration setzt (Pulse, moderater
  Spread/Drift, leichtes PWM, Sub an, Stack an) — **nur** über die o. g.
  Setter/Params (kein Sonderpfad). Aus ⇒ alle Teilblöcke neutral ⇒
  bitidentisch.

---

## §3 Messbatterie (gemessen, nicht behauptet)

Neue `T<n>`-Blöcke in `offline_test.cpp` (Index hinter dem aktuell höchsten),
`WA<n>` in `capi_test.cpp`. Methodik wie bestehende Suite (Onset/RMS/FFT-Peak
mit Parabel-Interpolation/Klick-\|dx\|/kohärenter Alias-FFT wie T1/T16/T2).

Pro Baustein **immer drei** Pflichtmessungen:
1. **Wirkung** messbar vorhanden (s. u.).
2. **Neutral bitidentisch**: Baustein aus / Default ⇒ `max|d| = 0` gegen
   den unangetasteten Stand.
3. **Klickfrei** (Toggle/Parameterfahrt, \|dx\|-Metrik) **und** Alias
   ≤ −74 dBc wo Wellenform/Sub betroffen (kohärente FFT, T1-Methodik).

Wirkungsmessungen:
- B1: Pulse-Spektrum zeigt die erwartete Kammstruktur (FFT), Saw-Default
  bitidentisch.
- B2: PW-Variation verschiebt Obertonbalance monoton; PWM erzeugt periodische
  spektrale Modulation der erwarteten Rate.
- B3: Schwebungs-Hz steigt monoton mit Spread (wie T16); Drift erzeugt
  bandbegrenzten, beschränkten Pitch-Wander der spezifizierten Rate/Tiefe
  (statistische Schranke + Spektralsignatur), reproduzierbar bei festem Seed.
- B4: strukturell — alle 3 Osz auf newest Note (Frequenzverhältnisse),
  Voice-Modus übersteuert; aus ⇒ Modi unverändert.
- B5: zusätzliche Energie bei −12/−24 Halbtönen (FFT-Peak), Default-Level 0
  bitidentisch, Sub bandlimitiert.

**Benannter Treue-Konflikt — asymmetrische Pulse-Alias (B1/B2):** die in §2
geforderte Same-OS-Regime (PolyBLEP-2 + 4× Kaiser-Decimator wie Saw) trifft
bei symmetrischer Pulse (PW=0.5) das ≤ −74 dBc-Saw-Budget genau (T51, T57
gemessen −76.8 dBc). Bei asymmetrischer PW (≠ 0.5) trägt die Pulse alle
Harmonischen mit langsamerem 1/n-Abfall; das Same-OS-Regime liefert dort
~−64 dBc statt −74. Per CLAUDE.md §0.6 wird dieser Konflikt **benannt**,
nicht still weggeglättet: T57 verwendet zwei Schwellen — −74 dBc bei PW=0.5
(Saw-Budget) und −60 dBc bei PW=0.10/0.90 (BLEP-2-Grenze im Same-OS-Regime).
Abhilfe wäre höhere BLEP-Ordnung oder 8× OS, würde aber das in §2 B1
geforderte Same-OS-Regime verlassen; −60 dBc liegt praktisch weit unter
audible-Alias (~−40 dBc), also hörbar bandbegrenzt.

**Fett-Stellvertreterkorridore** (`// EXT-BASS DEFAULT`-Zielkorridore,
**kein** Treue-Score): spektrale Fülle/Partialzahl gegen Sinus-Baseline,
Tiefband-Energie & Spektralschwerpunkt, **AM-Tiefe & -Rate** aus
Spread/Drift, Resonanzspitzen-Energie. Diese belegen „die fettmachende DSP
ist nachweislich da und spezifikationsgemäß" — **nicht** perzeptive
Äquivalenz zur echten Bass (ehrliche Grenze, §8).

---

## §4 C-API (alle neuen Symbole in `build_wasm.sh` exportieren)

- Kontinuierliche Params: neue `Param`-Enum-Werte hinter `Volume` anhängen
  (bestehende IDs NIE umnummerieren; `paramOfId`-Map + `kArpModePid=16`
  prüfen, kollisionsfrei). Über vorhandenes `para3_set_param`.
- Diskret: `para3_bass_mode(p,on)`, `para3_bass_preset(p)`,
  `para3_osc_wave(p,osc,wave)`, `para3_bass_stack(p,on)`,
  `para3_bass_drift_seed(p,seed)`, `para3_bass_pwm_src(p,src)`.
- `capi_test` `WA<n>`: alle neuen IDs/Funktionen sweepen (finite,
  beschränkt; „aus" ⇒ Ausgabe wie ohne Aufruf).

---

## §5 Frontend — wie das mit der bestehenden Oberfläche konform geht

**Antwort auf die offene Frage:** Die aktuelle `para3-responsive.html` ist
bewusst das **originalgetreue Volca-Keys-Layout** und bildet **nichts**
davon ab. Es wird **nicht umgebaut** (`CLAUDE_FRONTEND_PARITY.md`,
No-Restructure). Stattdessen ein **separates, klar als „EXT · BASS
CHARACTER" badge-markiertes Panel**, optisch vom getreuen Bereich abgesetzt,
Untertitel „Erweiterung – nicht Teil der Volca-Keys-Treue".

Visueller Entwurf (mitnehmbar, interaktiv, mit Mapping-Annotationen):
`docs/ui/EXT-BASS_UI_ENTWURF.html` — verbindlich für Layout/Disziplin,
nicht für Pixel; Umsetzung gegen `para3-responsive.html` per
`CLAUDE_FRONTEND_PARITY.md`.

Bedienmodell = **beides** (wie gewünscht):
- Oben ein **Master**: Enable-Toggle + „Fat Preset"-Knopf (ein Tipp →
  kohärente fette Konfiguration via `bassPreset()`). Aus = Panel
  ausgegraut/eingeklappt ⇒ Engine bitidentisch.
- Darunter **Feinregler**: Waveform Saw/Pulse (pro Osz), Pulse Width,
  PWM Depth + PWM-Quelle, Bass Spread, Drift Rate/Depth, Sub Level,
  Stack-Toggle (zeigt Voice-Mode-Wähler sichtbar als „überstimmt").
- Mapping-Disziplin: kontinuierliche Regler sind echte
  `setParamNorm`-Params, erscheinen aber **nur im EXT-Panel** (NICHT in die
  getreuen `KNOB_PARAM`/`INERT_KNOBS` des Keys-Bereichs befördern).
  Diskrete Schalter = Controller-Hooks.
- Optionaler Read-only-„Character"-Indikator (Schwebung/Spektralfülle auf
  dem vorhandenen Scope) — klar als **Indikator** beschriftet, kein
  Treue-Score.

Playwright-User-Stories (Szenario-Gate, recherchieren→vorschlagen→abnicken):
mind. **US-BASS-COLD** (Panel aus ⇒ E2E-Ausgabe bitidentisch zum getreuen
Keys, über die volle Kette), **US-BASS-ENABLE** (Master an ⇒ Fett-Proxies
messbar im Korridor), **US-BASS-NOCLICK** (Enable/Disable ohne Naht-Klick).

---

## §6 Zwei-Instanzen-Layer: „fetter Bass-Loop + Keys live drüber"

Ehrlich: kein Einzel-Engine-DSP-Thema, sondern App-/Host-Ebene — exakt wie
real eine Volca Bass **neben** einer Volca Keys.

- Zwei PARA·3-Instanzen (die C-API trägt mehrere `Para3*`-Handles bereits):
  Instanz A = Bass-Config + ihr **Flux-Event-Loop** (kein Audio-Loop),
  Instanz B = Keys-Config, live gespielt.
- **Gemeinsamer Transport:** eine Instanz ist Clock-Master oder ein
  geteilter Tempo/Start-Broadcast; Flux-Loop von A läuft sample-genau
  synchron zu B. Sync gemessen (Sample-Offset 0 über N Loops).
- **Mix-Bus** im Frontend (zwei Worklet-Nodes → Summe). Pegel je Instanz.
- EXT-BASS-Config ist **pro Instanz**.
- Messbar: beide Instanzen finite; Sync sample-genau; Mix summiert korrekt;
  „Layer aus" (eine Instanz) ⇒ bitidentisch zur Einzel-Instanz.
- Grenze (ehrlich): echtes Layering wie zwei Geräte, **Addition auf
  App-Ebene** — nicht eine zweite Stimme in der Engine, kein Audio-Loop.

---

## §7 FIX-Block — die zwei realen `ui_test.mjs`-Defekte (`// FIX-UI`)

Im Eval-Stand schlägt `node wasm-bridge/ui_test.mjs` mit **2 Fehlern** fehl
(restliche Suiten grün). Beide hier mitbeheben:

### FIX-1 — Engine-Datei-Drift-Wächter ausgelöst
`para3-worklet.js`-Hash weicht ab (Guard meldet `want b9951f… got 079c1c…`):
ein UI-Sprint hat die Worklet-/Transport-Datei verändert.
- Den konkreten Diff von `para3-worklet.js` gegen den Referenzstand
  bestimmen. **Entscheiden:** (a) Änderung ist sachlich engine-/transport-
  notwendig → bewusst legitimieren: Referenz-Hash in `ui_test.mjs` mit
  **kommentierter Begründung** (was/warum) aktualisieren; (b) Änderung war
  unbeabsichtigter UI-Seiteneffekt → aus der Engine-/Transport-Datei
  **zurücknehmen** und in die UI-Schicht verlagern.
- Regel danach festschreiben: Engine-/Transport-Dateien ändern sich nur aus
  Engine-Gründen, nie als UI-Sprint-Seiteneffekt.
- `ui_test.mjs` muss danach **0 failures** zeigen.

### FIX-2 — `.tgt`-CSS gilt nicht für Knöpfe UND Fader
Assertion erwartet die Regel `.knob.tgt, .fader.tgt { … }`; fehlt → der
Motion-/Ziel-Armierungs-Zustand wird nicht einheitlich auf Knöpfe **und**
Fader angewandt (genau die Ecke der früheren „Knopf"-Frustration).
- Die kombinierte CSS-Regel ergänzen, sodass der Arm-/Zielzustand für beide
  Steuerelementtypen identisch sichtbar ist (kein Umbau sonst).
- `ui_test.mjs` → **0 failures**. Eine Playwright-Story ergänzen, die den
  sichtbaren Arm-Zustand auf Knopf **und** Fader prüft.

---

## §8 Was dieser Block NICHT ist (ehrlich)

- **Kein Volca-Bass-Klon.** Erweiterung, kein Hardware-Target (eine *Keys*
  hat keinen Bass-Modus). `// EXT-BASS` + Bitidentität-bei-aus belegen, dass
  der getreue Kern unangetastet ist.
- **Kein Audio-Loop.** Flux/F-Rec bleibt ereignisbasiert (hardware-treu).
- **Fett-Proxies sind kein Treue-Score** — sie belegen, dass die DSP da und
  spezifikationsgemäß ist, nicht perzeptive Äquivalenz zur echten Bass.
- **Defaults sind `// EXT-BASS DEFAULT`**, nicht `CALIB(E8)`.
- **WASM-Build + Playwright** laufen auf dem VPS
  (`ssh root@87.106.25.91`, `para3.levcon.at`) — dort grün nachweisen.

---

## §8b Offene Treue-Lücke (Weg A — bewusst gewählt, ehrlich dokumentiert)

Status: Bass-Charakter wird gebaut, **ohne** Treuebeweis gegen echte
Hardware (Mensch-Entscheidung „nein/später"). Damit niemand das später mit
„originalgetreu" verwechselt, gilt verbindlich:

- **Keine Treue-Behauptung.** Weder Code-Kommentar, UI-Text, Doku noch
  Commit-Message darf EXT-BASS als „originalgetreu/Volca-Bass-genau"
  bezeichnen. Erlaubt: „Bass-Charakter, strukturell der Volca Bass
  nachempfunden". Die UI-Beschriftung bleibt „EXT · Erweiterung".
- **Keine erfundenen Hardware-Zahlen.** Alle Zielwerte `// EXT-BASS DEFAULT`
  (Design). `CALIB(BASS)` wird **nicht** verwendet, solange kein vermessenes
  Referenzmaterial existiert. Fett-Metriken bleiben Design-Korridore, kein
  Treue-Score.
- **Was für Weg B (voller Hardware-Treuebeweis) noch fehlt** — der
  Restspalt ist jetzt klein und klar benannt: öffentliche Quellen belegen
  Topologie, Wellenformen, Detune-Mapping, Gruppenmodi, Filter-Steilheit/
  Self-Oscillation, EG-/LFO-Struktur (alles `// EXT-BASS REF`, messbar).
  **Nicht** öffentlich quantifiziert und daher offen (`CALIB(BASS)`,
  zurückgestellt): die feine **analoge Voicing** — konkret die
  Bass-vs-Keys-Filtervoicing-Differenz und die exakte Resonanz-/
  Übersteuerungs-Kennlinie. Nachholbar via:
  1. Referenzaufnahmen einer echten Volca Bass (Cutoff/Peak-Sweeps,
     Self-Osc-Punkt, EG-Hub; sauberer Line-Out, dokumentierte Pegel).
  2. Daraus die Voicing-Kennlinien als Δ-Targets ableiten.
  3. Ein `CLAUDE_EXT_BASS_CALIB.md` (analog Anhang C / E8) und Ersetzen der
     betroffenen Punkte durch `CALIB(BASS)` mit Δ-Korridoren.
- **Nicht vom Volca Bass belegte Bausteine** (B2 PWM, B5 Sub, B3-Drift):
  bleiben dauerhaft `// EXT-BASS DEFAULT` und werden nirgends als
  „Volca-Bass-treu" bezeichnet — bewusste Erweiterung über das Vorbild.
- **Heißt:** Struktur + Parameter-Verhalten sind gegen zitierbare Quellen
  **gemessen** (Prinzip gewahrt); nur die analoge Fein-Voicing ist als
  einziger Punkt ehrlich offen. Der Wechsel zu Weg B ist additiv und
  jederzeit möglich, ohne die Architektur zu ändern.

---

## §9 Blockplan (je 1 Test + 1 Commit) & Pflicht-Regression

Reihenfolge: **B1** Pulse → **B2** PWM → **B3** Spread+Drift → **B4** Stack
→ **B5** Sub → **FIX** (§7) → **2-Instanz-Layer** (§6) → **Playwright-
User-Stories** (§5, recherchieren→vorschlagen→abnicken).

Nach JEDEM Block volle Regression grün, **0 Warnungen**, kein `-ffast-math`:
`offline_test` (alle T inkl. neu, Baseline unverändert PASS), `capi_test`
(WA inkl. neu), `scope_source_test`, `ring/audio/port`, **`ui_test`
(0 failures, inkl. FIX-Block)**, sowie **auf dem VPS**: `build_wasm.sh` +
`wasm_parity` + `npx playwright test`. Fehlschlag ⇒ echten Defekt fixen und
neu messen, Erwartung NIE aufweichen. Liefern: geänderte Dateien +
Unified-Diff + ausgeführte Mess-/Test-Logs (inkl. VPS-Läufe).

### Quick-Checklist
- [ ] `// EXT-BASS` an jeder neuen Zeile; nur erlaubte Dateien
- [ ] B1–B5 je: Wirkung + neutral `max|d|=0` + klickfrei + Alias ≤ −74 dBc
- [ ] Saw-Pfad / alle Defaults ⇒ bitidentisch (Haupt-Akzeptanz)
- [ ] EXT-Panel separat, badge, default aus, kein UI-Restructure
- [ ] FIX-1 Worklet-Drift entschieden+behoben; FIX-2 `.tgt`-CSS; `ui_test` 0
- [ ] 2-Instanz-Layer: Sync sample-genau, Mix korrekt, „aus"=bitidentisch
- [ ] VPS: WASM-Δ + Playwright (US-BASS-*) grün
- [ ] Pro Block: Diff + Mess-Logs, ehrlich über Grenzen

---

## §10 Quellen (öffentlich, zitierbar — Basis für `// EXT-BASS REF`)

Gegen diese dokumentierten Angaben werden die REF-Korridore gemessen.
Beim Implementieren je REF-Konstante die zutreffende Quelle im Kommentar
nennen (Kurz-Tag genügt, z. B. `// EXT-BASS REF [KORG-SPEC]`).

- **[KORG-SPEC]** KORG, offizielle Volca-Bass-Spezifikation/Features —
  korg.com/us/products/dj/volca_bass (Structure 3VCO/1VCF/1VCA/1LFO/1EG;
  VCO Wave Saw/Square; VCF LP 12 dB/Oct; EG A/DR/Sustain; LFO Tri/Square,
  Ziele Amp/Pitch/Cutoff; Gruppen/Akkord 0/+3/+7; Slide; Self-Tuning;
  Seq 16 Steps/3 Parts/8 Patterns/Active Step).
- **[VSE]** Vintage Synth Explorer, „Korg Volca Bass" — vintagesynth.com
  (12 dB/Oct resonant LP, MiniKORG-700S-abgeleitet, anders gevoiced als
  Volca Keys; Self-Oscillation bei hohem Peak; kein Cutoff-Keytracking;
  Detune fein ≤50 Cent → Halbton → ±1 Oktave; Tonumfang > 6 Oktaven;
  EG Sustain on/off ohne Pegel; EG→Amp on/off ohne Tiefe).
- **[MR]** MusicRadar, Korg-Volca-Bass-Review — musicradar.com
  (MiniKORG-700S-Filter übersteuert musikalisch/„screams"; Steuersignale
  digital, 10-bit-DAC; Slide editierbar; kein Accent; LFO bis Audio-Rate).
- **[WIKI]** Wikipedia, „Volca Bass" — en.wikipedia.org (subtraktive
  Topologie; VCO je Saw/Square; Detune bis ±1 Oktave; ADR-EG mit
  schaltbarem Sustain; LFO Square/Triangle bis Audio-Rate). Sekundär.

Hinweis Urheberrecht: nur technische Parameter/Fakten übernommen
(paraphrasiert), keine längeren Textübernahmen. Quellen können sich
ändern — beim Kalibrieren gegen die jeweils aktuelle Primärquelle (KORG)
prüfen; bei Konflikt KORG-SPEC > VSE/MR > WIKI.
