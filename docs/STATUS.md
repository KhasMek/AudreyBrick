# Status

Maintained by the orchestrator. Last updated: 2026-11-17.

## Where we are

**M0 (source study) is complete and awaits the human's review. No application code exists. No implementation has been delegated.**

| Milestone | State |
|---|---|
| M0 source study | Done, pending human review |
| M1 host DSP | **Done and reviewed (M1-A to M1-D).** Awaiting the human's sign-off of the milestone |
| M2 host app | **Done and reviewed (M2-A, M2-B, M2-C): the app plays on the Mac with the full screen.** Awaiting the human's play test and sign-off of the milestone |
| M2b input path | Not started |
| M3 device build | Not started. Blocked on device checklist items 1 to 9 |
| M4 tuning | Not started |
| M5 presets, polish, release decision | Not started |

## M0 deliverables

| Document | Content |
|---|---|
| `docs/SOURCE_STUDY.md` | Commits studied, licence text, credits, submodule pins, host-build finding, history from the archive repo |
| `docs/PARAMETERS.md` | The 11 knobs and the switch: range, curve, smoothing, effect |
| `docs/SIGNAL_FLOW.md` | DSP chain, DaisySP modules, rate-dependent constants, audio input path |
| `docs/CONTROL_SCHEME_PROPOSAL.md` | Button mapping proposal, trade-offs, rejected alternatives |
| `docs/PLAN.md` | Hardware/DSP separation, modules, `ControlSource`, builds, verification |
| `docs/RISKS.md` | Risks, decisions needed, questions |
| `docs/DEVICE_CHECKLIST.md` | Numbered on-device checklist (not yet run) |
| `docs/briefs/M1.md` | Task briefs M1-A to M1-D and the review brief |

## Key findings

1. The firmware runs at **48 kHz with a 4-sample block**, not 96 kHz.
2. **Audio input is in the code**: mono (left codec input), unity gain, summed into both strings. No control affects it. The stock kit appears to have no input jack.
3. The whole DSP is about 1,000 lines, hardware-free apart from the SDRAM allocator, and **the unmodified engine compiles and runs on the Mac** (scratch test, 0 non-finite samples, about 180x real time).
4. Only libDaisy and DaisySP are submodules. RtMidi and RtAudio are not used. No host build exists upstream.
5. **Licence issue:** `ReverbSc` and `Tone` are LGPL-2.1 in current DaisySP, although MIT-labelled at the pinned commit.
6. Two copyright lines exist for the firmware: Synthux Academy (2026) and Infrasonic Audio LLC (2022).
7. Code order is string, overdrive, lowpass, highpass, reverb; the README lists the reverb before the EQ.

## Decisions

All M0 decisions were answered on 2026-11-17; see `docs/RISKS.md` section 2a. M1 is approved (A, B, C and D). Still open: the audio-input provenance question and whether a real unit or recordings are available (`docs/RISKS.md` section 3).

## Commit points

The human commits. The orchestrator stops at each point and proposes a message.

| # | Point | State |
|---|---|---|
| 1 | M0 docs and decisions | Committed |
| 2 | M1-A vendoring and build skeleton, reviewed | Committed |
| 3 | M1-B control abstraction and voice, reviewed | Committed |
| 4 | M1-C render tool, parity test, reference set, reviewed | Committed |
| 5 | M1-D aarch64 build check, reviewed | Committed |
| 6 | M2 plan and briefs | Committed |
| 7 | M2-A button control logic, reviewed | Committed |
| 8 | M2-B audio host and app shell, reviewed (first point where the human can play it on the Mac; blank window) | Committed |
| 9 | M2-C the screen, reviewed | Committed |
| 10 | Screen: plain engine values for the five unitless parameters | Ready to commit |

## What was verified and how

| Claim | Evidence |
|---|---|
| Commit and submodule pins | `git log`, `git submodule status` on a fresh clone |
| Source identical between official and archive repos | `diff -r` |
| Upstream engine builds and runs on macOS arm64 | Compiled with Apple clang 21, ran 4 settings for 20 s each; results in `PLAN.md` section 6. The harness was a throwaway outside the repo |
| `__arm__` undefined on arm64 macOS and aarch64 Linux | Compiled a test on the Mac; checked clang's predefined macros for `aarch64-linux-gnu` |
| `reverbsc.cpp`, `tone.cpp` match DaisySP-LGPL | `diff` against DaisySP-LGPL `c89d380` |

## Not verified

