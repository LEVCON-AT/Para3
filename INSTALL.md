# Einbau in den Repo (einmalig)

Ziel: Claude Code befolgt die Vorgehensweisen dauerhaft & kompaktionsfest.

## 1. Dateien an diese Stellen im Repo-Root kopieren

```
<repo>/CLAUDE.md                      <- aus diesem Paket (Root)
<repo>/.claude/rules/engine-dsp.md    <- aus diesem Paket
<repo>/.claude/rules/capi-wasm.md
<repo>/.claude/rules/frontend.md
<repo>/.claude/rules/e2e-gate.md
<repo>/.claude/rules/ext-arp.md
<repo>/.claude/rules/user-scenarios.md
<repo>/.claude/rules/lab-validation.md
<repo>/.claude/rules/staging-prod.md
<repo>/docs/specs/ENGINE_E1-E7_HANDOVER.md   <- HISTORISCH (nur E1–E7-Meilenstein)
<repo>/docs/specs/CLAUDE_EXT_BASS.md          <- nächster Arbeitsauftrag
<repo>/docs/specs/CLAUDE_EXT_ARP.md
<repo>/docs/specs/CLAUDE_USER_SCENARIOS.md
<repo>/docs/specs/CLAUDE_LAB_VALIDATION.md
<repo>/docs/specs/CLAUDE_STAGING_PROD.md
<repo>/docs/specs/CLAUDE_KORG_PARITY_DSP.md  *  siehe Hinweis unten
<repo>/docs/specs/CLAUDE_FRONTEND_PARITY.md  *
<repo>/docs/specs/CLAUDE_PARITY_E2E_GATE.md  *
```

`*` `CLAUDE_KORG_PARITY_DSP.md` und `CLAUDE_FRONTEND_PARITY.md` liegen im
**Repo bereits** unter `docs/specs/` — nicht überschreiben.
`CLAUDE_PARITY_E2E_GATE.md` existiert nicht separat; die E2E-Disziplin liegt
in `.claude/rules/e2e-gate.md` + `CLAUDE_USER_SCENARIOS.md`.

**Stand:** Das Repo ist ~30 Commits über E1–E7 (E1–E7 + EXT-ARP + EXT-FLUX
u. a.). `ENGINE_E1-E7_HANDOVER.md` dokumentiert nur den historischen
E1–E7-Meilenstein — **nicht** den aktuellen Stand. `engine-delivery/` (alter
E1–E7-Snapshot) NICHT über das Repo legen. Source of Truth = aktueller
Repo-Stand; Baseline = dessen volle vorhandene grüne Suite.

## 2. Wichtig zur Lade-Mechanik (verifiziert, offizielle Doku)

- Nur `CLAUDE.md` (Repo-Root), `.claude/CLAUDE.md`, `~/.claude/CLAUDE.md`,
  `CLAUDE.local.md` und **alle `.claude/rules/*.md`** werden automatisch
  geladen. Dateien wie `CLAUDE_BUILD.md` (Suffix) werden **nicht** automatisch
  geladen — entweder aus `CLAUDE.md` referenzieren oder ignorieren.
- Repo-Root-`CLAUDE.md` überlebt `/compact` (wird neu eingelesen).
- `.claude/rules/*.md` mit `paths:`-Frontmatter laden **nur**, wenn an einer
  passenden Datei gearbeitet wird → schlanker Kontext.
- `CLAUDE.md` kurz halten (< ~200 Zeilen); Detail gehört in `docs/specs/`
  und wird über die Regeln punktuell hereingeholt.

## 3. Verifizieren

In der Claude-Code-REPL `/memory` ausführen — es listet alle geladenen
Memory-Dateien mit Pfad/Reihenfolge. Erwartet: Root-`CLAUDE.md` immer; die
jeweilige `.claude/rules/`-Regel erscheint, sobald eine passende Datei
geöffnet/bearbeitet wird (z. B. `engine-dsp.md` beim Arbeiten an
`Para3Engine.hpp`). Wenn eine Regel fehlt, stimmt `paths:` oder der Ablageort
nicht.

## 3b. Playwright (User-Story-Gate)

`CLAUDE_USER_SCENARIOS.md` verlangt Playwright-Tests unter `tests/e2e/`.
Einmalig im Repo: `npm i -D @playwright/test` und
`npx playwright install --with-deps chromium`. Lauf:
`cd tests/e2e && npx playwright test` (MUSS 0 failed). Läuft NICHT in der
Engine-Agent-Sandbox (kein Browser/`emcc`) — Claude Code/menschseitig
ausführen; den §3-Story-Katalog füllst DU vor der Übergabe.

## 4. Git

`CLAUDE.md`, `.claude/rules/` und `docs/specs/` committen (teamweit gültig).
`CLAUDE.local.md` (falls je genutzt) gehört in `.gitignore` (persönlich).
