---
paths:
  - "wasm-bridge/*responsive*.html"
  - "wasm-bridge/para3-*.js"
---

# Regel: Frontend

**Vor Änderungen `docs/specs/CLAUDE_FRONTEND_PARITY.md` lesen.** Dort stehen
die echten, verifizierten Bezeichner der aktuellen UI (KNOB_PARAM,
INERT_KNOBS, emitKnob/emitFader, FADER_PARAM, VOICE_MODE, flushPattern,
paint, …) und die exakten Änderungspunkte je Sprint.

Pflicht:
- **Kein Restrukturieren** der bestehenden UI/JS. Nur die in der Spec
  benannten, punktuellen Änderungen.
- Synth-Parameter laufen über den vorhandenen Knopf-/Fader-Mechanismus
  (Trichter). Controller-Settings (Tempo, Swing, Step-Trigger, Tempo-Div,
  Active-Step, Metro, Flux, **Arp**) sind KEINE `setParamNorm`-Parameter —
  nicht in KNOB_PARAM/INERT_KNOBS aufnehmen, eigene Steuer-Hooks.
- Erweiterungen (z. B. Arp-Panel) im UI sichtbar als „EXT" kennzeichnen und
  vom Volca-getreuen Bereich absetzen. Default-Zustand = wie bisher
  klingend (Erweiterung aus).
- Ein Sprint ist erst „done", wenn auch das E2E-Gate grün ist
  (`.claude/rules/e2e-gate.md`).
