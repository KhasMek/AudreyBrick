# Status

Maintained by the orchestrator. Last updated: 2026-11-17.

## Where we are

**M0 (source study) is complete and awaits the human's review. No application code exists. No implementation has been delegated.**

| Milestone | State |
|---|---|
| M0 source study | Done, pending human review |
| M1 host DSP | Briefs written (`docs/briefs/M1.md`). **Not started.** Blocked on approval and decision D1 |
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
| 1 | M0 docs and decisions | Ready to commit |
| 2 | M1-A vendoring and build skeleton, reviewed | |
| 3 | M1-B control abstraction and voice, reviewed | |
| 4 | M1-C render tool, parity test, reference set, reviewed | |
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
- `THIRD_PARTY.md`: not created yet; nothing is vendored. It is part of M1-A.

## Log

- 2026-11-17: M0 source study completed. Docs written. `CLAUDE.md` given an "M0 corrections" block. Stopped for human approval.
- 2026-11-17: Human answered D1 to D7, asked for stereo-summed input with a gain control, and for tabs on L1/R1. M1 approved. Stopped at commit point 1.
