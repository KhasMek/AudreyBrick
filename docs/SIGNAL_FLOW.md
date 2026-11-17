# Signal flow

The DSP chain of Audrey II at upstream commit `e5ab799`, with DaisySP at `7f67768`. Everything is **[read]** unless marked **[inferred]**, **[calculated]** or **[ran]**.

**Sample rate: 48 kHz** (`SaiHandle::Config::SampleRate::SAI_48KHZ`). **Block size: 4 samples.** The engine processes one sample at a time; controls update once per block (12 kHz). Internal format is 32-bit float.

## 1. Chain, in code order

`Engine::Process(float in, float &outL, float &outR)` in `Source/FeedbackSynthEngine.cpp`. Two parallel channels (L, R) with shared parameters.

```
noise (-90 dBFS) ----+
audio in (mono) -----+
                     v
body delay read --> (+) -> [1 string] -> [2 overdrive] -> [3 LPF] -> [4 HPF] -> [5 reverb mix] --> S

S --> x feedback gain --> [6 body delay write]        (closes the loop back to "body delay read")
S --> x send --> [7 echo] --+
S --> (dry) ----------------+--> (+) --> x0.5 --> [8 volume] --> [9 limiter] --> out
```

`S` is the signal after the reverb mix. It goes three ways: back into the loop, into the echo, and dry to the output.

