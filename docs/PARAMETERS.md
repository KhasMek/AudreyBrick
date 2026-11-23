# Parameters

Every control of Audrey II as implemented at upstream commit `e5ab799`. Source: `Source/FeedbackSynthControls.{h,cpp}`, `Source/ParameterRegistry.h`, `Source/SmoothedValue.h`, `Source/DSPUtils.h`, `Source/FeedbackSynthEngine.cpp`. Everything is **[read]** unless marked **[inferred]**.

## 1. How a knob becomes a parameter

Once per audio callback, which is every **4 samples, so 12 kHz** (`FeedbackSynth_main.cpp`):

1. `Controls::Update` reads each ADC channel as a float in 0..1 and **inverts it**: `n = 1.0 - adc`. libDaisy returns the raw 16-bit DMA value divided by full scale, with 32x hardware oversampling and no software filtering.
2. `ParameterRegistry::UpdateNormalized` clamps `n` to 0..1 and maps it to the parameter range with `daisysp::fmap`:
   - `LINEAR`: `min + n * (max - min)`
   - `EXP`: `min + n*n * (max - min)`. This is a **square law**, not an exponential.
   - `LOG`: `min * (max/min)^n`. This is the true exponential (equal ratios per step).
3. The mapped value becomes the target of a `SmoothedValue`, a one-pole lowpass run once per callback: `value += coef * (target - value)`, with `coef = min(1 / (t * 0.1447597 * 12000), 1)`. `t` is the "smooth time" in the table. The function is named `onepole_coef_t60`, so `t` is roughly the time to settle within 60 dB and the time constant is `0.1448 * t`.
4. `Controls::Process` calls every parameter's handler with the smoothed value, every callback. Parameters are therefore stepped every 4 samples (a 12 kHz staircase).

The registry is a `std::unordered_map`, so the order in which handlers run within one callback is unspecified. The setters are independent, so this has no audible effect [inferred].

## 2. Parameter table

Order is the firmware's `Parameter` enum, which is also the ADC channel order. "Panel" is the label on the faceplate.

| # | Enum name | Panel | Range (n=0 to n=1) | Curve | Smooth `t` (time constant) | Registry initial | Engine setter and effect |
|---|---|---|---|---|---|---|---|
| 0 | `Frequency` | frequency | MIDI note 16 to 72, i.e. 20.6 Hz to 523.3 Hz | linear in note number (exponential in Hz) | 0.2 s (29 ms) | note 40 (82.4 Hz) | `SetStringPitch`: `mtof(note)` sets both strings' delay length, `48000 / f` samples (2330 down to 91.7) |
| 1 | `FeedbackGain` | the big knob with rings | -60 dB to +12 dB | linear in dB | 0.05 s (7.2 ms) | -60 dB | `SetFeedbackGain`: `fb_gain_ = 10^(dB/20)`, range 0.001 to 3.98. Scales what is written into the body delay. **Never reaches zero** |
| 2 | `FeedbackBody` | body | 0.001 s to 0.1 s | square (`EXP`) | 1.0 s (145 ms), then a second per-sample smoother, time constant 0.2 s | 0.001 s | `SetFeedbackDelay`: body delay length, 48 to 4800 samples. Right channel reads 4 samples earlier |
| 3 | `FeedbackLPFCutoff` | lowpass | 100 Hz to 18 kHz | exponential (`LOG`) | 0.05 s (7.2 ms) | 18 kHz | `SetFeedbackLPFCutoff`: 12 dB/oct biquad lowpass in the loop, Q 0.9. Coefficients recomputed every callback |
| 4 | `FeedbackHPFCutoff` | highpass | 10 Hz to 4 kHz | exponential (`LOG`) | 0.05 s (7.2 ms) | 250 Hz | `SetFeedbackHPFCutoff`: 12 dB/oct biquad highpass in the loop, Q 0.9 |
| 5 | `ReverbMix` | reverb dry/wet | 0 to 1 | linear | 0.05 s (7.2 ms) | 0 | `SetReverbMix`: crossfade dry to wet, **inside the feedback loop** and before the echo |
| 6 | `ReverbDecay` | reverb decay | 0.2 to 1.0 | pre-shaped, then linear (see below) | 0.05 s (7.2 ms) | 0.2 | `SetReverbFeedback`: `ReverbSc` feedback. 1.0 is an infinite tail |
| 7 | `EchoDelaySend` | delay mix | 0 to 1 | square (`EXP`) | 0.05 s (7.2 ms) | 0 | `SetEchoDelaySendAmount`: gain into the echo delay. It is a **send**, not a dry/wet mix. The echo return level is fixed |
| 8 | `EchoDelayTime` | delay time | 0.05 s to 5.0 s | square (`EXP`), after the switch scaling | 0.1 s (14.5 ms), then a per-sample smoother, time constant 0.5 s | 0.5 s | `SetEchoDelayTime`: both echo delays. The slow second smoother is what makes time changes pitch-warp like tape |
| 9 | `EchoDelayFeedback` | delay repeats | 0 to 1.5 | linear | 0.05 s (7.2 ms) | 0 | `SetEchoDelayFeedback`: above 1.0 the echo grows until its soft clipper holds it |
| 10 | `OutputVolume` | volume | 0 to 1 | square (`EXP`) | 0.05 s (7.2 ms) | 0.5 | `SetOutputLevel`: final gain before the limiter |

