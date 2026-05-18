# PARA·3 — User-Story-Gate (Playwright)

Per `docs/specs/CLAUDE_USER_SCENARIOS.md`: formale, reproduzierbare Tests gegen
die ausgelieferte UI bei `https://para3.levcon.at/`. Bedienung über echte
DOM-Klicks/Drags, Assertions über den deterministischen Audio-Tap
`window.__para3Capture` (gated `?e2e=1`, production bitidentisch wenn aus).

## Voraussetzungen (einmalig)

```
cd tests/e2e
npm install                         # JS-only, kein Browser-Binary
npx playwright install chromium     # ACHTUNG: lädt Chromium-Binary
                                    #         (.exe auf Windows)
```

Der zweite Schritt lädt Chromium in `~/.cache/ms-playwright/` (Linux/macOS)
bzw. `%LOCALAPPDATA%\ms-playwright\` (Windows). Bei restriktivem
Antivirus (Sophos &Co.) kann das blocken — dann auf VPS oder dedizierter
Test-Maschine ausführen.

## Tests laufen

```
npx playwright test                 # alle Specs, headless, 0 retries
npx playwright test --headed        # mit sichtbarem Browser (Debug)
npx playwright test smoke.spec.ts   # einzelne Datei
```

Akzeptanz: `OVERALL: 0 failed`. Per CLAUDE.md ist das Teil der Pflicht-
Prüfroutine — kein "Sprint done", solange das Gate rot ist.

## Test-Bestand

* `smoke.spec.ts` — Sprint 7 Infrastruktur-Smoke (Bootstrap + Capture-Hook)
* `demos.spec.ts` — US-DEMO-P1..P5 + US-DEMO-DIRTY + US-DEMO-ISOLATION
* `mandatory.spec.ts` — Pflicht-Stories US-COLD/ONE/PERSIST/IDEM/ORDER

## Audio-Tap

`window.__para3Capture(n)` liefert die letzten `n` Samples des Engine-
Outputs als `Float32Array`. Sample-Rate via
`window.__para3CaptureSampleRate()`, aktueller Schreib-Index via
`window.__para3CaptureCursor()`. Capture-Ring ist 8 Sekunden lang.

Hook ist gated:
* aktiv bei `?e2e=1` oder `window.__PARA3_E2E = true` VOR dem audio.start
* aus → kein ScriptProcessor-Node, Produktionsgraph bit-identisch

## Helper

`helpers/audio.ts` bietet `bootstrap()`, `capture()`, `rmsWindows()`,
`detectOnsets()`, `goertzelPower()`, `specDb()`, `peakRMS()`,
`waitSamples()` — gleiche Metrik-Semantik wie die C++/mjs-Suite.

## VPS-Lauf (optional)

Auf dem Deploy-Host (`para3.levcon.at`-VPS) ginge identisch:
```
ssh root@87.106.25.91
cd /opt/synth/para3-engine/tests/e2e
npm install
npx playwright install --with-deps chromium    # plus apt deps
npx playwright test
```

Dort kein Sophos-Issue, dafür apt-Eingriff für `--with-deps`.

## Hinweis zur aktuellen Test-Validierung

Solange `npx playwright install chromium` nicht ausgeführt wurde, lässt
sich `npx playwright test` lokal nicht starten (kein Browser). Tests
wurden in dieser Phase **via Playwright-MCP-Tool** validiert (gleiches
Playwright, andere Aufruf-Schicht — kein Browser-Binary-Download). Der
Test-Code im Repo ist identisch lauffähig sobald die einmalige
Installation erfolgt — siehe oben.
