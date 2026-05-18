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

<!-- weitere User-Stories hier eintragen -->

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
