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