### Reverb decay curve

The knob value is shaped before the registry sees it: `n' = ftension(n, -3) = (e^(-3n) - 1) / (e^(-3) - 1)`, then mapped linearly to 0.2..1.0. This rises fast and flattens at the top: n=0.25 gives 0.64, n=0.5 gives 0.85, n=0.75 gives 0.95, n=1 gives 1.0.

### The switch (panel `!!`, Seed pin D14)

- `daisy::Switch`, inverted polarity with pull-up, debounced: sampled at most once per millisecond, reads as pressed after **8 consecutive** pressed samples (about 8 ms).
- When on, the delay-time knob value is **halved before the square-law mapping**: `time = 0.05 + (n * 0.5)^2 * 4.95`. So the time drops to roughly a quarter of its span, not to half: at n=1 it goes from 5.0 s to 1.29 s. The source comment says "doubles or halves delay time instantly for doppler warp".
- The jump is smoothed by the two delay-time smoothers (14.5 ms then 0.5 s), which produces the pitch sweep.
- The switch does **nothing else**. It does not affect the audio input or any routing.

## 3. Values that are fixed in the firmware (no knob)

| Constant | Value | Where |
|---|---|---|
| Noise level into the strings | -90 dBFS peak (`3.16e-5`), same sample to both channels | `Engine::Init` |
| String loop gain | 0.8 | `KarplusString::ProcessInternal` |
| String damping lowpass | one-pole at 8 kHz | `KarplusString::Reset` |
| String DC blocker coefficient | 0.99 | `daisysp::DcBlock` |
| String `SetBrightness(0.98)`, `SetDamping(0.4)` | **stored but never used**; the code that read them is commented out | `KarplusString.cpp` |
| Overdrive drive | 0.4, which works out to pre-gain 5.18 (+14.3 dB) and post-gain 1.0 [calculated from `overdrive.cpp`] | `Engine::Init` |
| Loop filter Q | 0.9 for both | `Engine::Init` |
| Reverb damping lowpass | 12 kHz | `Engine::Init` |
| Stereo offset of the right body delay | 4 samples | `Engine::Process` |
| Echo bandpass | 800 Hz, Q 1.0, 12 dB/oct | `EchoDelay::Init` |
| Echo time lag | 0.5 s time constant | `Engine::Init` |
| Dry/echo sum | `0.5 * (dry + echo)` | `Engine::Process` |
| Output limiter pre-gain | 0.7, plus an internal 0.7 | `FeedbackSynth_main.cpp`, `limiter.cpp` |

## 4. Defaults and start-up behaviour

- **The firmware has no defaults.** `SmoothedValue::get()` snaps to its target on the first call, and `Update` runs before `Process` in the callback, so the first processed block already uses the physical knob positions. The "registry initial" values in the table are never heard.
- Two internal values do glide at power-up regardless of knobs: the body delay starts at 1000 samples and the echo time at 5.0 s, each moving to the knob value with its own time constant (0.2 s and 0.5 s). Both lines are empty at that point, so this is inaudible unless feedback is already up [inferred].
- If the very first ADC read returns 0 before the first DMA conversion completes, every parameter would start at its maximum for one block and then smooth back [inferred, a hardware race we should not reproduce].
- **Proposal for the port's reset-to-default values:** use the registry initial values, since they are the developer's own stated starting point. As normalised knob positions: frequency 0.429, feedback 0, body 0, lowpass 1.0, highpass 0.537, reverb mix 0, reverb decay 0, delay mix 0, delay time 0.302, repeats 0, volume 0.707. This gives silence at start, as the instrument intends.

## 5. Not present in the firmware

No MIDI, no CV, no presets, no input gain, no input enable, no mute, no parameter that depends on another, and no control that changes behaviour at power-up.

## 6. Modulation offsets (port addition, MOD-A)

