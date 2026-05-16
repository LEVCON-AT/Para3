# PARA·3 — Integration (W-Sprint 3)

So bringst du die verifizierte Engine in dein HTML-Projekt zum Klingen.

## 1. WASM bauen (einmalig, auf deiner Maschine mit emsdk)

```
cd wasm-bridge
./build_wasm.sh            # erzeugt para3.wasm, druckt die Import-Liste
```

Gleiche die gedruckte Import-Liste mit dem Stub-Set in `para3-worklet.js`
(`makeImports`) ab. Wegen `-sSTANDALONE_WASM` ist sie klein (WASI + wenige
`env`-Symbole) und bereits abgedeckt; ein zusaetzlich gelistetes Symbol
einfach als `() => 0` ergaenzen.

## 2. Servieren (COOP/COEP Pflicht fuer SharedArrayBuffer)

```
node serve.mjs .  8080
```

Setzt `Cross-Origin-Opener-Policy: same-origin` +
`Cross-Origin-Embedder-Policy: require-corp` und `.wasm` als
`application/wasm` (hier real per curl verifiziert). Ohne diese Header gibt
es kein `SharedArrayBuffer` → kein Ring. Jeder andere Server muss dieselben
Header senden.

Dann `http://localhost:8080/` (die `index.html`-Harness) oder deine eigene
Seite.

## 3. An die bestehende UI binden

`index.html` ist nur der minimale, lauffaehige Einstieg. Die fertige,
gestaltete Oberflaeche ist `para3-responsive.html`. Anbindung:

```js
import { Para3Audio, PARAM, MODE } from './para3-audio.js';

const audio = new Para3Audio();
startButton.onclick = async () => {           // user gesture nötig
  await audio.start({ wasmUrl: './para3.wasm' });
  await audio.resume();
};

const c = () => audio.controls;               // einziger Steuerpfad
```

UI-Element → Aufruf (alles geht durch den einen Trichter → Ring → Engine):

| UI | Aufruf |
|---|---|
| Taste an/aus | `c().noteOn(midi)` / `c().noteOff(midi)` |
| Cutoff/Reso/Drive-Regler | `c().setParam(PARAM.CUTOFF, 0..1)` … |
| Voice-Mode-Wahl | `c().setMode(MODE.UNIRING)` … |
| LFO→Cutoff-Tiefe | `c().setParam(PARAM.LFO_CUT_DEPTH, 0..1)` |
| Delay-Mix | `c().setParam(PARAM.DELAY_MIX, 0..1)` |
| Tempo / Swing | `c().seqTempo(bpm)` / `c().seqSwing(0..1)` |
| Step setzen | `c().seqStep(idx,note,gate,motionOn,motionCut)` |
| Pattern uebernehmen | `c().seqCommit()` (atomar, lock-frei) |
| Play/Stop | `c().seqStart()` / `c().seqStop()` |
| Motion-Aufnahme | `c().seqArmRecord(true/false)` |
| MIDI-CC (z. B. #74) | `c().midiCC(74, 0..1)` |

Alle Regler liefern **normalisiert 0..1**; die CALIB-Taper sitzt in der
Engine (`ParaEngine::taper`). Damit ist „gleiche Einstellung = gleicher
Klang" garantiert, egal ob UI, MIDI oder Automation die Quelle ist.

## 4. Was hier gemessen wurde (ohne Browser, ehrlich)

- `node ring_test.mjs` → lock-freier Ring: FIFO, kein Verlust/Duplikat,
  bit-exakter Double-Roundtrip, Full/Empty, schwerer Wrap, echter
  Cross-Thread-Worker. OVERALL PASS.
- `node audio_test.mjs` → komplette UI→Ring→Worklet-Dispatch-Kette: 76-Op-
  Session bit-exakt, Backpressure ohne Blockieren, 21.750 Nachrichten
  FIFO-exakt mit korrekter Drop-Buchhaltung. OVERALL PASS.
- `serve.mjs` → COOP/COEP/`application/wasm` per curl real bestaetigt.
- C-API nativ (`capi_test`) inkl. 128-Quanten-Bitidentitaet; Engine 13/13.

Nicht hier ausfuehrbar (kein emsdk/Browser in der Build-Sandbox): der
finale `emcc`-Lauf und das Abspielen im Browser — ein dokumentierter Befehl
auf deiner Maschine. Alle Logik davor ist real gemessen, nichts gefakt.
