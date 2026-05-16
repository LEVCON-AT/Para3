# CLAUDE_BUILD.md — PARA·3 Ausführungs-Runbook für Claude Code

Ziel des Auftraggebers: **PARA·3 offline auf dem Handy spielbar.**
Arbeitsweise (verbindlich): **messen, nicht behaupten. Keine Fakes, keine
Stubs, keine Helfer-Abkürzungen.** Jeden Schritt mit dem angegebenen
Kriterium verifizieren, bevor der nächste beginnt. Bei Fehlschlag: echte
Ursache fixen und neu messen — niemals das Kriterium aufweichen.

## Repo-Lage

- `Para3Engine.hpp`, `offline_test.cpp` — die verifizierte DSP-Engine
  (13/13 Tests, header-only, framework-agnostisch).
- `wasm-bridge/` — C-API (`para3_capi.*`, `capi_test.cpp`), lock-freier
  Ring (`para3-ring.js`, `ring_test.mjs`, `ring_worker.mjs`),
  `build_wasm.sh`, `para3-worklet.js`, `para3-audio.js`, `serve.mjs`,
  `audio_test.mjs`, `index.html`, `integration.md`.

## Voraussetzungen herstellen

1. Node ≥ 18, g++ ≥ 12, **Emscripten (emsdk)** aktiviert (`emcc` im PATH),
   `wabt` (`wasm-objdump`) optional aber empfohlen.
2. Sanity vor allem anderen — diese müssen unverändert grün sein:
   - `g++ -O2 -std=c++17 -Wall -Wextra -msse2 offline_test.cpp -o /tmp/e && /tmp/e`
     → `OVERALL: PASS (0 failures)` und **0 Compilerwarnungen**.
   - `cd wasm-bridge && g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp capi_test.cpp -o /tmp/c && /tmp/c`
     → `OVERALL: PASS`, 0 Warnungen.  (`-I..` findet `Para3Engine.hpp`.)
   - `node ring_test.mjs` → `OVERALL: PASS`.
   - `node audio_test.mjs` → `OVERALL: PASS`.
   Wenn eines rot ist: stop, Ursache fixen, dieses Dokument erst dann weiter.

---

## PHASE A — WASM bauen & verifizieren

A1. `cd wasm-bridge && ./build_wasm.sh ..`
    → erzeugt `para3.wasm`, druckt die Import-Liste.

A2. **Import-Abgleich (Pflicht, nicht überspringen):** Vergleiche die
    gedruckte Import-Liste mit `makeImports()` in `para3-worklet.js`.
    Jedes dort nicht vorhandene `env`/`wasi_*`-Symbol als minimalen,
    semantisch korrekten Stub ergänzen (`() => 0` nur wenn der Rückgabewert
    nachweislich ignoriert wird; sonst korrekt implementieren). Kein „abort"/
    „proc_exit" stummschalten — diese müssen werfen.

A3. **WASM == nativ (Gleichwertigkeitsmessung).** Schreibe `wasm_parity.mjs`:
    instanziiere `para3.wasm` in Node mit denselben Imports wie der Worklet,
    rendere mit identischer Eingabesequenz wie ein kleiner nativer Treiber
    über `para3_capi` (z. B. 1 s Note 57, fixe Parameter), und vergleiche die
    Ausgaben Sample für Sample.
    Kriterium: max. Abweichung ≤ 1e-6 (Float-Rundung), **kein** NaN/Inf,
    Ausgabe nicht still. Bei größerer Abweichung: echte Ursache (Flags,
    Speicher, Import) finden — nicht tolerieren.

A4. Desktop-Hörtest: `node serve.mjs . 8080`, im Browser
    `http://localhost:8080/` → Start, Tasten, Regler, Sequencer.
    Kriterium: Ton da, kein Knacksen bei Noten/Reglern/Sequencer,
    Konsole fehlerfrei.

Phase A erst abgeschlossen, wenn A3 und A4 erfüllt sind.

---

## PHASE B — Offline-Handy (PWA + SAB-freier Steuerpfad)

Begründung: Der SAB-Ring braucht Cross-Origin-Isolation (COOP/COEP), was mit
einer komplett offline installierten PWA in Konflikt steht. Der Audio-Pfad
(WASM im AudioWorklet) braucht **kein** SAB — nur der Steuerpfad.

