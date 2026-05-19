# PARA·3 — Staging / Prod Workflow

**Pflicht-Spec für alle Code-Änderungen ab 2026-05-19.** Definiert die zwei
Umgebungen, Git-Branch-Strategie, Deploy-Pfade und die Promote-Disziplin.

## Umgebungen

| Umgebung | URL | VPS-Pfad | PM2-Service | Port |
|---|---|---|---|---|
| Staging | `https://staging.para3.levcon.at` | `/opt/synth/para3-engine-staging` | `para3-staging-serve` | 8090 |
| Prod    | `https://para3.levcon.at`          | `/opt/synth/para3-engine`         | `para3-serve`         | 8080 |

VPS: gemeinsam (siehe CLAUDE.local.md für SSH-Zugang). Beide Hosts hängen am
selben nginx (TLS terminierend, reverse-proxy zu localhost), beide haben
eigene Let's Encrypt Zertifikate.

**Staging ist ein git-Checkout, prod ist eine rsync-Sync-Kopie.**
Beide werden aus demselben Code-Stand bestückt, aber prod hat KEIN `.git`
und wird ausschließlich über `tools/deploy/promote-to-prod.sh` aktualisiert.

## Branch-Strategie

```
local edit
  ↓ commit
  ↓ git push origin staging
[origin/staging]
  ↓ ssh "git pull" (deploy-staging.sh)
[VPS /opt/synth/para3-engine-staging]
  ↓ serve.mjs :8090
[https://staging.para3.levcon.at]
  ↓
  ↓  (Tests laufen, Lab-Validation grün)
  ↓
local: git checkout main && git merge --ff-only staging && git push
[origin/main]
  ↓ promote-to-prod.sh rsync
[VPS /opt/synth/para3-engine]
  ↓ serve.mjs :8080
[https://para3.levcon.at]
```

**Regel:** Neue Commits gehen IMMER zuerst auf `staging`. `main` enthält nur
fast-forward-merges von `staging`. **Niemals direkt auf `main` committen.**

## Deploy-Wörter

Der Mensch sagt im Chat:

| Kommando | Wirkung |
|---|---|
| `push` | lokal → `git push origin staging` → ggf. `deploy-staging.sh` (Auto-Pull auf VPS) |
| `promote to prod` (oder `nach prod`) | merge staging → main, push, `promote-to-prod.sh` rsync auf VPS prod, pm2 reload |
| `revert prod` | (nicht implementiert; manuell via git revert + promote) |

`push` ist niedrigriskant und häufig. `promote to prod` ist der eine seltene,
bewusste Schritt; nur wenn **alle** Suiten + Lab-Validation auf staging grün
sind (siehe `CLAUDE_LAB_VALIDATION.md`).

## Deploy-Helper

Unter `tools/deploy/`:

- **`deploy-staging.sh`** — `ssh VPS "cd staging-dir && git pull && (wenn Engine
  geändert) rebuild WASM && (wenn serve.mjs geändert) pm2 reload"`. Idempotent.
- **`promote-to-prod.sh`** — rsync staging → prod (whitelisted; ohne `.git`,
  `_stage`, `*.o`, `node_modules`, measurement SVGs). `pm2 reload para3-serve`.

Beide Scripts honorieren `PARA3_VPS=root@...` zum Override.

## Cache + Service-Worker

Jede Umgebung hat eine eigene SW-Scope (Browser-Default: per Origin), die
Cache-Versionen kollidieren nicht. **Aber:** `CACHE_VER` muss bei jeder
inhaltlichen Asset-Änderung bumpen, sonst sehen Benutzer keine Updates.
Pattern: `'para3-v<n>'`, Increment bei jedem deploy mit Asset-Diff.

Empfehlung für Mixed-Cache während Tests: `?nocache=<random>` im URL anfügen.
Service-Worker-Reset via `navigator.serviceWorker.getRegistrations()` →
`unregister()`, dann `caches.delete()` (siehe MCP-Walk-Through-Pattern).

## Test-Routing

