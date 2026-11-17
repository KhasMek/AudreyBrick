# Status

Maintained by the orchestrator. Last updated: 2026-11-17.

## Where we are

**M0 (source study) is complete and awaits the human's review. No application code exists. No implementation has been delegated.**

| Milestone | State |
|---|---|
| M0 source study | Done, pending human review |
| M1 host DSP | **M1-A, M1-B and M1-C done and reviewed.** M1-D (aarch64 Linux build check) not started; it needs the Docker daemon running |
| M2 host app | Not started. Blocked on the control scheme (D2) |
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
| 4 | M1-C render tool, parity test, reference set, reviewed | Ready to commit |
| 5 | M1-D aarch64 build check, reviewed | |

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

## Log

- 2026-11-17: M0 source study completed. Docs written. `CLAUDE.md` given an "M0 corrections" block. Stopped for human approval.
- 2026-11-17: Human answered D1 to D7, asked for stereo-summed input with a gain control, and for tabs on L1/R1. M1 approved. Stopped at commit point 1.