| Step | What | Module | Detail |
|---|---|---|---|
| 0 | Sum into the string | | `inL = bodyDelayL.Read(d) + noise + in`; `inR = bodyDelayR.Read(max(1, d - 4)) + noise + in`. Linear-interpolated read. `d` is the smoothed body delay in samples |
| 1 | String resonator | `infrasonic::KarplusString` (modified copy of DaisySP's), which uses `daisysp::DelayLine<float, 8192>`, `daisysp::DcBlock`, `daisysp::Tone`, `daisysp::CrossFade` | Per sample: Hermite read at `48000/f` samples, add input, hard clamp to plus or minus 20, DC block, multiply by 0.8, one-pole lowpass at 8 kHz, write back. Output is the value just written |
| 2 | Distortion | `daisysp::Overdrive` (from Plaits) | `SoftClip(5.18 * x) * 1.0` at drive 0.4 [calculated]. Output bounded to plus or minus 1 |
| 3 | Loop lowpass | `infrasonic::LPF12` (`BiquadCascade<1, LowPass>`) | Transposed direct form II, one shared coefficient set with separate L/R state. Q 0.9 |
| 4 | Loop highpass | `infrasonic::HPF12` | Same structure. Q 0.9 |
| 5 | Reverb | `daisysp::ReverbSc` (Costello, from Csound) | True stereo in and out. `samp -= (samp - verb) * mix`, a linear crossfade. Damping lowpass 12 kHz. Wet gain inside the module is 0.35 |
| 6 | Body delay write | `daisysp::DelayLine<float, 12000>` x2 | Writes `samp * fb_gain_`. This closes the outer loop back to step 0 |
| 7 | Echo | `infrasonic::EchoDelay<240000>` x2: `daisysp::DelayLine`, `infrasonic::BPF12`, `daisysp::SoftClip` | Fed `samp * send`. Per sample: read (linear), bandpass 800 Hz Q 1, soft clip, write `out * feedback + in`. Output is fully wet |
| 8 | Mix and volume | | `out = 0.5 * (samp + echo) * output_level_` |
| 9 | Output limiter | `daisysp::Limiter` x2 (from stmlib), in `FeedbackSynth_main.cpp`, not in the engine | `ProcessBlock(buf, 4, 0.7)`: peak follower, gain `1/peak` above 1.0, then `SoftLimit(x * 0.7 * gain * 0.7)` |

Points that differ from the README or are easy to miss:

- **The EQ comes before the reverb**, not after it as the README says.
- **The reverb is inside the feedback loop.** Reverb mix and decay shape what the string is fed.
- **The echo is outside the loop.** It only colours the output.
- The loop has several hard bounds: the string clamps at plus or minus 20, the overdrive at plus or minus 1. Feedback gain above 0 dB therefore saturates and cannot blow up, provided no NaN enters.
- The echo's soft clipper bounds its loop at plus or minus 1 for any feedback up to 1.5.
- Feedback gain at minimum is -60 dB, not off.
- `Limiter` has a low net gain: `0.7 * 0.7 = 0.49` (-6.2 dB) below threshold. Measured on the host with volume at 0.5, the output peaks at about 0.28 to 0.36 of full scale even at +12 dB feedback with echo feedback at 1.5 [ran].
- `SoftLimit` is not a hard ceiling. A transient faster than the peak follower's attack can exceed 1.0 for a few samples, and the Daisy codec then clips it [inferred].

### Where the stereo image comes from

Both channels get the same noise, the same input and the same parameters. Two things in the code separate them: the right body delay reads **4 samples** earlier than the left, and `ReverbSc` is a true-stereo network with different delay lines per side.

The 4-sample offset matters far more than it looks. With reverb mix at 0 nothing couples the two channels, so they are two independent nonlinear feedback loops with slightly different delays, and each settles on its own mode. Measured on the M1 renders with default knobs and feedback raised: the channels are identical (correlation 1.000) while only the noise floor sounds, and fully decorrelated (correlation 0.000) once the loop oscillates, with the left at about 1070 Hz and the right at about 1152 Hz. Those are roughly the 13th and 14th harmonics of the 82.4 Hz string, picked out by the 1 ms body delay and the 250 Hz highpass [ran, measured independently by the M1-C implementer and reviewer]. So the instrument's wide stereo image comes from the two loops diverging, and the reverb then mixes them.

A consequence for playing it: with the body knob at minimum the drone is a fairly static pair of tones, and the frequency knob moves them by selecting string harmonics rather than setting the pitch directly. Movement comes once body or reverb is brought into the loop. Near the top of the frequency range the output does lock to the string's own pitch (measured 522 Hz against 523.3 Hz nominal).

## 2. DaisySP modules used

All are plain C++ with no hardware dependency [read each file]. `dsp.h` has three `#ifdef __arm__` branches (inline assembly for `fmax`/`fmin`, a debug breakpoint); `__arm__` is not defined on Apple Silicon or on aarch64 Linux, so the portable branches are used [ran: checked both targets with clang].

| Module | File | Used by | Licence status |
|---|---|---|---|
| `DelayLine` | `Utility/delayline.h` (header only) | strings, body delay, echo | MIT |
| `dsp.h` helpers: `fclamp`, `fmax`, `fmin`, `fonepole`, `fmap`, `Mapping`, `mtof`, `pow10f`, `SoftClip`, `SoftLimit`, `DSY_CLAMP`, `PI_F` | `Utility/dsp.h` | everything | MIT |
| `DcBlock` | `Utility/dcblock.{h,cpp}` | string | MIT |
| `CrossFade` | `Dynamics/crossfade.{h,cpp}` | string | MIT |
| `Overdrive` | `Effects/overdrive.{h,cpp}` | engine | MIT |
| `WhiteNoise` | `Noise/whitenoise.h` | engine | MIT |
| `Limiter` | `Dynamics/limiter.{h,cpp}` | main | MIT |
| `Tone` | `Filters/tone.{h,cpp}` | string | **LGPL-2.1 upstream since 2023** |
| `ReverbSc` | `Effects/reverbsc.{h,cpp}` | engine | **LGPL-2.1 upstream since 2023** |

The firmware includes the umbrella `<daisysp.h>`, which pulls in every module. We only need the nine above. With `USE_ARM_DSP` undefined, the full umbrella also compiles on the host [ran].

Two portability notes:
- `WhiteNoise::Process` multiplies a signed `int32_t` seed by 16807 and relies on wrap-around. That is undefined behaviour in C++. Build the vendored DaisySP with `-fwrapv` so the sequence matches the ARM build.
- Never build the DSP with `-ffast-math`. It would change the results.

## 3. Every sample-rate-dependent constant

The firmware passes `sample_rate` to most modules, so many values scale correctly. Several do not. "Rate-aware" means the value is computed from the sample rate at run time.

| Constant | Value at 48 kHz | Rate-aware? | Where |
|---|---|---|---|
| Sample rate | 48000 | | `FeedbackSynth_main.cpp:9` |
| Block size, hence control rate | 4 samples, 12 kHz | No. Fixed block size | `FeedbackSynth_main.cpp:10` |
| Parameter smoothing coefficients | 0.01151 (0.05 s), 0.005757 (0.1 s), 0.002878 (0.2 s), 0.0005757 (1.0 s) per block | Yes, from the control rate | `ParameterRegistry::Register` |
| Parameter staircase | values change every 4 samples | No | callback structure |
| Body delay smoother | coef `1/(0.2 * 48000)` = 1.042e-4 per sample | Yes | `Engine::Init` |
| Echo time smoother | coef `1/(0.5 * 48000)` = 4.167e-5 per sample | Yes | `EchoDelay::SetLagTime` |
| `kMaxFeedbackDelaySamp` | 12000 samples (250 ms). The knob only reaches 4800 | **No**. Buffer size in samples | `FeedbackSynthEngine.h` |
| `kMaxEchoDelaySamp` | 240000 samples (5 s), exactly the knob maximum; the read clamps to 239999 | **No** | `FeedbackSynthEngine.h` |
| String `kDelayLineSize` | 8192 samples. Lowest pitch without the fallback upsampler is `48000/8188` = 5.9 Hz, well under the knob's 20.6 Hz, so the upsampler never runs | **No** | `KarplusString.h` |
| String delay clamp | 4 to 8188 samples; frequency clamp `f/sr` at most 0.25 (12 kHz) | **No** (in samples) | `KarplusString.cpp` |
| Body delay minimum | 1 sample | **No** | `Engine::SetFeedbackDelay` |
| Right-channel stereo offset | 4 samples = 83 microseconds | **No** | `Engine::Process` |
| Implicit loop delays | one sample each for the string write/read and the body delay write/read; these slightly flatten the pitch | **No** | structure |
| `DcBlock` coefficient | 0.99 per sample (corner about 76 Hz) [calculated]. `Init` ignores its `sample_rate` argument | **No** | `dcblock.cpp` |
| String damping `Tone` | 8000 Hz | Yes | `KarplusString::Reset` |
| Loop LPF and HPF, echo BPF | cutoff via `tan(pi * f / sr)`; cutoff clamped to `sr/2`. The 18 kHz maximum is close to Nyquist | Yes | `BiquadFilters.cpp` |
| `ReverbSc` delay lengths | 8 lines, 1933 to 4127 samples, defined as `N / 48000` seconds | Yes, but the buffer `DSY_REVERBSC_MAX_SIZE` = 98936 floats is fixed and sized for about 48 kHz | `reverbsc.cpp` |
| `ReverbSc` modulation segment lengths | `sr / rate` samples | Yes | `reverbsc.cpp` |
| `ReverbSc` damping | 12000 Hz | Yes | `Engine::Init` |
| `Limiter` attack and release | 0.05 and 0.00002 per sample | **No** | `limiter.cpp` |
| Interpolation loss | linear (body, echo) and Hermite (string) reads roll off high frequencies relative to Nyquist | **No** (inherent) | `delayline.h` |
| Switch debounce | 8 samples at 1 kHz, by wall clock | Independent of audio rate | `switch.cpp` |

**Conclusion:** run the engine at 48 kHz with 4-sample control blocks and every one of these stays as the original. 48 kHz is also the Brick's target rate in the platform notes. Running the engine at any other rate would require retuning every "No" row.

## 4. Audio input path

**Found in the code.** It has been there since the first public commit (`5e4ac92`, 2024-01-21).

`FeedbackSynth_main.cpp:22`:
```cpp
engine.Process(IN_L[i], OUT_L[i], OUT_R[i]);
```
`FeedbackSynthEngine.cpp:112-114`:
```cpp
inL = fb_delayline_[0].Read(fb_delay_samp_) + noise_samp + in;
inR = fb_delayline_[1].Read(daisysp::fmax(1.0f, fb_delay_samp_ - 4.f)) +
      noise_samp + in;
```

| Question | Answer |
|---|---|
| Where does it enter? | At the input of the string resonators, summed with the noise and the body-delay return. It is inside the feedback loop's summing point, ahead of the string, the overdrive, the filters, the reverb and the echo |
| Mono or stereo? | **Mono.** Only the codec's left input (`IN_L`, libDaisy `in[0]`, Seed `IN 00`, pin 16) is read. The right input is ignored. The same sample goes to both strings |
| Gain staging | **Unity.** No gain, trim, limiter or gate. libDaisy delivers codec samples as floats in plus or minus 1.0 |
| Which controls affect it? | **None directly.** There is no input level, enable or routing control, and the switch does not touch it. Every knob shapes it indirectly because it passes through the whole chain |
| Different behaviour when input is present? | No separate code path and no detection. Consequences that follow from the structure [inferred]: the input is audible **even with feedback at minimum**, since it passes straight through string, overdrive, filters, reverb and output; the string acts as a comb resonator tuned by the frequency knob; the overdrive's +14 dB pre-gain means a full-scale input is heavily saturated; a loud input dominates the -90 dBFS noise as the loop's excitation |
| Level calibration | Unknown. The analogue full-scale level of the Seed's input is a hardware property, not in this code |
| Is it reachable on the stock instrument? | Apparently not without a modification: the routing image wires both panel jacks to the codec **outputs** [inferred from `doc/audrey-simple-routing.jpg`, see `SOURCE_STUDY.md` section 5] |

What this means for the port: the faithful behaviour is "mono input, unity gain, added at the string input". Input gain, the mono fold-down, metering and the mute safeguards are our additions around that point and must default to a state that leaves the original behaviour reachable (gain 1.0, input enabled by the user).

## 5. Start-up state of the engine

`Engine::Init` sets, before any knob is read: strings at note 40; body delay smoother at 1000 samples heading to 64; echo time 5.0 s, echo feedback 0.5, lag 0.5 s; reverb feedback 0.85, damping 12 kHz; LPF 18 kHz; HPF 60 Hz; overdrive 0.4. All knob-driven values are overwritten in the first callback (see `PARAMETERS.md` section 4). The limiter's peak follower starts at 0.5. All delay lines start cleared.
