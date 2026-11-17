# Control scheme proposal

**Status: proposal, awaiting the human's approval. Nothing here is decided.** Based on `PARAMETERS.md`. The Brick's button names follow the SDL GameController API. Whether Menu and the Fn switch are free for apps is unknown (platform notes, open question 6), so neither is used.

## 0. Revision after the human's feedback (2026-11-17)

Accepted as a first test, with these changes. Where the sections below disagree, this section wins. The details are settled in M2.

- **L1 / R1 switch tabs**, as GrainBrick does.
- **Feedback moves to L2 (down) / R2 (up)**, live on every tab. A (hold) stays as momentary maximum.
- **One tab per section of the instrument** rather than two large pages: LOOP (frequency, body), FILTER (lowpass, highpass), REVERB (dry/wet, decay), DELAY (mix, time, repeats, switch), OUT (volume, limiter), INPUT (source, gain, meter), PRESETS. With only two to four parameters per tab, M2 should try mapping them straight onto buttons (for example d-pad left/right for the first parameter, up/down for the second) instead of select-then-adjust.
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
