# Modulation renders (MOD-A)

Renders of the modulation scripts in `scripts/mod/` (docs/briefs/MOD.md, MOD-A), kept **separate from the reference set**: `reference/SHA256SUMS` still covers exactly the 25 files of the original set (22 renders and 3 inputs), and `scripts/make_reference.sh` without `--set mod` never looks at `scripts/mod/` or `reference/mod/`. As in `reference/`, only `SHA256SUMS` and this README are committed; the WAVs are git-ignored.

`SHA256SUMS` here covers 9 renders and the 3 generated inputs (12 files, 90,624,528 bytes), made on the same machine, compiler and flags as `reference/README.md` lists.

MOD-C (2026-11-21) added `mod_ramps_unipolar` (a ramp shape and a unipolar slot). The other 11 hashes did not change: the four new shapes and the polarity leave a bipolar slot of the old shapes exactly as it was, and the envelope's move to the input before the gain does not show here, because `audrey-render` feeds the input at unity gain (the envelope's input is the input itself, as before).

```
scripts/make_reference.sh --set mod            # render into reference/mod/, wavcheck, write reference/mod/SHA256SUMS
scripts/make_reference.sh --set mod --check    # render and compare with reference/mod/SHA256SUMS
```

`--out` and `--build` work as for the reference set. `audrey-render --chunk <frames>` renders with another chunk size; every script here gives the same file at 1, 256 and 1000 frames per call.

## Script commands

A modulation line sets one field of one slot (`src/control/ScriptControlSource.h`):

```
<time> mod<1-4> shape  <sine|triangle|square|rampup|rampdown|expup|expdown|samplehold|envelope>
<time> mod<1-4> target <none|knob-name|InputGain|DelaySwitch>
<time> mod<1-4> rate   <0..1>
<time> mod<1-4> hz     <0.01..2000>
<time> mod<1-4> depth  <-1..1>
<time> mod<1-4> polarity <bipolar|unipolar>     (MOD-C; the envelope is always unipolar)
```

## Thresholds

The wavcheck defaults apply except where a script says otherwise in its header:

- `mod_samplehold_reverb`: `@click 1500`, `@jump 0.5`. Every sample-and-hold step is a jump of the reverb dry/wet mix, a click by nature (the brief accepts naive steps). Largest click ratio 862, largest jump 0.24.
- `mod_four_slots_clamp`: `@click 200`. The square and sample-and-hold steps move the string's delay length in one jump. Largest click ratio 107.8.

The near-event threshold (30, in the 0.1 s after each script line) is unchanged everywhere. The audio-rate render `mod_lowpass_200hz` passes the default thresholds (largest click ratio 11.5).

## The renders

| Render | Seconds | Peak L / R | Jumps max | Click elsewhere / near events |
|---|---|---|---|---|
| mod_envelope_lowpass | 12 | 0.0792 / 0.0731 | 0.0067 | 4.1 / 0.0 |
| mod_feedback_triangle | 40 | 0.1617 / 0.1593 | 0.0912 | 4.2 / 0.3 |
| mod_four_slots_clamp | 20 | 0.2629 / 0.2872 | 0.1620 | 107.8 / 7.0 |
| mod_frequency_drift | 24 | 0.1231 / 0.1211 | 0.0623 | 12.0 / 8.5 |
| mod_lowpass_200hz | 20 | 0.1826 / 0.2153 | 0.0445 | 11.5 / 8.4 |
| mod_ramps_unipolar | 20 | 0.1500 / 0.1499 | 0.0064 | 7.7 / 7.2 |
| mod_samplehold_reverb | 20 | 0.6281 / 0.6366 | 0.2373 | 862.1 / 0.2 |
| mod_switch_square | 26 | 0.2381 / 0.2258 | 0.0162 | 8.1 / 0.3 |
| mod_volume_to_zero | 24 | 0.3412 / 0.3256 | 0.0130 | 6.6 / 0.3 |

No render has a non-finite sample, and every DC offset is below 0.0001.

**Overshoot.** Stepped modulation (square, sample-and-hold, a target change) can push the output briefly past full scale. The original's output stage (the daisysp Limiter) lets a sudden step through for a few samples; its bound is SoftLimit(14) = 1.7434 (`docs/PARAMETERS.md` section 6). The device's 16-bit conversion and the recorder clip such samples; the recorder saturates rather than wraps. No render in this set exceeds 1.0.

## Loop-filter cutoffs: the slew limit

Unlimited audio-rate modulation of `FeedbackLPFCutoff` or `FeedbackHPFCutoff` made the vendored loop filters unstable. They are transposed direct form II biquads whose coefficients are recomputed every control block; once their state reached inf, the output turned to NaN for good. The cases that failed are kept in `tests/mod/mod_filter_test.cpp`, for example a sine on the highpass at 200 Hz, depth 0.3, knob 0.25.

Since then the modulated cutoff moves by at most 0.035 octave per control block (`docs/SIGNAL_FLOW.md` section 6, `docs/PARAMETERS.md` section 6 for the scans). Fast, deep cutoff modulation is therefore reduced in depth as the rate rises: about 0.53 octave peak at 200 Hz and 0.105 at 1 kHz. Slow modulation is unaffected. `mod_lowpass_200hz` (depth 0.15 = 1.1 octaves requested) is reduced by it.
