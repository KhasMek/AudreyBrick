# Risks, decisions and device tests

Written at the end of M0 (2026-11-17). The on-device checklist is in `DEVICE_CHECKLIST.md`.

## 1. Risks

| ID | Risk | Assessment | Mitigation |
|---|---|---|---|
| R1 | **Sample rate** | Much smaller than feared: the firmware runs at 48 kHz, not 96 kHz. The remaining risk is that the Brick's audio path forces a different device rate (for example 44.1 kHz), or that the mic or a USB interface runs at another rate | Always run the engine at 48 kHz. Let SDL convert at the device boundary. Never retune the engine; the constants that are not rate-aware are listed in `SIGNAL_FLOW.md` section 3. Checklist items 4 and 11 |
| R2 | **CPU on the device** | Unmeasured. The engine runs about 180x real time on an M-series Mac, which suggests ample headroom on a Cortex-A53, but that is an estimate. Per-sample processing with a filter-coefficient update every 4 samples is cheap; the reverb and two 5-second delay lines are memory-heavy (about 2.5 MB), so cache behaviour matters | Measure with the probe app. Checklist item 7. Fallback: nothing cheap to cut without changing the sound, so a failure here would force a redesign discussion |
| R3 | **Runaway feedback and output level** | The loops saturate by design and cannot blow up numerically unless a NaN enters. The original limiter is not a hard ceiling. On headphones a sudden jump to full level is a hearing risk | Keep the original limiter. Add a separate switchable safety limiter with a true ceiling after it (M4). Start with volume at the default and feedback at minimum. Sanitise NaN at the input boundary and test extremes in M1 |
| R4 | **Acoustic feedback, internal mic plus speaker** | High. The input is summed at unity straight into a resonator followed by +14 dB of drive, and it is audible even at minimum feedback. Mic into speaker will howl | Input off by default. Warning on the input page. Mute the internal mic when the speaker is the output unless the user explicitly overrides, if the output route can be detected (unknown, checklist item 13). Input meter and gain always visible |
| R5 | **Input latency and duplex audio** | Unknown. The firmware's round trip is under a millisecond; the Brick's will be tens of milliseconds. The synthesis loop itself is internal and unaffected. Latency only delays the input, and lengthens any acoustic loop | Measure (checklist item 12). Choose buffer sizes afterwards. Do not promise that playing through the input feels like the hardware |
| R6 | **USB audio on the Brick** | Unknown. Stable KNULLI builds may lack USB audio capture; duplex is untested | Checklist items 10 and 11. If USB input is unavailable, ship with internal mic and WAV-file input only and say so |
| R7 | **Licence of the reverb and tone filter** | Real. `ReverbSc` and `Tone` were MIT-labelled at the pinned DaisySP commit but were moved to LGPL-2.1 by Electrosmith in 2023 because they derive from Csound. The reverb defines much of the sound and cannot be swapped without losing fidelity | Decision D1 below |
| R8 | **Attribution** | Two copyright lines exist for the same code (Synthux Academy 2026, Infrasonic Audio LLC 2022). The README credits RtMidi and RtAudio, which are not used. CREDITS.md links to a repo that no longer resolves | Ship both MIT notices, credit Nick Donaldson (Infrasonic Audio) and Roey Tsemah (Synthux Academy), and link the official repo. Do not credit RtMidi or RtAudio |
| R9 | **The name** | "Audrey II" belongs to the Synthux project. The panel also carries the Infrasonic Audio logo | Keep the working title. No Synthux or Infrasonic artwork. Decision D6 |
| R10 | **Audio input expectations** | The firmware's input is real but apparently unreachable on the stock kit (both jacks are outputs). There may be no reference for how it "should" sound | Implement exactly what the code does. Ask the human where the input information came from (Q1) |
| R11 | **Button control is slower than knobs** | Fast knob throws cannot be reproduced with held buttons | Momentary feedback-to-maximum, momentary switch, presets. Evaluate in M2 with the human |
| R12 | **Battery saver mutes audio after 5 minutes** | Reported for another app. A drone instrument is often left untouched for minutes | Checklist item 9. Keep the device awake or document the setting |
| R13 | **Undefined behaviour in reused code** | `WhiteNoise` relies on signed overflow; a compiler could miscompile it | Build with `-fwrapv`; the parity test would catch a change |
| R14 | **glibc floor** | Unknown device glibc; a build made in a newer container will not start | Checklist item 1 before choosing the build image |

