# AudreyBrick (working title)

A port of Synthux Academy's **Audrey II** "horrorscape" feedback synthesiser from the Daisy Seed to the TrimUI Brick running KNULLI.

@docs/PLATFORM_NOTES.md

Workflow and roles are in `docs/ORCHESTRATION.md`.

## M0 corrections (source study, 2026-11-17, upstream commit `e5ab799`)

These override the statements below where they conflict. Details: `docs/SOURCE_STUDY.md`, `docs/PARAMETERS.md`, `docs/SIGNAL_FLOW.md`.

- The firmware runs at **48 kHz with a 4-sample block**, not 96 kHz. [confirmed, code]
- Audio input **is in the code**: mono (left codec input), unity gain, summed into both strings with the noise. No control affects it. [confirmed, code]
- Submodules are **libDaisy and DaisySP only**. RtMidi and RtAudio are not used. [confirmed]
- Code order is string, overdrive, lowpass, highpass, **then** reverb, then the body delay. The echo's feedback tops out at 1.5 and is soft-clipped. [confirmed, code]
- The developer is Nick **Donaldson** (Infrasonic Audio). Two MIT copyright lines exist for the same code: `2026 Synthux Academy` (official repo) and `2022 Infrasonic Audio, LLC` (archive copy). Keep both. [confirmed]
- DaisySP's `ReverbSc` and `Tone`, both used by the firmware, are **LGPL-2.1** in current DaisySP. Do not vendor them until the human has decided how (`docs/RISKS.md`, D1). [confirmed]

## What Audrey II is (from public sources, to be verified against the code)

- A droning feedback instrument. Press coverage describes it as having no MIDI or CV, but **the human says the firmware also supports audio input, and that input must be part of the port.** The README does not describe it, so how the input enters the signal chain (into the string, the feedback loop, or the delay) must be found in the code. [human-reported, unverified in code]
- Core: a Karplus-Strong string model fed a constant stream of inaudible noise, so nothing sounds until the outer feedback gain rises. Chain per the official README: string, soft-clip distortion, reverb, gentle linear lowpass and highpass EQ, a long "body" delay fed back into the string, and a final delay whose feedback can grow without limit while saturating. Pitch is set by the string delay length. [confirmed, README]
- Hardware: Daisy Seed (Cortex-M7, 480 MHz, 64 MB SDRAM, 96 kHz / 24-bit audio), 11 knobs and a switch, stereo out. [reported, press coverage]
- Credits: collaborative project, developed by Nick Donadson (Infrasonic Audio) and designed by Roey Tsemah (Synthux Academy). The repo has a `CREDITS.md`. [reported]
- **Official repo:** https://github.com/Synthux-Academy/Audrey-II/ . Licence: **MIT** (the README says so and points to `LICENSE`). Layout seen on the repo page: `FeedbackSynth_main.cpp`, `Source/`, `doc/`, `lib/` (submodules: DaisySP, libDaisy, RtMidi, RtAudio), `Makefile`, `CREDITS.md`, `LICENSE`. Pinned submodule versions, the sample rate and the full control list were not visible. The repo has no published releases. The README links prebuilt firmware (`audrey-ii-v3.bin`) hosted in the separate `simple-designer-instruments` repo. [confirmed from the repo page; details unknown]

## Goal

A faithful port that sounds like the original, controlled with the Brick's buttons. Fidelity comes first. New features come only after the port is signed off by the human.

**Language:** C++17, as the original. **Target:** KNULLI on TrimUI Brick (aarch64), packaged as a PortMaster-style port. **Dev:** macOS on Apple Silicon.

## Port strategy

1. **Separate the DSP from the hardware.** The original mixes DSP with Daisy hardware code (libDaisy: ADC knob reads, GPIO switch, audio callback). We keep the DSP and replace everything hardware-facing with our own platform layer.
2. **Reuse DaisySP.** DaisySP is a hardware-independent DSP library (MIT). Vendor the version the firmware uses. Verify that it builds on macOS and on aarch64 Linux without libDaisy.
3. **Control abstraction.** Introduce a `ControlSource` interface that replaces the ADC knobs. On the Brick it is backed by button input and a parameter page system. On the host it can be backed by a keyboard, a gamepad, or a scripted parameter file for tests.
4. **Platform layer.** SDL2 audio callback and input, plus a simple UI. Same real-time rules as the platform notes.
5. **Sample rate.** The firmware reportedly runs at 96 kHz. Delay lengths, filter coefficients, reverb settings and anything tuned in samples depend on the sample rate. Either run the DSP at the firmware's rate, or port every rate-dependent constant to 48 kHz and re-verify the tuning and feel. Decide only after measuring the CPU cost on the device and after confirming which output rates the Brick's audio path accepts.

## Audio input (required)

