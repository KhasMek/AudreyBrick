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

---

# M2b-A2: file browser, crossfade between files, play modes, 10-minute cap

Written 2026-11-18 after the human tried M2b-A on the Brick. It builds on the uncommitted or just-committed M2b-A code in `src/input/`. Read the header comments of `InputStage.h`, `InputController.h`, `InputView.h`, `InputFiles.h` and `InputTypes.h` first; they describe the current behaviour exactly. Everything in M2b-A that this brief does not change must stay as it is, including its tests.

## What the human asked for

1. Files up to **10 minutes** (was 5). Streaming from the card is a later feature, not this task.
2. A **crossfade** when changing files, instead of fade out, gap, fade in.
3. A file list that can be **scrolled without loading anything**; loading is a separate, deliberate press. Browsing blind (one name at a time, each step loading a file) is what the human dislikes.
4. **Play modes** for the file: LOOP (as now) and ONCE (play to the end and stop).

## Design (decided by the orchestrator; say so in your report if something cannot work as written)

### Cap

- `kInputMaxSeconds = 600`. Update every comment, log text and screen text that says 5 minutes.
- Confirm by reading the code, and state in your report, the loader's **peak memory** for one load (decoded output plus what else?). A source file far longer than the cap must not cost more memory than a file exactly at the cap. Add a test if none proves this.
- Log `MemAvailable` from `/proc/meminfo` (where the file exists; skip silently elsewhere) to the diagnostic log at start-up and after each finished load, on the UI thread. One line each.

### Stage: crossfade between files

- When a new non-empty buffer is picked up while another is audible (source FILE, level > 0), the stage reads **both** for `kInputFileCrossfadeFrames` (new tuning constant in `InputTypes.h`, 4800 = 100 ms): the old one continues from where it is (still looping or ending by its mode), the new one starts at sample 0. **Equal-power** weights (the two files are unrelated material), from a table allocated in `prepare()`; no trigonometry per sample in `pull()`. At the end the old buffer is retired as now.
- A buffer that arrives while a crossfade is running waits in the pending slot until the crossfade ends (the existing rule: nothing is picked up while switching).
- If nothing is audible (level 0, or no current buffer, or the current one has ended in ONCE mode) the new buffer simply starts with the existing 10 ms fade-in.
- An **empty** buffer still means "nothing": the current one fades out over 10 ms and is retired.
- The source fade (OFF/FILE) multiplies on top of the crossfade as it does on top of playback now.
- The retire queue rule is unchanged (if full, the switch waits). Decide and document what "waits" means with two buffers held; the audio thread must never free.
- With the source OFF and nothing fading, `pull()` still returns nullptr and the app's output stays bit-identical to having no input stage. With a file ended in ONCE mode and nothing fading, it also returns nullptr.

### Stage: play modes

- `enum class InputPlayMode { Loop, Once }`, an atomic set from the UI thread. Default Loop.
- **Loop:** exactly the current behaviour, including the loop-point crossfade.
- **Once:** the file plays from sample 0 to its last sample with no loop crossfade, with a 10 ms fade-out over its last `kInputFadeFrames` samples (shorter files: half the file), then stops. A status atomic `ended()` becomes true.
- **Restart:** a UI-thread call `restart()` (an atomic counter the audio thread compares). On seeing it: if audible, 10 ms fade-out, then position 0 and fade in; if ended or silent, start at 0 with the fade-in. `ended()` clears.
- Changing Once -> Loop while ended restarts playback (as `restart()`). Changing Loop -> Once while playing: playback continues to the end of the file and stops there; if the position is already inside the loop-crossfade tail region, handle it without a click and document how.
- A newly arrived buffer always starts at 0 and plays, in either mode.
- OFF -> FILE with a loaded buffer starts from 0, as now, in either mode.

### Controller: browsing

