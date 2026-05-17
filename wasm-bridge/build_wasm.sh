#!/usr/bin/env bash
# =============================================================================
#  PARA-3 :: WASM build  (run where Emscripten is available)
#
#  Produces a STANDALONE wasm whose imports are minimal/known, so it can be
#  instantiated directly inside an AudioWorkletGlobalScope (no Emscripten JS
#  loader in the worklet — the worklet provides a tiny imports object).
#
#  Requires: emsdk activated (emcc on PATH). Verified-equivalent C-API logic
#  is already proven natively (capi_test) and the control ring concurrently
#  (ring_test) — this step only crosses the compile boundary.
# =============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ENGINE_DIR="${1:-$HERE/..}"      # where Para3Engine.hpp lives (default: parent)
OUT="$HERE/para3.wasm"

emcc \
  "$HERE/para3_capi.cpp" \
  -I"$ENGINE_DIR" -I"$HERE" \
  -std=c++17 -O3 -flto \
  -fno-exceptions -fno-rtti \
  -msimd128 \
  -sSTANDALONE_WASM=1 \
  -sEXPORTED_FUNCTIONS=_para3_create,_para3_destroy,_para3_reset,_para3_set_param,_para3_set_mode,_para3_set_lfo_shape,_para3_note_on,_para3_note_off,_para3_seq_set_tempo,_para3_seq_set_swing,_para3_seq_start,_para3_seq_stop,_para3_seq_arm_record,_para3_seq_set_step,_para3_seq_set_length,_para3_seq_commit,_para3_seq_current_step,_para3_midi_cc,_para3_set_lfo_sync,_para3_set_octave,_para3_seq_motion_set,_para3_seq_motion_lane_commit,_para3_seq_motion_smooth,_para3_seq_motion_rec,_para3_seq_motion_val,_para3_seq_motion_rejects,_para3_seq_step_trigger,_para3_seq_tempo_div,_para3_seq_active_step,_para3_seq_metronome,_para3_seq_flux_mode,_para3_seq_flux_loop_len,_para3_seq_flux_rec,_para3_seq_flux_note,_para3_seq_flux_commit,_para3_seq_flux_dropped,_para3_render,_malloc,_free \
  -sALLOW_MEMORY_GROWTH=0 \
  -sINITIAL_MEMORY=33554432 \
  -sTOTAL_STACK=1048576 \
  -sERROR_ON_UNDEFINED_SYMBOLS=1 \
  -Wl,--no-entry \
  -o "$OUT"

echo "built: $OUT"
echo
echo "=== wasm imports (reconcile these with para3-worklet.js IMPORTS) ==="
# emsdk ships wasm-objdump under upstream/bin or use wasm2wat fallback.
if command -v wasm-objdump >/dev/null 2>&1; then
  wasm-objdump -x "$OUT" | sed -n '/Import\[/,/Export\[/p'
else
  echo "(install wabt for 'wasm-objdump -x'; STANDALONE_WASM keeps imports"
  echo " to a small WASI set already stubbed in para3-worklet.js)"
fi
echo
echo "Notes:"
echo " * No -ffast-math (IEEE precision required by the band-limiting)."
echo " * Fixed heap (no growth) => deterministic real-time behaviour."
echo " * On WASM there is no FTZ/DAZ; the engine is denormal-safe by"
echo "   construction (tanh-bounded loops), so this is correct, not a gap."
