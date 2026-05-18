# CLAUDE_EXT_ARP — Arpeggiator (DSP-**Erweiterung**, NICHT Volca-Keys-Parität)

> **Lies das zuerst.** Dies ist eine **bewusste Erweiterung** über das
> originalgetreue Volca-Keys-Modell hinaus. Die echte Volca *Keys* hat **keinen**
> Arpeggiator (der sitzt in der Volca FM). Dieser Block ist deshalb klar als
> `EXT-ARP` markiert, **per Default AUS**, und der unantastbare Beweis ist:
> **Arp AUS ⇒ Engine bitidentisch zum aktuellen E1–E7-Stand**
> (`max|d| = 0.000e+00`). Der getreue Keys-Kern bleibt unberührt.

Stil/Disziplin identisch zu `CLAUDE_KORG_PARITY_DSP.md`: gemessen statt
behauptet, keine Fakes/Shortcuts, header-only, durch den Trichter, RT-safe,
klickfrei, neutral = bitidentisch, ein Block = ein Test = ein Commit.

---

## §0 Grundregeln (verbindlich)

1. **Marker.** Jede neue Code-Zeile trägt `// EXT-ARP` (NICHT `// E1..E7`).
   So bleibt im Diff für immer sichtbar, was Parität ist und was Erweiterung.
