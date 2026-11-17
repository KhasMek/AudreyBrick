# Reference set

Renders of every parameter script in `scripts/`, made by `audrey-render` (AudreyVoice) and checked by `wavcheck`. Later changes to the port must reproduce these hashes on the same machine and compiler, unless the change is meant to alter the sound.

## What is committed

**Only `SHA256SUMS` and this README.** The WAVs are not committed: the full set is 25 WAV files, 228,865,100 bytes (about 218 MiB), over the 60 MB limit in the M1-C brief (`reference/.gitignore` excludes `*.wav`). The scripts were not shortened to fit.

**Regenerate the WAVs locally before using them**, and in particular before any comparison across platforms: a fresh checkout has only the hashes.

`SHA256SUMS` covers the 22 renders and the 3 generated input files (`inputs/sine_burst.wav`, `inputs/noise_burst.wav`, `inputs/hot_burst.wav`).

## How to regenerate

```
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
scripts/make_reference.sh                     # render into reference/, wavcheck, rewrite reference/SHA256SUMS
(cd reference && shasum -a 256 -c SHA256SUMS)
scripts/make_reference.sh --check             # render into reference/, wavcheck, compare with SHA256SUMS
```

Options of `scripts/make_reference.sh`:

| Option | Effect |
|---|---|
| `--check` | Compare the hashes with `reference/SHA256SUMS` instead of writing any `SHA256SUMS` |
| `--out <dir>` | Render into `<dir>` (created if missing) instead of `reference/`. Without `--check`, the hashes go to `<dir>/SHA256SUMS`. `reference/` is never written; with `--check`, `reference/SHA256SUMS` is only read |
| `--build <dir>` | Build directory holding the tools (default `$BUILD_DIR`, else `build/host`). It must be configured; the three tools are rebuilt in it |

For a read-only checkout, for example the Linux build in M1-D:

```
scripts/make_reference.sh --build /work/build --out /work/ref            # renders and /work/ref/SHA256SUMS
scripts/make_reference.sh --build /work/build --out /work/ref --check    # compare with the macOS hashes
```

**On another compiler or C library, `--check` is expected to fail.** The hashes pin this machine's build only: a different libm (`expf`, `powf`, `tanf`, `cosf`), compiler version or flag set changes the low bits, and the feedback loops spread such differences through the whole render. Compare such renders by measurement and by ear instead (docs/PLAN.md section 6, item 5). Hashing uses `shasum -a 256`, or `sha256sum` where `shasum` is missing; the file format is the same.

Two consecutive runs gave identical hashes.

### Script header convention

The script format has no duration or input field, so each script states them in its comment header (`src/tools/ScriptMeta.h` is the only parser; `make_reference.sh` parses nothing itself):

```
# @duration <seconds>     length of the render; audrey-render uses it when no duration is given
# @input <file.wav>       optional input, a plain file name in the inputs directory
#                         (audrey-render --inputs-dir; make_reference.sh passes <out>/inputs)
# @jump <threshold>       optional wavcheck absolute jump threshold (default 0.25)
# @click <ratio>          optional wavcheck click-ratio threshold away from events (default 80)
# @click-near <ratio>     optional wavcheck click-ratio threshold just after events (default 30)
```

They are ordinary comments to `ScriptControlSource`. Each script also says in prose what it shows. Only `all_max.txt` sets a non-default threshold (`@jump 0.7`), with the reason in its header.

### wavcheck thresholds