- Nothing has been built for or run on Linux aarch64 or the Brick.
- The firmware binaries were not disassembled; that v3 corresponds to the official source is inferred from commit dates.
- The parameter registry and the control mapping were read, not executed.
- The claim that the stock kit has no input jack comes from one routing image.
- Device CPU headroom is an estimate.

## Definition-of-done items for this step

- Tests: none exist yet (no code).
- Separate review: **not done.** The M0 documents were written by the orchestrator and have not been checked by a reviewer agent. Recommended before M1 starts: have a reviewer check `PARAMETERS.md` and `SIGNAL_FLOW.md` against the upstream source.
- `THIRD_PARTY.md`: created in M1-A.

## M1-A result (2026-11-17)

- Vendored unmodified: 10 Audrey II source files (`e5ab799`), 11 MIT DaisySP files and 4 LGPL-treated files (`7f67768`), with licence texts. `scripts/verify_vendor.sh` re-fetches the pins and compares 38 files.
- Builds on macOS arm64: `audrey_lgpl` (shared: reverb and tone only) and `audrey_dsp` (static). Two smoke tests pass in Release and under ASan/UBSan.
- Implemented by one agent, reviewed by another, which independently diffed the vendored files against upstream and inspected the libraries' symbols. Review verdict: accept, with follow-ups. The follow-ups were applied by the implementer and re-verified by the orchestrator (clean rebuild, both test runs, vendor check).
- Added on review: `-ffp-contract=off -fno-fast-math` on all DSP code; licence notices for the code DaisySP ported from Mutable Instruments (stmlib, Plaits) and Soundpipe.
- Smoke-test numbers: feedback +6 dB for 5 s peaks at 0.70; feedback -60 dB peaks at 5.1e-5; no non-finite samples.

Open after M1-A:
- Root `LICENSE` added: MIT, `Copyright (c) 2026 KhasMek`, covering our own code only.
- Not researched: the origin and terms of the small musicdsp.org, openaudio blog and CMSIS-derived snippets inside DaisySP's `dsp.h` and `whitenoise.h`. They ship under DaisySP's MIT licence upstream.
- gcc and Linux are untested until M1-D.

## M1-B result (2026-11-17)

- Added `src/core` (`ControlSource`, `ParamTable`, `AudreyVoice`) and `src/control/ScriptControlSource`, built as `audrey_core`, with four test programs. Six tests pass in Release and under ASan/UBSan, with no compiler warnings.
- The implementer found no numeric difference between the upstream parameter table and `docs/PARAMETERS.md`.
- The reviewer wrote its own reference from the upstream `main` and `Controls` files and compared it with `AudreyVoice` over 15 s with every knob moving, the switch toggling and an input signal: **1,440,000 samples, zero differing bits.** Changing one limiter constant by one unit in the last place made the comparison fail, so it is sensitive.
- No allocation in `process()`: 0 `operator new` calls over 12,000 control blocks. Locks and `malloc` were checked by reading only.
- Any frame count gives bit-identical output to a single call; controls are read exactly once per 4 samples.
- Review follow-ups applied and re-verified by the orchestrator: script event times past 42 s now land on the right control block; script numbers parse the same in any system locale.
- Measured: all knobs at default for 10 s peaks at 3.5e-5. Feedback at maximum for 30 s stays finite in every run; peaks 0.20 (others at minimum, default volume), 0.21 (others at half), 0.81 (others at maximum).

Open after M1-B:
- Nothing inside the repo yet pins `AudreyVoice` to the upstream logic; the reviewer's reference was out of tree. M1-C's parity test closes this.
- Denormal flush-to-zero is not set anywhere yet. It belongs to the platform layer (M2).

## M1-C result (2026-11-17)

- Added the tools `audrey-render`, `wavcheck` and `audrey-gen-input`, an in-repo reference harness (`tests/ref`, transcribed from the upstream firmware, linking nothing from `src/core`), a parity test, 22 parameter scripts and `scripts/make_reference.sh`. dr_wav v0.14.6 is vendored (MIT-0) for WAV I/O in tools and tests only.
- **Parity:** all 22 scripts render bit-identically through `AudreyVoice` (fed in irregular chunks) and through the reference harness. This closes the gap left by M1-B.
- **The parity test was shown to bite.** The implementer injected 6 bugs and the reviewer 21 different ones into scratch copies of the wrapper. Every bug that changes the sound was caught, down to a one-unit-in-the-last-place change in a limiter constant. The one real blind spot found (an input clamp at plus or minus 1.0) was closed with a hot-input script and re-tested.
- **Reference set:** 25 WAVs (22 renders, 3 generated inputs), 229 MB, so only `reference/SHA256SUMS` is committed and the WAVs are regenerated. Hashes are stable across runs on this Mac and are specific to this compiler and C library.
- **Render health:** no non-finite samples; DC at most 1.6e-5; highest peak 0.80 (all knobs at maximum); no clicks at any knob move or switch toggle (level-relative click ratio at most 9.2 near events, against a limit of 30).
- Nine tests pass in Release and under ASan/UBSan with no warnings. Re-verified by the orchestrator from a clean build after the review follow-ups, along with the hash check and the vendor check.
- Render speed: about 135 to 155 times real time on one core of this Mac.