Not part of the firmware. A modulation slot moves a knob by an offset in normalised knob travel, applied **after** the parameter smoother above (`docs/SIGNAL_FLOW.md` section 6): the engine gets `map(clamp(n_s + offset, 0, 1))`, where `n_s` is the knob position that the smoothed value corresponds to. The curves are inverted as follows (`engineToKnob`, `src/core/ParamTable.cpp`):

| Curve | Knobs | Inverse |
|---|---|---|
| `LINEAR` | Frequency, FeedbackGain, ReverbMix, EchoDelayFeedback | `(v - min) / (max - min)` |
| `EXP` (square law) | FeedbackBody, EchoDelaySend, OutputVolume | `sqrt((v - min) / (max - min))` |
| `EXP` after the switch scaling | EchoDelayTime | `sqrt((v - min) / (max - min))`, divided by 0.5 while the switch (manual or modulated) is on |
| `LOG` | FeedbackLPFCutoff, FeedbackHPFCutoff | `log(v / min) / log(max / min)` |
| `LINEAR` after `ftension(n, -3)` | ReverbDecay | `t = (v - 0.2) / 0.8`, then `log1p(t * expm1(-3)) / -3` |

So an offset on the frequency adds semitones (56 per unit of travel), and on the two cutoffs multiplies the frequency by a fixed ratio per unit (180 for the lowpass, 400 for the highpass). One exception to the clamp: just after the switch turns on, the echo-time smoother still carries a value from before, whose inverse lies above 1 (up to 2). The clamp range then widens to include that position, so a small offset gives a small change rather than a jump.

**Cutoff slew limit.** On the modulated path the two loop-filter cutoffs move by at most 0.035 octave per control block (420 octaves per second) towards the modulated value; see `SIGNAL_FLOW.md` section 6 for why and for the depth this leaves at audio rates (about 0.53 octave peak at 200 Hz, 0.105 at 1 kHz). The original never moves a cutoff faster than its 7.2 ms smoother allows after a full-travel knob jump: about 0.087 octave per block for the lowpass (7.5 octaves) and 0.1 for the highpass (8.6 octaves). Unmodulated, the cutoffs are exactly the original's.

Scans (`tests/mod/mod_filter_scan.cpp`, 7190 cases: sine, triangle, square and sample-and-hold, 20 Hz to 2 kHz, depths 0.3 to 1 and -1, four slots summed, knobs 0 to 1, feedback 0 to 1 with the echo at maximum, the filter knob jumping 0 to 1 every 0.1 s, both filters modulated at once; Mac Release, with the input bursts):

| Limit, octaves per block | Failed cases (non-finite or above 1) |
|---|---|
| none | 592 of 7190 (3 s each) |
| 1.0 / 0.5 / 0.3 / 0.2 / 0.15 | 156 / 43 / 22 / 29 / 8 (3 s each) |
| 0.1 | 16 (12 s each); first failure at 2.9 s |
| 0.087 | 12 (12 s each); the highpass at knob 0 under a 100 Hz sine, after 9 s |
| 0.075 | 0 (30 s each); 0 of the 1363 knob-0 cases at 300 s each |
| 0.06 | 0 (30 s each) |
| **0.035 (chosen)** | 0 (30 s each); 0 of the 1363 knob-0 cases at 300 s each |

The largest limit that never failed is 0.075; 0.035 is under half of it.

**InputGain.** The effective input gain is the stage's own law at the input gain knob plus the offset, evaluated against the stage's smoothed gain block by block (`src/mod/ModControlSource.h`). Below knob 0.25 (-12 dB) the offset is tapered quadratically to 0 at gain 0, so closing an input fades the modulated gain to 0 continuously. When an input opens, the taper rises to full over at most 20 ms. The taper restarts from 0 only where the stage's gain is 0 or no slot has weight on InputGain, not when the offset happens to be 0 (MOD-B review: an Envelope slot after digital silence otherwise let each attack through unmodulated for about 15 ms; now it is ducked within a few milliseconds). Measured against the real input stage (`tests/mod/mod_inputgain_test.cpp`, constant offsets +1, +0.5, -0.2 and -1; scenarios: knob 0.25 to 0.5, knob 0.1 to 1.0, FILE on at gain 0 with the gain raised to 1.0 during the fade-in, close and reopen, FILE off and on, FILE to LIVE to FILE at gain 0.8, and a live device open with the 100 ms restore fade), the modulated gain moves faster than the stage alone: up to 0.0230 per sample (FILE on at gain 0 then 1.0, offset +1; the stage alone 0.0083), and up to 7 times the stage's step where the stage moves slowly at a low gain (close and reopen, offset +1: 0.0145 against 0.0021). These are faster ramps, not clicks (a click would be a step of the order of the gain itself). A positive offset moves the gain along a steeper part of the law, and the taper's rise adds up to 8/960 = 0.0083 per sample at the law's steepest slope. The test's bound, per scenario: at most 3.5 times the stage-alone step plus 0.0083, and never above 0.03. The input meter on the INPUT tab reads the stage's output, before the InputGain modulation.

