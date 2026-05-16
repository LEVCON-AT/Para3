# PARA·3 — Paket

Sound-genauer, produktionsreifer Software-Synth (Volca-Keys-Klasse),
header-only C++ DSP + verifizierte WASM/AudioWorklet-Brücke.

## Für Claude Code: HIER STARTEN

**Lies und befolge `CLAUDE_BUILD.md`** — das ist das verbindliche
Ausführungs-Runbook (Phasen, messbare Abnahmekriterien, „messen statt
behaupten"-Disziplin). Ziel des Auftraggebers: **PARA·3 offline auf dem
Handy spielbar** (PWA).

## Inhalt

- `Para3Engine.hpp` — DSP-Engine (header-only, 13/13 Messungen).
- `offline_test.cpp` — Engine-Messsuite (Beweis der Klangqualität).
- `README_STATUS.md` — Stand & Messergebnisse der Engine.
- `wasm-bridge/` — C-API, lock-freier Ring, AudioWorklet, Build-Skript,
  Server, Steuer-Glue, Tests, Integrationsanleitung.
- `CLAUDE_BUILD.md` — **das Runbook. Zuerst lesen.**
- `CLAUDE_INTEGRATE_UI.md` — den schönen Entwurf (`wasm-bridge/para3-responsive.html`) an die Engine binden. Nach Phase A/B lesen.
- `CLAUDE_SCOPE_AND_LAYOUT.md` — Mobile-Layout vollständig erreichbar machen + den Oszilloskop als **echtes, gemessenes** Signal (kein Fake). Nach der UI-Anbindung lesen.

## Reihenfolge

1. Sanity-Tests aus `CLAUDE_BUILD.md` (müssen grün sein, 0 Warnungen).
2. Phase A: WASM bauen + Gleichwertigkeit messen.
3. Phase B: Offline-Handy (SAB-freier Steuerpfad + PWA) — das Kernziel.
4. Phase C: optional, Sprint-1-Hardware-Kalibrierung (nur für 1:1-Klangkopie).

Ehrliche Grenzen stehen in `CLAUDE_BUILD.md` und `README_STATUS.md` —
nichts hier ist gefakt; jede Qualitätsaussage hat einen ausführbaren Test.