- A **cursor** (highlighted file) separate from the loaded file.
- **X + up / down:** move the cursor one file; wraps at the ends; repeats while held (after `tuning::kHoldDelaySeconds`, then a new constant, about 8 steps per second). Loads nothing.
- **X + right:** load the file under the cursor. If it is already the loaded file, `restart()` it instead (no reload).
- **X + left:** toggle the play mode, LOOP <-> ONCE.
- Plain left/right (source), up/down (gain) and Y + up/down (gain reset) are unchanged. X + up/down with the source not FILE does nothing, as do X + left/right.
- **No automatic load.** Selecting FILE with nothing loaded plays nothing and the screen says how to load (`X + RIGHT: LOAD`). Selecting FILE with a file already loaded and still listed plays it from the start, as now. (This removes M2b-A's "first file loads when FILE is selected". Update its tests.)
- While a load is pending, the loaded file keeps playing; X + right on another file replaces the pending request (the loader already supports superseding; keep that behaviour and its tests).
- Rescan rules stay (on selecting FILE, on entering the tab). After a rescan the cursor stays on the same name if it is still listed, else it clamps to the nearest valid index. If the loaded file has disappeared from the list it keeps playing and is still shown as playing.
- The play mode is not remembered between launches (nothing is yet).

### Screen (INPUT tab, source FILE)

- A list of up to five file names, a window that scrolls with the cursor. The cursor row is highlighted. The loaded file's row carries a mark that reads as "this one plays" without colour alone (for example a leading `>` or the word PLAY). Position text `3 OF 12`. If the list was cut at `kInputMaxFiles`, say so as now.
- One line for the loaded file: name (if not visible in the window it must still be readable here), length, `CUT TO 10 MIN` if cut, the play mode (`LOOP` / `ONCE`), and `ENDED` when it has ended in ONCE mode.
- A slim progress bar with elapsed and total time (`1:23 / 4:05`) for the loaded file, from the stage's position atomics.
- One status line, only when needed: `LOADING <name>` or `CANNOT LOAD: <reason>`.
- Key hints on the tab for the four X bindings, in the same style as the other tabs' labels (key caps from the existing whitelist).
- With the source OFF the tab looks as it does now.
- All text must pass the existing layout tests (no overlap, contrast, key-cap whitelist). Names are cut to fit their row with the existing display-name rule. `UiInputView` stays plain fixed-size data (for example five fixed name slots).
- Produce BMP screenshots through the existing screenshot path for: list with a file playing in LOOP; ONCE and ended; loading another file while one plays; a failed load; 12 files with the cursor in the middle; no files. Put them in the scratchpad or the build directory, not the repo, and give the paths in your report.

### Not in this task

Streaming, live input, recording, remembering settings, any change to the engine or to other tabs. `docs/STATUS.md` (the orchestrator updates it).

## Rules

- Real-time rules as ever: `pull()` never allocates, frees, locks, logs or does I/O. The allocation test (`tests/input/input_alloc_test.cpp`) must cover the crossfade, ONCE ending, restart and mode changes.
- Do not change anything under `third_party/`. Do not change git state (no add, commit, stash, checkout, reset).
- No firmware files in the repo or the package.
- Never open the Mac's real audio device; use `SDL_AUDIODRIVER=dummy`.

## Tests required (at least)

- Crossfade: sample-exact against a reference computed in the test from the two files and the weight table; equal-power property (sum of squared weights = 1 within float tolerance); no step larger than a stated bound for two full-scale DC files of opposite sign; old buffer retired exactly once; third buffer during a crossfade waits and then crossfades; empty buffer during a crossfade.
- Once: output equals the file then silence/nullptr; the end fade; `ended()`; restart from ended and from mid-file (click bound); Once -> Loop when ended; Loop -> Once at several positions including inside the tail region.
- Off bit-identical and file-loop sample-exact tests from M2b-A still pass unchanged where their behaviour is unchanged.
- Controller: cursor moves, wrap, repeat timing, no load on cursor movement, X + right loads / restarts, X + left toggles, no auto-load, rescan keeps the cursor by name, superseded load.
- Stress test (threads): extend the existing one with mode toggles and restarts; run under ThreadSanitizer.
- Cap: a file of 10 min + 1 s is cut to exactly 10 min of frames at 48 kHz.
- Layout tests for every new screen state.

## Also update

`packaging/README.txt`, `docs/INPUT_RUN_BRICK.md` (new steps: browse with X + up/down, load with X + right, mode with X + left, crossfade, a 10-minute file; keep it a short numbered checklist for the human), `docs/CONTROL_SCHEME_PROPOSAL.md` section 000, the script tests under `tests/platform/` if they rely on auto-load, and the header comments of every file whose behaviour changed.

## Verification before you report (give the numbers)

1. macOS Release: configure, build, `ctest`.
2. macOS ASan + UBSan build and `ctest`; TSan build and `ctest`.
3. The no-SDL build and `ctest`.
4. `scripts/verify_vendor.sh` and the M1 reference check (`scripts/make_reference.sh` or as STATUS.md describes).
5. Linux: the Debian 13 container builds (gcc and clang) and tests, as in earlier tasks.
6. `scripts/make_port.sh ~/.cache/audreybrick-device/knulli-scarab-20260511.sqfs` and its PASS/FAIL counts.

Do not leave background shells or processes running when you finish. Report: what you built, every deviation from this brief and why, the peak-memory answer, measured cost of `pull()` during a crossfade on the Mac, test counts for each build, screenshot paths, and anything you are unsure about.

---

# M2b-A3: file choice by holding X, with a pop-up list

Written 2026-11-18. The human saw M2b-A2's in-tab list and found it far too small. This task replaces the **browsing UI and its gestures only**. The stage (crossfade, LOOP / ONCE, restart), the loader, the 10-minute cap, the memory guard and everything else from M2b-A2 stay as they are.

## Behaviour (the human's design, details by the orchestrator)

### The tab

With the source FILE the INPUT tab goes back to M2b-A's layout: full-size SOURCE and GAIN rows and a FILE row. No in-tab list, no hint row. The FILE row shows, at the sizes M2b-A used or larger:
- `FILE 2 OF 6` (the cursor's position),
- the file name: the cursor's file while X is held, otherwise the loaded file (or `NOTHING LOADED` and `HOLD X: CHOOSE FILE` when nothing is loaded and files exist),
- for the loaded file: the progress bar, `1:23 / 4:05`, `LOOP` or `ONCE`, `ENDED`, and `CUT TO 10 MIN` / `FILE ENDS EARLY` when they apply,
- the small status lines as in M2b-A when needed (`LOADING <name>`, `CANNOT LOAD: <reason>`, and `PLAYING: <name>` whenever the name shown is not the loaded file).

### Holding X (INPUT tab, source FILE)

- **Pressing X** puts the cursor on the loaded file (if it is in the list; else it stays where it was, or the first file).
- **X + left / right:** the cursor to the previous / next file, wrapping, one step per press (as in M2b-A). Nothing loads.
- **X + up / down:** the first press opens the **pop-up list** without moving the cursor. While it is open, up / down move the cursor (wrapping, repeating while held at `kInputCursorRepeatHz` after `tuning::kHoldDelaySeconds`), and left / right still step it too.
- **Releasing X:** the pop-up closes. If a d-pad direction was pressed during this hold and the cursor is not on the loaded file, that file loads (crossfade as built). If the cursor is on the loaded file: nothing, except that a file that has ENDED in ONCE mode is played again. A hold with no d-pad press never loads or restarts anything.
- **X + Y:** toggle LOOP / ONCE. **X + Select:** play the loaded file again from its start (`restart()`). Neither counts as a d-pad press. Check how ControlModel treats Y and Select while X is held on this tab and make sure neither does anything else there (for example no parameter reset); say what you found.
- **L1 / R1 while X is held, or `releaseAll()`** (focus loss, controller removed): the choice is cancelled; the pop-up closes, nothing loads, and the d-pad is inert until released as now.
- With the source OFF, X does nothing on this tab, as before.
- Selecting FILE still loads nothing by itself.
- Plain left / right (source), up / down (gain) and Y + up / down (gain reset) are unchanged.

### The pop-up

- A panel drawn over the middle of the screen (it may cover the tab's rows and the parameter grid; the tab bar, the feedback bar, the switch box and the meters stay visible). A clear border and an opaque background.
- File names at **twice** the text size of M2b-A2's list or larger; as many rows as fit at that size (about eight); the window scrolls with the cursor. The cursor row is highlighted; the loaded file is marked `PLAY` (not by colour alone).
- A header: `FILES 3 OF 12` and the mode (`LOOP` / `ONCE`). A footer with the hints, using key caps from the existing whitelist: release X to load, Y mode, SELECT play again.
- `UiInputView` stays plain fixed-size data.
- The layout tests need a notion of an overlay layer: items in the overlay may cover the base layer, but must not overlap each other, must stay inside the panel, and must meet the contrast rule against the panel's background. Do not weaken the checks for the base layer.

## Remove

The M2b-A2 in-tab list, its compact SOURCE / GAIN rows, the hint row, and the X + up/down, X + right and X + left bindings as they were. Update or replace their tests.

## Tests required (at least)

Controller: cursor starts on the loaded file; left/right step; up/down open then move; repeat timing; release loads only after a d-pad press and only when the cursor differs; release on an ended ONCE file restarts; a hold with no d-pad press does nothing; X + Y and X + Select; cancel on tab change and on `releaseAll()`; the memory guard still holds (a release during a crossfade waits and then loads). Layout: every pop-up state (few files, 12 files with the cursor in the middle and the playing file out of the window, a long name, nothing loaded, loading while one plays, ONCE ended) plus the tab's states without the pop-up. The script tests under `tests/platform/` and `docker/knulli_rootfs_tests.sh` must load a file the new way.

Screenshots (BMP, in the scratchpad, paths in the report): the tab with a file playing; X held with left/right only (no pop-up); the pop-up with 4 files; the pop-up with 12 files; the pop-up in ONCE ended; a failed load.

## Also update

`packaging/README.txt`, `docs/INPUT_RUN_BRICK.md` (keep it a short numbered checklist; keep its Notes), `docs/CONTROL_SCHEME_PROPOSAL.md` section 000, header comments.

## Rules and verification

As M2b-A2: no git state changes, nothing under `third_party/`, no `docs/STATUS.md` edits, `SDL_AUDIODRIVER=dummy`, no background processes left. Run the whole verification list (macOS Release, ASan + UBSan, TSan, no-SDL, `verify_vendor.sh`, the reference check, `scripts/build_linux_arm64.sh`, `scripts/make_port.sh` with the Scarab image) and report the numbers, the deviations and what you are unsure about.

---

# M2b-B: live input (microphone, USB audio)

Written 2026-11-19. File input (M2b-A to A3) is committed and confirmed on the Brick. The human has a USB audio device to test with. Read the header comments in `src/input/` first; the stage's contracts (off is bit-identical, no allocation / lock / I/O / logging in `pull()`, fades, gain law, meters) all continue to hold.

## What is known and unknown about the device

- SDL on the Brick has only the `alsa` audio driver; ALSA's default PCM is PipeWire, for capture too. With nothing plugged in SDL lists one capture device, named `audiocodec, ` (the built-in codec; the mono mic). PipeWire's default source is the built-in card's capture.
- **Unknown, to be learned from the first device run:** what SDL lists when a USB interface is plugged in; whether opening a listed device by name works while PipeWire runs, or only the default device does; what format, channel count and buffer the capture side grants; the mic's level; capture-to-output latency; whether capture and playback clocks drift.
- So the design must not assume names. It lists whatever SDL reports and logs everything.

## Design

### Sources

`InputSource` becomes OFF, FILE, **LIVE** (left/right on the tab, in that order, no wrap). With LIVE selected, the capture device is chosen the same way a file is: hold X, left/right to step, up/down for the pop-up list, release X to open the choice. The list is: `SYSTEM DEFAULT` (SDL device `NULL`) first, then every name from `SDL_GetAudioDeviceName(i, 1)`, re-enumerated when LIVE is selected, when the INPUT tab is entered, when X is pressed, and on `SDL_AUDIODEVICEADDED` / `SDL_AUDIODEVICEREMOVED` for capture devices. Reuse the file chooser's code and pop-up rather than writing a second one; X + Y and X + Select do nothing in LIVE.

No device is opened until the user releases X on a choice (nothing opens by itself, as with files). The device stays open while the source is LIVE and is **closed when the source leaves LIVE** (so the mic is never open unseen) and reopened, same device, when LIVE is selected again in the same run.

### Capture path

- SDL-free core, `src/input/LiveInput.{h,cpp}`: a preallocated lock-free SPSC ring of mono float samples at 48 kHz (capacity about 1 s), written by the capture callback, read by `InputStage::pull()`.
- Thin SDL part in `src/platform/` (next to `AudioHost`): opens the capture device with `SDL_OpenAudioDevice(name, 1, ...)`, float32, 48000 Hz, 2 channels requested, `SDL_AUDIO_ALLOW_CHANNELS_CHANGE` only (SDL converts rate and format; we mix the channels ourselves), samples = the output buffer size. The capture callback mixes its channels to mono (**L + R** for stereo, as the human decided; mono as it is; more than two: the first two), clamps to +-8 as files are, and writes to the ring. It follows the audio-thread rules: no allocation, lock, logging or I/O. Opening and closing happen on the UI thread; log how long each took. If opening proves slow on the device it can move to a thread later; do not build that now.
- **Reading the ring (the stage, source LIVE):**
  - Start: output silence until the ring holds a target fill (`kLiveTargetFrames`, default two output buffers' worth, set from the output buffer size in `prepare()`), then fade in over the usual 10 ms.
  - Underrun (the ring cannot supply a whole chunk): play what is there, fade to silence without a click (hold a short fade state; do not output a hard zero edge), count it, and re-buffer to the target before fading back in.
  - Overfill (fill above target + a margin of about four output buffers, meaning the capture side runs fast or the output stalled): skip ahead to the target with a short crossfade (reuse the fade machinery), count it.
  - Publish atomics: current fill, minimum and maximum fill since last read, underruns, overfills, frames captured, frames read. The app logs them in the 10-second stats line while the source is LIVE. These numbers are how drift and latency get judged on the device. **Do not build adaptive resampling**; if the device shows steady drift it becomes a later task.
- LIVE and FILE never sound together. FILE -> LIVE and LIVE -> FILE use the existing 10 ms source fades (fade out, switch, fade in). A loaded file stays loaded while LIVE is selected.
- Device lost (unplugged; SDL reports the device removed, or the callback stops): the ring runs dry, the stage fades to silence as for an underrun; the UI shows `DEVICE LOST`, closes the device, and returns to "no device chosen". No crash, no stuck sound.

### Howl safeguards (CLAUDE.md, "Audio input")

- **Whenever a capture device is opened** (first choice, a new choice, or reopening on LIVE selected again), the **input gain is set to zero (-inf dB)** before the first sample can sound. The user raises it with up/down. Y + up/down still resets the gain to unity, as on FILE; keep that, it is a deliberate press. When the source goes back to FILE or OFF the gain is not restored (the user sees the value and sets it).
- The INPUT tab shows a warning while the source is LIVE: `SPEAKER + MIC CAN HOWL` and `USE HEADPHONES OR KEEP GAIN LOW`, readable (not the smallest text), in the warning colour already used for errors or an equally strong one that passes the contrast test.
- The input meter and the IN bar work for LIVE as for FILE (after gain). In addition show a small **pre-gain** level indicator on the LIVE row or meter so the user can see that the mic is alive while the gain is still at zero. Without it the user cannot tell a dead device from zero gain.
- No automatic muting based on the speaker: the app cannot tell whether headphones are in. Say so in the README.

### Screen (source LIVE)

The FILE row's place shows a DEVICE row in the same style: `n OF m`, the device name (the chosen one, or the cursor's while X is held, or `NO DEVICE CHOSEN` + `HOLD X: CHOOSE DEVICE`), then a line with what was granted (`48000 Hz 1 CH 256 FR`) and the pre-gain level, and a status line when needed (`CANNOT OPEN <name>: <SDL's reason, cut>`, `DEVICE LOST`). The warning lines sit on the tab while LIVE is selected whether or not a device is open. The pop-up is the file pop-up with device names and a header `DEVICES n OF m`; its hint row shows only what applies.

### Logging (this run is mainly for learning)

At start-up and at every re-enumeration that changes the list: every capture device name. On open: the name asked for, the spec asked for and the spec obtained (rate, format, channels, samples), the time taken, or SDL's error text. On close: the reason. Every 10 s while LIVE: the ring statistics above plus pre-gain and post-gain peaks. Add to the launcher's device-state block (keep its time limits and its no-command-substitution rule): `pactl list short sources` and `arecord -L` (first 30 lines), so the PipeWire and ALSA names of a USB interface are in the log.

### Host and tests

- **Never open a real capture device on the Mac** (it would prompt for microphone access and is not what we test). All automated tests run with `SDL_AUDIODRIVER=dummy` or without SDL. Give `LiveInput` a way to be fed directly by tests, and the app's self-test script language a way to simulate a capture stream (for example a scripted tone written into the ring at a given rate), so the whole path stage -> voice can be exercised end to end, including in `docker/knulli_rootfs_tests.sh`.
- Tests required at least: ring read/write under two threads (TSan; remember the project's note that TSan on Apple arm64 misses races in struct copies of 32 bytes or more, so keep shared things scalar); start-up buffering; underrun fade and recovery with a step bound; overfill skip with a step bound; capture slightly fast and slightly slow relative to output (for example +-0.5 %) for a long simulated run: bounded fill, counted corrections, no step above the bound; FILE <-> LIVE <-> OFF switching in every order, also mid-crossfade and mid-load; gain forced to zero on every open; device lost; off still bit-identical; LOOP file playback still sample-exact; the allocation test extended to LIVE; the fuzz test extended with LIVE source toggles and ring starvation; controller tests for the device chooser (list changes while X is held, chosen device disappears from the list, open failure, reopen on returning to LIVE); layout tests and snapshots for every LIVE state including the pop-up.
- Screenshots (BMP, scratchpad, paths in the report): LIVE with no device chosen; LIVE with a device open and signal; the device pop-up with three devices; open failed; device lost.

### Docs to write or update

`packaging/README.txt`; `docs/CONTROL_SCHEME_PROPOSAL.md` section 000; a new numbered checklist for the human, `docs/LIVE_INPUT_RUN_BRICK.md` (short steps; headphones first, then speaker at low volume; internal mic via SYSTEM DEFAULT and via the listed name; plug in the USB device before starting and also while running; choose it; clap or tap test to judge the delay by ear; unplug while playing; five minutes of LIVE with feedback up to gather drift statistics; what to send back); header comments. Not `docs/STATUS.md`, not `docs/PLATFORM_NOTES.md` (the orchestrator updates those from the device results).

## Rules and verification

As before: no git state changes; nothing under `third_party/`, `src/core`, `src/shim`; nothing newer than the SDL 2.0.18 API in non-test code; warning-free; no background processes left. Run the whole list (macOS Release, ASan + UBSan, TSan, no-SDL, `scripts/verify_vendor.sh`, `scripts/make_reference.sh --check`, `scripts/build_linux_arm64.sh`, `scripts/make_port.sh ~/.cache/audreybrick-device/knulli-scarab-20260511.sqfs`) and report the numbers, the cost of `pull()` in LIVE, every deviation and its reason, and what you could not verify off the device.