**Shapes, polarity and the envelope's input (MOD-C, 2026-11-21).** The shapes, in the order the MOD tab steps through them: SINE, TRIANGLE, SQUARE, RAMP UP, RAMP DOWN, EXP UP, EXP DOWN, S&H, ENVELOPE. The four ramps are bipolar -1..+1 like the other LFO shapes, over the same rate range (0.01 Hz to 2 kHz), not band-limited: RAMP UP rises linearly from -1 to +1 over the cycle and jumps back; RAMP DOWN mirrors it; EXP DOWN is a decay, `2 (exp(-k u) - exp(-k)) / (1 - exp(-k)) - 1` with **k = 5** (u the phase 0..1): +1 at the start, half its travel by u = 0.137, -0.848 at u = 0.5, exactly -1 at the end; EXP UP is EXP DOWN mirrored in time (slow start, fast finish, ends at +1). Each slot also has a **polarity**: BIPOLAR (default) or UNIPOLAR, which maps the LFO value to `(v + 1) / 2` (0..1): with a positive depth the target moves only above its knob, with a negative depth only below. The Envelope is always unipolar. A polarity change is blended over the 10 ms one-pole the depth uses (no click beyond a target change's). The **Envelope follows the input before the INPUT gain** (`SIGNAL_FLOW.md` section 6, point 4): it works with the gain at -inf dB, so an input can drive modulation without being heard; with the source OFF it is 0. EXP UP and EXP DOWN are not symmetric round the knob (both spend most of the cycle near -1, i.e. below it when bipolar), and the ramps are coarse at high rates (6 control blocks per cycle at 2 kHz).

**Envelope on INPUT GAIN (MOD-C review).** Since the envelope reads before the gain, it no longer shrinks when the input gain is turned down, so an Envelope slot on INPUT GAIN with a positive depth boosts a turned-down input much more than before: at gain knob 0.25 (-12 dB), an envelope of 0.3 and depth +1 the offset is the full +0.3 of travel (the taper is 1 at knob 0.25), the gain moves from law(0.25) = 0.25 to law(0.55) = 1.21, **about +13.7 dB**; with the envelope after the gain (MOD-B) the follower saw the input times 0.25, 0.075, and the boost was law(0.325)/law(0.25), about +4.6 dB. With the speaker and the built-in microphone that is an acoustic loop driven by its own level: louder input, more envelope, more gain. Use headphones, or a negative depth (ducking). The human chose the pre-gain envelope, so this stays and is documented for players (`packaging/README.txt`) and in the device checklist. A gain of exactly 0 (-inf dB) is still never opened: the InputGain rule is unchanged.

**DelaySwitch from an Envelope slot.** The request is on while the summed DelaySwitch offset is above 0. An Envelope slot with positive depth therefore turns the switch on whenever the follower is above its floor (1e-12): it is a gate with no threshold, on for any input (since MOD-C also at input gain -inf dB, as the follower reads before the gain) and for the follower's release after it. With UNIPOLAR, a positive depth requests the switch wherever the mapped value is above 0: a square still for half of each cycle, the other LFO shapes for all but an instant of it.

**Output overshoot.** Stepped modulation (square, sample-and-hold, the jump at the end of each ramp's cycle, a target change) can drive the output briefly past full scale. The original's output stage is the daisysp Limiter: pre-gain 0.7, a peak follower with a 0.05-per-sample attack, then SoftLimit. It passes a sudden step for a few samples. Its follower bounds what reaches SoftLimit at 0.7 x 20 = 14, so the output can never exceed SoftLimit(14) = 1.7434. The reviewer saw 1.743 for 5 samples. No limiter is added here; that is M4's decision. The device's 16-bit conversion and the recorder clip such samples: `floatToPcm16` saturates to +-32767 and never wraps (`tests/record/record_format_test.cpp`: +1.5, -7 and +-inf).

**Slow targets.** Two engine parameters have a second, per-sample smoother inside the vendored engine that modulation cannot bypass: **FeedbackBody** (time constant 0.2 s) and **EchoDelayTime** (0.5 s, which is what makes delay-time changes pitch-sweep). Modulating them faster than a few hertz is flattened. The delay switch request goes through the same echo-time smoothers. Every other target follows the modulation at the control rate (12 kHz).
