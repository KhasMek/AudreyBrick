# Control scheme proposal

**Status: proposal, awaiting the human's approval. Nothing here is decided.** Based on `PARAMETERS.md`. The Brick's button names follow the SDL GameController API. Whether Menu and the Fn switch are free for apps is unknown (platform notes, open question 6), so neither is used.

## 00. Changes made on the device (2026-11-18)

These override section 0 where they differ.

- **Quit is Menu + Start** (one press), as in other KNULLI games. The earlier Start + Select hold was removed because Select held + Start is KNULLI's screenshot.
- **Start by itself does nothing, and Select + Start must stay free** for KNULLI's screenshot. So the "freeze" slot planned for Start in section 0 needs a different gesture when presets are built (M5). Candidates: Start + A, or a slot on the PRESETS tab only.
- **A/B and X/Y are swapped by the launcher** so the Brick's labels match the table below. KNULLI names buttons by position; the Brick is labelled the Nintendo way.
- No in-app screenshot: KNULLI's own works.
- The screen shows no quit hint (the human's choice; Menu + Start is the system default).

## 000. The INPUT tab as built (M2b-A, M2b-A2, M2b-A3, M2b-B and M2b-B2, 2026-11-18/19)

INPUT is the sixth tab, after OUT. Left/right: source (OFF, FILE, LIVE; no wrap). Up/down: input gain, silence to +12 dB with unity in the middle. Y + up/down: gain back to unity. The global buttons work as everywhere. A slim IN bar under the output meters shows input level on every tab while a source is active.

Since M2b-A3 (2026-11-18; it replaces M2b-A2's in-tab list, which the human found too small), with the source FILE nothing loads by itself and a file is chosen by holding X:

- Pressing X puts the cursor on the playing file. X + left/right: previous / next file, one per press, wrapping; the FILE row shows the cursor's file. Nothing loads.
- X + up/down: the first press opens a large pop-up list (32 px names, eight rows, the playing file marked PLAY and named on a status line); it does not count as a choice unless nothing is playing. Then up/down move the cursor (held: 8 files per second after the hold delay) and left/right still step.
- Releasing X closes the list and loads the cursor's file, if a direction was pressed during the hold and it is not the playing file (the two crossfade over 100 ms; a file asked for during a crossfade loads once it is over). On the playing file: a load still pending is cancelled (the last choice wins), and a file that has ENDED in ONCE mode plays again. A hold without a direction does nothing.
- X + Y: play mode LOOP (default) or ONCE (to the end with a 10 ms fade, then silence). X + Select: play the file again from its start.
- L1/R1 while X is held (or losing focus): the choice is cancelled.
- The FILE row shows "n OF m", the name, a progress bar with elapsed and total time, LOOP/ONCE, ENDED, CUT TO 10 MIN, and LOADING / CANNOT LOAD / PLAYING when needed. Files are cut at 10 minutes.

With the source LIVE (M2b-B, 2026-11-19) a capture device is chosen the same way, from a list of SYSTEM DEFAULT (not on the Brick since M2b-B2, below) plus every capture device SDL reports (listed again when LIVE is selected, the tab is entered, X is pressed, or a device is plugged in or out):

- X + left/right steps, X + up/down opens the same pop-up (header DEVICES n OF m, the open device marked OPEN, one hint X RELEASE: OPEN), releasing X opens the cursor's device if a direction was pressed and it is not the open one. X + Y and X + Select do nothing. Nothing opens by itself.
- Every open sets the input gain to -inf dB first (howl safeguard); the player raises it with up/down or Y + up/down. Leaving LIVE closes the device; selecting LIVE again reopens the same device (gain to -inf again) if it is still listed.
- The tab's rows are compact with LIVE: SOURCE, GAIN (value still 40 px), DEVICE ("n OF m", the name, what the device delivers and a PRE bar with the level before the gain, or CANNOT OPEN / DEVICE LOST / HOLD X: CHOOSE DEVICE), and below them, across the rows and the input meter, the warning SPEAKER + MIC CAN HOWL / USE HEADPHONES OR KEEP GAIN LOW in inverse video. The SOURCE choices are drawn at 24 px on every source (three do not fit at 32 px).
- M2b-B2 (2026-11-19, the built-in mic on the Brick): with SDL's alsa driver (the Brick) the list has no SYSTEM DEFAULT, only the devices SDL names (possibly none: then X chooses nothing). The built-in mic (`audiocodec, `) is opened directly on its card (hw:0,0), mono (1 channel asked; the human's decision), with its input mixer set before and restored after; a device that fails by name is retried the same way. A device opened directly shows DIRECT (16 px) on the DEVICE row's top line, between the row's name and "n OF m". The buttons are unchanged.

With the source OFF, X + any direction does nothing on INPUT.

## 0. Button-by-button proposal, version 2 (2026-11-17)

**Status: approved by the human on 2026-11-17 as the first version, to be tweaked in use. The MOD tab comes after the plain port works on the Brick.** This replaces the page layout in sections 2 onward; the principle in section 1 and the trade-off reasoning still apply. It follows the human's feedback: tabs on L1/R1 as GrainBrick does, feedback on L2/R2, one tab per section.

The target is the TrimUI Brick. The same mapping is also bound to the Mac keyboard and a gamepad, only so the app can be developed and tested before it goes on the device.

### Always live, on every tab

| Button | Action |
|---|---|
| **L1 / R1** | Previous / next tab |
| **R2** (hold) | Feedback up, ramping (24 dB per second; a tap is 0.5 dB) |
| **L2** (hold) | Feedback down, ramping |
| **A** (hold) | Feedback to maximum while held; returns on release |
| **B** (hold) | Delay switch `!!` on while held (the doppler warp) |
| **Select** (hold) | Fine: d-pad and L2/R2 move at one eighth speed |
| **Select + B** | Latch the delay switch on or off |
| **Y + a d-pad direction** | Reset the parameter on that direction to its default |
| **Start** | Save the current state to the "freeze" slot |

Menu and the Fn switch are left alone until the device checklist shows whether KNULLI reserves them.

### Tabs

Each tab puts its first parameter on d-pad left/right and its second on up/down. Holding **X** shifts the d-pad to a third and fourth parameter. There is no select-then-adjust step.

| # | Tab | Left / right | Up / down | X + left / right | X + up / down |
|---|---|---|---|---|---|
| 1 | **LOOP** | frequency (one semitone per tap) | body | | |
| 2 | **FILTER** | lowpass | highpass | | |
| 3 | **REVERB** | dry/wet | decay | | |
| 4 | **DELAY** | time | mix | | repeats |
| 5 | **OUT** | safety limiter on/off (M4) | volume | | |
| 6 | **INPUT** | source: off, mic, USB, file | input gain | file select (when source is file) | |
| 7 | **MOD** | reserved, see below | | | |
| 8 | **PRESETS** | | select slot | X = load the slot, Select + X = save to it, Y (hold) = clear it | |

Every tab shows all eleven values, the feedback level, the switch state, the output meter and the limiter state in a strip that never changes; the tab's own parameters are drawn large with the button that moves each one next to it.

Stepping: a tap moves 1/128 of the knob's travel and holding ramps with acceleration (0.1 of travel per second rising to 0.4 over one second, so full travel takes about 2.9 s; a tuning constant for M4). Ramps are computed on the audio side at the control rate, so they do not zipper.

### Input sources (decided by the human, 2026-11-17)

Off (default), the Brick's internal microphone, a USB audio interface, or an audio file from `audreybrick/data/input/`. All of them are mixed to mono as L + R, scaled by the input gain, and fed to the original's unity-gain entry point. File playback loops; the file is loaded off the audio thread.

### Modulation tab (requested by the human, 2026-11-17; not designed yet)

The human would like LFOs and possibly an envelope that can modulate parameters, in the spirit of the Beetlecrab Tempera's modulation section. Notes so far:

- **Cost is not a concern.** Modulators would run at the 12 kHz control rate and add an offset to a knob's normalised value before the original mapping, so a handful of them is a few dozen arithmetic operations per control block.
- **It fits the architecture without touching the engine:** a modulation stage between the button control source and `AudreyVoice`, behind the same `ControlSource` interface. With all depths at zero the output is identical to the unmodulated port, which the parity test can enforce.
- **It is a new feature.** `CLAUDE.md` says new features come only after the faithful port is signed off. Recommendation: reserve the tab now, build the port through M3 so it runs on the Brick, sign off fidelity, then design and build modulation as its own milestone. The human can choose to pull it earlier.
- The orchestrator has not studied the Tempera's modulation section and must look it up before proposing a design. Open design questions: how many LFOs, shapes and rate range, what the envelope follows or is triggered by (the input level is the natural candidate), one destination per modulator or a matrix, and whether modulation settings are stored in presets.

## 0a. Earlier revision note (2026-11-17, superseded by section 0)

- **L1 / R1 switch tabs**, as GrainBrick does.
- **Feedback moves to L2 (down) / R2 (up)**, live on every tab. A (hold) stays as momentary maximum.
- **One tab per section of the instrument** rather than two large pages.
- The input tab gains an **input gain** control, and the input is the sum of the left and right channels.

## 1. Principle

The control layer stores **one normalised value, 0 to 1, per original knob**, plus the switch state. That is exactly what the firmware gets from its ADCs, so all of the original's curves and smoothing apply unchanged and a button press is equivalent to turning a knob by a fixed angle.

## 2. Proposed mapping

### Always live, on every page

| Button | Action | Why |
|---|---|---|
| **R1** (hold) | Feedback up, ramping | The main performance gesture. Shoulder buttons can be held while the thumbs work the d-pad |
| **L1** (hold) | Feedback down, ramping | |
| **A** (hold) | Feedback to maximum while held, returns to the set value on release | The designers' own hardware mod. The firmware's 7 ms feedback smoothing makes the jump click-free |
| **B** (hold) | Delay switch `!!` momentary: on while held | The doppler warp is a played gesture |
| **X** | Delay switch `!!` latch on/off | The original is a toggle |
| **Select** (hold) | Fine modifier for d-pad and L1/R1 | |
| **L2 / R2** | Previous / next page | As CLAUDE.md asks |

Feedback ramp: a tap moves 0.5 dB. Holding ramps at 24 dB per second, so the full 72 dB range takes 3 seconds. With Select held: 0.1 dB per tap and 4 dB per second. The interesting region is a few dB either side of where the loop starts to sing, so the fine rate matters.

### Per page

| Button | Action |
|---|---|
| **D-pad up / down** | Select a parameter on the page |
| **D-pad left / right** | Decrease / increase. Tap = 1/128 of the knob's travel. Hold = ramp with acceleration, full travel in about 2.5 s. With Select: 1/1024 per tap, slow ramp |
| **Y** (hold 0.6 s) | Reset the selected parameter to its default |
| **Start** | Open the preset page (or leave it) |

Frequency is the exception to the 1/128 step: a tap is exactly **one semitone** (1/56 of travel) and a fine tap is 10 cents, because the firmware's frequency scale is linear in note number.

### Pages

| Page | Parameters | Notes |
|---|---|---|
| 1 **LOOP** | frequency, body, lowpass, highpass, reverb dry/wet, reverb decay | Everything inside the feedback loop, matching the left and centre of the panel |
| 2 **DELAY** | delay mix, delay time, repeats, volume | The right side of the panel. Switch state shown here |
| 3 **INPUT** | source (off / internal mic / USB), input gain, input meter, speaker warning | Our addition around the original's unity-gain input. Source defaults to off |
| 4 **PRESETS** | 8 slots plus a "freeze" slot | Also reachable with Start |

**All eleven values, the feedback level, the switch state, an output meter and the limiter state are visible on every page.** 1024x768 has room for an overview strip. The current page and any held modifier are highlighted.

### Presets

- 8 numbered slots and one "freeze" slot. On the preset page: d-pad selects a slot, A loads, X saves, Y (hold) clears.
- "Freeze" stores the current state in one press (Select + Start from any page) and is also written when the app exits, so the next launch can resume.
- A preset stores the eleven normalised values and the switch latch. It does **not** store the input source, which always starts off.
- Loading is a jump in knob positions; the firmware's own smoothing handles it.
- Files live in `audreybrick/data/presets/`, outside the code folder.

## 3. Trade-offs

- **Feedback on L1/R1 rather than the d-pad.** It stays under the index fingers on every page, so no page switch can take it away. The cost is that two of the more comfortable buttons are permanently reserved.
- **Rate-based, not absolute.** A knob can be thrown from one end to the other in 100 ms; a held button cannot. The momentary A button covers the most important fast move. Other fast moves are slower than on the hardware. Presets partly compensate.
- **Six parameters on page 1.** More scrolling than a three-per-page layout, but only one page switch separates the two halves of the instrument, and the loop controls that interact (frequency, body, filters, reverb) stay together.
- **Two buttons for one switch (B and X).** Uses a spare face button to offer both a held and a latched warp. If you prefer fewer functions, drop one.
- **Ramping happens in the audio-side control source**, at the 12 kHz control rate, not in the UI thread at 60 Hz. That avoids audible stepping on the feedback ramp. The UI thread only publishes which buttons are held.
- **Semitone steps on frequency** make the instrument tunable, at the cost of making tiny detunes a Select-held action.

## 4. Alternatives considered and rejected

| Alternative | Why rejected |
|---|---|
| D-pad up/down as feedback, left/right as the selected parameter | Removes parameter selection from the d-pad, forcing one page per parameter or a second modifier |
| One parameter per button chord (e.g. hold X + d-pad = lowpass) with no pages | Eleven chords to memorise, nothing discoverable on screen, and no room for input or presets |
| Feedback-to-maximum as a latch | Unsafe on a speaker and it is not the designers' mod, which is momentary |
| L2/R2 as an analogue feedback control | The Brick's L2/R2 are digital |
| Absolute "snapshot" positions on face buttons (A = feedback 25%, B = 50% ...) | Not how the instrument is played; the sweet spot moves with every other setting |
| Auto-repeat steps instead of a continuous ramp | Audible stepping on feedback and delay time |
| Using Menu or the Fn switch as a modifier | Unknown whether KNULLI reserves them. Revisit after the device checklist |
| A "panic" button that clears all delay lines | Useful, but it is a new feature. Deferred until after the port is signed off. Volume and the limiter cover safety in the meantime |

## 5. Questions for the human

1. Is L1/R1 for feedback, with A as the momentary maximum, acceptable?
2. Two switch buttons (B hold, X latch), or only one?
3. Pages as proposed (6 + 4), or smaller pages?
4. Should presets restore feedback and volume, or leave those two where they are for safety?
5. Are the reset defaults in `PARAMETERS.md` section 4 acceptable?
