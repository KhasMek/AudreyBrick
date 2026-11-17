# Plan

Written at the end of M0 (2026-11-17). Reads with `SOURCE_STUDY.md`, `PARAMETERS.md`, `SIGNAL_FLOW.md`. Items marked **(decision)** wait for the human; see `RISKS.md`.

## 1. Strategy in one paragraph

Reuse the original DSP source files **unmodified**, at the original's 48 kHz and 4-sample control block, and replace only what touches hardware. The upstream engine already builds and runs on the Mac without changes (verified, section 6). Our own code is a thin layer: a control abstraction that feeds normalised knob values into the original's parameter mapping, a voice wrapper that reproduces the original audio callback, and the platform layer.

## 2. Separating hardware from DSP

### libDaisy dependencies in the firmware and their replacements

| # | Dependency | Where | Replacement |
|---|---|---|---|
| 1 | `DaisySeed hw`: `Init`, `SetAudioSampleRate(SAI_48KHZ)`, `SetAudioBlockSize(4)`, `StartAudio`, `AudioSampleRate`, `AudioBlockSize` | `FeedbackSynth_main.cpp`, `Controls::Init` | SDL2 audio device at 48 kHz. The voice wrapper runs the engine in fixed 4-sample control blocks inside whatever buffer SDL delivers |
| 2 | `AudioHandle::InputBuffer` / `OutputBuffer` (non-interleaved float), macros `IN_L`, `OUT_L`, `OUT_R` | `AudioCallback` | The voice wrapper takes plain `float*` buffers. The platform layer converts from SDL's interleaved format |
| 3 | `hw.adc`: `AdcChannelConfig`, `Init`, `Start`, `GetFloat(0..10)`, and the `1.0 - x` inversion | `Controls::initADCs`, `Controls::Update` | `ControlSource` (section 4), which supplies the already-inverted normalised value |
| 4 | `daisy::Switch` on GPIO D14 with 1 kHz debounce | `Controls` | A bool in the control frame. No debounce needed |
| 5 | `daisy::Pin`, `daisy::seed::A0..A10`, `D14` | `FeedbackSynthControls.cpp` | Dropped |
| 6 | SDRAM placement: `DSY_SDRAM_BSS`, `<dev/sdram.h>`, the 32 MiB `sdram_pool`, `SDRAM::allocate<T>()`, `asm("bkpt 255")` | `Source/memory/sdram_alloc.{h,cpp}`, used by `Engine::Init` for the two echo delays (0.96 MB each) and the reverb (0.4 MB) | Our own `memory/sdram_alloc.h` with the same interface, implemented as `new T(...)`. It is found first on the include path, so `FeedbackSynthEngine.cpp` stays untouched and the engine's `unique_ptr`s delete correctly. Allocation happens in `Init`, never on the audio thread |
| 7 | `<dev/sdram.h>` and `<arm_math.h>` includes | `FeedbackSynthEngine.h`, `DSPUtils.h` | Already inside `#ifdef __arm__`, which is undefined on arm64 macOS and aarch64 Linux (checked). No change |
| 8 | CMSIS `arm_common_tables.c`, `arm_sin_cos_f32.c` | `Makefile` | Dropped. The only call site is commented out |
| 9 | `System::GetNow()` | inside `daisy::Switch` | Dropped with the switch |
| 10 | `while(1) {}` main loop | `main` | SDL event and render loop |
| 11 | The codec and analogue stages | hardware | Not reproducible. Noted as a fidelity limit |

`ParameterRegistry.h` and `SmoothedValue.h` have no hardware dependency and are reused. `ParameterRegistry` uses `std::function` and `std::unordered_map`, which allocate only in `Register` (at init). Its `Process` and `UpdateNormalized` do a hash lookup and a call, with no allocation or lock, so it is acceptable on the audio thread. The reviewer must confirm this.

### DaisySP modules needed

`Utility/dsp.h`, `Utility/delayline.h`, `Utility/dcblock.{h,cpp}`, `Dynamics/crossfade.{h,cpp}`, `Dynamics/limiter.{h,cpp}`, `Effects/overdrive.{h,cpp}`, `Noise/whitenoise.h`, `Filters/tone.{h,cpp}`, `Effects/reverbsc.{h,cpp}`, all from commit `7f67768`.