2. **Default AUS.** `arpEnabled_ = false`. Solange aus, ist *jeder* Pfad
   exakt der bisherige Code (kein zweiter Pfad, kein „wenn aus dann fast
   gleich"). Mess-Akzeptanz §4-A.
3. **Datei-Politik (wie Anhang A).** Es dürfen NUR geändert werden:
   `Para3Engine.hpp`, `wasm-bridge/para3_capi.h`, `wasm-bridge/para3_capi.cpp`,
   `offline_test.cpp`, `wasm-bridge/capi_test.cpp`,
   `wasm-bridge/build_wasm.sh` (nur Export-Liste), sowie dieser Spec.
   **Kein** neues Engine-`.cpp`, **kein** Refactor, header-only.
4. **RT-safe.** Keine Allokation/Lock/Exception/Syscall in `render`. **Kein
   `rand()`/`std::random`** — eigener xorshift mit Seed (§2.5).
5. **Durch den Trichter.** Der Arp erzeugt Noten ausschließlich über
   `eng_->noteOn(int)`, `eng_->noteOff(int)`, `eng_->retrigger()` (E4.1).
   Es entsteht **kein** neuer Audiopfad — die Klickfreiheit ist die schon
   bewiesene Gate-Logik (T2). Keine eigene Hüllkurve, kein eigener Mixer.
6. **Koexistenz fix definiert (§1.3).** Der Arp transformiert NUR die
   Tasten-/MIDI-Notenquelle. Step-Sequencer (E3) und Flux (E5) bleiben
   **wörtlich unverändert** und sind separate Quellen.
7. **Ein Block = ein Test = ein Commit.** Blöcke A, B, C (§3). Nach jedem:
   volle Regression `offline_test` T1–T26+neu, `capi_test` WA1–WA6+neu,
   `scope_source_test`, `ring/audio/port`, **0 Compilerwarnungen**, kein
   `-ffast-math`. Erst grün, dann nächster Block.
8. **Ehrlichkeit.** `wasm_parity` ist nicht in der Sandbox lauffähig (kein
   `emcc`); Exporte müssen in `build_wasm.sh` ergänzt werden, das WASM↔nativ-Δ
   verifiziert der Mensch beim Build. Keine „nach Gehör"-Konstanten — die
   Arp-Defaults sind **Design-Defaults** (kein HW-Vorbild), markiert
   `// EXT-ARP DEFAULT` (NICHT `CALIB(E8)`, das ist Hardware-Kalibrierung).

---

## §1 Architektur & exakte Einordnung

### §1.1 Wo der Arp sitzt
Der Arp ist ein **Noten-Quellen-Transform** im `Controller`, zwischen
eingehenden Tasten/MIDI-Noten (`Controller::midiNoteOn/midiNoteOff` und die
`MidiEvent`-Schlange in `render`) und der Engine.

- **Arp AUS:** `midiNoteOn(n)` → `eng_->noteOn(n)` exakt wie bisher.
  `midiNoteOff(n)` → `eng_->noteOff(n)` exakt wie bisher. **Bitidentisch.**
- **Arp AN:** `midiNoteOn(n)` fügt `n` dem **Arp-Notenpool** hinzu und ruft
  die Engine NICHT direkt. `midiNoteOff(n)` entfernt `n` aus dem Pool (außer
  Hold/Latch, §2.6). Der Arp-Scheduler (taktsynchron) erzeugt daraus die
  zeitlich gesetzten `noteOn/noteOff` an die Engine.

### §1.2 Zusammenspiel mit Voice-Modi
Der Arp speist **einzelne** Noten in `alloc_` über `eng_->noteOn`. Die
bestehenden Modi (`Poly/Unison/Octave/Fifth/UniRing/PolyRing`) werden vom
Allocator unverändert auf **jede** Arp-Note angewandt. ⇒ „Arp + Fifth" =
arpeggierte Quinten. Das ist gewollt und wird in §4-T34 strukturell geprüft.
**Keine Änderung an `ParaAllocator`.**

### §1.3 Präzedenz/Koexistenz (eindeutig)
- `setArpEnabled()` verändert AUSSCHLIESSLICH das Verhalten von
  `midiNoteOn/midiNoteOff` (Tasten/MIDI).
- `onStep()` (Step-Sequencer, E3/E4) und der Flux-Player (E5) bleiben
  **wörtlich unverändert** — eigene Patterns, eigene Notenquelle, **nicht**
  durch den Arp geroutet.
- Daraus folgt zwingend: Arp AN, keine Taste gehalten, Sequencer gestoppt,
  kein Flux ⇒ **Stille** (= bitidentisch zu „kein Input").
- Gleichzeitiger Betrieb Arp + Step-Seq ist erlaubt (additive Quellen), aber
  **nicht** der getestete Normalfall; Tests fahren den Arp mit gestopptem
  Sequencer. Im Spec dokumentieren, im UI sichtbar machen (§5).

### §1.4 Timing-Quelle
Der Arp hat einen **eigenen sample-genauen Akkumulator** (wie `Clock`,
bewiesen jitterfrei in T20), abgeleitet aus dem Tempo der vorhandenen `Clock`.
Dafür **eine** minimale, nicht-invasive Ergänzung an `Clock`:

```cpp
double sixteenthSamples() const noexcept { return stepSamples_; } // EXT-ARP read-only
```
(reiner Getter, ändert kein Verhalten — Regression bleibt grün).

Der Arp läuft nur, wenn `clock_.running()` true ist (HW-Arp braucht Clock).
Bei `!running()` hält der Arp seinen Zustand (kein Fortschritt), gibt keine
neuen Noten — kein Free-Run (Determinismus, Testbarkeit). Dokumentieren.

---

## §2 Datenstrukturen, Zustandsautomat, Gleichungen

Alles als Member von `Controller` (POD, fixe Größen, keine Allokation).

### §2.1 Pool
```cpp
static constexpr int kArpCap = 16;                 // EXT-ARP
int   arpPool_[kArpCap];                            // gehaltene Noten (MIDI)
int   arpPoolN_ = 0;                                // Anzahl
```
- `arpAdd(n)`: wenn `n` schon im Pool → ignorieren (Dedupe). Sonst, wenn
  `arpPoolN_ < kArpCap`, anhängen (Einfügereihenfolge bleibt erhalten —
  nötig für `AsPlayed`). Bei vollem Pool: ältesten Eintrag NICHT verdrängen,
  neuen verwerfen + `arpDropped_++` (beobachtbar, analog FLUX-Drop).
- `arpRemove(n)`: stabil herausfiltern (Reihenfolge der übrigen bleibt),
  **außer** `arpHold_` (dann Pool unverändert, §2.6).

### §2.2 Modi
```cpp
enum class ArpMode { Up, Down, UpDown, AsPlayed, Random }; // EXT-ARP
```
Reihenfolge-Erzeugung pro Arp-Schritt liefert einen **Index 0..L-1** in eine
**effektive Sequenz** der Länge `L = arpPoolN_ * arpOct_` (§2.4). Aus dem
laufenden Index `arpIdx_`:

- **Up:** sortiere Pool aufsteigend → `srt[]`. Note = `srt[i % n] + 12*(i / n)`
  mit `i = arpIdx_`, `n = arpPoolN_`. `arpIdx_ = (arpIdx_+1) % L`.
- **Down:** wie Up, aber `j = L-1 - arpIdx_`, dann dieselbe Formel mit `j`.
- **UpDown:** klassisch mit **exklusiven Endpunkten** (Periode `2L-2` für
  `L>1`; bei `L==1` konstant): laufender Zähler `c`, `pos = (c < L) ? c
  : (2L-2 - c)`; Note aus `pos` wie Up; `c = (c+1) % max(1, 2L-2)`.
- **AsPlayed:** KEINE Sortierung — Pool in Einfügereihenfolge; Note =
  `arpPool_[i % n] + 12*(i / n)`, `i = arpIdx_`.
- **Random:** `i = xorshift() % L` (§2.5); Note wie Up aus `i`
  (Sortierung verwenden, damit Octave-Spreizung deterministisch ist).

Bei `arpPoolN_ == 0`: kein Schritt, keine Note (Arp idle, Stille).

### §2.3 Rate (Subdivision)
```cpp
enum class ArpRate { D_1_4, D_1_8, D_1_8T, D_1_16, D_1_16T, D_1_32 }; // EXT-ARP
```
Multiplikator in 16tel-Schritten (16tel-Dauer = `clock_.sixteenthSamples()`):

| Rate   | 16tel pro Arp-Schritt |
|--------|-----------------------|
| 1/4    | 4.0                   |
| 1/8    | 2.0                   |
| 1/8T   | 4.0/3.0  (Triole, exakt, NICHT gerundet) |
| 1/16   | 1.0                   |
| 1/16T  | 2.0/3.0               |
| 1/32   | 0.5                   |

```cpp
arpStepSamples_ = clock_.sixteenthSamples() * mult(arpRate_); // double, exakt
```
Eigener Akkumulator (sample-genau, jitterfrei wie T20):
```cpp
double arpAcc_ = 0.0;
// pro gerendertem Sample, NUR wenn arpEnabled_ && clock_.running():
arpAcc_ += 1.0;
if (arpAcc_ >= arpStepSamples_) { arpAcc_ -= arpStepSamples_; arpStep(); }
```
`arpStepSamples_` bei jedem Tempo-/Rate-Wechsel neu berechnen (Setter).

### §2.4 Octave-Range
```cpp
int arpOct_ = 1;  // EXT-ARP DEFAULT, 1..4
```
`L = arpPoolN_ * arpOct_`. Octave-Offset = `12 * (index / arpPoolN_)`
(siehe §2.2). Bei Wechsel `arpOct_` Index modulo neuem `L` clampen.

### §2.5 Random-PRNG (deterministisch, RT-safe, kein `rand()`)
```cpp
uint32_t arpRng_ = 0x9E3779B9u;            // EXT-ARP DEFAULT seed
inline uint32_t arpXorshift() noexcept {   // xorshift32
    uint32_t x = arpRng_; x ^= x<<13; x ^= x>>17; x ^= x<<5;
    return (arpRng_ = x);
}
```
`para3_arp_seed` setzt `arpRng_`. Gleicher Seed ⇒ identische Notenfolge
(T33 prüft Reproduzierbarkeit). Default ist **Design-Default**, kein
`CALIB(E8)` (es gibt kein Hardware-Vorbild für einen Keys-Arp).

### §2.6 Gate & Hold/Latch
```cpp
double arpGate_ = 0.5;   // EXT-ARP DEFAULT, 0..1 (Anteil der Schrittdauer)
bool   arpHold_ = false; // EXT-ARP Latch
int    arpCur_  = -1;    // aktuell klingende Arp-Note (-1 = keine)
double arpGateAcc_ = 0.0;// Restzeit bis NoteOff der aktuellen Note
```
**`arpStep()` (bei Akkumulator-Überlauf):**
1. Wenn `arpCur_ >= 0`: `eng_->noteOff(arpCur_)` (**OFF zuerst** — gleiche
   OFF-vor-ON-Disziplin wie E5; gibt den mono/paraphonen Allocator frei,
   sauberes Retrigger, klickfrei).
2. Wenn `arpPoolN_ == 0`: `arpCur_ = -1`, return (Stille, kein Klick).
3. Note `m` per Modus (§2.2) bestimmen. `eng_->noteOn(m)`; `arpCur_ = m`.
4. `arpGateAcc_ = arpGate_ * arpStepSamples_`.

**Pro Sample (nach dem Akkumulator-Block), wenn `arpCur_ >= 0`:**
```cpp
if (arpGateAcc_ > 0.0) {
    arpGateAcc_ -= 1.0;
    if (arpGateAcc_ <= 0.0 && arpGate_ < 1.0) {   // Staccato: früh aus
        eng_->noteOff(arpCur_); arpCur_ = -1;
    }
}
```
Bei `arpGate_ >= 1.0` (Legato): kein vorzeitiges OFF; das OFF im nächsten
`arpStep()` (Schritt 1) sorgt für sauberes Retrigger (env klickfrei, T2).

**Hold/Latch:** `arpHold_` true → `arpRemove` lässt den Pool unverändert.
Sonderfall „neuer Anschlag nach komplettem Loslassen": Controller merkt
`arpAnyPhysHeld_` (Zahl physisch gedrückter Tasten, unabhängig vom Pool).
Geht diese von 0→1 **und** `arpHold_` true → Pool **leeren**, dann die neue
Note aufnehmen (klassisches Latch-Verhalten neu starten). Genau so
implementieren, in §4-T34 geprüft.

### §2.7 Beobachtbarkeit
```cpp
long arpDropped_ = 0;  // EXT-ARP Pool-Overflow (analog FLUX), via C-API lesbar
```

---

## §3 Implementierungsblöcke (je 1 Test + 1 Commit)

**Block A — Kern + Bitidentität-aus + Up.**
Pool, Akkumulator, `arpStep()`, Modus Up, Rate-Tabelle, Gate, Enable.
`midiNoteOn/Off`-Verzweigung. C-API `enable/mode/rate/gate`.
Test **T27** (aus = bitidentisch; an+leer = Stille; an = Up-Folge & Rate
exakt & klickfrei). Volle Regression. Commit.

**Block B — Modi & Octaves.**
Down, UpDown, AsPlayed, Octave-Range. C-API `octaves`.
Tests **T28** (Modi-Reihenfolgen strukturell), **T29** (Octave-Range).
Regression. Commit.

**Block C — Random, Hold, Triolen, Drop, Koexistenz.**
xorshift+Seed, Hold/Latch, Triolen-Raten exakt, Pool-Drop-Zähler,
Koexistenz-Doku/Check (Arp + Fifth). C-API `hold/seed/dropped`.
Tests **T30** (Random reproduzierbar), **T31** (Hold/Latch + Arp×Fifth).
`capi_test` **WA7** (Voll-Sweep). `build_wasm.sh` Exporte. Regression. Commit.

(Test-Nummern T27.. fortlaufend hinter T26; WA7 hinter WA6.)

---

## §4 Tests — exakt, gemessen (nichts behaupten)

Methodik wie bestehende Suite: `ParaEngine`+`Controller` direkt, Onset-
Erkennung über Fenster-RMS-Flanken (wie T19/T23), Tonhöhe über
FFT-Peak mit parabolischer Interpolation (wie T17), Timing über
Schrittgrenzen-Samplezählung (wie T20), Klick über latenz-robustes
global-\|dx\| vs. steady (wie T2).

- **T27 ARP off bitidentisch + Up:**
  (a) Engine mit gehaltener Note, Arp **unangetastet** vs. `arpEnable(false)`
  → `max|d| == 0.0` über den ganzen Puffer. **Härtester Anti-Fake-Beweis.**
  (b) `arpEnable(true)`, Clock laufend, **kein** Pool → `max|x| < 1e-6`.
  (c) Pool {48,52,55}, Up, Rate 1/8, Gate 0.5: detektierte Onset-Tonhöhen
  in Reihenfolge 48,52,55,48,… (±~1 Halbton Mess-Toleranz); Onset-Abstand
  = `1/8`-Samples, Jitter ≤ Fensterraster; global-\|dx\| ≤ steady·1.6
  (klickfrei). finite.
- **T28 Modi:** gleicher Pool; Down liefert 55,52,48,…; UpDown
  48,52,55,52,48,… (exklusive Endpunkte); AsPlayed = Einfügereihenfolge
  (z. B. 52,48,55 gedrückt → 52,48,55,…). Strukturelle Tonhöhen-Sequenz.
- **T29 Octave-Range:** Pool {48}, Up, `octaves=2` → Folge 48,60,48,60,…
  (Tonhöhe wechselt exakt um eine Oktave, FFT-Peak-Verhältnis ≈ 2.0).
- **T30 Random reproduzierbar:** Seed S, Modus Random, Pool {48,52,55,59},
  zwei identische Läufe → **identische** Onset-Tonhöhenfolge; anderer Seed
  → andere Folge. (Determinismus, kein `rand()`.)
- **T31 Hold/Latch + Arp×Fifth:** Pool {48,52,55}, `hold=true`, alle Noten
  „loslassen" (midiNoteOff) → Arp läuft weiter (Onsets weiterhin vorhanden).
  Voice-Mode `Fifth`: pro Arp-Note sind **zwei** Tonhöhen präsent, f und
  ≈ f·2^(7/12) (FFT zeigt beide Partial-Gruppen) → strukturell „arpeggierte
  Quinte".
- **T32 (in T27c integriert) Rate exakt inkl. Triole:** zusätzlich Rate
  1/8T messen → Arp-Schrittabstand = `sixteenthSamples·4/3`, Jitter ≤ 1
  Sample über ≥10 Schritte (Triole nicht gerundet).
- **WA7** (`capi_test`): `para3_arp_enable/mode/rate/octaves/gate/hold/seed`
  voll durchsweepen bei gehaltenen Noten + Render → finite, beschränkt,
  `arpDropped()` > 0 nach Pool-Overflow (>16 Noten), Off→On→Off-Übergänge
  ohne NaN/Inf; und Off-Zustand erzeugt dieselbe Ausgabe wie ohne Arp-Aufruf.

**Regression-Pflicht:** T1–T26 und WA1–WA6 müssen **unverändert PASS**
(0 Fehler, 0 Warnungen). Jede Abweichung = echter Defekt → fixen und neu
messen, nicht Schwellen aufweichen.

---

## §5 C-API (alle in `build_wasm.sh` exportieren)

```c
void para3_arp_enable (Para3* p, int on);                 // EXT-ARP
void para3_arp_mode   (Para3* p, int mode);  // 0 Up 1 Down 2 UpDown 3 AsPlayed 4 Random
void para3_arp_rate   (Para3* p, int rate);  // Index in ArpRate (§2.3)
void para3_arp_octaves(Para3* p, int n);     // 1..4
void para3_arp_gate   (Para3* p, double g);  // 0..1
void para3_arp_hold   (Para3* p, int on);    // Latch
void para3_arp_seed   (Para3* p, unsigned int seed); // Random-Reproduzierbarkeit
long para3_arp_dropped(Para3* p);            // Pool-Overflow (Observability)
```
Exporte (Präfix `_`) in `EXPORTED_FUNCTIONS` von `build_wasm.sh` ergänzen,
analog zu den E1–E6-Symbolen. **Keine** bestehende Signatur ändern.

---

## §6 Frontend-Deltas (für die UI; Stil wie `CLAUDE_FRONTEND_PARITY.md`)

- Neues, **als „EXT" gekennzeichnetes** Arp-Panel (visuell klar vom
  Volca-getreuen Bereich abgesetzt — der Nutzer muss jederzeit sehen, dass
  das eine Erweiterung ist, kein Volca-Keys-Feature).
- Bedienelemente: Enable-Toggle, Mode-Selector (5), Rate-Selector (6),
  Octaves (1–4), Gate-Knopf, Hold-Toggle.
- **Wichtig:** Arp-Parameter sind **Controller-Settings**, KEINE
  `setParamNorm`-Parameter — sie laufen NICHT durch den Taper-Trichter
  (wie Tempo/Swing). Nicht in `KNOB_PARAM`/`INERT_KNOBS` aufnehmen; eigene
  Steuer-Hooks. Den bestehenden Knopf-/Fader-Mechanismus NICHT umbauen.
- Default-Zustand des UI: Arp **aus**. Beim Laden eines Patches muss „Arp
  aus" exakt den bisherigen Klang ergeben (Konsistenz mit §0-2).

---

## §7 E2E-Gate (Stil wie `CLAUDE_PARITY_E2E_GATE.md`)

`ext_arp_e2e.mjs` (neu, neben `e2e_test.mjs`), über echten
Ring/Port/Worklet. Block ist erst „done", wenn **alle drei** grün sind:

1. **Engine** (§4, alle neuen T + WA7, volle Regression, 0 Warnungen).
2. **Frontend** (Arp-Panel steuert die o. g. C-API-Hooks; Off-Default).
3. **E2E-Neutralität:** Mit Arp **aus** ist die Ende-zu-Ende-Ausgabe
   **bitidentisch** zum Stand vor dieser Erweiterung (gleiche Eingabe →
   gleiche Samples über die volle Kette). Plus: Enable/Disable im laufenden
   Audio erzeugt **keinen** Naht-Klick (T2-Metrik über die E2E-Ausgabe).

---

## §8 Was diese Erweiterung **nicht** ist (ehrlich)

- **Keine Volca-Keys-Parität.** Die Keys hat keinen Arp. Dieser Block ist
  eine Design-Erweiterung; jede `// EXT-ARP`-Markierung und die Bitidentität
  bei `arpEnabled_==false` belegen, dass der getreue Kern unangetastet ist.
- **Keine `CALIB(E8)`-Konstanten.** E8 ist Hardware-Kalibrierung gegen die
  echte Volca; für einen Keys-Arp gibt es kein Hardware-Vorbild. Alle
  Arp-Defaults sind **Design-Defaults** (`// EXT-ARP DEFAULT`) und als
  solche frei wählbar/dokumentiert — nicht „nach Gehör gegen Hardware".
- **Kein Fake-Timing.** Triolen werden exakt über den Sample-Akkumulator
  erzeugt, nicht auf das 16tel-Raster gerundet.
- **Kein zweiter Audiopfad.** Der Arp erzeugt nur `noteOn/noteOff/retrigger`;
  Klickfreiheit ist die bereits gemessene Gate-Logik (T2), nicht neue,
  unbewiesene DSP.
- **wasm_parity** bleibt menschseitig (kein `emcc` in der Build-Sandbox des
  Engine-Agents) — `build_wasm.sh`-Exporte ergänzen, Δ beim Build prüfen.

---

### Quick-Checklist für Claude Code
- [ ] `// EXT-ARP` an jeder neuen Zeile; nur die 6 erlaubten Dateien + Spec
- [ ] Block A: T27 (a) `max|d|==0` aus, (b) Stille leer, (c) Up+Rate+klickfrei
- [ ] Block B: T28 Modi, T29 Octaves
- [ ] Block C: T30 Random-Seed, T31 Hold + Arp×Fifth, Triole in T27c, WA7, Exporte
- [ ] Nach jedem Block: volle Regression grün, 0 Warnungen, kein `-ffast-math`
- [ ] E2E: Arp-aus bitidentisch über die volle Kette; Enable/Disable klickfrei
- [ ] Liefern: geänderte Dateien + Unified-Diffs + gemessene Logs + dieser Spec
