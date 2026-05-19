# PARA-3 Lab-Validation Manifest

| ID | Section | What | Metric | Expected | Measured | Unit | Pass | Curve |
|---|---|---|---|---:|---:|---|---|---|
| M0.1 | SelfTest | Pure 1 kHz sine FFT round-trip | peak_freq | 1000.0 | 1000.01 | Hz | PASS | [svg](../docs/measurements/M0.1-selftest-sine-1k.svg) |
| M0.2 | SelfTest | Engine wired into measure_main | rms | >0.01 | 0.1790 |  | PASS | [svg](../docs/measurements/M0.2-engine-smoke.svg) |
| M1.1 | Oscillator | Saw waveform at MIDI 60 | fundamental_freq | 261.63 | 261.61 | Hz | PASS | [svg](../docs/measurements/M1.1-saw-c4-spectrum.svg) |
| M1.2 | Oscillator | Band-limit / aliasing at C7 | worst_band_21k_24k | < -50 | -110.1 | dBFS | PASS | [svg](../docs/measurements/M1.2-aliasing-c7-spectrum.svg) |
| M1.3 | Oscillator | DETUNE spreads UNISON energy around f₀ | sigma_at_detune_0.9 | ≥ 0.5 (vs detune=0) | 9.17 | Hz | PASS | [svg](../docs/measurements/M1.3-detune-unison.svg) |
| M1.4 | Oscillator | setOctave(-2..+2) pitch accuracy | worst_relative_err | < 0.5 | 0.070 | % | PASS | [svg](../docs/measurements/M1.4-octave-shift.svg) |
| M1.5 | Oscillator | Portamento glide MIDI 60→72 reaches target within τ window | end_freq_err | < 1.0 | 0.07 | % | PASS | [svg](../docs/measurements/M1.5-portamento.svg) |
| M2.1 | VCF | Cutoff attenuates high band (low-pass action) | high_band_diff_1.0_vs_0.2 | > 30 | 143.3 | dB | PASS | [svg](../docs/measurements/M2.1-vcf-cutoff.svg) |
| M2.2 | VCF | Resonance lifts cutoff-band peak (Ladder ZDF) | peak_rise_0_to_0.9 | > 6 | 31.0 | dB | PASS | [svg](../docs/measurements/M2.2-vcf-resonance.svg) |
| M2.3 | VCF | Stability at RES=1.0 (tanh-bounded ladder, Volca-treu) | max_abs_at_res=1.0 | < 1.5 | 0.803 | |sample| | PASS | [svg](../docs/measurements/M2.3-vcf-stability.svg) |
| M2.4 | VCF | Filter slope past cutoff (8 kHz → 16 kHz) | slope_8k_to_16k | -30 ± 5 | -25.2 | dB/oct | PASS | [svg](../docs/measurements/M2.4-vcf-slope.svg) |
| M2.5 | VCF | DRIVE soft-clips into ladder (level + spectrum shift) | rms_rise_drive_0_to_1 | > 3 | 14.68 | dB | PASS | [svg](../docs/measurements/M2.5-drive.svg) |

Total: 12 — Failures: 0
