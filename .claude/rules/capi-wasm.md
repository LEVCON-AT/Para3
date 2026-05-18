---
paths:
  - "wasm-bridge/para3_capi.h"
  - "wasm-bridge/para3_capi.cpp"
  - "wasm-bridge/capi_test.cpp"
  - "wasm-bridge/build_wasm.sh"
---

# Regel: C-API & WASM-Bridge

Beim Erweitern der C-API gilt zwingend:
- Param-IDs ans Ende von `PARA3_P_*` (bestehende Werte NIE umnummerieren);
  Mapping in `mapParam` ergänzen.
- Jede neue Funktion: Deklaration in `para3_capi.h`, Implementierung in
  `para3_capi.cpp` (Null-Pointer-geschützt, ruft nur Engine/Controller).
- **Jede** neue exportierte Funktion MUSS in `build_wasm.sh`
  `EXPORTED_FUNCTIONS` (Präfix `_`) ergänzt werden — sonst fehlt sie im
  WASM. Bestehende Signaturen unverändert lassen.
- `capi_test.cpp`: neue IDs/Funktionen im Sweep ausüben (finite, beschränkt,
  Observability-Zähler wie `…_rejects`/`…_dropped` prüfen). Off-Zustand muss
  dieselbe Ausgabe liefern wie ohne den Aufruf.
- `build_wasm.sh`/`wasm_parity.mjs` werden auf dem VPS ausgeführt
  (`ssh root@87.106.25.91`, Host `para3.levcon.at`); dort `emcc` vorhanden.
  WASM↔nativ-Δ muss grün sein — nicht überspringen.

Danach volle Prüfroutine (Root-`CLAUDE.md`), 0 Warnungen, WA1–WA6 PASS.