- **Absolute jump** `|x[n] - x[n-1]|`, default 0.25. Fails the high-frequency, saturated `all_max` (max 0.61 everywhere), which therefore sets 0.7.
- **Level-relative click ratio**: the second difference of the signal divided by a robust local level of it (median of 32-sample block energies over +-10 ms, leaving out the sample's own block and its neighbours, floor 1e-4). See `src/tools/wavcheck.cpp`. It is reported overall, and with `--script` also per event (each event time and ramp end, 0.1 s window) and as "near events" against "elsewhere".
  - **Elsewhere, default 80.** The highest clean value in the set is 55.1 (`sweep_frequency`), followed by 51.3 (`sweep_body`) and 48.1 (`sweep_highpass`). These are the original DSP's own waveform corners: they occur throughout those passages, not at events. 80 gives about 1.45x margin.
  - **Near events, default 30.** In the 0.1 s after every event and ramp end the highest value in the set is 9.2 (`echo_feedback_max`), so 30 gives about 3x margin and directly measures clicks at parameter changes.
  - Sensitivity, measured by adding a 10 ms +0.1 pulse to copies of these renders: caught at an event in `sweep_lowpass` (ratio 50), `echo_feedback_max` (43), `sweep_reverb_decay` (35) and `switch_long_echo` (33); caught anywhere in quiet or moderate passages (`sweep_volume` at 10 s: 497; `input_noise_feedback`: 737; `sweep_body` at 20 s: 152). It cannot see a 0.1 step inside dense high-frequency drones (`feedback_rise`'s steady state, `all_max`, the start of `sweep_body`), where steps that size are part of the waveform.

## Build that produced SHA256SUMS

| Item | Value |
|---|---|
| Machine | Apple Silicon (arm64), macOS 26.5.1 (25F80) |
| Compiler | Apple clang version 21.0.0 (clang-2100.1.1.101), target arm64-apple-darwin25.5.0 |
| CMake | 3.31.3, `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS` empty |
| Flags, DSP and render targets | `-O3 -DNDEBUG -std=c++17 -arch arm64` (build type), then `-O2 -fwrapv -ffp-contract=off -fno-fast-math` (`AB_DSP_OPT_OPTIONS`, last, so `-O2` wins) and `-Wall -Wextra -Wpedantic`. Vendored sources add `-w` |
| Floating point | No flush-to-zero set by the render tool (arm64 default FPCR). Denormals have no speed penalty on this CPU |
| dr_wav | v0.14.6, https://github.com/mackron/dr_libs commit `35b5c392cc15456736c440a68dbe073178bbffe0` |
| Audrey II | commit `e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3`; DaisySP `7f67768eb22103e5f7743e709e090ac435a11fae` |
| Render chunk | `audrey-render` calls `AudreyVoice::process` with 256-frame chunks. The output does not depend on the chunk size (tested by the M1-B alignment test and the parity test) |

## The renders

From the run that produced `SHA256SUMS`. Click ratios: largest away from events / largest within 0.1 s after an event.

| Render | Seconds | Peak L / R | RMS L / R | DC L / R | Jumps (threshold) | Click elsewhere / near events |
|---|---|---|---|---|---|---|
| all_max | 30 | 0.8007 / 0.8033 | 0.2266 / 0.2273 | -0.000002 / -0.000016 | 0 (0.7) | 4.7 / 0.3 |
| all_min | 20 | 0 / 0 | 0 / 0 | 0 / 0 | 0 (0.25) | 0.0 / 0.0 |
| all_min_default_volume | 20 | 2.3e-6 / 2.3e-6 | 0.0000 / 0.0000 | 0 / 0 | 0 (0.25) | 0.0 / 0.0 |
| echo_extremes | 30 | 0.4118 / 0.3821 | 0.1333 / 0.1200 | +0.000006 / -0.000021 | 0 (0.25) | 14.6 / 6.3 |
| echo_feedback_max | 32 | 0.4280 / 0.4039 | 0.1362 / 0.1293 | -0.000015 / +0.000010 | 0 (0.25) | 18.3 / 9.2 |
| feedback_rise | 32 | 0.2216 / 0.2172 | 0.0843 / 0.0845 | -0.000000 / +0.000001 | 0 (0.25) | 4.4 / 4.1 |
| input_hot_burst | 20 | 0.2062 / 0.2062 | 0.0406 / 0.0406 | 0 / 0 | 0 (0.25) | 21.7 / 0.4 |
| input_noise_feedback | 20 | 0.1452 / 0.1634 | 0.0270 / 0.0280 | 0 / 0 | 0 (0.25) | 4.4 / 0.2 |
| input_sine_min_feedback | 20 | 0.2178 / 0.2178 | 0.0497 / 0.0497 | 0 / 0 | 0 (0.25) | 34.4 / 0.3 |
| lowpass_low_feedback_max | 28 | 0.2120 / 0.1825 | 0.0362 / 0.0389 | 0 / 0 | 0 (0.25) | 3.9 / 2.9 |
| silence_min_feedback | 20 | 3.5e-5 / 3.5e-5 | 0.0000 / 0.0000 | 0 / 0 | 0 (0.25) | 0.4 / 0.3 |
| sweep_body | 26 | 0.3080 / 0.2977 | 0.0855 / 0.0847 | +0.000003 / +0.000001 | 0 (0.25) | 51.3 / 8.2 |
| sweep_echo_feedback | 26 | 0.3575 / 0.3622 | 0.1102 / 0.1069 | -0.000002 / -0.000003 | 0 (0.25) | 19.2 / 7.2 |
| sweep_echo_send | 26 | 0.3390 / 0.3440 | 0.1072 / 0.1025 | -0.000002 / -0.000004 | 0 (0.25) | 19.0 / 6.9 |
| sweep_echo_time | 26 | 0.3614 / 0.3455 | 0.1149 / 0.1033 | -0.000004 / -0.000003 | 0 (0.25) | 18.2 / 6.3 |
| sweep_frequency | 26 | 0.2864 / 0.2978 | 0.0948 / 0.0949 | +0.000002 / +0.000003 | 0 (0.25) | 55.1 / 6.3 |
| sweep_highpass | 26 | 0.3104 / 0.3361 | 0.0956 / 0.1001 | +0.000003 / +0.000001 | 0 (0.25) | 48.1 / 3.5 |
| sweep_lowpass | 26 | 0.3211 / 0.3087 | 0.1000 / 0.0931 | -0.000000 / +0.000000 | 0 (0.25) | 20.2 / 5.3 |
| sweep_reverb_decay | 26 | 0.5840 / 0.6253 | 0.1171 / 0.1212 | -0.000000 / -0.000004 | 0 (0.25) | 36.7 / 8.0 |
| sweep_reverb_mix | 26 | 0.5241 / 0.6116 | 0.1327 / 0.1467 | -0.000002 / -0.000003 | 0 (0.25) | 25.9 / 5.1 |
| sweep_volume | 26 | 0.5227 / 0.5104 | 0.1059 / 0.0923 | -0.000006 / -0.000004 | 0 (0.25) | 14.5 / 7.0 |
| switch_long_echo | 34 | 0.3726 / 0.3690 | 0.1219 / 0.1087 | +0.000005 / +0.000000 | 0 (0.25) | 22.3 / 8.7 |

Render speed: about 135 to 155 times real time (Release, one core).
