#!/usr/bin/env bash
# PARA-3 :: Deploy aktueller staging-Branch nach staging.para3.levcon.at.
#
# Voraussetzung: lokal auf branch `staging`, alle gewollten Commits in
#                origin/staging gepusht.
# Wirkung:       ssh root@VPS → /opt/synth/para3-engine-staging
#                git pull origin staging
#                wasm bei Engine-Header-Änderungen neu builden
#                pm2 reload para3-staging-serve (falls serve.mjs geändert)
set -euo pipefail
VPS=${PARA3_VPS:-root@87.106.25.91}
SDIR=/opt/synth/para3-engine-staging

ssh "$VPS" "
  set -e
  cd $SDIR
  git fetch origin
  # Bewahre build-Artefakte (para3.wasm, icons, *.o), reset NUR auf
  # die tracked-files-state von origin/staging.
  git reset --hard origin/staging
  echo '---' && git log --oneline -3
  # Wenn Engine-Header/Source/Export-Liste geändert wurden: WASM neu builden.
  if [ -f wasm-bridge/build_wasm.sh ]; then
    if git diff --name-only HEAD~1 HEAD 2>/dev/null | grep -q 'Para3Engine.hpp\\|wasm-bridge/para3_capi\\|wasm-bridge/build_wasm.sh'; then
      echo '--- engine changed → rebuilding wasm ---'
      # emsdk liegt unter /opt/emsdk; emcc ist im non-interactive ssh
      # nicht im PATH — env explizit sourcen, sonst "command not found".
      source /opt/emsdk/emsdk_env.sh >/dev/null 2>&1 || true
      cd wasm-bridge && bash build_wasm.sh && cd ..
    fi
  fi
  # Wenn serve.mjs sich geändert hat, pm2 reload (Datei wird live geladen).
  if git diff --name-only HEAD~1 HEAD 2>/dev/null | grep -q 'wasm-bridge/serve.mjs'; then
    echo '--- serve.mjs changed → pm2 reload ---'
    pm2 reload para3-staging-serve
  fi
  echo '--- staging deploy done ---'
"
echo ''
echo 'Test gegen https://staging.para3.levcon.at/'
