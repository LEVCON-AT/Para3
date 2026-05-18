# CLAUDE_USER_SCENARIOS — User-Story-Gate (Playwright, verbindlich)

> **Warum es das gibt.** Die Mess-Suite (T1–T26, WA…) beweist die DSP
> *isoliert* — sie ruft `setParamNorm` direkt. Logisch-kaputte Sachen im
> echten Nutzungspfad (Maus klickt Regler im Browser → JS-Emit →
> Ring/Port → Worklet → WASM-Engine → Klang) rutschen durch (z. B. „jeder
> Knopf muss erst angefasst werden, sonst wirkt er nicht"). Dieses Gate
> schließt die Lücke, indem es die **reale UI im echten Browser** prüft.

## §0 Grundprinzip

1. **Recherchieren → vorschlagen → abnicken → exakt mit Playwright prüfen.**
   Pro Anforderung **recherchiert Claude Code** die relevanten User-Cases
   *entlang der aktuellen Anforderung*, formuliert sie als konkrete Stories
   (§3-Format) und legt sie dem Menschen zur **expliziten Freigabe** vor
   („abnicken"). Claude Code setzt KEINE Story ohne Freigabe um und erfindet
   keine still-grünen Tests. Erst die abgenickten Stories werden **wörtlich**
   als Playwright-Tests implementiert und sind dann bindend.
2. **Echte UI, echter Browser.** Verifikation ausschließlich über
   `@playwright/test` (Chromium, headless) gegen die real ausgelieferte
   Seite (responsive HTML + AudioWorklet + WASM). Bedienung über echte
   Locator-Interaktionen (Klick/Drag der tatsächlichen Regler/Buttons),
   **nicht** über direkte JS-/Engine-Aufrufe.
3. **Gemessen, nicht behauptet — auch hier.** Assertions laufen über einen
   deterministischen Audio-Tap (siehe §2) mit denselben Metriken wie die
   bestehende Suite (Onset/RMS/FFT-Peak/Klick-\|dx\|). Kein „Screenshot
   sieht ok aus".
4. **Regressionsrang.** Einmal grün, bleibt die Story im Playwright-Projekt
   und läuft in der Pflicht-Prüfroutine mit.
5. **Fehlschlag ⇒ echten Defekt fixen und neu messen.** Erwartung NIE
   aufweichen, Test NIE „flaky-tolerant" lockern. Eine rote User-Story
   blockiert „done".

## §1 Fest eingebaute Pflicht-Stories (immer, zusätzlich zu §3)

Müssen als Playwright-Tests existieren und grün sein — adressieren die
gemeldete Bug-Klasse direkt:

- **US-COLD — Cold-Start ohne Nach-Anfassen.** Seite laden, Audio
  starten/Connect klicken, **kein** Bedienelement berühren ⇒ der Klang
  entspricht den **angezeigten** Reglerstellungen. (Roter US-COLD ⇒
  struktureller Fix: beim Bereitwerden von Worklet/Port **einmalig alle**
  Regler **und** Pattern/Controller-Settings broadcasten — Initial-State-
  Flush — statt erst bei `onChange` zu senden.)
- **US-ONE — Eine Interaktion wirkt sofort & isoliert.** Genau einen Regler
  per Maus bewegen ⇒ genau dieser Aspekt ändert sich, sofort, **ohne**
  zweite Interaktion; andere Aspekte unverändert.
- **US-PERSIST — Persistenz über Kontextwechsel.** Pattern/Voice-Mode/Preset
  per UI wechseln und zurück ⇒ Reglerzustände bleiben/werden reapplied.
- **US-IDEM — Idempotentes Re-Emit.** Denselben Wert erneut setzen
  (Regler auf gleichen Wert) = No-Op: kein Klick, kein Sprung.
- **US-ORDER — Reihenfolge-Unabhängigkeit.** Reihenfolge der Reglereinstellung
  vor dem Start ändert das Ergebnis nicht.

## §2 Aufbau & Determinismus (verbindlich)

- Playwright-Projekt unter `tests/e2e/` (neu): `playwright.config.ts`
  (Chromium, headless, `workers:1`, `retries:0` — Flakiness ist ein Defekt,
  kein Toleranzfall), Specs `tests/e2e/*.spec.ts`.
- **Deterministischer Audio-Tap (test-only, neutralitätstreu):** die Seite
  exponiert NUR bei gesetztem Test-Flag (z. B. `?e2e=1` oder
  `window.__PARA3_E2E`) eine Funktion
  `window.__para3Capture(nSamples) -> Float32Array`, die den Klang über den
  vorhandenen Offline-/Scope-Renderpfad **synchron und deterministisch**
  nach den UI-Aktionen liefert. Pflicht: mit Test-Flag AUS ist die
  Produktionsausgabe **bitidentisch** (der Tap darf 0 Wirkung auf den
  Normalbetrieb haben — gleiche Anti-Blender-Disziplin wie überall;
  Hook klar als `// E2E-TAP test-only` markieren).
- Kein `waitForTimeout` für Korrektheit. Zeit/Render über den Sample-
  Zähler der Engine bzw. eine feste Sample-Anzahl, nicht über Wanduhr.
- Fester Seed/Zustand pro Test (Pattern, ggf. `para3_arp_seed`),
  kein Netzwerk, ein Worker, serielle Ausführung wo nötig.
- Assertion-Helfer (Onset/RMS/FFT-Peak/Klick) als gemeinsames TS-Modul,
  identische Schwellen-Logik wie die C++/mjs-Suite.

## §3 User-Story-Katalog (von Claude vorgeschlagen, vom Menschen abgenickt)

> Ablauf: Claude Code recherchiert entlang der aktuellen Anforderung,
> trägt Vorschläge unten ein (Status `VORGESCHLAGEN`), der Mensch setzt
> `FREIGEGEBEN` (oder ändert/streicht). Nur `FREIGEGEBEN` wird **wörtlich**
> als Playwright-Test umgesetzt (eine Story = ein `test(...)`).

```
### US-<kurzname> — <Titel>
Given:  <Seitenzustand: Preset/Pattern/angezeigte Reglerstellungen>
When:   <konkrete UI-Aktionen: welche Locatoren, Klick/Drag, Reihenfolge>
Then:   <messbar erwartetes Verhalten/Klang/Zustand>
Mess:   <Tap-Metrik: Onset@X, RMS-Verhältnis, FFT-Peak≈f, kein Klick …>
```

<!-- Beispiel (vom Menschen ersetzen/erweitern):
### US-BASSLINE — 16-Step-Bassline sofort spielbar
Given:  Default-Preset frisch geladen; Cutoff-Regler zeigt ~0.4; kurzes
        Decay; Pattern mit 8 Gates; Tempo 120.
When:   „Start" klicken — sonst NICHTS berühren.
Then:   Sofort hörbar die programmierte Bassline mit der angezeigten
        Cutoff-/Decay-Stellung (nicht stumm/falsch bis Cutoff bewegt wird).
Mess:   __para3Capture(96000): 8 Onsets im erwarteten Raster (Jitter 0);
        Spektral-Centroid passt zur angezeigten Cutoff-Stellung ±Tol.;
        kein Klick (|dx|-Metrik).
-->

<!-- =================================================================== -->
<!-- DEMO-TRACKS P1-P5 — vom Menschen freigegeben (Tempo 70→140 linear,
     Akzent-Linie + Dirty-Indikator, alle Funktionen über die 5 Tracks
     verteilt; Voice-Modi 5/6 abgedeckt, MOTION-REC-Targets 5/5
     unterschiedlich, alle 3 LFO-Shapes, alle 5 ARP-Modi via P5-Motion). -->

### US-DEMO-P1-HIPHOP — HIP-HOP-Groove @ 70 BPM lädt + spielt + Motion-VOL-Swell
Given:  Cold-Start, kein Bedienelement berührt. Audio gestartet (TAP TO START).
When:   Klick auf Button `P1`. Anschließend Klick `▶` (Play).
Then:   Display zeigt `P1: HIP-HOP`. Akzent-Linie unter P1 (loaded, clean).
        POLY-Voice, regulärer 16-Step-Sequenzer mit 8-Note-Bassgroove
        (C2/D#2/G2/F2/D#2-Cycle), METRO an, Tempo 70 BPM.
        MOTION-REC auf VOLUME: 16-Step-Lane mit Swell-Kurve (rampt von ~12 auf
        ~95 und wieder auf 18 zurück über die 16 Steps).
        FLUX/F·REC werden separat von einer Live-Record-Story exerziert
        (Pre-Baking aus JS ist nicht sample-genau).
Mess:   __para3Capture(~96000 Samples bei 48 kHz = 2 s = 1 Loop @ 70 BPM × 1/16 × 16):
        - 8 Note-Onsets im 2-s-Fenster (16 Steps mit Gates [1,0,0,1,0,0,1,0, 1,0,1,0, 1,0,1,0]),
        - RMS-Profil folgt der Swell-Kurve: mittlere Hälfte deutlich lauter als Anfang/Ende,
        - METRO-Tick (Quarter-Note) als zusätzliche Hochfrequenz-Bursts erkennbar.

### US-DEMO-P2-BERLIN — BERLIN-Sequenz @ 88 BPM mit TEMPO DIV 1/2, FIFTH-Voice, MOTION-REC/PORTAMENTO
Given:  Cold-Start, audio ready.
When:   Klick `P2`, Klick `▶`.
Then:   Display `P2: BERLIN`. FIFTH-Voice aktiv (zwei Pitches Quint auseinander).
        TEMPO DIV 1/2 (effektiv 44 BPM), ACT-Skips auf Step 4 und 11.
        LFO TRIANGLE+SYNC, sanfte Pitch-Modulation. Portamento glide
        progressiv stärker über 16 Steps (MOTION-REC-Lane).
Mess:   __para3Capture(192000 = 4 s):
        - FFT zeigt Doppel-Peak (Note + Note+7 Halbtöne) → Quint-Stack-Beleg,
        - Onset-Spacing 681 ms (= 88 BPM × 1/2 × 1/16 = 1/8 effektiv),
        - Steps 4 und 11 zeigen messbare Audio-Lücken (RMS <0.01 dort),
        - Pitch-Glide-Slope steigt über die 16 Steps an (Centroid-Drift).

### US-DEMO-P3-VKBRASS — Volca-Keys-Brass-Arp @ 105 BPM, UNISON, ARP UP HOLD ×2 Oct, MOTION-REC/DETUNE+SMOOTH
Given:  Cold-Start, audio ready.
When:   Klick `P3`, Klick `▶`.
Then:   Display `P3: VK-BRASS`. UNISON-Voice. ARP ON+HOLD, MODE UP, OCT×2,
        GATE 0.7, RATE 1/16, Akkord Cm9 latched. LFO SAW, CUT_INT positiv.
        MOTION-REC auf DETUNE mit SMOOTH on → kontinuierliche Detune-Drift.
        DELAY 1/8 dotted FB 0.30.
Mess:   __para3Capture(192000 = 4 s):
        - 4 verschiedene Note-Frequenzen detektierbar (C-Eb-G-Bb-Cycle inkl. Oct×2),
        - DETUNE-Drift: Spektral-Centroid wandert über die 16 Steps (SMOOTH-Interp),
        - LFO-modulierte Cutoff: 16th-Note-Periodizität im Centroid-Spektrum,
        - DELAY-Tail: nach abruptem Cutoff bleibt RMS >5 % des Peaks für >250 ms.

### US-DEMO-P4-ACID — TB-303 Acid Bassline @ 122 BPM, STEP TRIGGER, MOTION-REC/CUTOFF
Given:  Cold-Start, audio ready.
When:   Klick `P4`, Klick `▶`.
Then:   Display `P4: ACID`. POLY-Voice, 16-Step-Pattern (C2/C2/D#2/G1/C2/Bb1/...),
        STEP TRIGGER on, Resonance hoch, EG_INT positiv, schnelles DECREL.
        MOTION-REC auf CUTOFF: voller 16-Step-Sweep tief→hoch→tief.
        DELAY 1/16 ping-pong.
Mess:   __para3Capture(192000 = 4 s):
        - 16 saubere Note-Onsets in 1.97 s (122 BPM × 1/16 = 8 Hz Onset-Rate),
        - Spektral-Centroid wandert sinusoidal über 16 Steps (CUTOFF-Lane),
        - Resonance-Peak detektierbar als schmaler Spektral-Peak bei Cutoff-Freq,
        - STEP-TRIGGER-Beleg: jeder Onset hat Attack-Slope (kein verschmiertes Sustain).

### US-DEMO-P5-RNG — Ring-Mod Random-Arp @ 140 BPM, MOTION-REC/ARP_MODE cycelt alle 5 Modi
Given:  Cold-Start, audio ready.
When:   Klick `P5`, Klick `▶`.
Then:   Display `P5: RING-RND`. POLYRING-Voice, ARP ON+HOLD, OCT×3, GATE 0.5,
        Akkord C-F#-G# (Ganztoncluster). LFO SQUARE+CUT_INT (Stepper-Effekt).
        MOTION-REC auf ARP_MODE: 16-Step-Lane cyclt UP→DN→UPDN→AS→RND
        (jeweils ~3-4 Steps pro Modus). Tempo 140 BPM.
Mess:   __para3Capture(192000 = 4 s):
        - inharmonische Spektren (Ring-Mod-Sidebands) bei nicht-musikalischen Frequenzen,
        - 4 messbar verschiedene Spektral-Signaturen über die 16 Steps (4 Mode-Blöcke),
        - Cross-Block-Diff der Spektren signifikant (Wechsel von sortiert zu zufällig),
        - SQUARE-LFO-Cutoff: harte 1/16-Stepper-Transienten im Spektrogramm.

### US-DEMO-DIRTY — Demo-Edit setzt Dirty-Indikator, Re-Click setzt zurück
Given:  P1 wurde geladen (Akzent-Linie solid `--accent-dim`). Audio läuft.
When:   Eine beliebige Knob/Fader-Bewegung (z. B. Cutoff +20 %).
Then:   Akzent-Linie unter P1 dimmt auf `--accent-faint`. Display zeigt `P1: FLUX-HOP*`.
When:   Erneuter Klick auf `P1`.
Then:   Original-State wieder geladen, Akzent-Linie wieder solid, Display ohne `*`.
Mess:   Spektrum nach Re-Click bitidentisch zum initialen Load (`max|d|<1e-6` über
        eine 100-ms-Vergleichsfensterung).

### US-DEMO-ISOLATION — Wechsel zwischen P1..P5 ohne Reststate-Verwicklung
Given:  Cold-Start, audio ready.
When:   Sequentiell P1 → P2 → P3 → P4 → P5 klicken, jeweils Play + 2 s Audio capturen,
        Stop, nächste Demo. Am Ende erneut P1 klicken.
Then:   Jeder Track produziert seine charakteristische Signatur, kein Rest aus dem
        vorigen Track (z. B. kein Ring-Mod-Klang nach Wechsel auf POLY-Track).
        Re-Load P1 am Ende bitidentisch zum ersten P1-Load.
Mess:   5 verschiedene Spektral-Signaturen, paarweise Diff signifikant; finaler P1-Load
        `max|d|<1e-6` zum ersten P1-Load über 100-ms-Fenster.


## §4 Ausführung & Akzeptanz

```
cd tests/e2e
npx playwright install --with-deps chromium   # einmalig
npx playwright test                            # MUSS: 0 failed
```

Teil der Pflicht-Prüfroutine. Ein Sprint/Block ist erst „done", wenn
Engine-Suite **und** E2E-Gate **und** dieses Playwright-User-Story-Gate
(alle definierten Stories + US-COLD/ONE/PERSIST/IDEM/ORDER) grün sind.

**Ausführung auf dem VPS.** Playwright + Chromium + WASM-Build laufen auf
dem Server, auf dem `para3.levcon.at` deployt ist: `ssh root@87.106.25.91`.
Dort `npx playwright test` (und der WASM-Build/`wasm_parity`) ausführen und
**grün nachweisen** — nicht überspringen. Der VPS-Zugang ist sensibel:
gehört in eine private/gitignored Ops-Datei, nicht ins öffentliche Repo.

## §5 Was dieses Gate NICHT ist

Kein Ersatz für die DSP-Messungen (T-Suite) — eine **Ergänzung** auf der
realen UI-/Praxisebene. Es beweist nicht „die DSP ist exakt", sondern
„die definierten User-Stories funktionieren im echten Browser exakt wie
beschrieben, ohne unlogische Handgriffe".
