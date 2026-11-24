# Status

Maintained by the orchestrator. Last updated: 2026-11-17.

## Where we are

**M0 (source study) is complete and awaits the human's review. No application code exists. No implementation has been delegated.**

| Milestone | State |
|---|---|
| M0 source study | Done, pending human review |
| M1 host DSP | **Done and reviewed (M1-A to M1-D).** Awaiting the human's sign-off of the milestone |
| M2 host app | **Done and reviewed (M2-A, M2-B, M2-C): the app plays on the Mac with the full screen.** Awaiting the human's play test and sign-off of the milestone |
| M2b input path | **M2b-A (INPUT tab, file input) done and reviewed; awaiting the human's device run (`docs/INPUT_RUN_BRICK.md`).** M2b-B (mic and USB) follows that run |
| M3 device build | **The app runs on the Brick; two device runs done (2026-11-18).** Buttons, keep-awake, screenshots and sound confirmed by the human. M3-C (quit on Menu + Start) is done, reviewed and confirmed on the device in a third run. **The plain port runs on the Brick with its full control scheme.** Awaiting the human's choice of what comes next |
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
| 10 | Screen: plain engine values for the five unitless parameters | Committed |
| 11 | M3-A device build, device behaviour and the Port package, reviewed; firmware findings and the first-run checklist | Committed |
| 12 | M3-B fixes from the first device run (button swaps, CPU frequency logging, opt-in governor, signal hardening), reviewed; device results and the second-run checklist | Committed |
| 13 | M3-C: quit on Menu + Start, hold-to-quit removed; second-run results and the third-run checklist | Committed |
| 14 | M2b plan | Committed |
| 15 | M2b-A and M2b-A2: the input stage, the INPUT tab, file input with a hold-X pop-up browser (M2b-A3), crossfade, LOOP/ONCE and a 10-minute cap, reviewed; device checklist | Committed |
| 16 | M2b-B: live input (LIVE source, device chooser, capture ring with drift handling, howl safeguards), reviewed; device checklist | Committed |
| 17 | M2b-B2: the built-in mic on the direct hardware route (mono, mixer set and restored), no SYSTEM DEFAULT on ALSA, reviewed; confirmed on the Brick | Committed (de2fc16) |
| 18 | REC: recording the output to a 16-bit WAV on Start + Y, red border and REC readout (`docs/briefs/REC.md`), reviewed; device checklist `docs/RECORDING_RUN_BRICK.md` | Committed; confirmed on the Brick |
| 19 | MOD: four modulation slots (sine, triangle, square, four ramps, sample-and-hold, input envelope; bipolar or unipolar) on any setting (`docs/briefs/MOD.md`). MOD-A engine, then MOD-B tab | Implemented, reviewed and verified; two device runs done (work as expected); MOD-D gesture rework (the human's design) run on the device by the human: working as expected. Committed (`579be0f`) (`docs/MOD_RUN_BRICK.md`) |
| 20 | PRESET: presets as JSON files, a PRESET tab with a scrolling tab bar, Start + A quick save, user data moved to `/userdata/saves/audreybrick/` (`docs/briefs/PRESET.md`). PRESET-A store, PRESET-B tab, PRESET-C data folder | Built and verified on the host 2026-11-23 (92/92 Release, ASan and TSan clean, 68/68 without SDL, 25 + 12 reference hashes, Linux build OK, 314 port tests). A and B reviewed once each; C's one-time move was reviewed twice and then removed (PRESET-D, reviewed once); the fixes after each last review are verified by tests but not re-reviewed. Device run pending: `docs/PRESET_RUN_BRICK.md` |

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

## M3-A result (2026-11-18)

- **Firmware facts** were read from the image the human runs (KNULLI Scarab 20260511): `docs/PLATFORM_NOTES.md` section 10. Build limit: symbol versions up to GLIBC_2.39 and GLIBCXX_3.4.32. SDL 2.32.8 with ALSA audio and only the Mali video driver.
- **Device build:** Debian 12 arm64 container (`scripts/build_device.sh`), GCC 12.2. The binary needs at most GLIBC 2.34, GLIBCXX 3.4.29, CXXABI 1.3.11; the script fails if a limit is exceeded. SDL is linked by soname and not bundled. `libaudrey_lgpl.so` ships separately in `libs.aarch64/`.
- **`std::tanf`:** GCC 12's library does not declare it and the unmodified upstream `DSPUtils.h` calls it. `src/shim/std_cmath_compat.h` supplies it for libstdc++ older than 14 (orchestrator's decision). Arithmetic is unchanged: parity passes on Debian 12, whose renders match the same 17 of 25 macOS reference files as Debian 13.
- **Tested against the device's own libraries** (`scripts/test_on_knulli_rootfs.sh`, a container whose root is extracted from the firmware image): the device's loader resolves everything; the app logs "linked 2.32.8"; the audio, controls and logging self-test passes with the test-only `--no-video` option; the launcher works from any directory, with spaces in the path and without the executable bit; SIGTERM, SIGINT, SIGHUP and a killed launcher all end the app cleanly.
- **Not testable off the device:** the display path (Mali driver, EGL, renderer) and ALSA output. Without a framebuffer the app exits in milliseconds with SDL's error in both logs.
- **Device behaviour added to the app:** `--fullscreen`; quit on SDL_QUIT, Alt+F4, Escape, and (at the time; replaced by Menu + Start in M3-C) Start + Select held one second; a diagnostic log (`--log`); renderer reset handling; integer scaling only when the output is at least 1024x768; an overrun counter (one atomic increment on the audio thread, nothing else new there).
- **Launcher** (`packaging/AudreyBrick.sh`): self-contained bash, no PortMaster. Keeps the last 8 runs' logs, records device state at start (battery-saver settings, CPU governor, sound cards, capture devices), forwards signals, and keeps the device awake by creating `/var/run/battery-saver/audreybrick.pause` while the app runs (the idle watcher's hooks skip dim, mute and suspend while any `*.pause` file exists; read from the firmware's scripts, untested on the device). `AUDREY_KEEP_AWAKE=0` turns that off.
- **Package:** `scripts/make_port.sh <image>` builds, tests the build in the device root, packages, and tests the package itself; it prints hashes so the packaged binary is the tested one. Output in `dist/` (git-ignored).
- Tests: 39 in Release and ASan/UBSan, 30 under ThreadSanitizer and without SDL, 30 in each Debian 13 container, 39 in the Debian 12 device build. Vendor check and reference hashes pass. Re-verified by the orchestrator from clean builds, including `make_port.sh` (exit 0).
- Review: no blocking findings. The reviewer checked the packaged binary's symbols against the device's own library files, ran the launcher in 14 situations of its own, and read the firmware's launch, hotkey and idle scripts. Its six should-fix items (package the tested binary, keep more logs, forward signals, avoid cropping on an unexpected screen size, write launcher failures into the log folder, log more device state) were applied; the follow-up round was verified by the orchestrator, not re-reviewed.

