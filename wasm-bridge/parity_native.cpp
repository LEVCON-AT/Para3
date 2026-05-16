// =============================================================================
//  PARA-3 :: native driver for WASM parity (Phase A3)
//
//  Renders an exact, deterministic sequence through the native para3_capi and
//  writes the float samples to stdout (raw little-endian, host-order f32).
//  wasm_parity.mjs runs the SAME sequence through para3.wasm in Node and
//  compares sample-by-sample.
//
//  build: g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I.. para3_capi.cpp
//         parity_native.cpp -o parity_native
//  run  : ./parity_native > parity_native.f32
// =============================================================================
#include "para3_capi.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "parity_seq.h"          // shared scenario (sample rate, block, ops)

int main() {
    Para3* p = para3_create(PARITY_SR, PARITY_BLOCK);
    if (!p) { std::fprintf(stderr, "parity_native: create failed\n"); return 2; }

    // identical scenario, identical order
    for (int i = 0; i < PARITY_OPS_N; ++i) parity_apply(p, &PARITY_OPS[i]);

    std::vector<float> buf(PARITY_BLOCK);
    int frames_left = PARITY_FRAMES;
    while (frames_left > 0) {
        int n = frames_left < PARITY_BLOCK ? frames_left : PARITY_BLOCK;
        para3_render(p, buf.data(), n);
        std::fwrite(buf.data(), sizeof(float), (size_t)n, stdout);
        frames_left -= n;
    }
    para3_destroy(p);
    return 0;
}
