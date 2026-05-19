// =============================================================================
//  PARA-3 :: UI regression test  (U1 + U2 + U3 sprints — UI-only changes)
//
//  Three objective gates this test enforces:
//
//   (a) UNTOUCHED-PROOF.  The "do not change" file set has md5 identical to the
//       pre-U1 baseline (md5_baseline below). Any drift fails the test. This
//       is the hard guarantee that we did not move any engine/bridge byte
//       while reshaping the UI.
//
//   (b) HTML STATIC ASSERTIONS.  Specific patterns in para3-responsive.html
//       that the sprints REQUIRE must be present (renderKeyboard fn, breakpoint
//       detection, generalised midiOfKey math, etc.). Catches accidental
//       reverts in a copy/paste.
//
//   (c) PURE-FUNCTION UNIT TEST.  We re-implement the same MIDI math here
//       (the in-HTML logic is duplicated on purpose — duplication serves as a
//       spec). For every valid breakpoint (N = 1..5 octaves) the helper must
//       return a contiguous, monotonically rising MIDI sequence starting at
//       BASE_C and ending at BASE_C + 12*N. Catches off-by-one and the
//       "double-octave-shift" bug that triggered the E6 cleanup.
//
//  Run:  node ui_test.mjs
// =============================================================================
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO = join(__dirname, '..');

// --- baseline of untouched files (md5; current state after E1–E7 + EXT-ARP +
// EXT-FLUX + RELEASE-FIX + LAB-1..10 + staging-prod-merge) ----------------
// PURPOSE: catch UI-only sprints that ACCIDENTALLY touch an engine/bridge
// file. Legitimate engine evolution is the new standard — when an engine/
// bridge file is intentionally changed (with its own T<n>/WA<n> test +
// commit gate), the corresponding hash here is rebaselined as part of that
// commit. This is NOT a frozen "never change" gate, it is a "no-stealth-
// change" gate.
const md5_baseline = {
  // EXT-BASS B5 rebaseline: Sub-Oszillator (Oscillator-Instanz mit wave=Pulse,
  // Square @ PW=0.5) als ParaEngine-Member. subPitch_-RampParam folgt der
  // newest Note minus 12 ST (-1 Oktave). Sub vor VCF gemischt → durchläuft
  // Filter+Envelope wie reguläre Stimmen. Param BassSubLevel=22 mit Default 0
  // ⇒ subSamp * 0 = 0 exakt ⇒ bit-identisch (T70). Tests: T70-T74 + WA13.
  // Keine neuen C-API-Funktionen (Level läuft über para3_set_param).
  'Para3Engine.hpp':                            'ae49a0187047c4b774ea0e148d32ccdd',
  'offline_test.cpp':                           '498b7e6cf287b39f2ffcb54d8e1a7ce5',
  'wasm-bridge/para3_capi.h':                   '7baab254d08164e9f87a0fa860483a2c',
  'wasm-bridge/para3_capi.cpp':                 'c5c29b61a1bca8025e4feb56b402c802',
  'wasm-bridge/capi_test.cpp':                  '5b3bcf9957a72750b3e2ee91ad89c8cf',
  'wasm-bridge/scope_source_test.cpp':          '646828487b3a002a565b9ec87a7abe55',
  'wasm-bridge/parity_native.cpp':              'ffdb9666262ae54961d58dc7ec19d4b0',
  'wasm-bridge/parity_seq.h':                   '9043aba77b26cecb2aa1324ba805e07a',
  'wasm-bridge/build_wasm.sh':                  'cbc877405b3a4a1d53579cac2671e399',
  'wasm-bridge/wasm_parity.mjs':                '9a396e68954d830a3aac0bde40b887cb',
  'wasm-bridge/para3-audio.js':                 '720ac2319bc49df338795641d58b19b8',
  'wasm-bridge/para3-ring.js':                  'e11936ecb1857130ffe9ee03d4e2ddbc',
  'wasm-bridge/para3-port.js':                  'b32b05892fc3afe81e8ab6ad750aa898',
  'wasm-bridge/para3-worklet.js':               'd3016e62d43588394ed06124a512aeb0',
  'wasm-bridge/audio_test.mjs':                 '037acf634432569aa0edbe4a8458a595',
  'wasm-bridge/ring_test.mjs':                  '76ad067b33f87f310810257a1c65ff24',
  'wasm-bridge/port_test.mjs':                  '75e950283a90d7a3cefe9037fb73c408',
};

let fails = 0;

// ----- (a) UNTOUCHED-PROOF -----------------------------------------------
{
  const drift = [];
  for (const [rel, want] of Object.entries(md5_baseline)) {
    const buf = readFileSync(join(REPO, rel));
    // Normalize CRLF → LF so the test is reproducible on Windows checkouts
    // (autocrlf=true) AND on the Linux VPS — the canonical repo format is LF.
    const norm = Buffer.from(buf.toString('binary').replace(/\r\n/g, '\n'), 'binary');
    const got = createHash('md5').update(norm).digest('hex');
    if (got !== want) drift.push(`${rel}: want ${want}, got ${got}`);
  }
  const pass = drift.length === 0;
  console.log(`\nU-A: engine/bridge untouched-proof`);
  console.log(`   files checked    : ${Object.keys(md5_baseline).length}`);
  console.log(`   md5 drift        : ${drift.length}`);
  if (drift.length) for (const d of drift) console.log(`      ${d}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL (UI sprints touched an engine file!)'}`);
  if (!pass) fails++;
}

