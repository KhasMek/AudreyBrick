# M2b task briefs: audio input

**Status: written 2026-11-18. M2b-A started on the human's instruction ("first audio input").**

M2b outcome: the INPUT tab. The instrument can be fed from an audio file, from the Brick's internal microphone, or from a USB audio interface, with an input gain, a meter and safeguards against speaker howl. With the source off, the app sounds exactly as it does now.

What the original does (`docs/SIGNAL_FLOW.md` section 4): one mono input sample is added, at unity gain, to both strings together with the noise and the body-delay return. No control affects it. `AudreyVoice::process` already takes that sample stream as `inMono`; today the app passes none.

Decisions already made by the human (`docs/RISKS.md` section 2a): sources are off, file, internal mic and USB; stereo sources are mixed as `L + R`; there is an input gain; the source is off at every start.

Device facts (`docs/PLATFORM_NOTES.md` sections 10 to 12): SDL's audio driver is ALSA into PipeWire at 48 kHz; SDL lists one capture device (`audiocodec`, the internal mic); a USB interface would appear as another; duplex latency and mic level are unmeasured; the callback budget at the default governor is about a third of a 256-frame buffer.

M2b is two tasks, each ending with a run on the device:

- **M2b-A, the input stage and file input.** Everything except live capture: the INPUT tab, the input stage on the audio path, gain, meters, file loading and looping. Fully testable on the Mac.
- **M2b-B, live input.** Capture devices (mic, USB), the clock-drift handling between capture and playback, and the howl safeguards. Needs the device to judge.

## Design shared by both tasks

```
UI thread                                   audio thread (output callback)
  INPUT tab state (source, gain, file)        InputStage::pull(buf, frames):
  loader thread: decode + resample WAV   -->    source = off   -> nothing (voice gets nullptr)
  hands a finished buffer over (pointer)        source = file  -> read the loop, advance
  reads meters and status through atomics       source = live  -> read the capture ring (M2b-B)
                                               apply smoothed gain, measure peak
                                             AudreyVoice::process(controls, buf or nullptr, ...)
```

- **Off means off.** With the source off the voice is called with `nullptr`, exactly as now, so the output is bit-identical to the current app. A test enforces this.
- **Gain.** One input-gain control, a knob position 0..1 mapped by a square law to a linear gain of 0 to 4 (`gain = (2n)^2`, so n = 0.5 is unity and n = 1 is +12 dB). Shown in dB ("-inf dB" at zero). Smoothed on the audio thread with a one-pole of about 10 ms so changes do not click. The stage computes `in = gain * mono`.
- **Meters.** Peak of the signal after gain, published like the output meters. Shown large on the INPUT tab and, whenever the source is not off, as a small always-visible IN indicator so the player can see input activity from any tab.
- **File source.** WAV files in `audreybrick/data/input/` (the launcher creates the folder; the package ships none). A loader thread decodes with dr_wav (already vendored, MIT-0), mixes to mono as `L + R` (mono files as they are), converts to 48 kHz if needed, and caps the length at 5 minutes (about 58 MB as float; longer files are cut and the screen says so). The finished buffer is handed to the audio thread by pointer through a single-slot lock-free exchange; the audio thread never allocates, frees, reads a file or waits. The old buffer is freed on the UI thread. Playback loops, with a short crossfade (about 10 ms) at the loop point and a short fade when a file starts or the source changes, so nothing clicks.
- **Controls on the INPUT tab** (L1/R1 reach it like any tab; it comes after OUT): d-pad left/right selects the source; up/down moves the gain; X + left/right picks the file when the source is FILE; Y + up/down resets the gain. L2/R2, A, B, Select and Menu + Start behave as everywhere.
- **Nothing is remembered between launches** in M2b (presets come later). The source is off at every start.
- **Not in M2b:** recording the output to a file (requested by the human on 2026-11-18; planned as its own small task afterwards), presets, the safety limiter, modulation.

Rules for both tasks: do not run any git command that changes state (the human commits); do not edit `docs/**`, `CLAUDE.md`, `third_party/**` (dr_wav is used as vendored), `src/shim`, `src/core`; the engine is not touched; our files carry `// Copyright (c) 2026 KhasMek` and `// SPDX-License-Identifier: MIT`; each file includes what it uses; every target that includes DSP headers applies `${AB_DSP_OPT_OPTIONS}` and `${AB_WARNING_OPTIONS}`; warning-free with clang, gcc 12 and gcc 14; nothing newer than SDL 2.0.18 API in non-test code; no allocation, lock, I/O, logging or waiting on the audio thread; never run the app with the real audio device on the Mac (`SDL_AUDIODRIVER=dummy`). If a constraint cannot be met or something is ambiguous, stop and report.

---

## M2b-A: the input stage, the INPUT tab and file input

