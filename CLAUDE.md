# PARA·3 — Projektgedächtnis (Claude Code, Repo-Root)

PARA·3 ist ein klanggetreuer, produktionsreifer Software-Klon der **KORG
Volca Keys** (paraphon: 3 Saw-Oszillatoren → Mixer → EIN gemeinsamer VCF →
EIN gemeinsamer VCA/EG → Digital-Delay). C++17, header-only DSP.

Diese Datei ist die **oberste, nicht verhandelbare** Vorgabe. Sie wird jede
Session automatisch geladen und überlebt `/compact`. Detail-Specs liegen in
`docs/specs/`, path-scoped Regeln in `.claude/rules/` (laden nur beim
Arbeiten an passenden Dateien). **Antworten an den Menschen auf Deutsch.**

## Unantastbare Invarianten

- **Keine Fakes, kein Blender, keine Helper-Shortcuts.** Echte DSP,
  band-limitiert, RT-sauber. Keine vorgetäuschten/„fast richtigen" Pfade.
- **Gemessen statt behauptet.** Jede Qualitätsaussage steht als realer,
  ausgeführter Test in der Suite. Defekte echt fixen und neu messen — **nie**
  Schwellen aufweichen, um „grün" zu erzeugen.
- **Neutral/Aus = BITIDENTISCH** zum vorherigen Engine-Stand:
  `max|d| = 0.000e+00`. Das ist der primäre Anti-Blender-Beweis und Pflicht
  für jeden neuen Parameter/jede neue Funktion.
- **Header-only, kein Refactor, keine neue Engine-`.cpp`.** Nur diese Dateien
  dürfen sich ändern: `Para3Engine.hpp`, `wasm-bridge/para3_capi.h`,
  `wasm-bridge/para3_capi.cpp`, `offline_test.cpp`,
  `wasm-bridge/capi_test.cpp`, `wasm-bridge/build_wasm.sh` (nur Export-Liste).
  Frontend/E2E zusätzlich nur die in `.claude/rules/frontend.md` /
  `e2e-gate.md` genannten Dateien.
- **Durch den Trichter.** Parameter immer via `setParamNorm` (Taper →
  RampParam-Glätter). RT-safe: keine Allokation/Lock/Exception/Syscall in
  `render`. Kein `rand()`/`std::random` (eigener seedbarer PRNG).
- **Klickfrei & bandbegrenzt.** Treue-Konflikte werden **benannt**, nicht
  still weggeglättet (Code-Kommentar + Spec).
- **0 Compilerwarnungen. Niemals `-ffast-math`.**
- **Ein Block = ein Test = ein Commit.** Volle Regression muss grün sein,
  bevor der nächste Block beginnt.
- **`CALIB(E8)`** = Hardware-Kalibrierungs-Platzhalter: Algorithmus voll
  implementiert, NUR die Zahl offen. Nie „nach Gehör". E8 (Messung gegen die
  echte Volca) ist Aufgabe des Menschen, nicht in Software.
- **`EXT-…`** = bewusste Erweiterung jenseits der Volca-Treue (z. B. der
  Arpeggiator). Jede solche Zeile trägt `// EXT-<name>`, Default AUS, und
  „AUS ⇒ bitidentisch zum Paritäts-Stand". `EXT-…-DEFAULT`-Konstanten sind
  Design-Defaults (kein HW-Vorbild), **nicht** `CALIB(E8)`.
