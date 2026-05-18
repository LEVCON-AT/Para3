---
paths:
  - "Para3Engine.hpp"
  - "wasm-bridge/para3_capi.h"
  - "wasm-bridge/para3_capi.cpp"
  - "offline_test.cpp"
  - "wasm-bridge/capi_test.cpp"
---

# Regel: EXT-ARP (Arpeggiator — Erweiterung, NICHT Volca-Parität)

Nur relevant, wenn am Arpeggiator gearbeitet wird. **Maßgeblich:
`docs/specs/CLAUDE_EXT_ARP.md` — vor Implementierung vollständig lesen.**

Harte Punkte:
- Die echte Volca *Keys* hat **keinen** Arp. Dies ist eine bewusste
  Erweiterung. Jede neue Zeile trägt `// EXT-ARP` (nie `// E1..E7`).
- **Default AUS.** `arpEnabled_==false` ⇒ Engine **bitidentisch** zum
  E1–E7-Stand (`max|d|=0.000e+00`). Das ist die Haupt-Akzeptanz (T27a).
- Kein zweiter Audiopfad: Arp erzeugt nur `noteOn/noteOff/retrigger`;
  Klickfreiheit = bewährte Gate-Logik. OFF-vor-ON wie E5.
- Eigener sample-genauer Akkumulator (Triolen exakt, nicht gerundet),
  deterministischer xorshift statt `rand()` (seedbar, reproduzierbar).
- Defaults sind `// EXT-ARP DEFAULT` (kein HW-Vorbild) — **nicht**
  `CALIB(E8)`.
- Step-Sequencer (E3/E4) und Flux (E5) bleiben **wörtlich unverändert**;
  der Arp transformiert nur die Tasten/MIDI-Notenquelle.
- Blöcke A/B/C je mit eigenem Test (T27…) + Commit; volle Regression
  (T1–T26, WA1–WA6) bleibt unverändert PASS; neue Exporte in
  `build_wasm.sh`; E2E-Neutralität bei „Arp aus".