Confirmed from source: none includes a hardware header, all memory is static inside the objects, and the only platform conditionals are the `__arm__` branches in `dsp.h`, which fall back to portable code. Confirmed by compiling and running them on macOS arm64. For Linux aarch64 the evidence is the source reading plus the fact that clang's `aarch64-linux-gnu` target does not define `__arm__`; an actual aarch64 Linux build is task M1-D.

The firmware includes the umbrella `<daisysp.h>`. We supply a trimmed `daisysp.h` that includes only the nine modules above, so we do not vendor the rest of the library.

## 3. Module breakdown

```
CMakeLists.txt
THIRD_PARTY.md
licenses/                       licence texts shipped with the port
third_party/
  audrey-ii/                    upstream commit e5ab799, files byte-identical:
    Source/BiquadFilters.{h,cpp} DSPUtils.h EchoDelay.h FeedbackSynthEngine.{h,cpp}
           KarplusString.{h,cpp} ParameterRegistry.h SmoothedValue.h
    LICENSE  LICENSE.archive-2022  CREDITS.md  UPSTREAM.md
  daisysp/                      commit 7f67768, MIT modules only + LICENSE
  daisysp-lgpl/                 reverbsc and tone (decision R7) + LICENSE
  dr_libs/dr_wav.h
src/
  shim/daisysp.h                trimmed umbrella header
  shim/memory/sdram_alloc.h     heap-backed stand-in, same interface
  core/ControlSource.h          interface + ControlFrame + Knob enum
  core/ParamTable.{h,cpp}       names, ranges, curves, defaults, display formatting
  core/AudreyVoice.{h,cpp}      engine + parameter registry + limiters; the original callback
  control/ScriptControlSource.{h,cpp}   timed parameter script (tests, renders)
  control/ButtonControlSource.{h,cpp}   M2: button state to knob values
  tools/audrey_render.cpp       script + optional input WAV -> output WAV
  tools/wavcheck.cpp            NaN/inf, DC, peak, RMS, click detector
  platform/                     M2+: SDL audio, input, UI, presets
tests/
  ref/                          test-only reference harness (see section 6)
scripts/                        parameter scripts for the reference set
reference/                      rendered reference WAVs + hashes
```

Ownership rule: `third_party/` is written once by M1-A and then read-only. `src/core` and `src/control` belong to M1-B. `src/tools`, `tests/ref`, `scripts`, `reference` belong to M1-C.

`FeedbackSynthControls.{h,cpp}` is not vendored as code because it is bound to libDaisy. Its parameter table (`registerParams`) and its `Update` logic are transcribed into `AudreyVoice`, with the upstream copyright notice kept in that file.

### `AudreyVoice`

```cpp
class AudreyVoice {
public:
    static constexpr int kSampleRate   = 48000;
    static constexpr int kControlBlock = 4;      // samples, as the firmware

    void init();   // allocates; call before audio starts, never on the audio thread

    // Any frame count. Controls are read and applied every kControlBlock samples,
    // counted across calls. inMono may be nullptr (treated as silence).
    void process(ControlSource& controls, const float* inMono,
                 float* outL, float* outR, std::size_t frames) noexcept;
};
```

Inside `process`, every 4 samples: read a `ControlFrame`, apply the reverb-decay shaping and the switch's halving exactly as `Controls::Update` does, call `UpdateNormalized` for the eleven parameters, call `params.Process()`, run the engine for 4 samples, then run the two `Limiter`s on those 4 samples with pre-gain 0.7. That is the firmware's `AudioCallback`, line for line.

The firmware's limiter stays on in `AudreyVoice`: it is part of the original output stage. The additional safety limiter that CLAUDE.md asks for is a separate, switchable stage after it, designed in M4.

## 4. `ControlSource`