What the renders show about the instrument (all from the original DSP, confirmed by parity):
- With default knobs and feedback raised, the drone is a fairly static pair of tones near 1070 Hz (left) and 1152 Hz (right), string harmonics selected by the 1 ms body delay and the highpass. Movement appears once body or reverb is in the loop.
- Left and right decorrelate completely once the loop oscillates, even with reverb at zero. `docs/SIGNAL_FLOW.md` is corrected.
- The loop starts to self-oscillate around feedback knob 0.5 (about -24 dB) at default settings. A good drone setting for tests: feedback 0.7, body 0.3, reverb mix 0.25, decay 0.5.
- The echo is outside the loop (bit-identical output with send at zero) and stays bounded at maximum repeats.
- The lowpass kills the drone below about 220 Hz at moderate feedback; at +12 dB it survives to 100 Hz.

Open after M1-C:
- The follow-up changes (the level-relative click detector, script-header handling in the tools) were verified by tests and by the orchestrator, but not by a second full review.
- The click detector cannot see a 0.1 step inside dense high-frequency drones such as `all_max`.
- Nobody has listened to any render yet. That is for the human.
- The switch's 8 ms debounce is not modelled, by design.

## M1-D result (2026-11-17)

- Added `scripts/build_linux_arm64.sh` and `docker/` (a Debian 13 arm64 image pinned by digest, the in-container steps, a WAV comparison program and an optional maths-library probe). No source, CMake or existing script needed changing.
- **gcc 14.2 / glibc 2.41 on Linux aarch64:** everything builds with no warnings; all nine tests pass in Release and under ASan/UBSan/LeakSanitizer; the parity test is bit-identical there for all 22 scripts; `process()` makes zero allocations under libstdc++.
- **Linux against macOS:** 14 of 22 renders bit-identical; the 8 that differ are explained by last-bit differences in a few maths-library results and by Apple clang rewriting `powf`. Details and consequences are in `docs/PLAN.md` section 6, item 5.
- Render speed in the container equals native (about 140 times real time on an Apple core). This says nothing about the Cortex-A53.
- Reviewed by a separate agent, which re-ran the script, reproduced the table with its own comparison, confirmed the explanation from the binaries' imports and the code, and found no uninitialised reads with valgrind on four renders and the smoke test.
- Review follow-up: the script could delete unrelated folders if `--out` pointed at a directory containing the repo. Fixed; it now refuses any output path inside or around the repo and any non-empty directory it did not create. The orchestrator re-tested three refusal cases and a full run on the real repo; `reference/SHA256SUMS` unchanged.

What M1-D does not tell us: the device's glibc (probably older than 2.41, so these binaries are not device binaries), anything about SDL2 on the device, or device CPU cost.

## M1 summary

The original DSP runs on the Mac and on Linux aarch64 without libDaisy, unmodified, at the firmware's 48 kHz and 4-sample control block. `AudreyVoice` reproduces the firmware's audio callback bit for bit against two independent transcriptions. 22 scripted renders are checked for NaN, DC, peak and clicks, and hashed. The human has listened to the renders and reports they sound right (2026-11-17).

Carried forward:
- M2 needs the control scheme details settled (tabs on L1/R1, feedback on L2/R2).
- Set flush-to-zero in the platform layer (M2) and regenerate the reference hashes when it lands.
- The switch's 8 ms debounce is not modelled.
- Device checklist items 1 to 9 are needed before M3. A probe app does not exist yet.
- Two questions to the human are still open: where the audio-input information came from, and whether a real unit or recordings are available for comparison.

## M2-A result (2026-11-17)

