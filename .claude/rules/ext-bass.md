---
paths:
  - "Para3Engine.hpp"
  - "wasm-bridge/para3_capi.h"
  - "wasm-bridge/para3_capi.cpp"
  - "offline_test.cpp"
  - "wasm-bridge/capi_test.cpp"
  - "wasm-bridge/ui_test.mjs"
  - "tests/e2e/**"
---

# Regel: EXT-BASS (fetter Volca-Bass-Charakter — Erweiterung, NICHT Parität)

Nur relevant beim Arbeiten am Bass-Charakter. **Maßgeblich:
`docs/specs/CLAUDE_EXT_BASS.md` — vor Implementierung vollständig lesen.**

Harte Punkte:
- Eine *Keys* hat keinen Bass-Modus. Bewusste Erweiterung. Jede neue Zeile
  `// EXT-BASS`. Default AUS ⇒ Engine **bitidentisch** (`max|d|=0`) — Haupt-
  Akzeptanz je Block.
- Eine geteilte paraphone Stimme; „Bass" ist eine Konfiguration davon, kein
  zweiter Klang. Layering = §6 (zwei Instanzen, App-Ebene), kein Audio-Loop.
- 5 Bausteine B1–B5 (Pulse, PWM, Spread+Drift, Stack/Mono, Sub), je eigener
  Test + Commit; bandlimitiert (Alias ≤ −74 dBc, T1-Methodik), klickfrei,
  Saw-Pfad/Defaults bitidentisch.
- Kontinuierlich = `setParamNorm`-Params (Trichter); diskret (Waveform,
  Stack, Master) = Controller-Hooks. EXT-Panel separat, kein UI-Restructure.
- Defaults sind `// EXT-BASS DEFAULT` (kein HW-Vorbild), **nicht**
  `CALIB(E8)`. Fett-Metriken sind Korridore, kein Treue-Score.
- **Beleg-Tiering** (§1b/§10): `// EXT-BASS REF` nur mit öffentlicher
  Quelle im Kommentar (KORG-SPEC/VSE/MR), Akzeptanz = messbarer Korridor
  gegen den belegten Wert. B2 PWM / B5 Sub / B3-Drift sind NICHT belegt →
  bleiben `// EXT-BASS DEFAULT`, nie als „bass-treu" bezeichnen.
- **Keine Treue-Behauptung** (Weg A): EXT-BASS nirgends als „originalgetreu/
  Volca-Bass-genau" bezeichnen — nur „Bass-Charakter, strukturell
  nachempfunden". Kein `CALIB(BASS)`, keine erfundenen HW-Zahlen. Offene
  Treue-Lücke + Nachhol-Schritte: siehe `CLAUDE_EXT_BASS.md §8b`.

- FIX-Block (§7): die zwei realen `ui_test.mjs`-Defekte mitbeheben
  (Worklet-Hash-Drift entscheiden+beheben; `.knob.tgt,.fader.tgt`-CSS
  ergänzen) → `ui_test` 0 failures.
- Volle Regression nach jedem Block grün; WASM + Playwright auf dem VPS
  (`ssh root@87.106.25.91`) grün nachweisen. Fehlschlag ⇒ echten Defekt
  fixen, Erwartung NIE aufweichen.
