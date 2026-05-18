---
paths:
  - "tests/e2e/**"
  - "wasm-bridge/*responsive*.html"
  - "wasm-bridge/para3-*.js"
---

# Regel: User-Story-Gate (Playwright)

**Maßgeblich: `docs/specs/CLAUDE_USER_SCENARIOS.md` — vor Arbeit lesen.**

Hart:
- Arbeitsweise: Claude Code **recherchiert** die User-Cases entlang der
  aktuellen Anforderung, **schlägt** sie im §3-Katalog vor (Status
  `VORGESCHLAGEN`) und wartet auf das **Abnicken** des Menschen
  (`FREIGEGEBEN`). Nur freigegebene Stories werden **wörtlich** als
  Playwright-Tests umgesetzt. Keine still-grünen/erfundenen Tests.
- Verifikation NUR über `@playwright/test` (Chromium, headless) gegen die
  reale Seite; Bedienung über echte Locator-Klicks/Drags der tatsächlichen
  Regler — **nicht** via direkte JS-/Engine-Aufrufe.
- Assertions über den deterministischen, test-only Audio-Tap
  (`window.__para3Capture`), mit denselben Metriken wie die C++/mjs-Suite.
  Der Tap MUSS neutralitätstreu sein: Test-Flag AUS ⇒ Produktionsausgabe
  bitidentisch. Hook als `// E2E-TAP test-only` markieren.
- Pflicht-Stories US-COLD/ONE/PERSIST/IDEM/ORDER immer grün. US-COLD fängt
  „Knopf wirkt erst nach Nach-Anfassen"; roter US-COLD ⇒ struktureller Fix
  (Initial-State-Flush aller Regler/Pattern beim Connect, nicht erst bei
  `onChange`).
- `retries:0`, `workers:1`, kein `waitForTimeout` für Korrektheit. Flakiness
  = Defekt, nicht Toleranz. Fehlschlag ⇒ echten Defekt fixen, Erwartung NIE
  aufweichen.
- Teil der Pflicht-Prüfroutine; „done" erst, wenn Engine-Suite **und**
  E2E-Gate **und** dieses Playwright-Gate grün sind.
- Ausführung auf dem VPS: `ssh root@87.106.25.91` (Deploy-Host
  `para3.levcon.at`); `npx playwright test` + WASM dort grün nachweisen,
  nicht überspringen. VPS-Zugang sensibel → private/gitignored Ops-Datei.
