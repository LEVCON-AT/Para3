---
paths:
  - "Para3Engine.hpp"
  - "offline_test.cpp"
---

# Regel: Engine-DSP (Parität)

**Vor jeder Änderung an `Para3Engine.hpp`/`offline_test.cpp` zuerst
`docs/specs/CLAUDE_KORG_PARITY_DSP.md` lesen** (Andock-Stellen mit
Zeilennummern in Anhang B, Bindungen in Anhang D).

Pflicht:
- Header-only, kein Refactor, keine neue `.cpp`. Bestehende Klassen-Layouts
  und Zeilenanker respektieren (Anhang B).
- Neuer Parameter: ans Ende von `enum class Param`; Taper-Case; Routing in
  `setParamNorm`; eigener `RampParam`-Glätter, in `prepare` neutral
  gesnappt. Bipolar via `(n-0.5)*2`. **Neutral = bitidentisch** (`max|d|=0`)
  — als erster Test-Check beweisen.
- Geteilte Stufen (EG/LFO/VCF/VCA) nur **lesen**, nicht duplizieren
  (Volca ist paraphon: EINE Hüllkurve/EIN Filter/EIN VCA).
- Klickfrei: nur über bewährte Pfade (env-Gate, RampParam, bandlimitierte
  Oszillatoren). Treue-Konflikt → Kommentar `// <Sprint> Treue-Konflikt: …`
  + Spec, nicht still glätten.
- `CALIB(E8)`-Konstante: Algorithmus voll, Zahl als Default beschriftet
  `// CALIB(E8) …`. Nie nach Gehör. E8 ist menschseitig.
- Neuer Test `T<n>` in `offline_test.cpp` vor dem Summary-Block; misst
  Wirkung **und** Neutral-Bitidentität **und** Klickfreiheit. Danach volle
  Prüfroutine (siehe Root-`CLAUDE.md`), 0 Warnungen, die **volle vorhandene
  Repo-Suite** unverändert PASS (Anzahl aus dem Repo lesen, nicht annehmen).