| Test | Ziel-URL (Default) |
|---|---|
| Engine T-Tests (offline_test) | lokal kompiliert auf VPS, kein HTTP |
| C-API WA-Tests | lokal kompiliert auf VPS |
| Lab-Measurements | lokal kompiliert auf VPS |
| Scope-Source / Transport | lokal kompiliert auf VPS |
| WASM-Parity | lokal kompiliert auf VPS |
| Playwright / MCP-Playwright | **staging.para3.levcon.at während Dev**, `para3.levcon.at` als post-promote Smoke-Test |

Lab-Validation läuft IMMER zuerst auf staging. Erst wenn dort grün, promote.

## Promote-Gate (vor jedem `promote to prod`)

Pflicht-Checkliste, bevor `promote-to-prod.sh` läuft:

1. `staging` und `main` sind in derselben Commit-History (kein Rebase, keine
   parallel-Drifts auf `main`).
2. Auf staging-VPS: Engine T-Tests `OVERALL: PASS`.
3. Auf staging-VPS: C-API WA-Tests `OVERALL: PASS`.
4. Auf staging-VPS: Scope-Source, Transport, WASM-Parity alle PASS.
5. Auf staging-VPS: `/tmp/measure` → MANIFEST hat **0 Failures**.
6. MCP-Playwright Pflicht-Stories US-COLD/ONE/PERSIST/IDEM/ORDER + die
   feature-spezifischen Stories grün gegen `https://staging.para3.levcon.at`.
7. Mensch sagt explizit `promote to prod`.

## Initial-Setup (zur Reproduktion)

Wenn Staging-Infrastruktur neu aufgebaut werden muss, in dieser Reihenfolge:

```bash
# (1) DNS: A-Record staging.para3.levcon.at → VPS-IP (z. B. 87.106.25.91)
#     AAAA-Record entweder leer oder VPS-IPv6.

# (2) lokal staging-Branch
git checkout -b staging
git push -u origin staging

# (3) VPS clone
ssh root@VPS "
  cd /opt/synth
  git clone -b staging git@github.com:LEVCON-AT/Para3.git para3-engine-staging
  cd para3-engine-staging/wasm-bridge
  pm2 start serve.mjs --name para3-staging-serve -- . 8090
  pm2 save
  # build artefakte aus prod ziehen (para3.wasm + icons), bis staging
  # sie selbst baut.
  cp /opt/synth/para3-engine/wasm-bridge/para3.wasm .
  cp /opt/synth/para3-engine/wasm-bridge/icon-*.png .
"

# (4) Nginx-Site
ssh root@VPS "
  cat > /etc/nginx/sites-available/staging.para3.levcon.at << 'NGX'
server {
    server_name staging.para3.levcon.at;
    location = /sw.js {
        proxy_pass http://127.0.0.1:8090/sw.js;
        add_header Cache-Control 'no-cache, no-store, must-revalidate' always;
    }
    location / {
        proxy_pass http://127.0.0.1:8090;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }
    listen 80;
}
NGX
  ln -sf /etc/nginx/sites-available/staging.para3.levcon.at /etc/nginx/sites-enabled/
  nginx -t && systemctl reload nginx
  certbot --nginx -d staging.para3.levcon.at --non-interactive --agree-tos --redirect -m admin@levcon.at
"

# (5) Smoke
curl --resolve staging.para3.levcon.at:443:VPS-IP -s -o /dev/null -w '%{http_code}\n' \
  https://staging.para3.levcon.at/para3-responsive.html
# erwartet: 200
```

## Sicherheitsband

- **Niemals `git push origin main --force`.** Main bleibt linear durch
  `--ff-only` Merges. Wenn ein Bug nach prod gelangt: revert-commit auf
  staging → validate → promote.
- **Keine Geheimnisse / Tokens in `staging` oder `main`.** VPS-IP gehört in
  CLAUDE.local.md (gitignored).
- **`promote-to-prod.sh` löscht keine docs/measurements-Files auf prod.** Die
  Lab-Validation-Curves bleiben unter staging und werden bei promote
  explizit ausgeschlossen.