- Added the button control logic with no SDL: `ControlModel` (UI side: tabs, taps, holds, modifiers, with caller-supplied time), a wait-free single-producer single-consumer queue, the held-state word, and `ButtonControlSource` (audio side: applies commands and integrates ramps at the 12 kHz control rate). Built as `audrey_control`. All tuning numbers are in `src/control/ControlTuning.h`.
- 17 tests pass in Release and ASan/UBSan, the 8 control tests pass under ThreadSanitizer, and all 17 pass with gcc 14 on Linux aarch64 in both containers. No warnings. Reference hashes unchanged. Re-verified by the orchestrator from clean builds, including the Linux script (exit 0).
- With no input, output is bit-identical to the M1 voice at defaults. `read()` makes zero allocations.
- Review: the first version failed to build with gcc on Linux (a missing include); fixed, and the Linux build is now part of every M2 task's verification. The reviewer derived 14 groups of expected values by hand, all matching, and injected 21 bugs, of which the tests caught 18. The three misses are closed: a bare queue test under ThreadSanitizer (shown to fail when either queue ordering is weakened) and reset tests on parameters with non-zero defaults.
- Learned: ThreadSanitizer with Apple clang on arm64 does not see races in struct copies of 32 bytes or more, so the queue's ordering is tested with a 24-byte payload. The 72-byte command type itself cannot be race-checked on macOS.

Decisions the implementer made where the scheme was silent (all tested; for the human to judge when playing):
- L2/R2 use the same 250 ms tap-then-ramp delay as the d-pad, so a tap is exactly 0.5 dB.
- After a tab change or an X press/release, a held d-pad direction does nothing until released. L2/R2 keep ramping through a tab change.
- Opposite directions held together cancel.
- L2/R2 while A is held move the stored feedback unheard; the sound jumps to it on release.
- The frequency ramp is continuous; only taps are semitones.
- Full travel on a held d-pad takes about 2.9 s.

Notes for later tasks: M2-B must call `ControlModel::releaseAll` on focus loss and controller removal. Presets (M5) must save the stored feedback, not the frame value.

## M2-B result (2026-11-17)

- Added `src/platform` (`AudioRender`: the SDL-free callback body; `AudioHost`: the SDL audio device; `InputMap`: gamepad and keyboard to buttons; flush-to-zero; meters) and the app shell (`src/app/App`, `main`, `UiSnapshot`). The executable is `build/<dir>/audreybrick`. SDL is optional at configure time (`AUDREY_WITH_SDL`).
- On this Mac the device opens as requested: CoreAudio, 48 kHz, stereo, float32, 256 frames.
- Tests: 25 pass in Release and ASan/UBSan, 16 under ThreadSanitizer, 22 without SDL, and 22 in each Linux container (no SDL there). Reference hashes unchanged. Re-verified by the orchestrator from clean builds, including the Linux script (exit 0) and a silent self-test.
- The render path is bit-identical to `AudreyVoice` for buffers of 64, 256, 1024 and 5000 frames, and makes zero allocations. Flush-to-zero changed no sample over 20 s.
- Review: no blocking findings. The callback, shutdown order and thread sharing were read line by line; three injected bugs in the render path were all caught. Follow-ups applied: the self-test now ends on audio time, fails on undelivered events or unmet expectations, and its ctest asserts the expected feedback and volume values and output level (shown to fail when the feedback ramp rate is halved); no non-test code needs SDL newer than 2.0.18; trigger state is cleared on focus loss; flush-to-zero is checked every callback.
- **Callback CPU on the Mac:** a steady 0.75% of a 256-frame buffer when measured as thread CPU time. Wall-clock spikes of 10 to 60% occur and were shown to be the OS descheduling the thread, not our code. Audio-thread priority on the Brick is a question for the device measurements.
- SDL's dummy audio driver does not run at real-time pace (0.65 to 1.12 measured), so the self-test's scripted events follow audio time.

Open after M2-B:
- The human has a play-by-ear checklist; the window is blank until M2-C.
- Two self-test failure paths (timeout, undelivered event) are checked by reading only.
- If a keyboard key and the matching pad button are held together, the first release releases the button. Left as is.
- Not tested with a physical gamepad, only SDL's virtual controller.

## M2-C result (2026-11-17)

