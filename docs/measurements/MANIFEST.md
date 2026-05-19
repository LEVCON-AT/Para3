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
| M3.1 | VCA | ATTACK monotonically increases ramp time | t_attack_at_0.6 | monoton ↑ | 397.6 | ms | PASS | [svg](../docs/measurements/M3.1-attack.svg) |
| M3.2 | VCA | Envelope decays to sustain level | sustain_ratio | 0.25 .. 0.40 | 0.325 |  | PASS | [svg](../docs/measurements/M3.2-decay-sustain.svg) |
| M3.3 | VCA | Higher DecRel → longer release | t_release_at_0.6 | > 2 × t_release_at_0.2 | 300 | ms | PASS | [svg](../docs/measurements/M3.3-release.svg) |
| M3.4 | VCA | EG_INT bipolar opens/closes filter | high_band_diff_EG=1_vs_0 | > 6 | 23.3 | dB | PASS | [svg](../docs/measurements/M3.4-egint.svg) |
| M3.5 | VCA | Click-free note transitions | max_sample_diff_at_transitions | < 0.2 | 0.1806 | amp | PASS | [svg](../docs/measurements/M3.5-click-free.svg) |
| M4.1 | LFO | All 4 LFO shapes produce frequency modulation | min_sigma | > 1 | 46.16 | Hz | PASS | [svg](../docs/measurements/M4.1-lfo-shapes.svg) |
| M4.2 | LFO | LFO_RATE monotonically increases LFO frequency | f_lfo_at_0.85 | Kendall-τ ≥ 0.5 | 8.14 | Hz | PASS | [svg](../docs/measurements/M4.2-lfo-rate.svg) |
| M4.3 | LFO | LFO_PITCH_DEPTH widens pitch modulation | pp_at_0.9 | > 50 | 136.8 | Hz | PASS | [svg](../docs/measurements/M4.3-lfo-pitch-depth.svg) |
| M4.4 | LFO | LFO_CUT_DEPTH modulates filter cutoff | pp_at_1.0 | > 5 | 40.1 | dB | PASS | [svg](../docs/measurements/M4.4-lfo-cutoff-depth.svg) |
| M5.1 | Delay | DELAY_TIME monotonically lengthens echo offset | onset_at_0.75 | monoton ↑ | 747 | ms | PASS | [svg](../docs/measurements/M5.1-delay-time.svg) |
| M5.2 | Delay | FEEDBACK sustains late-tail energy (echo train) | tail_rise_fb_0_to_0.9 | > 20 | 61.2 | dB | PASS | [svg](../docs/measurements/M5.2-delay-feedback.svg) |
| M5.3 | Delay | DELAY_MIX scales echo amplitude monotonically | dyn_range_mix_0_to_1 | > 30 | 87.3 | dB | PASS | [svg](../docs/measurements/M5.3-delay-mix.svg) |
| M6.1 | VoiceMode | POLY plays 3 distinct fundamentals (C-major triad) | worst_relative_err | < 2 | 0.22 | % | PASS | [svg](../docs/measurements/M6.1-poly-c-major.svg) |
| M6.2 | VoiceMode | UNISON concentrates energy at f₀ | energy_ratio_f0_band | > 0.5 of <1 kHz | 0.779 |  | PASS | [svg](../docs/measurements/M6.2-unison-c4.svg) |
| M6.3 | VoiceMode | OCTAVE adds 1-octave companion to the played note | has_octave_peak | yes (60 + ±12) | yes |  | PASS | [svg](../docs/measurements/M6.3-octave.svg) |
| M6.4 | VoiceMode | FIFTH adds +7-semitone companion (perfect fifth) | has_fifth_peak | yes (60 + 67) | yes |  | PASS | [svg](../docs/measurements/M6.4-fifth.svg) |
| M6.5 | VoiceMode | UNIRING adds ring-mod side-bands not in pure saw | new_peaks_vs_saw | ≥ 5 | 24 |  | PASS | [svg](../docs/measurements/M6.5-uniring.svg) |
| M6.6 | VoiceMode | POLYRING produces difference frequency between two notes | has_diff_peak_~130Hz | yes | yes |  | PASS | [svg](../docs/measurements/M6.6-polyring.svg) |
| M6.7 | VoiceMode | Mode switch transient bounded (Volca-typisch) | max_sample_diff_at_switch | < 0.30 (audible click would be > 0.5) | 0.2345 | amp | PASS | [svg](../docs/measurements/M6.7-mode-switch-clickfree.svg) |
| M7.1 | Sequencer | Step timing matches BPM | worst_relative_err | < 2 | 1.32 | % | PASS | [svg](../docs/measurements/M7.1-step-timing.svg) |
| M7.2 | Sequencer | Tempo-Div scales step duration ×div | ratio_div_4_to_1 | ≈ 4.0 (±10 %) | 3.96 |  | PASS | [svg](../docs/measurements/M7.2-tempo-div.svg) |
| M7.3 | Sequencer | SWING delays odd step proportionally | onset_rise_swing_0_to_0.45 | ≈ 5400 (0.45 · 12000) | 5400 | samples | PASS | [svg](../docs/measurements/M7.3-swing.svg) |
| M7.4 | Sequencer | Step velocity linearly scales peak amplitude | linear_r | > 0.97 | 1.000 |  | PASS | [svg](../docs/measurements/M7.4-step-vel.svg) |
| M7.5 | Sequencer | Step gateLen<1 cuts late-window RMS | ratio_gl=0.2_to_1.0 | < 0.3 | 0.001 |  | PASS | [svg](../docs/measurements/M7.5-step-gate.svg) |
| M7.6 | Sequencer | setActiveStep(false) silences that step's block | skip_to_normal_mid_ratio | < 0.7 | 0.000 |  | PASS | [svg](../docs/measurements/M7.6-active-step-skip.svg) |
| M7.7 | Sequencer | Motion-smooth reduces block-to-block param swing | swing_smooth_off_vs_on | off > on | 94.3 / 67.9 | dB | PASS |  |
| M8.1 | FLUX | FLUX replay reproduces sample-accurate onsets | onset_err_loop1 | < 100 | 79 | samples | PASS | [svg](../docs/measurements/M8.1-flux-replay.svg) |
| M8.2 | FLUX | FLUX param event darkens second half of loop | early_to_late_drop | > 8 | 106.0 | dB | PASS | [svg](../docs/measurements/M8.2-flux-param.svg) |
| M8.3 | FLUX | Quantize snaps off-grid event onto loop/16 | quant_vs_fine_diff | ≥ 500 | 984 | samples | PASS |  |
| M8.4 | FLUX | FLUX loop length respected, two onsets in two loops | onset_err_max | < 200 | 79 | samples | PASS |  |
| M9.1 | ARP | ARP UP plays chord ascending cyclically | first_6_notes | 60,64,67,60,64,67 | 60,64,67,60,64,67 | MIDI | PASS |  |
| M9.2 | ARP | ARP DN plays chord descending cyclically | first_6_notes | 67,64,60,67,64,60 | 67,64,60,67,64,60 | MIDI | PASS |  |
| M9.3 | ARP | ARP UP+DN contains both ascending and descending steps | has_both_directions | yes/yes | up:y/dn:y |  | PASS |  |
| M9.4 | ARP | ARP AS-PLAYED honours input order | first_3_notes | 67,60,64 (input order) | 67,60,64 | MIDI | PASS |  |
| M9.5 | ARP | ARP RANDOM is reproducible with same seed | identical_sequence_runs | identical | identical |  | PASS |  |
| M9.6 | ARP | ARP rate index 0 → 5 increases transition count | ratio_fast_to_slow | ≥ 4 | 17.0 |  | PASS |  |
| M9.7 | ARP | ARP octaves widens MIDI range | range_3oct_vs_1oct | ≥ 20 | 24 / 0 | semitones | PASS |  |
| M9.8 | ARP | ARP gate < 1 reduces total RMS | ratio_gate_0.3_to_0.9 | < 0.85 | 0.681 |  | PASS |  |
| M9.9 | ARP | HOLD latches arp pool after noteOff | ratio_no_hold_to_hold | < 0.1 (no-hold goes silent) | 0.000 |  | PASS |  |

Total: 51 — Failures: 0