```
Goal: The app has an INPUT tab with OFF and FILE sources, an input gain and input meters;
      a WAV file from data/input/ loops into the instrument's original input point; with
      the source off the output is bit-identical to today's.

Context: M1 to M3 are complete; the app runs on the device. AudreyVoice::process already
      accepts a mono input buffer. src/platform/AudioRender is the SDL-free callback body
      (it passes nullptr today). src/app has Tabs, ControlModel (M2-A), InputRouter, App,
      UiSnapshot; src/ui has the layout. dr_wav is in third_party/dr_libs and is linked
      only into the tools today.

Scope: new src/input/** (the SDL-free input stage: gain smoother, file loop player, the
      buffer hand-over, meters, the INPUT tab's state model, WAV decode + mono mix +
      rate conversion + length cap as a plain function a loader thread can call);
      src/platform/AudioRender.{h,cpp} (call the stage; pass its buffer or nullptr to the
      voice); src/app/Tabs.{h,cpp} (add the INPUT tab after OUT; this M2-A file may be
      changed for that purpose only), src/app/ControlModel.{h,cpp} only if the new tab
      needs it and then minimally, src/app/InputRouter.{h,cpp}, src/app/App.{h,cpp},
      src/app/UiSnapshot.h, src/app/SdlDiagnostics.* and DiagLog.* for log lines;
      src/ui/** for the INPUT tab and the IN indicator; packaging/AudreyBrick.sh (create
      data/input, pass its path) and packaging/README.txt; scripts/package_port.sh and
      docker/knulli_rootfs_tests.sh as far as the new folder and option need; tests under
      tests/input/**, tests/platform/**, tests/ui/**; CMake lines.
Out of scope: capture devices and anything live (M2b-B); recording; presets; src/core;
      src/control; the engine; any change to how existing parameters behave or display.

Constraints:
  - The design section above is the specification for behaviour.
  - Rate conversion happens off the audio thread at load time. Use a small windowed-sinc
    (or polyphase) resampler of our own in src/input so the code is SDL-free and testable,
    OR SDL_AudioStream in the platform layer; choose one, say why, and test its output
    (a sine in, the right frequency and level out, no gross aliasing for 44.1 kHz and
    96 kHz sources).
  - dr_wav may now be linked into the app through a small wrapper target; it must stay off
    the audio thread. Update THIRD_PARTY.md's "Built into" line for dr_wav (this one edit
    outside the scope list is allowed).
  - The loader thread is the only new thread. It is started on demand, never blocks the UI
    thread for more than a frame, and is joined on shutdown. A file that fails to load
    leaves the previous state untouched and shows a short reason on the INPUT tab.
  - File list: *.wav / *.WAV in the input folder, sorted by name, rescanned when the INPUT
    tab is entered and when FILE is selected; at most 256 entries; names longer than fits
    are cut with "..". An empty folder shows "NO FILES IN data/input".
  - UiSnapshot gains what the INPUT tab needs (source, gain normalised and in dB, file name,
    index and count, file length, load state and error text, input peak, clip). The layout
    follows every existing rule (named panels, no overlap, minimum sizes, Brick button names
    only, state never by colour alone, contrast at least 4.5:1).
  - INPUT tab screen: SOURCE row (left/right) showing OFF or FILE, with LIVE sources absent
    in this task; GAIN row (up/down) with value in dB and a bar; FILE row (X + left/right)
    with name, "n of m" and length, dimmed unless the source is FILE; a large input meter
    with peak hold and clip; nothing about microphones yet.
  - The always-visible IN indicator must not disturb the existing strip of eleven values
    plus CPU or the output meters; find room (for example a slim third bar in the meter
    panel) and show it only when the source is not off.
  - Diagnostic log: source changes, each file load (name, channels, rate, frames, seconds,
    converted or not, cut or not, time taken), load failures, and in the 10-second stats
    line the input peak when a source is active.
  - Self-test support: the button script can drive the INPUT tab like any other; add a way
    for the self-test to point at an input folder (an option) so an automated run can
    select FILE and verify sound comes out.

Acceptance criteria:
  1. With the source OFF, AudioRender's output is bit-identical to a build without the
     stage for the existing scripted render (test compares against AudreyVoice driven with
     nullptr), for buffer sizes 64, 256, 1024 and 5000.
  2. With FILE selected and gain at unity, the samples handed to the voice equal the mono
     mix of the file, looped, apart from the documented fades; a test checks the loop
     point crossfade and that a stereo file equals its L + R mix.
  3. The audio path performs zero allocations with a file playing, during a hand-over of a
     new file, and while gain ramps (operator-new counting test); the old buffer is freed
     on the UI thread; ThreadSanitizer is clean for a stress test that swaps files
     repeatedly while the audio side pulls.
  4. Gain: mapping at n = 0, 0.5 and 1 (0, 1.0, 4.0); dB text; smoothing reaches the target
     and produces no step larger than a stated bound; Y + up/down resets to unity.
  5. Loading: mono and stereo, 16-bit, 24-bit and float files at 44.1, 48 and 96 kHz load
     and play at the right pitch and level; a file longer than 5 minutes is cut with a
     visible note; a corrupt file, an empty file and a non-WAV file fail cleanly with a
     reason; a missing folder is handled.
  6. UI: layout tests for the INPUT tab in each state (off; file playing; X held; loading;
     error; no files; long names; clip) and for the IN indicator on another tab; screenshots
     written and viewed.
  7. A self-test run that selects FILE with a test tone in the input folder and raises
     nothing else produces output above a stated level (the input is audible through the
     chain even at minimum feedback), and the same run with the source OFF stays silent.
  8. Everything that passed before still passes: macOS Release, ASan/UBSan, TSan (control,
     platform, ui, input), no-SDL; scripts/verify_vendor.sh; scripts/make_reference.sh
     --check; scripts/build_linux_arm64.sh; scripts/make_port.sh with the firmware image.

How to verify: the commands named in criterion 8, from clean build directories, plus the
  self-test of criterion 7 with dummy drivers.

Report back: files added and changed; the resampling choice and its measured quality;
  test summaries for every build; measured cost of the input stage per callback on the Mac
  (with a file playing) so we can judge the device budget; the screenshots' paths (view
  them yourself first); the package listing, zip path, size and SHA-256s with confirmation
  the packaged files are the tested ones; deviations; open questions; and a numbered
  checklist for the human's device run (save data/ first; copy the two items; put one or
  two WAV files into roms/ports/audreybrick/data/input/ and how; go to the INPUT tab;
  select FILE; raise gain; hear it through the instrument at minimum feedback, then with
  feedback up; change file; watch the meters; note crackles and the callback load; send
  back data/).
```

