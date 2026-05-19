---
paths:
  - "tools/deploy/**"
  - "docs/specs/CLAUDE_STAGING_PROD.md"
---

# Regel: Staging / Prod Workflow

**Maßgeblich: `docs/specs/CLAUDE_STAGING_PROD.md` — vor Arbeit lesen.** Die
Spec definiert die zwei Umgebungen, Branch-Strategie, Deploy-Helper und das
Promote-Gate.

Hart:
- **Zwei Umgebungen, ein VPS.** `staging.para3.levcon.at` (`:8090`,
  `/opt/synth/para3-engine-staging`, PM2 `para3-staging-serve`) ist ein
  git-Checkout des `staging`-Branches. `para3.levcon.at` (`:8080`,
  `/opt/synth/para3-engine`, PM2 `para3-serve`) ist eine reine
  rsync-Sync-Kopie ohne `.git`.
- **Branch-Disziplin.** Neue Commits gehen IMMER zuerst auf `staging`. `main`
  enthält nur `--ff-only`-Merges von `staging`. Niemals direkt auf `main`
  committen, niemals `git push --force` auf `main`.
- **Deploy-Wörter.** Der Mensch sagt `push` → lokal `git push origin staging`
  (plus optional `deploy-staging.sh`). Der Mensch sagt `promote to prod` (oder
  `nach prod`) → `git checkout main && git merge --ff-only staging && git push`
  und dann `tools/deploy/promote-to-prod.sh`. Niemals beides eigeninitiativ
  ausführen.
- **Promote-Gate erfüllt sein.** Vor jedem `promote to prod`: Engine T-Tests,
  C-API WA-Tests, Scope-Source, Transport, WASM-Parity, Measurement-Suite,
  Lab-Validation und die Pflicht-Stories US-COLD/ONE/PERSIST/IDEM/ORDER plus
  die feature-spezifischen Stories grün **gegen staging**. Erst dann promote.
- **MCP-Playwright zielt während Dev auf `staging.para3.levcon.at`.** Nach
  promote läuft derselbe Story-Sweep zusätzlich als Smoke gegen
  `para3.levcon.at`. Keine direkten Tests gegen prod während aktiver
  Entwicklung.
- **`promote-to-prod.sh` ist whitelisted-rsync.** Schließt `.git`, `_stage`,
  `node_modules`, `*.o`, Measurement-SVGs/-PNGs und das MANIFEST aus. Niemals
  prod-Dateien aus eigener Initiative löschen.
- **Build-Artefakte bleiben auf dem VPS.** `para3.wasm`, Icons, `*.o` werden
  nicht in git committet. Staging baut sie bei Engine-/serve-Diff selbst
  (siehe `deploy-staging.sh`); prod erbt sie über rsync.
- **VPS-Geheimnisse gehören in `CLAUDE.local.md` (gitignored).** Niemals VPS-
  Adresse, SSH-Konfig oder ähnliches in `staging`/`main` committen.
