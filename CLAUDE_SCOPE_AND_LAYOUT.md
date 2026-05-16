# CLAUDE_SCOPE_AND_LAYOUT.md — Layout-Fit + echter Oszilloskop

Gleicher Arbeitsstandard wie bisher: **messen, nicht behaupten. Kein Fake,
keine leere Animation, keine Helfer-Abkuerzung.** Jeder Punkt hat ein
ausfuehrbares bzw. geraetegeprueftes Abnahmekriterium. Bei Fehlschlag echte
Ursache fixen und neu pruefen — niemals das Kriterium aufweichen.

Gilt fuer `wasm-bridge/para3-responsive.html` (Anbindung siehe
`CLAUDE_INTEGRATE_UI.md`). Der Parameter-Trichter bleibt die einzige
Steuerquelle; Scope/Layout duerfen den RT-Audiopfad **nicht** beeinflussen.

---

## TEIL 1 — Mobile-Layout: alles muss erreichbar sein

Befund (Geraete-Screenshot Hochformat): angedockte Tastatur verdeckt
EG/LFO/DELAY/SEQUENCER; feste `vh`-Einheiten ignorieren Browserleiste +
Safe-Area; letzte Sektionen nicht scrollbar.

### Umsetzung

1. **Dynamische Viewport-Einheiten.** Alle `vh` → `svh`/`dvh` (bzw.
   `100dvh`-Container). Kein `100vh` mehr.
2. **Tastatur als Schublade (Hochformat).** Default **eingeklappt**; der
   bereits vorhandene `#kbtn` (⌨ KEYS) toggelt sie. Eingeklappt: nur ein
   schmaler Griff (≤ 40 px). Ausgeklappt: feste Hoehe `Hk`.
3. **Scrollbereich frei.** Der scrollende Panel-Container bekommt
   `padding-bottom: calc(var(--kbH) + env(safe-area-inset-bottom) + 12px)`,
   wobei `--kbH` = aktuelle Schubladenhoehe (0/Griff vs. `Hk`). Beim
   Toggeln `--kbH` aktualisieren. Oben `env(safe-area-inset-top)` beachten.
4. **Kleine Screens.** Bei Viewport-Hoehe < 720 CSS-px: Knob-Sektionen als
   2-spaltiges Raster ODER Sektionen (VOICE/OSC/VCF/EG/LFO/DELAY/SEQ) als
   Akkordeon (nur eine offen). Keine horizontale Scrollbar, kein
   Abschneiden.
5. **Landscape** unveraendert: Mehrspaltig + Keyboard-Drawer (bestehende
   Logik beibehalten, nur dvh/Safe-Area nachziehen).

### Abnahme (Pflicht, Geraet + Emulation)

- Chrome-DevTools Geraete 360×640 **und** 390×844 **und** echtes Handy:
  jeder Control von VOICE bis SEQUENCER/MOTION ist per Scroll erreichbar
  und bedienbar; die Tastatur (egal ob ein/ausgeklappt) verdeckt **nie**
  einen Control, den man gerade bedient.
- Keine horizontale Scrollbar; nichts hinter Notch/Home-Indicator/
  Browserleiste verborgen (Safe-Area sichtbar respektiert).
- Theme-Wechsel (OBSIDIAN/BONE) aendert nichts an der Erreichbarkeit.

---

## TEIL 2 — Echter Oszilloskop (kein Fake)

Befund: Der Prototyp zeichnet eine **synthetische** Kurve (`frame()`/
`lfo()`-Schleife). Das ist eine leere Animation und wird **vollstaendig
entfernt**.

### Umsetzung

1. **Echte Quelle.** Read-only `AnalyserNode` am realen Graph:
   `workletNode → analyser → ctx.destination` (Analyser veraendert das
   Signal nicht; reines Metering, kein Eingriff in den RT-Pfad).
   `analyser.fftSize` z. B. 2048, `smoothingTimeConstant=0`.
2. **Zeichnen.** Pro `requestAnimationFrame`:
   `analyser.getFloatTimeDomainData(buf)` → `buf` (echte Ausgangssamples)
   auf das `#scope`-Canvas mappen (volle Breite, vertikale Mitte =
   0, Skala fest, kein Auto-Gain das Stille „aufblaest").
3. **Stille ist flach.** Wenn die Engine nichts spielt, ist `buf ≈ 0` →
   **flache Linie**. **Kein** dekorativer Ersatz, kein LFO-Zeichner,
   keine Mindestamplitude. Flach ist die korrekte, ehrliche Darstellung.
4. Der alte synthetische Zeichner inkl. `lfo()`-Hilfsfunktion wird
   geloescht (nicht auskommentiert).

### Nachweis A — headless, Quelle ist das echte Signal

Schreibe `wasm-bridge/scope_source_test` (C++ ueber die C-API, Stil von
`capi_test.cpp`):

- Render einen Block bei **Stille** (kein NoteOn): max \|x\| < 1e-6
  → ein Fake-Wackler wuerde hier durchfallen.
- NoteOn 69 (A4), kurz einschwingen, Block rendern: bestimme f0 via
  Nulldurchgang/FFT (vorhandene Methodik aus `offline_test.cpp`):
  |f0 − 440·2^((69−69)/12)| ≤ 1 Hz (bzw. < 5 Cent), Amplitude > 0.
- Belege explizit: die Pufferinhalte sind die **Engine-Ausgabe**
  (bit-identisch zu `para3_render`), keine separate Erzeugung.
- Kriterium: `OVERALL: PASS`, 0 Warnungen. Damit ist bewiesen, dass das,
  was der Scope anzeigt, das reale Signal ist.

### Nachweis B — In-App-Selbsttest (Anti-Fake-Gate)

Versteckter Self-Test (Query `?selftest=1` o. ae.):

1. `noteOn(69)`; nach 200 ms aus den **Analyser**-Zeitdaten die
   dominante Frequenz schaetzen (Autokorrelation/Zero-Cross).
   PASS wenn ≈ 440 Hz (± 5 Cent).
2. `noteOff(69)`; nach Release RMS der Analyser-Daten < 1e-3
   (Scope wird flach).
3. Beide Ergebnisse in die Konsole als `SCOPE SELFTEST: PASS/FAIL`.

Begruendung: Eine reine Animation kann **nicht gleichzeitig**
„still ⇒ flach" und „A4 ⇒ 440 Hz" erfuellen. Besteht B, ist der Scope
nachweislich echt.

### Abnahme

- `scope_source_test` → PASS, 0 Warnungen; Engine-Suite 13/13 weiterhin
  gruen (Analyser/Scope aendern den RT-Pfad nicht).
- In-App `SCOPE SELFTEST: PASS` auf Desktop **und** Handy.
- Sichtpruefung: Note gespielt → Wellenform bewegt sich passend; Hand von
  der Taste → Linie wird flach (kein Rest-Gewackel).
- Kein zweiter Audiopfad, kein Mehrfach-`para3_render` fuer den Scope;
  Analyser ist read-only am bestehenden Node.

---

## Eiserne Regeln (unveraendert)

- Jede Qualitaetsaussage hat einen ausfuehrbaren/geraetegeprueften Test.
- Scope ist read-only Metering; RT-Audiopfad und Parameter-Trichter
  bleiben unangetastet (Engine 13/13, capi, ring, audio_test nach jeder
  Aenderung erneut gruen, 0 Warnungen).
- Kein Fake, keine leere Animation, kein „nah genug" statt Messung.
- Stille = flache Linie. Das ist korrekt, nicht ein Mangel.