- Added `src/ui`: an SDL-free part (font atlas data, value formatting, meter ballistics, layout into a draw list with named panels) and an SDL renderer that draws the list. Text uses the public-domain `font8x8` bitmap font, vendored under `third_party/font8x8` (commit `8e279d2`), scaled by whole numbers. No font library is needed on the device.
- Screen, top to bottom: tab bar (L1/R1); feedback band (L2/R2) with a striped MAX state that keeps the stored value visible; switch box; the current tab's parameters with their d-pad arrows, large values and position bars; L/R output meters with peak hold and clip; a strip with all eleven values plus CPU; a status line; a footer explaining the global buttons, each lighting while held.
- Tests: 31 pass in Release and ASan/UBSan, 22 under ThreadSanitizer, 26 without SDL, 26 in each Linux container. Vendor check (43 files), reference hashes and the self-test pass. Re-verified by the orchestrator from clean builds, including the Linux script (exit 0). The orchestrator viewed five of the screenshots.
- Review: nothing blocking. Every displayed value was checked at minimum, default and maximum; 25 extra extreme-value renders showed no overlap or overflow; the accelerated renderer's output is pixel-identical to the software one. The reviewer found the layout tests too weak (seven wrong displays passed); they now tie each parameter to its arrows and strip cell, check every panel for overlap, and allow only Brick button names. All nine injected bugs now fail a test.
- Display fixes from review: consistent cents at half-semitones, larger LATCHED/HELD, a mark on the delay time when the switch scales it, cut names end in "..", faint text raised to at least 4.5:1 contrast.

Changes after the human saw it (2026-11-17), applied before the commit and re-verified by the orchestrator from clean builds:
- The five parameters with no unit (reverb dry/wet, reverb decay, delay mix, repeats, volume) were first changed to show the knob position as a percentage (that version is in the "Add the screen" commit). The human then chose the truthful form: they show the **plain engine value with two decimals** (dry/wet and mix 0.00 to 1.00, decay 0.20 to 1.00, repeats 0.00 to 1.50, volume 0.00 to 1.00), and **the bar is drawn from the same value** against the parameter's engine maximum, so number and bar agree. A tick on the repeats bar marks 1.00 (unity gain). Parameters with units still show engine units with the bar at knob position.
- Consequence: for the squared parameters (mix, volume) the bar moves slowly at the bottom and faster at the top while a button is held. That is the firmware's curve made visible.
- For those five the bar is quantised to the displayed hundredths, so a single fine step may not move bar or number.
- The "NOT USED ON THIS TAB" strip is removed; unbound directions are not shown.
- Human's answers: colours fine for now; "SHIFT" for X is right; percentages fine; meter range -36 to 0 dB fine.
- Not re-reviewed by a separate agent; the bug-injection table was not rerun after this last change.

Open after M2-C:
- The footer, strip names and several status words are 16 px, about 1 mm on the Brick's screen. Legibility must be checked on the device (M3).
- Renderer device-reset events are not handled (M3). A resized Mac window scales glyphs non-integrally; the Brick runs at exactly 1024x768.

## Pitch range finding (2026-11-17, from the human's first play test)

The human played M2-B on the Mac: generally working as expected, but the pitch range seemed limited to about an octave, unlike the hardware.

Measured by the orchestrator (scratch program on `AudreyVoice`, autocorrelation pitch of the left channel after 8 s, reverb mix 0): the frequency control's string pitch spans 20.6 to 523 Hz as in the firmware, but **with body at minimum the heard pitch stays near 960 to 1000 Hz for frequency 0.0 to 0.7 and jumps to about 1920 Hz above that**, at every feedback level tried. The 1 ms body delay sets the pitch, not the string. With body at maximum and feedback high, the output follows the string over most of the range (20.6, 28.5, 39.4, 54.5, 103.7, 521.7 Hz measured at the corresponding settings), with jumps to harmonics in places. Lowering the highpass does not change the body-at-minimum result.

Cause: the port starts every control at the firmware's registry initial values, which puts body at minimum. A hardware unit starts at its physical knob positions. This is original DSP behaviour at an unlucky start position, not a port bug.

Decision: choose a playable init patch by ear after the presets work (M5). Until then, raise body on the LOOP tab (hold Up) after starting the app.

## Log

- 2026-11-17: M0 source study completed. Docs written. `CLAUDE.md` given an "M0 corrections" block. Stopped for human approval.
- 2026-11-17: Human answered D1 to D7, asked for stereo-summed input with a gain control, and for tabs on L1/R1. M1 approved. Stopped at commit point 1.
- 2026-11-17: M1-A to M1-D implemented, reviewed and committed. The human listened to the renders and reports they sound great.
- 2026-11-17: Human approved the button mapping as a first version, named the input sources (mic, USB, file) and asked for a modulation tab after the plain port works on the Brick. M2 briefs written. Stopped at commit point 6.