Open after M3-A:
- Everything in `docs/FIRST_RUN_BRICK.md` is untested until the human runs it.
- `README.txt` in the package says "source repository: to be published"; resolve before any public distribution.
- (Superseded in M3-C: quit is now Menu + Start.)
- The device's SDL also has an OSS `dsp` audio driver, not listed in the platform notes' table.

## First device run and M3-B (2026-11-18)

**First run (package 7af286f-dirty), two sessions on the Brick.** The app started in 0.4 s, drew at a steady 60 frames per second through the Mali driver and GLES2, played through ALSA at 48 kHz with the 256-frame buffer it asked for, recognised every button, and quit cleanly with Start + Select both times. No audio callback overran. Details: `docs/PLATFORM_NOTES.md` section 11. The human reported A and B swapped, CPU spikes, and possibly one crackle.

**M3-B, from that run:**
- **Button swaps.** `--swap-ab` and `--swap-xy` in `InputMap`, both on by default from the launcher. A/B was reported by the human; X/Y is inferred from the firmware's controller tables (about 85 % confident) and was not checked in the first run. If KNULLI's own per-port "SWITCH A/B and X/Y" is on for a launch (flag file `/var/run/ports_xbox_layout.flag`), the launcher turns both swaps off so they cannot cancel.
- **Measurement.** Each 10-second stats line now carries the CPU frequency per policy (current, and the window's minimum and maximum) and a six-bucket histogram of callback load. The histogram is the only new work on the audio thread: one relaxed atomic update per callback, covered by the allocation test.
- **Governor: opt-in.** The launcher can set a governor while the app runs (`AUDREY_GOVERNOR`) and restore it afterwards, but the default is `keep`. Orchestrator's decision on the reviewer's advice: the first run had no overruns, the slow-CPU explanation is unconfirmed, `performance` costs battery, and KNULLI's own per-port POWER MODE does the same with its own restore. Restores happen only if the governor is still the value we set.
- **Signals.** The device's bash 5.2.21 aborts if a trap runs while it parses a command substitution; the launcher's own shell now runs none, and its traps only set a flag before the app starts. The app handles SIGTERM, SIGINT and SIGHUP itself from before `main` and always exits through its normal shutdown. Sweeps in the device-root container: 260 launcher runs with 0 aborts and 0 unclean end states; 400 app runs with exit actions run in 399 (one signal landed inside the dynamic loader before any program code; the launcher covers that case).
- Tests: 43 in Release and ASan/UBSan, 34 under ThreadSanitizer, 33 without SDL, 33 in each Debian 13 container, 43 in the device build, 202 device-root checks. Vendor check and reference hashes pass. Re-verified by the orchestrator from clean builds, including `make_port.sh` (exit 0); the packaged binary's hash equals the tested build's.
- Review: nothing blocking. The reviewer ran about 25 failure and hostile-input scenarios against a fake sysfs tree (the launcher never wrote outside the governor files), found the bash abort and the unconditional-restore problem, and worked out the X/Y mapping from the firmware. Its findings were applied; that follow-up round was verified by the orchestrator, not re-reviewed.

Open:
- Everything in `docs/SECOND_RUN_BRICK.md`: the four face buttons, the system exit path, keep-awake over six minutes, and the governor comparison.
- A USB pad with Xbox-style labels would have its A/B and X/Y swapped by our defaults.
- The swap applies per launch; there is no on-screen setting for it.

## Second device run and M3-C (2026-11-18)

**Second run (package 9b9d7db-dirty).** Results in `docs/PLATFORM_NOTES.md` section 12. In short: all four face buttons now match their labels; keep-awake works (12 minutes untouched, no dim or mute); the slow-CPU explanation is confirmed (408 MHz and 32 to 36 % callback load at the default governor, 1416 MHz and 11 to 12 % in KNULLI's High Performance mode); 9 callbacks overran in about 15 minutes at the default governor and none in High Performance, with nothing audible. The human finds the small text acceptable and decided to leave the default governor alone for now.

**System hotkeys, worked out from the firmware and confirmed by the human.** KNULLI's exit for any game is Menu + Start pressed twice quickly (a soft reset that sends SIGTERM); its screenshot is Start pressed while Select is held. Both work with our port. Our Start + Select hold-to-quit collided with the screenshot.

**M3-C:**
- Quit is Menu + Start, one press, handled by the app (`src/app/MenuQuit`); Menu is not a control-model button.
- The hold-to-quit feature and its banner are removed. Select + Start has no function in the app.
- An in-app screenshot (Start + X, own PNG writer) and an evmapy `.keys` file were built and then removed at the human's decision: KNULLI's screenshot works, and the app handling Menu + Start itself makes the helper unnecessary. Nothing of either remains.
- A `MENU+START QUIT` hint was added to the status line and then removed at the human's request (it looked out of place and cut the controller name; Menu + Start is the system default). The screen has no quit hint.
- Event routing between the input map, the control model and the quit logic now lives in `src/app/InputRouter`, which is tested with synthetic SDL events.
- Review: no blocking findings; the reviewer confirmed the removal left nothing behind and that a SIGTERM arriving during shutdown is harmless. It injected three bugs into the App-level wiring that no test caught; with `platform_router_test` each of them now fails a test. That follow-up round was verified by the orchestrator, not re-reviewed.
- Tests: 44 in Release and ASan/UBSan, 35 under ThreadSanitizer, 33 without SDL, 33 in each Debian 13 container, 44 in the device build, 206 device-root checks. Vendor check and reference hashes pass. Re-verified by the orchestrator from clean builds, including `make_port.sh` (exit 0); the packaged binary's hash equals the tested build's.

**Third run (package 401ae63-dirty), three sessions.** Each ended with `exit: Menu + Start` and a clean shutdown, in both press orders. The KNULLI screenshot the human sent shows the normal screen with no banner. The human reports everything working as expected.

Open:
- What a habitual double Menu + Start press does after the app has already closed was not reported.
- The M5 "freeze" slot can no longer use Start alone or Select + Start; see `CONTROL_SCHEME_PROPOSAL.md` section 00.

## M2b-A result (2026-11-18)

- New `src/input/` (no SDL): `InputStage` on the audio path (source state, loop player with a 10 ms crossfade, fades at start and change, a smoothed gain, the input peak), a windowed-sinc `Resampler`, `InputFiles` (folder listing, WAV decoding with dr_wav, `L + R` mono mix, conversion to 48 kHz, a 5-minute cut), an `InputLoader` thread, and `InputController` for the tab. `AudioRender` calls the stage and passes its buffer, or `nullptr` when the source is off, to the voice.
- The INPUT tab (sixth), an always-visible IN bar while a source is active, `--input-dir`, and `data/input/` in the package. Files go in `roms/ports/audreybrick/data/input/` on the card.
- **Off is bit-identical** to the app without the stage, for buffers from 1 to 5000 frames, including after a file has played and with the gain moving. Checked by the project's test and independently by the reviewer.
- **The file path is exact:** what the voice receives equals the file's mono mix, looped, apart from the documented fades (the reviewer's own model matched bit for bit). No step at the loop point larger than the signal's own slope.
- **Audio thread:** zero allocations or frees while playing, swapping files and ramping gain. Buffers pass to the audio thread through a single-slot atomic exchange and come back through an SPSC queue to be freed on the UI thread. ThreadSanitizer clean under millions of swaps.
- **Resampler:** level within 0.001 dB; tones above 24 kHz suppressed by about 100 dB (reviewer's measurement). A 5-minute 192 kHz stereo file streams through in about 2 s on the Mac with 58 MB peak memory.
- **Bad files:** about 30 kinds refused with a short reason or loaded sanely; nothing outside the input folder is read; samples are clamped to plus or minus 8 at load.
- **Cost:** about 2 microseconds per 256-frame callback on the Mac (0.04 % of the buffer). The device figure is not measured yet.
- Tests: 54 in Release and ASan/UBSan, 46 under ThreadSanitizer, 40 without SDL, 40 in each Debian 13 container, 54 in the device build, 222 device-root checks. Vendor check and reference hashes pass. Re-verified by the orchestrator from clean builds, including `make_port.sh` (exit 0); the packaged binary's hash equals the tested build's.
- Review: no blocking findings; 17 of 18 injected bugs caught (the other was in dead code, since removed). Follow-ups applied: the FILE row names the file that is actually playing when a load is pending or failed; no exception can escape the input path; clearer failure reasons; non-ASCII file names shown with "?"; the folder scan is timed in the log. That round was verified by the orchestrator, not re-reviewed.

Open after M2b-A:
- Not yet on the device: folder scan time on the SD card, load times, the callback load with a file playing, legibility of the two small status lines in the FILE row.
- For the human to judge: a file change fades out then in (a brief gap) where a crossfade is possible; X + left/right does not repeat while held.
- Nothing about input is remembered between launches.

## Pitch range finding (2026-11-17, from the human's first play test)

The human played M2-B on the Mac: generally working as expected, but the pitch range seemed limited to about an octave, unlike the hardware.

Measured by the orchestrator (scratch program on `AudreyVoice`, autocorrelation pitch of the left channel after 8 s, reverb mix 0): the frequency control's string pitch spans 20.6 to 523 Hz as in the firmware, but **with body at minimum the heard pitch stays near 960 to 1000 Hz for frequency 0.0 to 0.7 and jumps to about 1920 Hz above that**, at every feedback level tried. The 1 ms body delay sets the pitch, not the string. With body at maximum and feedback high, the output follows the string over most of the range (20.6, 28.5, 39.4, 54.5, 103.7, 521.7 Hz measured at the corresponding settings), with jumps to harmonics in places. Lowering the highpass does not change the body-at-minimum result.

Cause: the port starts every control at the firmware's registry initial values, which puts body at minimum. A hardware unit starts at its physical knob positions. This is original DSP behaviour at an unlucky start position, not a port bug.

Decision: choose a playable init patch by ear after the presets work (M5). Until then, raise body on the LOOP tab (hold Up) after starting the app. **Withdrawn by the human on 2026-11-23:** no init patch; the start-up state stays as it is.

## Future features (requested, not scheduled)

- **Streaming file input from the card** (human, 2026-11-18): removes the length cap on input files. Needs a reader thread feeding the audio thread, with the loop point and sample-rate conversion done on the fly. Until then files are loaded whole and cut at 10 minutes.
- **Recording the output to a file** (human, 2026-11-18): see `docs/briefs/M2b.md`, "Recording". Planned after M2b.
- **Modulation tab** (human, 2026-11-17): after the plain port is signed off.

## Log

- 2026-11-23: The human ran the MOD-D build (working as expected; the `X SLOT` hint was removed from the TARGET row on request) and committed modulation. Presets designed with the human: one JSON file per preset holding the knobs, the switch latch and the modulation slots, nothing about the input; numbered names; a PRESET tab with a scrolling tab bar; right loads, X/Y + held right overwrite/delete; Start + A saves from any tab; no init patch; user data moves to `/userdata/saves/audreybrick/` (confirmed in the image: KNULLI creates `/userdata/saves`; the ports system lists only `.sh`/`.squashfs`) with a one-time move, so updates cannot delete it. Brief written; PRESET-A started.
- 2026-11-23: **The one-time move described in the next entry was removed the same day (PRESET-D, the human's decision):** the human is the only user and their card was already moved. The launcher now only chooses the data folder, never touches an old `audreybrick/data/` while `saves/audreybrick` is in use, and logs one notice if that folder still holds files. Reviewed once (nothing blocking); the review's wording and checklist-order findings fixed by the orchestrator. Open from that review: pruning can delete a hand-made file named like a numbered log, a reset run counter makes new logs be pruned at once (both logs only, both older than this change), and rotation in a fallback `data/` has no test.
- 2026-11-23: Presets implemented (PRESET-A, B, C). The data-folder move was reviewed twice because it handles the user's recordings: the first review found a path that could delete a complete file, and the move was redesigned; the second found a log overwrite, a file left hidden on a FAT target and two untested protections, all fixed. Killing the launcher before each of 2263 commands, on one file system and across two, loses nothing. Known limit: a folder whose name FAT refuses stays in the old `data/` (cross-partition only). One-time warning for this update: do not replace the old `audreybrick` folder wholesale; back up `data/` first.
- 2026-11-21: The human ran the MOD build on the Brick: working; the tab's UX needs rework after more testing (switching slots felt unintuitive). MOD-C on the human's request, implemented, reviewed and verified: the envelope follows the input before the gain (works at -inf dB; consequence: an envelope on the input gain with positive depth boosts a turned-down input much harder, documented with a headphones warning), four more shapes (ramp up/down, exponential ramp up/down), and a per-slot polarity (bipolar/unipolar) as a fifth row (the orchestrator's design, to be judged on the device). The post-review fixes were not re-reviewed.
- 2026-11-21: The human ran the MOD-C build: working well. MOD-D, the human's rework of the tab: the SLOT row is replaced by a TARGET row (left/right step the target, wrapping); holding X lights the four slot cards, dims the rows and makes left/right select the slot (wrapping); the cards are dimmed otherwise with the selected one framed; the cursor row is kept across slots; the target pop-up is removed; Y + left/right on TARGET sets NONE and Y + X + left/right resets the whole slot. Reviewed (nothing blocking); the fixes after the review (dimmed active cards brighter, a Y held from another tab no longer resets, target repeat capped at one step per update, checklist step 6) were verified by builds but not re-reviewed. Engine unchanged.
- 2026-11-20: MOD-B (the MOD tab) implemented, reviewed and verified: a seventh tab with four slot cards, the hold-X target list, violet tags and moving-value markers on the other tabs, an effective input meter. Review fixes: the input-gain taper no longer resets on a zero offset (envelope ducking let attacks through), strip tags always shown, depth snaps to its grid so 0.0% means off, Y stops ramps. Accepted: with a slot on the input gain a gain change can ramp up to about three times faster than unmodulated. The post-review fixes were not re-reviewed. Stopped at the commit point for the human's device run.
- 2026-11-20: MOD-A (modulation engine) implemented, reviewed and verified: Release 81, ASan+UBSan 81, TSan 73, no-SDL 60 tests; 25 reference hashes unchanged, 17 of 25 on Linux as before; a separate modulation set of 8 renders in `reference/mod`. Findings: audio-rate modulation of the two filter cutoffs drove the original's filters to a stuck NaN, so modulated cutoffs are limited to 0.035 octave per control block (largest limit that never failed in a 7190-case scan: 0.075); stepped modulation can overshoot full scale for a few samples (peak up to 1.74, bounded by the original's limiter; left for the M4 limiter decision); an envelope on the delay switch is a gate with no threshold. Cost on the Mac: +0.9 % idle, +14 % with four audio-rate slots (needs measuring on the Brick). MOD-B (the tab) started.
- 2026-11-20: The human decided to build modulation before presets (presets must store it; presets are not in the original either, so the "new features after sign-off" rule is not a reason to wait) and set the design: four slots, any setting as target, sine/triangle/square/sample-and-hold plus an input envelope follower, rates from slow drift to audio rate, targets chosen with the hold-X pop-up. Orchestrator finding: the original smooths every knob (7 to 145 ms), so modulation is applied after that smoother in `AudreyVoice`; body and delay time stay slow because of smoothers inside the vendored engine. MOD-A started.
- 2026-11-20: Recording device run: six recordings, no drops, longest write 28 ms (`PLATFORM_NOTES.md`, "Recording run"); committed. The human asked for the input gain to be kept per source within a session (FILE, and each capture device by name; first open of a device still at -inf). Implemented, reviewed (nothing blocking; the FILE gain-before-source ordering fix and howl guidance applied by the orchestrator and confirmed with the reviewer's probe), verified: Release 72/72, ASan/UBSan 72/72, TSan 64/64, no-SDL 52/52, Linux 52/52, device build 72/72, rootfs checks 234 pass. Open: two quick opens can give the first a 10 ms fade-in. Device run on 2026-11-20: gain memory works (12 restores, mic and USB independent) and a recording stopped by quitting was saved cleanly (`PLATFORM_NOTES.md`). Ready for the human to amend the recording commit.
- 2026-11-19: REC implemented, reviewed (nothing blocking) and verified from clean builds: Release 72/72, ASan/UBSan 72/72, TSan 64/64, no-SDL 52/52, Linux 52/52 (17 of 25 references), device build 72/72, rootfs checks 234 pass. Accepted as built: Y-before-Start on INPUT with X held still toggles LOOP/ONCE; readout under the feedback bar; limits in MiB. Open: the border-drawn-last screenshot test is vacuous. Stopped at commit point 18 for the device run.
- 2026-11-19: The human chose the recording design: Start + Y, 16-bit, no length cap (only the WAV limit and card space), red border plus REC time. Brief written (`docs/briefs/REC.md`); implementation started.
- 2026-11-19: Built-in mic device run: the direct route, the mixer set and restore, and USB by name all work on the Brick (`PLATFORM_NOTES.md`, "Built-in mic run"). M2b is confirmed on the device; commit point 17 is ready.
- 2026-11-19: M2b-B2 (built-in mic: direct hardware route through `AUDIODEV`, mixer set and restore with `amixer`, mono, no SYSTEM DEFAULT on ALSA) implemented and reviewed (nothing blocking; follow-ups applied and verified by the orchestrator, not re-reviewed). Orchestrator's clean runs: macOS Release 64/64, ASan/UBSan 64/64, TSan 56/56, no-SDL 46/46, Debian 13 containers 46/46 each, vendor and reference checks OK, `make_port.sh` exit 0 with 234 PASS and 0 FAIL. None of the built-in mic route has run on the device. Commit point 16 (M2b-B) was committed by the human as `8b45e8c`; this is commit point 17.
- 2026-11-19: Live-input device run: USB capture works; the built-in mic does not (results and diagnosis in `PLATFORM_NOTES.md`, "Live input run and capture diagnosis"). The human allowed adb for the diagnosis, with a rule to ask before anything further on the device. Brief M2b-B2 (direct hardware route, mixer set-up, no SYSTEM DEFAULT on ALSA) written and delegated.
- 2026-11-19: M2b-B (live input) implemented and reviewed three times (full review, a review of the fix round, a confirmation review). Fixed along the way: automatic and settable capture buffering for bursty delivery, crossfaded repeats for a slow capture clock, the hold fade taken after gain, the stage never reads before a session's first sample, load order in `pull()`. The confirmation review found nothing to fix before the device run. Deferred minors: `liveRunning_` survives a fade-out that ends inside a chunk when LIVE returns in the very next pull without a new session (stage API only; the app always closes the device); two stale comments about the raise rule in `InputStage.h` / `.cpp`; one late capture callback can double the live target for the session. Orchestrator's clean runs: macOS Release 63/63, ASan/UBSan 63/63, TSan 55/55, no-SDL 45/45, Debian 13 containers 45/45 each (17 of 25 references matching), vendor and reference checks OK, `make_port.sh` exit 0 with 232 PASS and 0 FAIL. `CaptureHost` has run only against SDL's dummy driver. Stopped at commit point 16 for the device run (`docs/LIVE_INPUT_RUN_BRICK.md`).
- 2026-11-19: The human ran file input on the Brick: it works as expected (results in `PLATFORM_NOTES.md`, "File input run"). Commit point 15 committed. The human has a USB audio device for testing. Brief M2b-B (live input) written and delegated.
- 2026-11-18: The human tried file input on the Brick and asked for a 10-minute cap, a crossfade between files, a scrollable file list that does not auto-load, and LOOP / ONCE play modes. Brief M2b-A2 written and delegated.
- 2026-11-18: M2b-A2 implemented and reviewed. The review found one blocking bug (a click when an ended ONCE file restarted during a file crossfade); fixed, with a random-operation fuzz test added. Also from the review: a superseded load is cancelled, no new load starts while the stage holds two buffers (at most two 10-minute buffers, 230 MB, alive at once), exact progress for equal-length files, a replaced file is reloaded. The fixes were verified by the orchestrator but not sent back for a second review. Orchestrator's clean runs: macOS Release 56/56, ASan/UBSan 56/56, TSan 48/48, no-SDL 42/42, Debian 13 containers 42/42 each (17 of 25 references matching), vendor and reference checks OK, `make_port.sh` exit 0 with 222 PASS and 0 FAIL. Stopped at commit point 15 (M2b-A and M2b-A2 together) for the device run.
- 2026-11-18: The human found M2b-A2's in-tab list far too small and asked for X + left/right stepping with load on release, and X + up/down opening a pop-up list. Brief M2b-A3 written, implemented and reviewed (nothing blocking). Review follow-ups applied: returning to the playing file cancels a pending load, the counter matches the name shown, failures name the file, the press that opens the list does not load, no repeat backlog, the old buffer is freed before a new load starts, a status line in the pop-up. The follow-ups were verified by the orchestrator but not re-reviewed.
- 2026-11-17: M0 source study completed. Docs written. `CLAUDE.md` given an "M0 corrections" block. Stopped for human approval.
- 2026-11-17: Human answered D1 to D7, asked for stereo-summed input with a gain control, and for tabs on L1/R1. M1 approved. Stopped at commit point 1.
- 2026-11-17: M1-A to M1-D implemented, reviewed and committed. The human listened to the renders and reports they sound great.
- 2026-11-17: M2 (A, B, C) implemented, reviewed, played by the human and committed. The human chose to get it running on the Brick before the input path; the firmware image (Scarab 20260511) was downloaded and inspected, findings in `docs/PLATFORM_NOTES.md` section 10.
- 2026-11-18: The human asked for audio input next and for output recording to be added to the plan. M2b-A implemented, reviewed and verified. Stopped at commit point 15 for the device run.
- 2026-11-18: Third device run: Menu + Start quits cleanly; KNULLI screenshots are clean. The human asked for the quit hint to be removed; done. Stopped at commit point 13.
- 2026-11-18: Second device run: buttons, keep-awake, screenshots and the governor question all settled. The human chose Menu + Start for quitting and no in-app screenshot. M3-C in progress.
- 2026-11-18: The human ran the package on the Brick: it works. M3-B (button swaps, frequency logging, opt-in governor, signal hardening) implemented, reviewed and verified. Stopped at commit point 12 for the second run.
- 2026-11-18: M3-A implemented, reviewed and verified. Package in `dist/`. Stopped at commit point 11 for the human's first run on the Brick.
- 2026-11-17: Human approved the button mapping as a first version, named the input sources (mic, USB, file) and asked for a modulation tab after the plain port works on the Brick. M2 briefs written. Stopped at commit point 6.