## 2a. Decisions made by the human (2026-11-17)

| ID | Outcome |
|---|---|
| D1 | **(a).** The whole project will be open source. `reverbsc` and `tone` are treated as LGPL-2.1 and built as a separate shared library (`audrey_lgpl`) |
| D2 | Proposal accepted as a first test, with one change: **L1/R1 switch tabs** (as GrainBrick does), so feedback moves to **L2/R2**. One tab per section. Details to be settled in M2 |
| D3 | Yes: engine fixed at 48 kHz with 4-sample control blocks |
| D4 | Yes: reset defaults are the firmware's registry initial values |
| D5 | Yes: M1-D (aarch64 Linux build check) is part of M1 |
| D6 | No contact needed. Synthux and Nick Donaldson already know the human as someone who ports Audrey II. The working title stays until the human says otherwise |
| D7 | The corrections block in `CLAUDE.md` stays |
| New | **Input:** mix the left and right input channels to mono before the engine's unity-gain entry point, and add an input gain control. Implemented in the M2b input stage as `in = gain * (L + R)`, gain default 1.0. A plain sum keeps a left-only source identical to the original; the gain control covers stereo sources that sum hotter |
| New | **Commits:** the human commits. The orchestrator stops at each good commit point and proposes a short, plainly worded message (no `feat(...)` prefixes) |

## 2. Decisions needed from the human (as asked at the end of M0)

| ID | Decision | Recommendation |
|---|---|---|
| D1 | **How to treat `reverbsc` and `tone` (R7).** Options: (a) treat them as LGPL-2.1, publish the port's full source, and build them as a separate shared library so users can relink; (b) rely on the MIT label of the pinned commit; (c) replace them with different code | **(a).** It respects the upstream author's correction and costs little, since the port is open source anyway. (b) is legally doubtful because the code derives from LGPL Csound. (c) loses fidelity. This blocks M1-A |
| D2 | **Control scheme** (`CONTROL_SCHEME_PROPOSAL.md`, five questions) | As proposed. Blocks M2, not M1 |
| D3 | **Engine rate fixed at 48 kHz with 4-sample control blocks**, resampling only at the device boundary if needed | Yes |
| D4 | **Reset defaults** = the firmware's registry initial values | Yes |
| D5 | **Add an aarch64 Linux build of the DSP core to M1** (task M1-D). CLAUDE.md scopes M1 to the Mac, so this is a scope change | Yes; it is cheap and removes a risk early |
| D6 | **Contact Synthux Academy and Nick Donaldson** before any public release, and the final name | Yes, contact both. Worth asking them about the input (Q1) and the LGPL point at the same time |
| D7 | **Corrections to CLAUDE.md.** A short "M0 corrections" block was added so workers do not act on the 96 kHz and submodule statements. Review it | Keep |

## 3. Questions for the human

1. Where did the audio-input information come from? On the stock kit both jacks appear to be outputs. Is there a known hardware mod, and do you have access to a modified unit or recordings?
2. Do you have a real Audrey II, or reference recordings, for listening comparisons?
3. Which KNULLI build is on your Brick (stable or development)? It decides whether USB audio is worth testing now.
4. Will the port's source be public? (Assumed yes. It makes D1(a) straightforward.)

## 4. What needs a test on the Brick

Everything in `DEVICE_CHECKLIST.md`. The items that block design decisions: glibc version (build image), accepted output rate and stable buffer size, CPU headroom, button mapping and reserved buttons, screen-dim and mute behaviour, mic capture and duplex latency, USB audio, and whether the app can tell speaker from headphones.