- **User-Story-Gate (Playwright).** Mess-Tests beweisen die DSP isoliert —
  sie fangen KEINE Bugs im echten Nutzungspfad (z. B. „Knopf wirkt erst nach
  Nach-Anfassen"). Arbeitsweise pro Anforderung: Claude Code **recherchiert
  die relevanten User-Cases entlang der aktuellen Anforderung, schlägt sie
  vor, und wartet auf die explizite Freigabe des Menschen** („abnicken").
  Erst freigegebene Stories werden **wörtlich** als **Playwright**-Tests
  umgesetzt (Chromium, echte UI-Klicks/Drags gegen die reale Seite,
  Assertions über den deterministischen test-only Audio-Tap). Pflicht-Stories
  US-COLD/ONE/PERSIST/IDEM/ORDER gelten immer. Keine still-grünen Tests;
  Fehlschlag ⇒ echten Defekt fixen, nie Erwartung aufweichen.

## Pflicht-Prüfroutine (nach JEDEM Block, alles muss PASS sein)

```
# 1) Engine-Suite (T1..Tn)
g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o /tmp/e && /tmp/e

# 2) C-API-Suite (WA1..WAn)
cd wasm-bridge && g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. \
  para3_capi.cpp capi_test.cpp -o /tmp/c && /tmp/c

# 3) Scope-Source
cd wasm-bridge && g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. \
  para3_capi.cpp scope_source_test.cpp -o /tmp/s && /tmp/s

# 4) Transport (Node)
cd wasm-bridge && node ring_test.mjs && node audio_test.mjs && node port_test.mjs

# 5) WASM-Build + WASM↔nativ-Δ — auf dem VPS (emcc dort vorhanden):
#    ssh root@87.106.25.91
#    cd <repo>/wasm-bridge && bash build_wasm.sh && node wasm_parity.mjs
#    -> muss PASS / Δ im Toleranzband sein (NICHT überspringen)
```

Build/WASM/Playwright laufen auf dem VPS, auf dem `para3.levcon.at`
deployt ist: `ssh root@87.106.25.91`. Diese Schritte werden dort
ausgeführt und müssen grün sein — nicht „menschseitig übersprungen".
Der VPS-Zugang ist sensibel: gehört in eine private/gitignored Ops-Datei
(z. B. `CLAUDE.local.md`), nicht in ein öffentliches Repo.

Akzeptanz: alle Suiten `OVERALL: PASS (0 failures)`, **0 Compilerwarnungen**.
T1–T26 und WA1–WA6 sind die bestehende Baseline und müssen **unverändert**
PASS bleiben — jede Abweichung ist ein echter Regressionsdefekt.

Jede neue C-API-Funktion MUSS in `build_wasm.sh` `EXPORTED_FUNCTIONS`
(Präfix `_`) ergänzt **und** im `capi_test`-Sweep ausgeübt werden.

## Architektur (Kurz)

Signalfluss `Para3Engine.hpp`: `Oscillator×3 → Mixer → LadderZDF (EIN VCF)
→ AdsrEnvelope (EIN VCA/EG) → Delay`. `ParaAllocator` macht Note→Osz,
`Lfo` global, `Clock` sample-genaues Step-Timing, `Controller` treibt
Sequencer/Flux/Motion und alle Parameter durch den Trichter.
Klassen-Zeilenlandkarte: siehe `docs/specs/ENGINE_E1-E7_HANDOVER.md`.

## Stand & Specs (kanonisch in `docs/specs/`)

- E1–E7 (KORG-Parity) **fertig, gemessen, grün** — `ENGINE_E1-E7_HANDOVER.md`
- DSP-exakte Regeln: `CLAUDE_KORG_PARITY_DSP.md` (Anhang A Datei-Politik,
  Anhang B Andock-Stellen, Anhang C E8-Messprotokoll, Anhang D Bindungen)
- Frontend-Deltas: `CLAUDE_FRONTEND_PARITY.md`
- Gemeinsames Abnahme-Gate: `CLAUDE_PARITY_E2E_GATE.md`
- User-Story-Gate via Playwright (vom Menschen definiert, exakt geprüft):
  `CLAUDE_USER_SCENARIOS.md`
- Erweiterung Arpeggiator (NICHT Parität): `CLAUDE_EXT_ARP.md`
- Lab-Validation Methodologie (10-Sprint-Workflow für Datenblatt + Bericht +
  Audit + Polish, reproduziert die Qualität der LAB-1..10-Serie):
  `CLAUDE_LAB_VALIDATION.md`
- Staging / Prod Workflow (zwei Umgebungen, Branch-Strategie, Deploy-Helper,
  Promote-Gate; gilt für alle Code-Änderungen ab 2026-05-19):
  `CLAUDE_STAGING_PROD.md`

Vor Arbeit an einem Bereich die zugehörige `.claude/rules/`-Regel beachten;
sie verweist auf die volle Spec in `docs/specs/` — diese **vor** der
Implementierung lesen. Bei Konflikt gilt: diese `CLAUDE.md` > Spec > Prompt.

## Liefer-Disziplin

Pro Block: geänderte Dateien + Unified-Diff gegen den Vorstand +
ausgeführte Mess-Logs + grüne volle Regression. Ehrlich über Grenzen
(WASM/Playwright werden auf dem VPS verifiziert, nicht übersprungen). Den von Claude Code evolvierten
Repo-Stand **nie** blind überschreiben — nur die erlaubten Dateien gezielt.