// ----- (b) HTML STATIC ASSERTIONS ----------------------------------------
const html = readFileSync(join(REPO, 'wasm-bridge/para3-responsive.html'), 'utf8');
{
  // U1 requirements
  const checks = [
    { re: /function\s+renderKeyboard\s*\(/,             label: 'renderKeyboard fn defined' },
    { re: /function\s+detectOctaves\s*\(/,              label: 'detectOctaves fn defined' },
    { re: /W_PER_OCT\s*=\s*\[\s*0\s*,\s*2\s*,\s*4\s*,\s*5\s*,\s*7\s*,\s*9\s*,\s*11\s*\]/, label: 'W_PER_OCT array (7 entries, semitones-within-octave)' },
    { re: /B_PER_OCT\s*=\s*\{[^}]*'C#'\s*:\s*1[^}]*'A#'\s*:\s*10[^}]*\}/, label: 'B_PER_OCT semitone map' },
    { re: /B_AFTER_W\s*=\s*\[\s*0\s*,\s*1\s*,\s*3\s*,\s*4\s*,\s*5\s*\]/,  label: 'B_AFTER_W black-key in-octave indices' },
    { re: /kbd\.dataset\.octaves\s*=/,                  label: 'kbd[data-octaves] tag set on render' },
    { re: /(window|new ResizeObserver|orientationchange)/, label: 'resize/orientation re-render hook' },
    { re: /idx\s*%\s*7/,                                label: 'midiOfKey uses idx%7 (multi-octave white math)' },
    { re: /el\.dataset\.oct/,                           label: 'midiOfKey reads el.dataset.oct for blacks' },
    // Audit: magic-numbers replaced by named constants.
    { re: /KBD_BREAKPOINTS\s*=\s*Object\.freeze/,       label: 'KBD_BREAKPOINTS named constant (no inline thresholds)' },
    { re: /BLACK_WHITE_VISUAL_RATIO\s*=\s*9\s*\/\s*12\.5/, label: 'BLACK_WHITE_VISUAL_RATIO named (replaces 0.72 literal)' },
  ];
  // Negative check: the bare 0.72 literal must NOT appear in the white/black
  // width formula anymore. We allow 0.72 elsewhere in case some animation
  // ever uses it, but it must not be the keyboard geometry source.
  const bareRatio = /Bw\s*=\s*W\s*\*\s*0\.72/.test(html);
  if (bareRatio) checks.push({ re: /__NEVER__/, label: 'Bw = W * 0.72 (bare literal) — must be replaced' });
  const failed = checks.filter(c => !c.re.test(html));
  const pass = failed.length === 0;
  console.log(`\nU-B: HTML static assertions (U1 markers)`);
  console.log(`   checks           : ${checks.length}, failed: ${failed.length}`);
  if (failed.length) for (const f of failed) console.log(`      MISSING: ${f.label}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b2) U2 LAYOUT BREAKPOINTS in HTML ---------------------------------
{
  // The CSS must declare each industry-standard breakpoint with its max-width
  // and grid column count. We regex the @media blocks (collapsing whitespace
  // so the test isn't brittle to indentation).
  const css = html.replace(/\s+/g, ' ');
  const checks = [
    // Mobile landscape (legacy behaviour, capped at <1024)
    { re: /@media\s*\(orientation:landscape\)\s*and\s*\(max-width:1023px\)\s*\{[^}]*\.app\s*\{[^}]*max-width:\s*100%/, label: 'mobile-landscape (<1024) keeps max-width:100%' },
    // Tablet portrait
    { re: /@media\s*\(min-width:\s*720px\)\s*and\s*\(orientation:\s*portrait\)\s*\{[^}]*\.app\s*\{[^}]*max-width:\s*760px/, label: 'tablet-portrait ≥720 → max-width:760px' },
    { re: /@media\s*\(min-width:\s*720px\)[\s\S]*?\.scroll\s*\{[^}]*grid-template-columns:\s*repeat\(2\s*,\s*1fr\)/, label: 'tablet-portrait → 2-col grid' },
    // Tablet landscape + desktop
    { re: /@media\s*\(min-width:\s*1024px\)[\s\S]*?\.app\s*\{[^}]*max-width:\s*1200px/, label: 'tablet-landscape/desktop ≥1024 → max-width:1200px' },
    // EXT-ARP UI sprint: 3-col base became 6-col so the arp+seq row can be
    // split 50/50 ("arp arp arp seq seq seq"). Visually identical: each
    // section still spans 2/6 = 33%.
    { re: /@media\s*\(min-width:\s*1024px\)[\s\S]*?\.scroll\s*\{[^}]*grid-template-columns:\s*repeat\(6\s*,\s*1fr\)/, label: 'tablet-landscape/desktop → 6-col grid (was 3-col, doubled for arp+seq 50/50)' },
    // Desktop wide
    { re: /@media\s*\(min-width:\s*1600px\)[\s\S]*?\.app\s*\{[^}]*max-width:\s*1560px/, label: 'desktop-wide ≥1600 → max-width:1560px (U6 expansion)' },
    { re: /@media\s*\(min-width:\s*1600px\)[\s\S]*?grid-template-columns:\s*repeat\(4\s*,\s*1fr\)/, label: 'desktop-wide → 4-col grid' },
    // Sequencer (.sec.full) must occupy the full-width "seq" area in every
    // multi-col media. U4 replaced grid-column:1/-1 with grid-area:seq.
    { re: /\.sec\.full\s*\{\s*grid-area:\s*seq\s*\}/, label: '.sec.full → grid-area:seq' },
  ];
  const failed = checks.filter(c => !c.re.test(css));
  const pass = failed.length === 0;
  console.log(`\nU-B2: U2 layout breakpoints in CSS`);
  console.log(`   checks           : ${checks.length}, failed: ${failed.length}`);
  if (failed.length) for (const f of failed) console.log(`      MISSING: ${f.label}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b3) U3 OCTAVE-CLARITY MARKERS in HTML -----------------------------
{
  const checks = [
    { re: /function\s+midiToName\s*\(/,                        label: 'midiToName fn defined (display helper)' },
    { re: /function\s+updateOctIndicator\s*\(/,                label: 'updateOctIndicator fn defined' },
    { re: /id\s*=\s*["']octind["']/,                            label: '#octind element exists in HTML' },
    { re: /\.octind\s*\{[^}]*position:\s*absolute/,            label: '.octind CSS positioned absolutely' },
    { re: /\.octind\s*\{[^}]*display:\s*none/,                 label: '.octind hidden by default (oct=0)' },
    { re: /midiToName\s*\(\s*note\s*\+\s*12\s*\*[^)]*K\.oct\.get/, label: 'keyDown displays SOUNDING midi (note + 12*K.oct)' },
    { re: /id\s*===\s*['"]oct['"][\s\S]*?updateOctIndicator/,  label: 'emitKnob hooks oct -> updateOctIndicator' },
  ];
  const failed = checks.filter(c => !c.re.test(html));
  const pass = failed.length === 0;
  console.log(`\nU-B3: U3 octave-clarity markers`);
  console.log(`   checks           : ${checks.length}, failed: ${failed.length}`);
  if (failed.length) for (const f of failed) console.log(`      MISSING: ${f.label}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (c1b) detectOctaves VIEWPORT BAND TEST -----------------------------
//
// Re-implementation (specification duplicate) of the in-HTML decision. Each
// case names the device the band represents so a future reviewer can see
// what the threshold means. Aligned with the CSS U2 breakpoints (720/1024/
// 1280 picks 3-col grid range; 1600 is wide).
{
  const KBD_BREAKPOINTS = [
    { minWidth: 1680, octaves: 5 },
    { minWidth: 1280, octaves: 4 },
    { minWidth: 1024, octaves: 3 },
    { minWidth:  720, octaves: 2 },
    { minWidth:    0, octaves: 1 },
  ];
  const detectOctaves = (w) => {
    for (const b of KBD_BREAKPOINTS) if (w >= b.minWidth) return b.octaves;
    return 1;
  };
  const cases = [
    { w:  375, want: 1, dev: 'iPhone SE portrait' },
    { w:  430, want: 1, dev: 'iPhone 14 Pro Max portrait' },
    { w:  667, want: 1, dev: 'iPhone SE landscape (under tablet band)' },
    { w:  744, want: 2, dev: 'iPad mini portrait (tablet portrait)' },
    { w:  820, want: 2, dev: 'iPad portrait' },
    { w:  932, want: 2, dev: 'iPhone 14 Pro Max landscape' },
    { w: 1024, want: 3, dev: 'iPad landscape (tablet landscape)' },
    { w: 1180, want: 3, dev: 'iPad Air landscape' },
    { w: 1280, want: 4, dev: 'small desktop / 13"' },
    { w: 1366, want: 4, dev: 'common laptop' },
    { w: 1680, want: 5, dev: '27" 1080p / wide desktop' },
    { w: 1920, want: 5, dev: 'desktop 1080p' },
    { w: 2560, want: 5, dev: '4K monitor' },
    // exact boundary samples
    { w:  719, want: 1, dev: 'just below tablet break' },
    { w:  720, want: 2, dev: 'tablet break (matches CSS @media min-width:720)' },
    { w: 1023, want: 2, dev: 'just below tablet-landscape break' },
    { w: 1279, want: 3, dev: 'just below desktop break' },
    { w: 1599, want: 4, dev: 'just below wide-desktop break' },
  ];
  const bad = cases.filter(c => detectOctaves(c.w) !== c.want);
  const pass = bad.length === 0;
  console.log(`\nU-C1b: detectOctaves viewport bands`);
  console.log(`   cases            : ${cases.length}, failed: ${bad.length}`);
  if (bad.length) for (const b of bad)
    console.log(`      FAIL: w=${b.w} (${b.dev}) got=${detectOctaves(b.w)} want=${b.want}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (c2) U3 PURE midiToName UNIT TEST ----------------------------------
{
  const NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  const midiToName = (m) => (m < 0 || m > 127)
    ? 'OOR'
    : NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
  const cases = [
    // Standard MIDI conventions
    { m: 0,   want: 'C-1' },        // lowest valid MIDI
    { m: 12,  want: 'C0'  },
    { m: 21,  want: 'A0'  },        // piano lowest
    { m: 60,  want: 'C4'  },        // middle C
    { m: 69,  want: 'A4'  },        // A440
    { m: 72,  want: 'C5'  },
    { m: 108, want: 'C8'  },        // piano highest
    { m: 127, want: 'G9'  },        // MIDI top
    // OOR
    { m: 128, want: 'OOR' },        // engine clamps
    { m: -1,  want: 'OOR' },
    // Black keys
    { m: 61,  want: 'C#4' },
    { m: 70,  want: 'A#4' },
  ];
  const bad = cases.filter(c => midiToName(c.m) !== c.want);
  const pass = bad.length === 0;
  console.log(`\nU-C2: midiToName (display) unit test`);
  console.log(`   cases            : ${cases.length}, failed: ${bad.length}`);
  if (bad.length) for (const b of bad)
    console.log(`      FAIL: m=${b.m} got=${midiToName(b.m)} want=${b.want}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b4) U4 LAYOUT SYMMETRY + SEQ COMPACTION ---------------------------
{
  // (i) Each section gets a grid-area name. We require all 7.
  const sectionAreas = ['vco', 'osc', 'vcf', 'eg', 'lfo', 'dly', 'seq'];
  const inlineSec = sectionAreas.filter(a =>
    !new RegExp(`style="grid-area:${a}"`).test(html));
  // (ii) grid-template-areas declarations per breakpoint. We collapse
  //      whitespace so the grep tolerates indentation.
  const css = html.replace(/\s+/g, ' ');
  // EXT-ARP UI sprint: arp row added between eg/lfo and seq at every
  // breakpoint. 3-col desktop became 6-col so arp+seq can split 50/50.
  const templates = [
    { label: '2-col (mobile-landscape / tablet-portrait) areas: 4-short paired + eg/lfo + arp + seq',
      re: /grid-template-areas:\s*"vco\s+osc"\s*"vcf\s+dly"\s*"eg\s+lfo"\s*"arp\s+arp"\s*"seq\s+seq"/ },
    { label: '6-col (tablet-landscape / desktop) areas: doubled sections + arp/seq 50/50',
      re: /grid-template-areas:\s*"vco\s+vco\s+osc\s+osc\s+vcf\s+vcf"\s*"eg\s+eg\s+lfo\s+lfo\s+dly\s+dly"\s*"arp\s+arp\s+arp\s+seq\s+seq\s+seq"/ },
    { label: '4-col (wide desktop) areas: 4-short row + eg×2 lfo×2 + arp×2 seq×2',
      re: /grid-template-areas:\s*"vco\s+osc\s+vcf\s+dly"\s*"eg\s+eg\s+lfo\s+lfo"\s*"arp\s+arp\s+seq\s+seq"/ },
  ];
  const tmplFailed = templates.filter(t => !t.re.test(css));
  // (iii) Step sequencer compaction at ≥720: 16×1 grid, lane shorter.
  const seqComp = [
    { label: 'seq steps: 16-col grid at ≥720',
      re: /@media\s*\(min-width:\s*720px\)[\s\S]*?\.sec-seq\s+\.steps\s*\{[^}]*grid-template-columns:\s*repeat\(16/ },
    { label: 'seq lane: compact 42px height at ≥720',
      re: /@media\s*\(min-width:\s*720px\)[\s\S]*?\.sec-seq\s+\.lane\s*\{[^}]*height:\s*42px/ },
  ];
  const compFailed = seqComp.filter(c => !c.re.test(css));
  // (iv) Mobile portrait (no media query) must NOT carry grid-template-areas
  //      — phone portrait stays single-column flex/block (user constraint).
  // We count actual CSS declarations "grid-template-areas:" (with colon, so
  // mentions in comments don't count). We expect exactly 4 declarations:
  // mobile-landscape, tablet-portrait, desktop-3col, wide-desktop-4col.
  const areaDecls = (html.match(/grid-template-areas\s*:/g) || []).length;
  const mobilePortraitClean = areaDecls === 4;

  const failed = inlineSec.length + tmplFailed.length + compFailed.length + (mobilePortraitClean ? 0 : 1);
  console.log(`\nU-B4: U4 layout-symmetry + seq compaction markers`);
  console.log(`   grid-area inline tags : ${sectionAreas.length - inlineSec.length}/${sectionAreas.length}` +
              (inlineSec.length ? `   missing: ${inlineSec.join(',')}` : ''));
  console.log(`   grid-template-areas   : ${templates.length - tmplFailed.length}/${templates.length}`);
  if (tmplFailed.length) for (const t of tmplFailed) console.log(`      MISSING: ${t.label}`);
  console.log(`   seq compaction        : ${seqComp.length - compFailed.length}/${seqComp.length}`);
  if (compFailed.length) for (const c of compFailed) console.log(`      MISSING: ${c.label}`);
  console.log(`   mobile-portrait clean : ${mobilePortraitClean ? 'yes (areas only in @media)' : 'NO (leaked to default block)'}`);
  console.log(`   -> ${failed === 0 ? 'PASS' : 'FAIL'}`);
  if (failed) fails++;
}

// ----- (b5) U5 MOTION-CAPABILITY AUDIT ------------------------------------
{
  // Mirror of the in-HTML motionParamId / isMotionCapable. Each UI control's
  // id is checked against the engine's "motion-fähig" rule: all PARA3_P_*
  // except RESONANCE; oct/tmp are not normalised PARAMs so they can never be
  // motion targets. The cases enumerate every interactable control in the
  // HTML and pin its expected motion-capability.
  const PARAM_RES = 1, PARAM_VOL = 15;
  const KNOB_PARAM = {
    cut:0, pk:PARAM_RES, lrt:5, lpi:6, lci:3, dt:7, df:8,
    egi:12, det:13, por:14, vol:PARAM_VOL,
  };
  const FADER_PARAM = { atk:9, dec:10, sus:11 };
  const NON_PARAM = ['oct', 'tmp'];               // engine-shaped controls, NOT normalised PARAMs

  const motionParamId = (id) => KNOB_PARAM[id] ?? FADER_PARAM[id];
  const isMotionCapable = (id) => {
    const pid = motionParamId(id);
    return pid !== undefined && pid !== PARAM_RES;
  };

  const cases = [
    // knobs that SHOULD be motion targets (U5 fixes vol; engine spec confirms)
    ...['cut','lrt','lpi','lci','dt','df','egi','det','por','vol'].map(id =>
       ({ id, want: true, why: 'normalised PARAM, not RESONANCE' })),
    // faders — all three are motion-fähig per engine (ATTACK/DECREL/SUSTAIN)
    ...['atk','dec','sus'].map(id =>
       ({ id, want: true, why: 'fader writes PARAM through funnel' })),
    // hard rejects — three only, with their reasons
    { id: 'pk',  want: false, why: 'engine rejects RESONANCE (Volca semantics)' },
    { id: 'oct', want: false, why: 'setOctave is int, not a normalised PARAM' },
    { id: 'tmp', want: false, why: 'seqTempo is BPM, not a normalised PARAM' },
  ];
  const bad = cases.filter(c => isMotionCapable(c.id) !== c.want);
  // Plus: the HTML must contain the U5 markers (named refactors, fader hook).
  const htmlChecks = [
    { re: /vol:\s*PARAM\.VOLUME/,                                   label: 'KNOB_PARAM.vol = PARAM.VOLUME (was special-cased in emitKnob)' },
    { re: /function\s+motionParamId\s*\(/,                          label: 'motionParamId helper (knobs + faders)' },
    { re: /function\s+motionEntry\s*\(/,                            label: 'motionEntry helper' },
    { re: /function\s+motionTargets\s*\(/,                          label: 'motionTargets iterator over K and F' },
    { re: /if\s*\(\s*motionArm\s*\)\s*\{\s*setTarget\s*\(\s*id\s*\)/, label: 'fader slot pointerdown hooks setTarget when armed' },
    { re: /F\[id\]\s*=\s*\{[\s\S]*?name\s*,\s*el\s*:\s*f\s*\}/,     label: 'F entries carry name+el for setTarget lookup' },
    { re: /\.knob\.tgt\s*,\s*\.fader\.tgt\s*\{/,                    label: '.tgt CSS applies to both knobs AND faders' },
  ];
  const htmlBad = htmlChecks.filter(c => !c.re.test(html));
  const pass = bad.length === 0 && htmlBad.length === 0;
  console.log(`\nU-B5: U5 motion-capability audit`);
  console.log(`   isMotionCapable: ${cases.length - bad.length}/${cases.length} match`);
  if (bad.length) for (const b of bad)
    console.log(`      FAIL: id=${b.id} want=${b.want} got=${isMotionCapable(b.id)}  (${b.why})`);
  console.log(`   HTML markers   : ${htmlChecks.length - htmlBad.length}/${htmlChecks.length}`);
  if (htmlBad.length) for (const c of htmlBad) console.log(`      MISSING: ${c.label}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b6) U6 DESKTOP LIFT-OFF SHADOW + EDGE -----------------------------
{
  // The app must carry a box-shadow + 1px outline at every tablet+ breakpoint
  // and the colours must be theme-driven (so bone theme isn't a cream block on
  // pure black). Mobile portrait must STILL not carry box-shadow.
  const css = html.replace(/\s+/g, ' ');
  const checks = [
    { re: /--app-shadow\s*:\s*0\s*,\s*0\s*,\s*0/,           label: 'dark theme: --app-shadow defined (rgb 0,0,0)' },
    { re: /\[data-theme="bone"\][\s\S]*?--app-shadow\s*:\s*60\s*,\s*40\s*,\s*20/, label: 'bone theme: warm --app-shadow override' },
    { re: /\[data-theme="bone"\][\s\S]*?--app-edge\s*:\s*rgba\(255\s*,\s*238\s*,\s*210/, label: 'bone theme: parchment --app-edge' },
    { re: /@media\s*\(min-width:\s*720px\)\s*and\s*\(orientation:\s*portrait\)[\s\S]*?box-shadow:[^;]*rgba\(var\(--app-shadow\)/, label: 'tablet-portrait box-shadow uses var(--app-shadow)' },
    { re: /@media\s*\(min-width:\s*1024px\)[\s\S]*?box-shadow:[^;]*rgba\(var\(--app-shadow\)/, label: 'desktop (≥1024) box-shadow uses var(--app-shadow)' },
    { re: /@media\s*\(min-width:\s*1600px\)[\s\S]*?box-shadow:[^;]*rgba\(var\(--app-shadow\)/, label: 'wide desktop (≥1600) box-shadow uses var(--app-shadow)' },
    { re: /@media\s*\(min-width:\s*1600px\)[\s\S]*?max-width:\s*1560px/, label: 'wide desktop expanded 1480→1560 max-width' },
  ];
  // Negative: mobile portrait .app block must NOT carry box-shadow (mobile is
  // the base; shadow only kicks in at tablet+). Match the FIRST .app{...}
  // rule and stop at its first '}' so we don't consume sibling selectors.
  const m = html.match(/\.app\s*\{[^}]*\}/);
  const mobileHasShadow = m && /box-shadow:/.test(m[0]);
  const failed = checks.filter(c => !c.re.test(css));
  const pass = failed.length === 0 && !mobileHasShadow;
  console.log(`\nU-B6: U6 desktop lift-off (shadow + edge + bone-theme glow)`);
  console.log(`   checks               : ${checks.length - failed.length}/${checks.length}`);
  if (failed.length) for (const f of failed) console.log(`      MISSING: ${f.label}`);
  console.log(`   mobile-portrait clean: ${mobileHasShadow ? 'NO (box-shadow leaked into default .app)' : 'yes (no shadow on mobile)'}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b7) B3 VISUAL CURSOR HONOURS tempoDiv -----------------------------
// User-reported regression: visual cursor walks at 1/1 speed even when div is
// 1/2 or 1/4, so the lit step doesn't match the audible note. The cursor
// formula in the frame() function must scale step-seconds by tdivVal.
{
  const checks = [
    { re: /sm\s*=\s*\(\s*60\s*\/\s*\(\s*bpm\s*\*\s*4\s*\)\s*\)\s*\*\s*tdivVal/,
      label: 'visual cursor sm formula multiplied by tdivVal' },
    { re: /let\s+tdivVal\s*=\s*1/,
      label: 'tdivVal initialised to 1 (no-op default)' },
    { re: /tdivVal\s*=\s*d\s*;/,
      label: 'tdivVal updated on div button click' },
  ];
  const failed = checks.filter(c => !c.re.test(html));
  const pass = failed.length === 0;
  console.log(`\nU-B7: B3 visual cursor honours tempoDiv (1/2, 1/4 alignment)`);
  console.log(`   checks   : ${checks.length - failed.length}/${checks.length}`);
  if (failed.length) for (const f of failed) console.log(`      MISSING: ${f.label}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b8) B4 VISUAL CURSOR ANCHORED TO seqStart MOMENT -----------------
// Without this anchor the cursor's starting phase depends on how long the
// user idled on splash. User-reported symptom: cursor and audible note out
// of phase until app reload. Engine pairs (T29/T30) prove step 0 fires at
// t=t0 — the cursor must too.
// EXT-ARP-FIX3 upgrade: FIX2 dropped the fractional acc on tdiv changes,
// which let the engine fire ahead of the cursor (user saw "klang eine Note
// zu früh"). FIX3 tracks cursorStep + cursorAccSec to mirror the engine's
// {step, acc} state exactly.
{
  const checks = [
    { re: /let\s+cursorStep\s*=\s*0\s*;[\s\S]{0,300}?let\s+cursorAccSec\s*=\s*0\s*;[\s\S]{0,300}?let\s+cursorRegimeTs\s*=\s*0\s*;/,
      label: 'cursorStep + cursorAccSec + cursorRegimeTs declared' },
    { re: /if\s*\(\s*playing\s*\)\s*\{[\s\S]*?cursorStep\s*=\s*0[\s\S]*?cursorAccSec\s*=\s*0[\s\S]*?cursorRegimeTs\s*=\s*audio\.ctx/,
      label: 'Play resets step/acc/ts (Volca restart-from-0)' },
    { re: /cursorAccSec\s*\+\s*\(\s*tNow\s*-\s*cursorRegimeTs\s*\)/,
      label: 'tdiv handler computes total = acc + (now - regimeTs)' },
    { re: /stepsFired\s*=\s*Math\.floor\(\s*total\s*\/\s*smOld\s*\)/,
      label: 'tdiv handler counts steps fired in old regime' },
    { re: /cursorAccSec\s*=\s*total\s*-\s*stepsFired\s*\*\s*smOld/,
      label: 'tdiv handler folds fractional carry into new cursorAccSec' },
    { re: /total\s*=\s*cursorAccSec\s*\+\s*Math\.max\(\s*0\s*,\s*t\s*-\s*cursorRegimeTs\s*\)/,
      label: 'frame() formula = cursorAccSec + elapsed-in-regime' },
    { re: /stepsAdvanced\s*=\s*Math\.floor\(\s*total\s*\/\s*sm\s*\)/,
      label: 'frame() step count = floor(total/sm)' },
  ];
  const failed = checks.filter(c => !c.re.test(html));
  const pass = failed.length === 0;
  console.log(`\nU-B8: EXT-ARP-FIX3 cursor mirrors engine acc + step`);
  console.log(`   checks   : ${checks.length - failed.length}/${checks.length}`);
  if (failed.length) for (const f of failed) console.log(`      MISSING: ${f.label}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b9) EXT-ARP UI panel markers (UI2: knob layout) ------------------
// UI2 industry-standard rework: only ON/HOLD remain as toggles; MODE/RATE/
// OCT/GATE are knobs (data-int="1" for the discrete ones). No tooltips
// (title attributes) — the user explicitly didn't want them.
{
  const checks = [
    { re: /<div\s+class="sec\s+sec-arp"\s+style="grid-area:arp">/,
      label: 'ARP section present with grid-area:arp' },
    { re: /<h2>ARP\s*·\s*<b>EXT<\/b><\/h2>/,
      label: 'ARP · EXT header in seqhead style' },
    { re: /<button\s+class="tbtn"\s+id="arpOn">ON<\/button>/,
      label: 'ON toggle (no tooltip)' },
    { re: /<button\s+class="tbtn"\s+id="arpHold">HOLD<\/button>/,
      label: 'HOLD toggle (no tooltip)' },
    // EXT-ARP-MOTION: MODE is now a 5-button .wave picker (same icon style
    // as LFO WAVE) — motion-fähig via PARAM.ARP_MODE (pid 16). RATE / OCT
    // / GATE remain knobs (continuous-or-discrete as before).
    { re: /class="wave"\s+id="arpMode"\s+data-k="amd">/,
      label: 'ARP MODE .wave picker (id=arpMode, data-k=amd)' },
    { re: /data-mode="0"[\s\S]{0,200}<polyline\s+points="1,14 5,14[\s\S]*?23,2"/,
      label: 'MODE button 0 (UP) — ascending staircase polyline' },
    { re: /data-mode="1"[\s\S]{0,200}<polyline\s+points="1,2[\s\S]*?23,14"/,
      label: 'MODE button 1 (DN) — descending staircase polyline' },
    { re: /data-mode="2"[\s\S]{0,200}<polyline\s+points="1,14 5,10 10,6 14,6 19,10 23,14"/,
      label: 'MODE button 2 (UP-DN) — pyramid polyline' },
    { re: /data-mode="3"/,
      label: 'MODE button 3 (AS-PLAYED)' },
    { re: /data-mode="4"[\s\S]{0,200}<circle/,
      label: 'MODE button 4 (RND) — circle dots' },
    { re: /data-name="RATE"\s+data-k="arpRate"\s+data-int="1"/,
      label: 'RATE knob (int-stepped, 0..5)' },
    { re: /data-name="OCT"\s+data-k="arpOct"\s+data-int="1"/,
      label: 'OCT knob (int-stepped, 1..4)' },
    { re: /data-name="GATE"[^>]*data-k="arpGate"/,
      label: 'GATE knob (continuous, 0..100)' },
    // negatives: no leftover MODE knob, no leftover segment radios.
    { re: /data-k="arpMode"\s+data-int/, neg: true,
      label: 'NEG: no MODE knob anymore (replaced by .wave picker)' },
    { re: /id="arpRate"[^>]*data-r=/, neg: true,
      label: 'NEG: no segment radio for RATE' },
    { re: /id="arpOct"[^>]*data-o=/,  neg: true,
      label: 'NEG: no segment radio for OCT' },
    // grid-template-areas must list "arp" before "seq" on each breakpoint.
    { re: /grid-template-areas:[\s\S]*?"arp arp"[\s\S]*?"seq seq"/,
      label: '2-col layouts: arp row above seq row' },
    { re: /grid-template-areas:[\s\S]*?"arp arp arp seq seq seq"/,
      label: '6-col desktop: arp+seq 50/50 (3+3 of 6)' },
    { re: /grid-template-areas:[\s\S]*?"arp arp seq seq"/,
      label: '4-col wide desktop: arp+seq 50/50 (2+2 of 4)' },
  ];
  let pass = true;
  console.log(`\nU-B9: EXT-ARP UI panel structure + layout (UI2 knob layout)`);
  for (const c of checks) {
    const hit = c.re.test(html);
    const ok = c.neg ? !hit : hit;
    if (!ok) { console.log(`      ${c.neg ? 'LEAKED' : 'MISSING'}: ${c.label}`); pass = false; }
  }
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (b10) EXT-ARP emit paths bypass the taper trichter -----------------
// Spec §6: arp params are Controller settings, NOT setParamNorm. UI2: all
// four arp knobs (mode/rate/oct/gate) route through the emitKnob else-
// branches (NOT KNOB_PARAM). The two toggles call their controls directly.
{
  const checks = [
    { re: /emit\(\(c\)\s*=>\s*c\.arpEnable\(/,   label: 'arpEnable emit path' },
    { re: /emit\(\(c\)\s*=>\s*c\.arpHold\(/,     label: 'arpHold emit path' },
    // EXT-ARP-MOTION: MODE picker emits arpMode + seqMotionVal (motion live).
    { re: /id\s*===\s*['"]amd['"]\s*\)[\s\S]*?c\.arpMode\([\s\S]*?c\.seqMotionVal\(PARAM\.ARP_MODE/,
      label: 'amd emitKnob branch: arpMode + seqMotionVal(ARP_MODE)' },
    { re: /id\s*===\s*['"]arpRate['"]\s*\)\s*\{[\s\S]*?emit\(\(c\)\s*=>\s*c\.arpRate\(/,
      label: 'arpRate via emitKnob else-branch' },
    { re: /id\s*===\s*['"]arpOct['"]\s*\)\s*\{[\s\S]*?emit\(\(c\)\s*=>\s*c\.arpOctaves\(/,
      label: 'arpOct via emitKnob else-branch' },
    { re: /id\s*===\s*['"]arpGate['"]\s*\)\s*\{[\s\S]*?emit\(\(c\)\s*=>\s*c\.arpGate\(/,
      label: 'arpGate via emitKnob else-branch' },
    // EXT-ARP-MOTION: 'amd' MUST be in KNOB_PARAM (motion-target plumbing)
    // but RATE / OCT / GATE / MODE-knob must still be absent (no taper).
    { re: /KNOB_PARAM\s*=\s*\{[\s\S]*?\bamd\s*:\s*PARAM\.ARP_MODE/,
      label: 'KNOB_PARAM contains amd:PARAM.ARP_MODE for motion targeting' },
    { re: /KNOB_PARAM\s*=\s*\{[^}]*\barpGate\s*:/, neg: true,
      label: 'NEG: arpGate must NOT be in KNOB_PARAM' },
    { re: /KNOB_PARAM\s*=\s*\{[^}]*\barpRate\s*:/, neg: true,
      label: 'NEG: arpRate must NOT be in KNOB_PARAM' },
    { re: /KNOB_PARAM\s*=\s*\{[^}]*\barpOct\s*:/,  neg: true,
      label: 'NEG: arpOct must NOT be in KNOB_PARAM' },
  ];
  let pass = true;
  console.log(`\nU-B10: EXT-ARP emit paths bypass setParamNorm trichter`);
  for (const c of checks) {
    const hit = c.re.test(html);
    const ok = c.neg ? !hit : hit;
    if (!ok) { console.log(`      ${c.neg ? 'LEAKED' : 'MISSING'}: ${c.label}`); pass = false; }
  }
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

// ----- (c) PURE-FUNCTION UNIT TEST ---------------------------------------
{
  // Spec-duplicate of the in-HTML math. If you change one, change the other.
  const W_PER_OCT = [0, 2, 4, 5, 7, 9, 11];
  const B_PER_OCT = { 'C#':1, 'D#':3, 'F#':6, 'G#':8, 'A#':10 };
  const BASE_C = 48;
  const whiteMidi = (idx) => BASE_C + Math.floor(idx / 7) * 12 + W_PER_OCT[idx % 7];
  const blackMidi = (name, oct) => BASE_C + oct * 12 + B_PER_OCT[name];

  const cases = [];
  for (const N of [1, 2, 3, 4, 5]) {
    const totalWhites = 7 * N + 1;
    // first white is always BASE_C (= 48)
    cases.push({ what: `N=${N} first white = 48`, got: whiteMidi(0), want: 48 });
    // last white is BASE_C + 12*N (the closing C of the Nth octave block)
    cases.push({ what: `N=${N} last white = ${48 + 12 * N}`,
                 got: whiteMidi(totalWhites - 1), want: 48 + 12 * N });
    // monotonic: white[i+1] > white[i] for all i
    let monotonic = true;
    for (let i = 1; i < totalWhites; i++)
      if (whiteMidi(i) <= whiteMidi(i - 1)) { monotonic = false; break; }
    cases.push({ what: `N=${N} whites strictly monotonic`, got: monotonic, want: true });
    // octave-cycle: black C# of octave m is BASE_C + 12*m + 1
    for (let m = 0; m < N; m++) {
      cases.push({ what: `N=${N} C#${m} = ${48 + 12 * m + 1}`,
                   got: blackMidi('C#', m), want: 48 + 12 * m + 1 });
    }
  }

  // Regression-with-N=1: first 8 whites + 5 blacks must match the pre-U1
  // values exactly so legacy code/users get bit-identical behaviour.
  const legacyW = [48, 50, 52, 53, 55, 57, 59, 60];   // C3..C4 inclusive
  const legacyB = { 'C#':49, 'D#':51, 'F#':54, 'G#':56, 'A#':58 };
  for (let i = 0; i < legacyW.length; i++)
    cases.push({ what: `legacy N=1 white[${i}] = ${legacyW[i]}`,
                 got: whiteMidi(i), want: legacyW[i] });
  for (const [n, v] of Object.entries(legacyB))
    cases.push({ what: `legacy N=1 black ${n} = ${v}`,
                 got: blackMidi(n, 0), want: v });

  const bad = cases.filter(c => c.got !== c.want);
  const pass = bad.length === 0;
  console.log(`\nU-C: pure-function midi math (re-implementation)`);
  console.log(`   cases            : ${cases.length}, failed: ${bad.length}`);
  if (bad.length) for (const b of bad) console.log(`      FAIL: ${b.what}  got=${b.got}`);
  console.log(`   -> ${pass ? 'PASS' : 'FAIL'}`);
  if (!pass) fails++;
}

console.log('\n==================================================');
console.log(`${fails ? 'OVERALL: FAIL' : 'OVERALL: PASS'}  (${fails} failure${fails === 1 ? '' : 's'})`);
process.exit(fails ? 1 : 0);