```cpp
enum class Knob : std::uint8_t {      // same order as the firmware's Parameter enum
    Frequency, FeedbackGain, FeedbackBody, FeedbackLPFCutoff, FeedbackHPFCutoff,
    ReverbMix, ReverbDecay, EchoDelaySend, EchoDelayTime, EchoDelayFeedback,
    OutputVolume, Count
};

struct ControlFrame {
    float knob[static_cast<int>(Knob::Count)];  // 0..1, the firmware's (1 - adc) value
    bool  delaySwitch;                          // true = switch on
};

class ControlSource {
public:
    virtual ~ControlSource() = default;
    // Called on the audio thread once per control block (12 kHz).
    // Must not allocate, lock, block or do I/O.
    virtual void read(ControlFrame& out) noexcept = 0;
};
```

- Values are raw knob positions. All mapping, curves and smoothing stay in the original code.
- `ScriptControlSource` (M1): preloaded list of timed events, `time knob value [ramp-seconds]`, linearly ramped like a hand turning a knob. Deterministic, advanced by block count.
- `ButtonControlSource` (M2): the UI thread publishes held-button state and target values through atomics; `read` integrates ramps at the control rate.
- Presets, pages and input-source selection sit above this interface and do not change it.

Audio input is not part of `ControlSource`. It reaches `AudreyVoice::process` as `inMono`. The input stage (source selection, gain, mono fold-down, meter, mute safeguards) is a separate module in M2b that produces that buffer.

Decided 2026-11-17: the input stage computes `inMono = gain * (L + R)` with a smoothed gain, default 1.0, and passes it to the engine's unity-gain entry point. A mono or left-only source at gain 1.0 is therefore identical to the original firmware. `AudreyVoice` itself is unchanged by this.

## 5. Build setup

**Common:** CMake 3.20 or newer, C++17, no compiler extensions. DSP targets build with `-O2 -fwrapv -ffp-contract=off -fno-fast-math` (the CMake variable `AB_DSP_OPT_OPTIONS`) and never with `-ffast-math`. Contraction is off so that clang on macOS and gcc on Linux do the same arithmetic. The firmware's own compiler probably fused multiply-adds, so rounding-level differences from the hardware exist either way [inferred]; in a saturating feedback loop these change the exact waveform but not its character. Any target that compiles DSP code, including upstream header-only code, must apply `AB_DSP_OPT_OPTIONS`. Warnings are enabled for our code and disabled for `third_party/`. Tests are plain executables registered with CTest, with no test framework dependency.

**Targets:**
- `audrey_dsp` (static): vendored upstream sources plus the MIT DaisySP modules. No SDL.
- `audrey_lgpl` (shared or static, per decision R7): `reverbsc.cpp` and `tone.cpp`.
- `audrey_core` (static): `src/core`, `src/control`.
- `audrey-render`, `wavcheck`, and the tests.
- `audreybrick` (M2+): the SDL app.

**Host build (macOS arm64):** Apple clang 21, CMake 3.31 and SDL2 2.32.6 from Homebrew are installed on this machine.
```
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure
```

**Device build (Linux aarch64):** Docker 29.4 is installed; an arm64 Linux container runs natively on Apple Silicon. Proposed order of preference:
1. A Debian or Ubuntu arm64 container whose glibc is **no newer than the device's**. The device's glibc version is unknown until checklist item 1, so the image is chosen after that.
2. PortMaster's builder image, if it is confirmed to exist under the name in the platform notes and its glibc fits.
3. `zig cc` with a pinned glibc floor as the fallback (zig is not installed here).

The app links dynamically against the device's own `libSDL2` and bundles nothing else except, if chosen, `libaudrey_lgpl.so` in `libs.aarch64/`. Packaging follows the platform notes, section 5.

Denormals: set flush-to-zero on the audio thread on both platforms (the FPCR FZ bit on arm64). Whether the Daisy runs with flush-to-zero is not visible in the firmware; the effect on audible output is nil either way [inferred].

## 6. Verification of sound parity

**Can the original DSP be run on the host? Yes.** During M0 the unmodified upstream `FeedbackSynthEngine.cpp`, `KarplusString.cpp`, `BiquadFilters.cpp` and the pinned DaisySP modules were compiled with Apple clang (`-std=c++17 -O2 -fwrapv`) against a scratch stand-in for the SDRAM allocator, and run for 20 seconds per setting:

| Feedback | Echo feedback | Non-finite samples | Peak before limiter | Peak after limiter | DC |
|---|---|---|---|---|---|
| -60 dB | 0 | 0 | 0.0001 | 0.0000 | 0.000000 |
| 0 dB | 0 | 0 | 0.581 | 0.278 | -0.000001 |
| +12 dB | 0 | 0 | 0.609 | 0.291 | -0.000001 |
| +12 dB | 1.5 | 0 | 0.770 | 0.362 | -0.000011 |

Speed was about 180 times real time on this Mac. That harness was a throwaway in the scratch directory; it is not in the repo. It set engine parameters directly and did not exercise the parameter registry.

**What cannot be run on the host:** the firmware binary itself, libDaisy, the ADC, the codec and the analogue path. No Daisy hardware is assumed to be available, so there is no recording of the real instrument to compare against unless the human supplies one.

**Approach:**

1. **Reference harness** (`tests/ref/`, test-only): the upstream engine and `ParameterRegistry`, driven by a transcription of `AudioCallback` and `Controls::Update` that is written independently of `AudreyVoice`, reading the same parameter script.
2. **Bit-exact parity test:** for every script in `scripts/`, `AudreyVoice` output must equal the reference harness output sample for sample, on the same machine and compiler. Since both use the same DSP sources, any difference is a bug in our layer.
3. **Reference set:** the rendered WAVs and their SHA-256 hashes are committed under `reference/`. Later refactors must reproduce them. Scripts cover: silence at minimum feedback; a slow feedback rise; each knob swept alone at a fixed feedback; the switch toggled during a long echo; echo feedback at 1.5; every knob at minimum and at maximum; and an input WAV (sine burst, noise burst) with feedback at minimum and raised.
4. **Health checks on every render** (`wavcheck`): no NaN or inf, DC offset below a threshold, peak at or below 1.0, and a click detector on parameter changes.
5. **Cross-platform comparison.** Measured in M1-D (gcc 14.2, glibc 2.41, Debian 13 arm64 in Docker, `scripts/build_linux_arm64.sh`) against the macOS renders:
   - 14 of the 22 renders are bit-identical. They are the ones whose scripts never pass a differing argument to the maths library.
   - The other 8 differ because a small share of `expf`, `expm1f` and `tanf` results differ by one unit in the last place between glibc and Apple's library, and because Apple clang turns `powf(2, x)` and `powf(10, x)` into `exp2f` and `__exp10f` while gcc keeps `powf`. Fewer than 1% of the arguments used are affected.
   - Because the instrument is a saturating feedback loop, a one-bit difference can grow. Three renders stay 66 to 99 dB below the signal, two grow but stay partly correlated, and three (`sweep_frequency`, `sweep_reverb_decay`, `all_max`) end up fully decorrelated. In each case the difference starts at rounding size and grows smoothly. Every Linux render passes `wavcheck`.
   - Ruled out as causes: uninitialised memory (valgrind), fused multiply-add, signed overflow, denormal handling, handler ordering.
   - Consequences: bit-exact regression is **per toolchain** (the committed hashes are for Apple clang on macOS only). Across platforms, compare by level, spectrum and ear, not by hash and not by sample difference on long renders. No compiler flag is worth adding to chase cross-platform equality. For the device build prefer **gcc**, which keeps `powf` as the firmware's own arm-gcc build does. An older glibc on the device may round some of these functions differently again [inferred].
   - Enabling flush-to-zero on the audio thread (M2) may change the reference hashes; regenerate them deliberately when that lands.
6. **Listening tests** by the human against the real instrument or published recordings (M4). This is the only check on the analogue-side differences.

## 7. Milestone notes

- **M1** is specified as task briefs in `docs/briefs/M1.md`. Not started.
- **M2** depends on the control scheme being approved.
- **M2b** adds the input stage around the unity-gain entry point found in the code.
- **M3** depends on device checklist items 1 to 6.
- The sample-rate question in CLAUDE.md is largely settled by the source study (the firmware is 48 kHz), subject to the device accepting 48 kHz output (checklist item 4).