The port must support the original's audio input path. Open questions for the code study: where the input enters the chain, whether it is mono or stereo, what gain staging and level controls exist, and whether any knob or the switch changes the input routing.

Brick-specific constraints, none of which we have tested:
- The Brick has only a **mono internal mic and no line-in**. A synth or line source needs a USB-C audio interface. Which builds support USB audio, and whether input and output can run together with acceptable latency, are open questions (see the platform notes).
- **Acoustic feedback risk.** Internal mic plus speaker plus a runaway-feedback instrument can howl. Default to a safe state: input off or at low gain until the user enables it, a visible input meter and gain control, and a clear warning on the input screen about speaker use. Consider auto-muting the input when the speaker is active. The output limiter matters even more with input enabled.
- Input latency interacts with the feedback loops. Measure it on the device before deciding buffer sizes.
- Provide a mono-to-stereo policy and an input gain setting. Keep the audio thread's input handling allocation-free like everything else.
- Input source selection (internal mic, USB, off) lives on its own page.

## Controls: the central design problem

Audrey II has 11 knobs and a switch, and the Brick has digital buttons only. We do not yet know what each knob does. The first job is to read the source and produce a complete parameter table (name, range, curve, default, what it affects).

Design rules once the table exists:
- The big **feedback** control is the heart of the instrument and must be playable in real time. It needs the most direct mapping, for example a dedicated pair of buttons for gradual change, plus a momentary "feedback to maximum" button. (The designers published a hardware mod that does exactly that: a button that shorts the feedback control to maximum while held.) [reported]
- Group the other parameters into pages, one tab per group (for example string, body, filter and EQ, tape delay, reverb). L2 / R2 switch pages. The d-pad selects a parameter and changes its value. Show all current values on screen at all times.
- Hold-to-fine-adjust and hold-to-reset-to-default.
- Parameter changes must be smoothed, since the original's knobs are continuous and smoothed by the hardware read path.
- Save and load presets. Include a "freeze current state" preset slot.

## Behaviour to preserve

- Audio input behaves as in the original (see the audio input section).
- Silence until feedback rises, then thick drones, runaway tape feedback that saturates but does not blow up. Add a safety limiter on the output, clearly documented and switchable, for the speaker and headphones. The original's output stage should be matched first and the limiter evaluated separately.
- Stereo output and its widening behaviour.
- Startup state, and any behaviour that depends on knob positions at power-up.

## Verification

- Build the DSP on the Mac and render WAV files with scripted parameter sweeps. Keep these as the reference set.
- If the original firmware can be built or its DSP run on the host with the same parameter script, render the original and the port side by side and compare spectrally and by ear. If that is not possible, say so and rely on careful code reading and listening tests by the human.
- Check for NaN, inf, DC offset, and instability at extreme settings. Runaway feedback is intended, a crash or a stuck NaN is not.
- Measure CPU on the device. The Brick's cores are much faster than a Cortex-M7 in raw terms, but the audio path has more latency and jitter. Do not assume.

## Licensing and credit

- The original is credited prominently: name, authors, and a link to the Synthux Academy repository, in the README, the app's about screen and `THIRD_PARTY.md`.
- Keep the original MIT licence text and copyright notices with all derived code, and include `CREDITS.md`'s content in our credits. Re-check the `LICENSE` file for the exact copyright line before the first copy. Do not use Synthux Academy logos, artwork or panel graphics.
- The name "Audrey II" belongs to the Synthux project. Use a clearly different working title for the port unless Synthux agrees otherwise. **The human may want to contact Synthux Academy before any public release.** Do not publish or submit to PortMaster without the human's decision.

## Milestones

- **M0, source study:** study the official repo (URL above), record the commit studied and the pinned submodule versions, and produce `docs/PARAMETERS.md` (every control and what it does) and `docs/SIGNAL_FLOW.md` (the DSP chain with every rate-dependent constant).
- **M1, host DSP:** the DSP builds and runs on the Mac without libDaisy and renders reference WAVs from scripted parameters.
- **M2, host app:** SDL audio and input on macOS with the button-based control scheme and the parameter pages.
- **M2b, input path:** implement the audio input (host first: a WAV file or the Mac's input as the source), with gain, metering and the acoustic-feedback safeguards.
- **M3, device build:** cross-compile, package as a Port, run on the Brick. The human runs the on-device checklist: audio rate and buffer, CPU, buttons, screen-dim behaviour, internal mic and USB input, input latency, and speaker feedback behaviour.
- **M4, tuning:** sample-rate decisions, smoothing, the limiter, listening tests with the human.
- **M5, presets, polish, credits, release decision.**

## Definition of done for any change

Tests pass on the host. A separate agent has reviewed the change. The audio path has no allocation, lock or I/O. A rendered WAV has been inspected for NaN, clicks and DC offset. `THIRD_PARTY.md` and `docs/STATUS.md` are updated. If it affects the device, a numbered human test checklist exists.
