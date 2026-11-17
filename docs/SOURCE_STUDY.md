# Source study (M0)

Studied on 2026-11-17 from fresh clones. Tags: **[read]** read in the source, **[ran]** observed by running something, **[inferred]** reasoning, not read.

## 1. What was studied

| Item | Value |
|---|---|
| Official repo | https://github.com/Synthux-Academy/Audrey-II |
| Commit studied | `e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3` (2026-08-03, "Add Credits section to README"), branch `main`, 4 commits total [read] |
| Firmware size | 1,364 lines across `FeedbackSynth_main.cpp`, `Makefile` and `Source/` [read] |
| `lib/DaisySP` pin | `7f67768eb22103e5f7743e709e090ac435a11fae` (2023-02-13, "Don't pass Tone float params by reference (#180)"). Not a tagged release; it predates DaisySP `V1.0.0` and the LGPL split [read] |
| `lib/libDaisy` pin | `694fc55a4892bc9411296f224ad96bba2bf44b67` (2023-02-10, "docs: use explicit grouping; omit comments from output (#563)"). Not a tagged release; it lies between v5.3.0 and v6.0.0 [read, ordering inferred from dates] |
| RtMidi, RtAudio | **Not present.** `.gitmodules` lists only libDaisy and DaisySP. The README's licence paragraph names RtMidi and RtAudio, but no submodule, source file or include refers to them [read] |

### Host or desktop build

None exists upstream. The Makefile targets the Daisy (`-DTARGET_DAISY`, libDaisy core Makefile, ARM toolchain). The README sentence about RtMidi and RtAudio looks like a leftover from a desktop harness that was never published [inferred].

The DSP is nevertheless host-ready: `DSPUtils.h` and `FeedbackSynthEngine.h` guard their ARM includes with `#ifdef __arm__`, and `DSPUtils.h` has an explicit non-ARM branch. **The unmodified engine was compiled and run on this Mac** with a scratch harness and a 15-line stand-in for `sdram_alloc.cpp`: zero source changes, no NaN or inf at any feedback setting tried, about 180x real time [ran]. See `PLAN.md` section 6.

## 2. Licence

`LICENSE` in the official repo, quoted in full:

```
MIT License

Copyright (c) 2026 Synthux Academy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

**Copyright line: `Copyright (c) 2026 Synthux Academy`.**

**Two copyright lines exist for the same code.** The archived copy in `simple-designer-instruments/official/audrey-ii/LICENSE` is the same MIT text with `Copyright (c) 2022 Infrasonic Audio, LLC`. The source files are byte-identical between the two repos. We keep both notices (see `RISKS.md` R8).

Other notices inside the firmware source:
- `Source/KarplusString.h`: "Modified version of KarplusString class from DaisySP ... Original code licensed as MIT Copyright (c) 2020 Electrosmith, Corp."
- `Source/memory/sdram_alloc.cpp`: "inspired by Eurorack Blocks MonotonicMemoryPool" (we do not reuse this file).

### Licence problem found: the reverb and the string's damping filter

DaisySP at the pinned commit ships everything under one MIT `LICENSE` (`Copyright (c) 2020 Electrosmith, Corp.`). In December 2023 Electrosmith moved the Csound-derived modules into a separate repo, **DaisySP-LGPL, under LGPL-2.1**. Two modules the firmware uses are among them:

- `Effects/reverbsc.{h,cpp}` (`ReverbSc`, "Ported from csound/soundpipe", Sean Costello and Istvan Varga). The `.cpp` in DaisySP-LGPL `c89d380` is identical to the pinned one apart from blank lines [ran diff].
- `Filters/tone.{h,cpp}` (`Tone`, used inside the string). `tone.cpp` is byte-identical [ran diff].

The other modules we need (`dsp.h`, `delayline.h`, `dcblock`, `overdrive`, `crossfade`, `limiter`, `whitenoise`) are not in the LGPL repo and remain MIT [read].

So the Audrey II README's claim that everything is MIT is accurate for the pinned commit's label, but the upstream author has since relabelled these two files. This needs your decision before M1 vendors anything (`RISKS.md` R7).

## 3. Credits

`CREDITS.md`, in full:

> [Nick Donaldson](https://github.com/Synthux-Academy/audrey-ii-simple/commits?author=ndonald2) - concept and firmware
>
> [Roey Tsemah](https://github.com/roeytsemah) - visual and hardware design

- The developer's name is **Nick Donaldson** (CLAUDE.md spelled it "Donadson"). Code namespace is `infrasonic`, and the panel carries the Infrasonic Audio logo.
- The link target `Synthux-Academy/audrey-ii-simple` does not resolve (`git ls-remote` says repository not found), so it is private, renamed or deleted [ran].

## 4. `simple-designer-instruments` repo

https://github.com/Synthux-Academy/simple-designer-instruments, studied at `b16423ea875afc7bb3d8c4282e730127f3808d3f` (2026-08-03).

- `official/audrey-ii/` holds a full source copy, marked as an archive that points to the new repo. Same submodule pins. Source identical to the official repo; only `LICENSE` (copyright line), `Makefile` (target name, `libs` targets), `README.md` and the absent `CREDITS.md` differ [ran diff].
- This repo has the only real code history:

| Date | Commit | Author | Change |
|---|---|---|---|
| 2024-01-21 | `5e4ac92` | Vladislav Litvinenko | "Add audrey". Audio input, 48 kHz and block size 4 are already present |
| 2024-08-23 | `56f5afa` | Nick Donaldson | "Swap high/lowpass pins" (ADC pins A4 and A8 exchanged) |
| 2025-03-07 | `fca417d` | Nick Donaldson | "Add limiter to prevent output hardclipping": the two `daisysp::Limiter` in `main` |
| 2025-03-10 | `1dce031`, `5d0cfb4` | Nick Donaldson | Merge `audrey-v2`, update v3 binary |

- Binaries: `audrey-ii.bin`, `audrey-ii-v2.bin`, `audrey-ii-v3.bin` (about 92 kB each). The official source matches the state after the limiter commit, which is what v3 was built from [inferred from dates; binaries not disassembled].
- No extra documentation: the README is the same text. Nothing describes the audio input.

## 5. Hardware facts from the repo's `doc/` images

- `doc/audrey-panel-cheeks.jpg`: 11 knobs, one toggle switch marked `!!`, two jacks. Panel labels: **frequency**, the large unlabelled knob with rings (feedback), **body**, **lowpass**, **highpass**, reverb **dry/wet**, reverb **decay**, delay **mix**, delay **repeats**, delay **time**, **volume**.
- `doc/audrey-simple-routing.jpg`: the two jacks go to Seed pins 18 and 19, which the pinout comment in `FeedbackSynthControls.cpp` names `OUT 00` and `OUT 01`. Pins 16 and 17 (`IN 00`, `IN 01`) are not routed. The switch goes to pin 15 (D14).

**Consequence: the stock kit has two output jacks and no input jack.** The firmware does process the codec's left input (see `SIGNAL_FLOW.md` section 4), but using it on real hardware needs an added jack on pin 16 [inferred from the routing image; not stated in any text].

## 6. Corrections to what CLAUDE.md assumed

| CLAUDE.md said | Finding |
|---|---|
| 96 kHz / 24-bit | Firmware runs at **48 kHz**, block size 4 [read, `FeedbackSynth_main.cpp:9-10`] |
| Submodules include RtMidi and RtAudio | Only libDaisy and DaisySP [read] |
| Chain: string, distortion, reverb, EQ, body delay | Code order is string, distortion, **EQ, then reverb**, body delay [read] |
| "Nick Donadson" | Nick Donaldson [read] |
| Final delay feedback "can grow without limit" | Feedback gain tops out at 1.5 and the loop is soft-clipped to plus or minus 1 [read] |
| Audio input, unverified | Confirmed in code: mono, left channel, unity gain, summed into both strings [read] |
