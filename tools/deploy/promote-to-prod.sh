#!/usr/bin/env bash
# PARA-3 :: Promote staging → prod nach successful staging-Validierung.
#
# WICHTIG: nur ausführen wenn Lab-Validation auf staging GRÜN ist
# (engine T-Tests, c-api WA-Tests, scope-source, transport, wasm-parity,
#  measurement-suite, MCP-Playwright Stories).
#
# Workflow:
#   1. lokal `git checkout main && git merge --ff-only staging && git push`
#   2. dann diesen Script: rsync staging→prod auf dem VPS, prod neu starten.
#
# Prod ist (Stand 2026-05-19) KEIN git-Checkout — wird über rsync
# aus dem staging-Tree synchronisiert. Build-Artefakte (.wasm, icons)
# werden mitkopiert, *.o + _stage + node_modules werden ausgeschlossen.
set -euo pipefail
VPS=${PARA3_VPS:-root@87.106.25.91}
SDIR=/opt/synth/para3-engine-staging
PDIR=/opt/synth/para3-engine

ssh "$VPS" "
  set -e
  echo '--- check staging is on main HEAD ---'
  cd $SDIR
  git fetch origin
  STAGE_HEAD=\$(git rev-parse HEAD)
  MAIN_HEAD=\$(git rev-parse origin/main)
  if [ \"\$STAGE_HEAD\" != \"\$MAIN_HEAD\" ]; then
    echo 'WARN: staging HEAD '\"\$STAGE_HEAD\"' != main '\"\$MAIN_HEAD\"
    echo '      \"git pull origin staging\" auf VPS oder \"git push\" lokal vor promote.'
  fi
  echo '--- rsync staging → prod (wasm-bridge + engine sources + tools) ---'
  rsync -av --delete \\
    --exclude='.git' \\
    --exclude='_stage' \\
    --exclude='node_modules' \\
    --exclude='*.o' \\
    --exclude='docs/measurements/M*.svg' \\
    --exclude='docs/measurements/UI-*.png' \\
    --exclude='docs/measurements/MANIFEST.*' \\
    $SDIR/ $PDIR/
  echo '--- pm2 reload para3-serve ---'
  pm2 reload para3-serve
  echo '--- prod promote done ---'
"
echo ''
echo 'Test gegen https://para3.levcon.at/'