---

## M2b-B: live input (outline; briefed in full after M2b-A's device run)

Capture through a second SDL audio device into a lock-free ring that the output callback drains. Sources listed from SDL's capture devices (the internal mic by its name, anything else as USB/external), refreshed on device add and remove. Handling of capture and playback clocks that differ (USB) by keeping the ring near a target fill and dropping or repeating a few samples with a short fade when it drifts. Safeguards: switching to a live source sets the input gain to zero so the player raises it deliberately; a warning on the INPUT tab when the source is the internal mic ("MIC + SPEAKER CAN HOWL: USE HEADPHONES"); the gain and meter always visible. Log lines for capture device open, obtained format, ring fill, overruns and underruns. The device run measures mic level, round-trip latency by ear, behaviour with a USB interface if the human has one, and the callback load with capture running.

## Recording (requested 2026-11-18; planned as its own task after M2b)

The human would like a button combination, available on any tab, that starts and stops recording the instrument's output to a file. Assessment: cheap. The audio thread copies each output block into a lock-free ring (a memory copy, no I/O); a writer thread drains it to a WAV file in `data/recordings/`. Stereo 48 kHz at 16 bits is about 11.5 MB per minute and the card has tens of gigabytes free. The costs are a second thread doing disk writes and the SD card's write latency, which the ring absorbs. Open choices for then: the button combination (Start is free as a modifier, for example Start + A), 16-bit or float files, a maximum length, and an on-screen REC indicator with elapsed time.

## Review brief (after each task)

```
Goal: Independent review of M2b task <ID> before the human runs it on the device.
Context: You did not write this change. Read CLAUDE.md, docs/ORCHESTRATION.md,
      docs/PLATFORM_NOTES.md (6, 7, 10 to 12), docs/SIGNAL_FLOW.md section 4,
      docs/briefs/M2b.md. The change is the uncommitted work shown by git status.
Scope: read everything; change nothing; no git command that changes state.
Acceptance criteria for the review:
  1. You ran the verification commands yourself.
  2. Blocking findings for: any allocation, lock, I/O, logging, wait or unbounded loop
     reachable from the audio callback; a data race in the buffer hand-over or the meters;
     a use-after-free or leak in the file swap; output that differs from before when the
     source is off; a click at file start, loop point, source change or gain change; a
     crash or hang on any bad file; a path that reads files outside the input folder;
     behaviour that departs from docs/briefs/M2b.md.
  3. You wrote at least one independent check of the "off is bit-identical" claim and of
     the file path's samples, and injected at least four bugs into a scratch copy to see
     whether the tests catch them.
  4. You looked at every INPUT-tab screenshot and judged it as the earlier UI was judged.
Report back: findings ranked by severity with file and line; per acceptance criterion of
  the task "verified by running X", "verified by reading" or "not verified"; what you assumed.
```
