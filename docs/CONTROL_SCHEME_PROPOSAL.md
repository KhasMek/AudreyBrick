# Control scheme proposal

**Status: proposal, awaiting the human's approval. Nothing here is decided.** Based on `PARAMETERS.md`. The Brick's button names follow the SDL GameController API. Whether Menu and the Fn switch are free for apps is unknown (platform notes, open question 6), so neither is used.

## 00. Changes made on the device (2026-11-18)

These override section 0 where they differ.

- **Quit is Menu + Start** (one press), as in other KNULLI games. The earlier Start + Select hold was removed because Select held + Start is KNULLI's screenshot.
- **Start by itself does nothing, and Select + Start must stay free** for KNULLI's screenshot. So the "freeze" slot planned for Start in section 0 became **Start + A** (PRESET-B, below).
- **A/B and X/Y are swapped by the launcher** so the Brick's labels match the table below. KNULLI names buttons by position; the Brick is labelled the Nintendo way.
- No in-app screenshot: KNULLI's own works.
- The screen shows no quit hint (the human's choice; Menu + Start is the system default).
- **Recording is Start + Y** (REC, decided by the human on 2026-11-19). Hold Start and press Y, or hold Y and press Start, on any tab (also while the INPUT tab's pop-up list is open, which stays open): recording of the final stereo output to a 16-bit 48 kHz WAV in `data/recordings/` starts or stops. Once per chord (one of the two must be released before it fires again). The Y of the chord does nothing else: with Start held first, that Y never reaches the tabs (no Y + d-pad reset, no X + Y LOOP/ONCE); with Y held first, it was an ordinary Y press until Start came, and from the chord on it is no reset modifier until it is released and pressed again. Start alone still does nothing; Select + Start stays KNULLI's screenshot; Start + Select + Y counts as Start + Y; Menu + Start still quits (finishing the file). While recording, a 4-pixel red frame runs round the whole screen and `REC m:ss` shows in red under the feedback bar's left half (x 20, y 158). No other recording UI (no browser, playback or delete).
- **Start + A saves a new preset** (PRESET-B, decided by the human on 2026-11-23; the "freeze" of section 0), on any tab, also while recording and while the INPUT tab's pop-up is open. Either press order, once per chord, like Start + Y. With Start held first, that A never reaches the tabs, so feedback MAX does not sound; with A held first, MAX sounds from A's press until Start completes the chord, then the tabs are told A is up (MAX ends) and the real A release is held back. MAX never changes the stored feedback, which is what a preset saves, so the order makes no difference to the file. A message (`SAVED PRESET-0008`) appears where the recorder's readout is. Start + Y + A fires both. Section 0b describes the PRESET tab.

## 0b. The PRESET tab and the scrolling tab bar (PRESET-B, 2026-11-23)

Designed with the human (`docs/briefs/PRESET.md`); a first pass, to be judged on the device (`docs/PRESET_RUN_BRICK.md`).

- **The tab bar scrolls.** Eight tabs do not fit at the bar's 24 px text. The bar shows a run of whole tabs that always holds the current tab and its neighbour on each side (LOOP or PRESET at the bar's edge when they are current), then as many more as fit, one at a time, alternately right and left. Where tabs are hidden, a small marker (a tab stub with an arrow pointing to them) sits at that end of the bar, between the tabs and the L1 or R1 key. The run depends only on the current tab (no animation, no memory). L1/R1 still wrap: L1 from LOOP is PRESET.
- **What a preset is** (the human's decisions): one JSON file in the presets folder (`docs/PRESET_FORMAT.md`), holding the sound: the eleven knobs (the stored feedback, not MAX), the delay switch latch, and the four modulation slots. Nothing about the audio input (source, device, file, play mode, gain) and nothing momentary (A or B held) is stored or changed by a load. No preset is loaded at start-up.
- **The tab:** a title line with the count (`12 PRESETS`; `2101 PRESETS, FIRST 2000 SHOWN` past the list's cap), then five rows of 32 px names: **`+ SAVE NEW`** on top, then the presets by file name without `.json` (as the folder sorts them: case-insensitive, numbers in natural order). The highlighted row is in inverse video with `>`; the preset loaded or saved last carries a **LAST** tag until another is loaded or saved (it does not claim the settings still match); arrows at the right show that the list continues above or below. Under the list, one panel: the message line (`LOADED PRESET-0007`, `SAVED PRESET-0008`, `OVERWROTE ..`, `DELETED ..` in green for 2 s; `LOAD FAILED: <REASON>`, `SAVE FAILED: ..`, `OVERWRITE FAILED: ..`, `DELETE FAILED: ..` in inverse red for 4 s, as is `BUSY, TRY AGAIN` when a load, a completed hold or another save arrives while a save still waits for the audio side; that action is dropped) and a hint line (`> LOAD`, `X + > HOLD: OVERWRITE`, `Y + > HOLD: DELETE`; on SAVE NEW `> SAVE NEW` and `START + A: SAVE NEW ON ANY TAB`). During a hold the panel becomes the hold bar: `OVERWRITE PRESET-0007` or `DELETE PRESET-0007` at 24 px, KEEP HOLDING, and a 14 px bar that fills over the second (orange for overwrite, red for delete). An empty folder shows NO PRESETS YET; no folder given, NO PRESET FOLDER SET; a missing folder, PRESET FOLDER NOT FOUND (the first save creates it); a folder that cannot be listed, CANNOT READ THE PRESET FOLDER. The footer's X cell reads `X + > OVERWRITE` and the Y cell `Y + > DELETE` here (`>` is the d-pad icon with only its right arm lit, not a lone right arrow, which is the symbol on the Brick's Start button: only right, held, acts; HOLD does not fit in the cell, the hint line says it).
- **Gestures:** up/down move the highlight one row per press, wrapping at both ends (up from SAVE NEW reaches the last entry in the list); held, a direction repeats at 8 rows per second after 0.25 s, at most one step per frame, and stops at either end (only a new press wraps); both held: nothing. **Right** on SAVE NEW saves a new preset (`preset-NNNN.json`, one more than the highest number); right on a preset loads it, at once. **X held, then right held about 1 s** overwrites the highlighted preset with the current state; **Y held, then right held about 1 s** deletes it. The bar fills while holding; releasing right early does nothing. Pressing or releasing X or Y, releasing right, an up/down press, L1/R1 and focus loss cancel a hold, and a held right stays inert until released (so a hold never restarts by itself, and right is inert after a hold has fired). X + right and Y + right on SAVE NEW do nothing; with X and Y both held right does nothing; X or Y pressed on another tab does not count here until released and pressed again; L1/R1 drop the X and Y states. Left and Select mean nothing on this tab. After a save the new preset is highlighted and marked; after a delete the next preset is highlighted (the previous one at the end of the list, SAVE NEW when none is left). The list is read again when the tab is entered and after each action, not every frame; the highlight stays on the same file when the list changes.
- **Loading is audible by design:** every stored value jumps to the preset's and the knob and modulation smoothing do the rest (no extra fade). With A held, the feedback stays at MAX until A is released, then it is the loaded value.
- **Saving waits for the audio side:** a save (SAVE NEW, overwrite, Start + A) stores the values the audio side has applied, so a change made a moment before is in the file; that normally takes one audio buffer. If the audio side has not caught up within half a second, the save fails with `SAVE FAILED: AUDIO NOT RUNNING` and nothing is written.
- **Messages on the other tabs:** the same message shows in the header where the recorder's readout is (x 20, y 158; green or red, cut to 30 characters). If the recorder has something to say too: a failure beats a success; between two of a kind the newer wins; any preset message beats the running `REC m:ss` (the red border still shows that a recording runs). On the PRESET tab the message is on the tab itself and the header keeps the recorder's readout.

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
- Every open sets the input gain to -inf dB first (howl safeguard). The first open of a device in a run stays there; the player raises it with up/down or Y + up/down. Leaving LIVE closes the device; selecting LIVE again reopens the same device if it is still listed.
- Gain memory (2026-11-20, the human's request; for the running session only, nothing is saved): the gain is remembered per source. FILE has its own gain (unity at start); each capture device has its own, by its name as listed, for up to 8 names (a ninth forgets the least recently opened). A later open of a device whose gain is remembered (after OFF or FILE, after another device, after DEVICE LOST and choosing it again) still goes to -inf dB first and, once the open has succeeded, restores that gain with a 100 ms fade-in. A failed open stays at -inf dB and forgets nothing. Every gain change goes to the current source at once; with LIVE and no device open, or with OFF, changes are not remembered. Selecting FILE applies FILE's gain; OFF leaves the gain as it was. When the source changes, the old source fades out at its own gain and the new one fades in at its own, so the change of gain is never heard. The GAIN row shows the restored value; the warning is unchanged.
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
| **Start** | Nothing by itself (section 00) |
| **Start + Y** | Start / stop recording the output to `recordings/rec-NNNN.wav` in the data folder (`/userdata/saves/audreybrick/` on KNULLI since PRESET-C; section 00, REC) |
| **Start + A** | Save a new preset, `presets/preset-NNNN.json` in the data folder (PRESET-B, section 00 and 0b) |

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
| 7 | **MOD** | the cursor row's value (TARGET, SHAPE, RATE, DEPTH, POLARITY) | the cursor row | select the slot 1-4 (wrapping) | nothing |
| 8 | **PRESET** | right: load (on SAVE NEW: save new) | move through the list | X held + right held 1 s: overwrite | Y held + right held 1 s: delete (PRESET-B, section 0b) |

Every tab shows all eleven values, the feedback level, the switch state, the output meter and the limiter state in a strip that never changes; the tab's own parameters are drawn large with the button that moves each one next to it.

Stepping: a tap moves 1/128 of the knob's travel and holding ramps with acceleration (0.1 of travel per second rising to 0.4 over one second, so full travel takes about 2.9 s; a tuning constant for M4). Ramps are computed on the audio side at the control rate, so they do not zipper.

### Input sources (decided by the human, 2026-11-17)

Off (default), the Brick's internal microphone, a USB audio interface, or an audio file from `input/` in the data folder (`/userdata/saves/audreybrick/input/` on KNULLI since PRESET-C; `audreybrick/data/input/` before). All of them are mixed to mono as L + R, scaled by the input gain, and fed to the original's unity-gain entry point. File playback loops; the file is loaded off the audio thread.

### Modulation tab (as built, MOD-A and MOD-B, 2026-11-20; MOD-C and MOD-D, 2026-11-21)

Designed with the human on 2026-11-20 (`docs/briefs/MOD.md`); the engine side is MOD-A, the tab MOD-B. MOD is the seventh tab, after INPUT; L1/R1 wrap through it (PRESET-B added PRESET after it, so L1 from LOOP is now PRESET, and a second L1 is MOD). It binds no knob: like INPUT, its d-pad, X and Y move the tab's own state (`src/app/ModPanel.h`).

- **Four slots**, each with a shape (sine, triangle, square, ramp up, ramp down, exponential ramp up, exponential ramp down, sample-and-hold, envelope follower on the audio input; MOD-C added the four ramps), a polarity (MOD-C: BIPOLAR, the default, swings both ways round the knob; UNIPOLAR maps the LFO to 0..1, so a positive depth only moves the target above its knob and a negative one only below; the envelope is always unipolar), a rate (0.01 Hz to 2 kHz, exponential; for the envelope the same control is the release, 5 s to 5 ms), a depth (-100 % to +100 % of the target's travel) and one target (None, any of the eleven knobs, the input gain, the delay switch). Applied after the original's parameter smoother; with every slot off the output is bit-identical to the faithful port. Settings last until the app quits; presets store them (PRESET-B, section 0b).
- **Model (MOD-D, the human's design after the second device run):** the rows are one slot's properties, the cards are the four slots, and holding X turns the d-pad to the cards.
- **Screen:** five editor rows for the selected slot (TARGET: the target's name, and the knob's tab after it; SHAPE; RATE, or RELEASE for the envelope, with a position bar; DEPTH with a bar centred on zero; POLARITY, MOD-C: BIPOLAR or UNIPOLAR, and for the envelope UNIPOLAR dimmed with FIXED BY ENVELOPE), the cursor row bright with an accent frame; under them four slot cards side by side (number, shape, target or NO TARGET, rate, depth with the polarity at its right as `+/-` or `+`, and an activity bar centred on zero showing the slot's output, after the polarity, times its depth). The cards are normally dimmed as a group (faint text, as the DELAY tab's dimmed rows); while X is held they are lit and the rows are dimmed instead (the cursor row keeps a thin frame), and the X cap is lit. In both states the selected card has the accent frame and its number in inverse video, and an active card (target set, depth not 0) is on the lighter panel colour with its activity bar in the accent colour, an idle one dark.
- **Gestures (MOD-D):** d-pad up/down moves the cursor between TARGET, SHAPE, RATE, DEPTH and POLARITY (no wrap, no repeat). Left/right change the row's value for the selected slot, right increases: TARGET steps through the list (None, then the knobs in the value strip's order: FEEDBACK, FREQUENCY, BODY, LOWPASS, HIGHPASS, DRY/WET, DECAY, TIME, MIX, REPEATS, VOLUME, then INPUT GAIN, DELAY SWITCH), wrapping at both ends, at once, and repeats while held at 8 per second after the 0.25 s hold delay (the 14 entries go round in about 1.9 s); SHAPE one step per press, no wrap; POLARITY right UNIPOLAR, left BIPOLAR, one step per press (nothing for the envelope); RATE a tap of 1/100 of travel (about 13 % in frequency), DEPTH a tap to the next point of a 2 % grid (exactly 0 is reachable from either side); held, RATE and DEPTH ramp at the knobs' ramp speeds in travel (DEPTH's travel is 2, so its ramp moves twice as many depth units and a ramp passes through 0 without stopping); Select held: fine (RATE 1/1000, DEPTH 0.2 %, ramps at 1/8; nothing on TARGET). For RELEASE, right makes the release longer. **X held** (pressed on MOD): left/right select slot 1-4, wrapping (4 to 1, 1 to 4), one step per press with no repeat; up/down do nothing; Select means nothing; the cursor row is kept (on DEPTH, X + right lands on the next slot's DEPTH). Y + left/right resets the cursor row (TARGET None, SHAPE sine, RATE 1 Hz, DEPTH 0, POLARITY bipolar, which does nothing under the envelope); Y + up/down does nothing. **Y + X + left/right** (either held first) resets the whole selected slot (all five properties, the stored polarity too). There is no target pop-up any more. A DEPTH ramp ends on the 0.2 % grid, so 0.0% always means exactly 0. A tab change, pressing or releasing X or Y, and a row change make held directions inert (so a value ramp or a TARGET repeat never continues into slot selection, nor the reverse); X pressed on another tab does not count on MOD; L1/R1 while X is held changes the tab and drops the X state (X must be pressed again on MOD). The cursor row and the selected slot are remembered across tabs. Selecting a slot is not logged; the half-second `mod: slot ...` settings line stays.
- **Other tabs:** a knob targeted by a slot with non-zero depth carries a violet tag with the slot numbers (`M1`, `M13`) next to its name in the tab rows (under the name), the feedback band (after FEEDBACK) and the value strip (on the label line, or the value line where the label leaves no room; where the full tag fits neither, `M` and the number of slots, then `M`), and a thin violet bar along the bottom of its value bars to the moving value `clamp(knob + offset)`, at least a short stub so a value held at 0 still shows; the printed value stays the knob's own setting. The INPUT tab's gain row has the same (the marker at `clamp(knob + offset)`, none moved while the gain is at -inf), and its meter shows the level times the InputGain ratio while a slot targets the gain. The switch box shows the switch slots' tag, and while only the modulation holds the switch on it is drawn dark with a green frame and green ON and MOD. With no slot active every screen is as before MOD-B (only the tab bar gained MOD).
- **Engine notes** (`docs/PARAMETERS.md` section 6): BODY and TIME (and the delay switch) respond slowly because of the engine's own smoothers; deep, fast cutoff modulation is shallower at audio rates (the slew limit); modulation never opens an input whose gain is at -inf. MOD-C: the envelope follows the input **before** the INPUT tab's gain, so with the gain at -inf an input can drive modulation without being heard; a polarity change is blended over 10 ms like a target change.
- **History:** the human found switching slots unintuitive on the first two device runs (2026-11-21: the slot was a row among its own properties, and the target had a pop-up list); MOD-D replaced the SLOT row with TARGET, moved slot selection to X + left/right on the cards and removed the pop-up. Not yet run on the device.

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

**Superseded by section 0b (PRESET-B, as built).** The original proposal, kept for reference:

- 8 numbered slots and one "freeze" slot. On the preset page: d-pad selects a slot, A loads, X saves, Y (hold) clears.
- "Freeze" stores the current state in one press (Select + Start from any page) and is also written when the app exits, so the next launch can resume.
- A preset stores the eleven normalised values and the switch latch. It does **not** store the input source, which always starts off.
- Loading is a jump in knob positions; the firmware's own smoothing handles it.
- Files live in `presets/` in the data folder, outside the code folder: `/userdata/saves/audreybrick/presets/` on KNULLI (PRESET-C), `audreybrick/data/presets/` where `/userdata/saves` is missing.

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
