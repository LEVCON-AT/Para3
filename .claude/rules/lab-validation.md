---
paths:
  - "tools/measure/**"
  - "docs/measurements/**"
  - "docs/DATENBLATT.md"
  - "docs/BERICHT.md"
  - "docs/AUDIT.md"
---

# Regel: Lab-Validation

**Maßgeblich: `docs/specs/CLAUDE_LAB_VALIDATION.md` — vor Arbeit lesen.** Die
Spec definiert die 10-Sprint-Struktur (LAB-1..10), Pflicht-Toolkit unter
`tools/measure/`, Standard-Fallen, Engine-Eigenheiten und Akzeptanz.

Hart:
- **10 Sprints, ein Commit pro Sprint.** LAB-1 baut Infrastruktur. LAB-2..6
  pro Engine-Block je SVG-Plot + Messwert + Pass/Fail. LAB-7 UI-Walk-Through.
  LAB-8 Code-Audit (`docs/AUDIT.md`). LAB-9 Datenblatt + Bericht. LAB-10
  Polish + Push.
- **Neutral-Bitidentität first.** Jede Mess-Erweiterung darf den Default-Pfad
  NICHT um `max|d| > 0` ändern. Wenn doch, ist die Mess-Methode falsch — nie
  Erwartung aufweichen.
- **Methodologie-Fixes statt Threshold-Senken.** Bei FAIL zuerst Methode
  hinterfragen (Peak vs Band-Energy, Sustain-Level, Warm-up-Zeit, ARP-Routing
  via `midiNoteOn`). Reale Engine-Eigenheiten (LadderZDF schwingt nicht selbst,
  FLUX-Quantize ist `loopLen/16`, …) als Stability-/Charakteristik-Test
  formulieren, nicht als Bug-Fail.
- **Vollregression nach jedem DSP-Sprint** (Engine T-Tests + C-API WA-Tests
  + Scope-Source + Transport + WASM↔native + Lab-Measurements). 0 Compiler-
  Warnings, alle Suiten `OVERALL: PASS`.
- **Push erst nach expliziter `push`-Anweisung.** Auch bei vollständig grüner
  Vollregression nicht eigeninitiativ pushen.
- Drei verpflichtende Doku-Files am Ende: `docs/DATENBLATT.md` (Spec-Tabellen
  mit Querverweis zu M-IDs), `docs/BERICHT.md` (narrativer Bericht mit
  eingebetteten SVGs), `docs/AUDIT.md` (JS-Bridge / Style / Magic-Numbers).
