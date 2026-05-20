---
paths:
  - "wasm-bridge/*e2e*.mjs"
  - "wasm-bridge/*_test.mjs"
  - "wasm-bridge/wasm_parity.mjs"
---

# Regel: E2E-Abnahme-Gate

**Maßgeblich: `docs/specs/CLAUDE_PARITY_E2E_GATE.md`.**

Ein Sprint/Block ist erst **gemeinsam „done"**, wenn ALLE drei grün sind:
1. **Engine** — alle neuen `T…` + Regression T1–T26, 0 Warnungen.
2. **Frontend** — die UI-Deltas steuern die korrekten C-API-Hooks.
3. **E2E** — über echten Ring/Port/Worklet:
   - die erwartete Wirkung ist messbar vorhanden,
   - **Neutralität bitidentisch über die volle Kette**: gleiche Eingabe →
     gleiche Samples wie vor dem Sprint, wenn der neue Pfad neutral/aus ist,
   - **kein Naht-Klick** beim Aktivieren/Deaktivieren (T2-Metrik über die
     E2E-Ausgabe).
4. **User-Story-Gate (Playwright)** — `docs/specs/CLAUDE_USER_SCENARIOS.md`:
   alle vom Menschen definierten User-Stories + die Pflicht-Stories
   (US-COLD/ONE/PERSIST/IDEM/ORDER) als Playwright-Tests grün (Chromium,
   echte UI gegen die reale Seite). Fängt unlogische Praxis-Bugs (z. B.
   „Knopf wirkt erst nach Nach-Anfassen"), die die isolierten DSP-Tests
   strukturell nicht sehen.

`build_wasm.sh` + `wasm_parity.mjs` + Playwright laufen auf dem VPS
(`ssh root@87.106.25.91`, Deploy-Host `para3.levcon.at`). Dort ausführen
und grün nachweisen — NICHT überspringen.