B1. **SAB-freier Steuer-Transport (Pflicht für Offline-Mobile).**
    Implementiere einen zweiten Transport mit identischer Protokoll-Semantik:
    Haupt-Thread → `AudioWorkletNode.port.postMessage(msg)` →
    Worklet sammelt Messages und wendet sie zu Beginn von `process()` an,
    **vor** dem Rendern (Sample-Genauigkeit auf Blockgrenze; für Steuerrate
    ausreichend — der DSP-Pfad bleibt unangetastet).
    - `Para3Controls` bekommt eine Transport-Abstraktion: `RingTransport`
      (bestehend, Desktop/COOP) **oder** `PortTransport` (neu, offline-mobil).
      Gleiche Methoden, gleiche OP-Codes, gleiche Doubles.
    - `para3-worklet.js`: zusätzlich `this.port.onmessage` → in eine interne
      Queue; in `process()` zuerst Ring **und** Queue drainen (eine
      gemeinsame `dispatch(op,i0,i1,d)`-Funktion, keine Logikduplikation).
    - `Para3Audio.start({ transport: 'auto' })`: nutzt SAB nur wenn
      `crossOriginIsolated === true`, sonst automatisch `PortTransport`.
    Verifikation `port_test.mjs` (analog `audio_test.mjs`): dieselbe
    76-Op-Session über `PortTransport`, der Worklet-Dispatch dekodiert
    bit-exakt dieselbe Sequenz (Order, IDs, exakte Doubles).
    Kriterium: `OVERALL: PASS`. Kein Logik-Drift zwischen den Transports
    (gemeinsame `dispatch`-Funktion testen).

B2. **PWA-Hülle.** `manifest.webmanifest` (name, icons, `display:standalone`,
    `orientation`), `sw.js` Service-Worker: precache von `index.html` bzw.
    `para3-responsive.html`, `para3-audio.js`, `para3-worklet.js`,
    `para3-ring.js`, `para3.wasm`, Icons. Cache-First für diese Assets,
    Versionsschlüssel für Update.
    Kriterium: DevTools → Application → „installable"; nach erstem Laden
    Netzwerk trennen, App neu starten → lädt vollständig offline.

B3. **Mobile Audio-Unlock & Robustheit.**
    - Start nur per User-Geste; `AudioContext.resume()` nach Geste.
    - `AudioContext` ohne erzwungene `sampleRate` erstellen (Gerät bestimmt
      sie; Engine ist SR-agnostisch — `para3_create` bekommt
      `ctx.sampleRate`).
    - iOS Safari: nach Tab-Wechsel/Sperre `ctx.state` prüfen und bei Bedarf
      erneut `resume()`.
    Kriterium: Auf einem echten Android-Chrome **und** iOS-Safari-Gerät:
    installierte PWA, **Flugmodus an**, App startet, spielt, kein Knacksen
    bei Tasten/Sequencer; nach Sperren/Entsperren wieder Ton.

B4. **UI.** `wasm-bridge/para3-responsive.html` ist die fertige, gestaltete
    Oberfläche (jetzt im Paket). Anbindung vollständig spezifiziert in
    **`CLAUDE_INTEGRATE_UI.md`** (genaue Mapping-Tabelle + ehrliche
    „nicht faken"-Liste C). Der Parameter-Trichter deckt bereits das volle
    Panel ab (regressionsgeprüft: Engine 13/13, capi, ring, audio_test).
    `index.html` ist nur Harness — nicht ausliefern.
    Kriterium: Alle Controls der Mapping-Tabelle steuern hörbar den Klang;
    Liste-C-Controls bleiben nachweislich inert; nichts umgeht den Trichter.

Phase B abgeschlossen, wenn B1–B4 je ihr Kriterium erfüllen, insbesondere
der **echte Offline-Lauf im Flugmodus auf einem Handy** (B3).

---

## PHASE C — OPTIONAL, separat: Sprint-1-Kalibrierung

Nur falls „klanglich exakte Kopie eines bestimmten Volca-Keys" gewünscht ist.
**Nicht** nötig für einen sehr guten, spielbaren Offline-Synth.
Erfordert echte Hardware + Mess-Rig (Außenarbeit, kein reiner Code).
Vorgehen: jeden `// CALIB(sprint1)`-Wert in `Para3Engine.hpp` durch den
gemessenen Wert ersetzen, danach komplette Engine-Suite (13/13) erneut
grün stellen. Niemals Defaults „nach Gehör" als Messung ausgeben.

---

## Abnahme des Gesamtziels

Erfüllt, wenn: installierte PWA auf dem Handy, **offline (Flugmodus)**,
startet und spielt; kein Knacksen bei Noten/Reglern/Sequencer; alle
Sanity-Tests (Engine 13/13, capi, ring, audio, port_test, wasm_parity)
grün; null Compilerwarnungen; keine Engine-Aufrufe am Parameter-Trichter
vorbei.

## Eiserne Regeln

- Eine Quelle der Wahrheit fürs Steuerprotokoll (OP-Codes/Slot-Layout);
  Ring- und Port-Transport teilen die Worklet-`dispatch`-Funktion.
- Kein `-ffast-math`. Fester WASM-Heap (kein Growth). Keine
  Allokation/Locks/Async im `process()`/Render.
- Jede Behauptung über Qualität muss ein ausführbarer Test belegen.
- Bei Unsicherheit lieber einen zusätzlichen Messtest schreiben als raten.
