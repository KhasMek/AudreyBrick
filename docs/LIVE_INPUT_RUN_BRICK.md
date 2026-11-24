# Device run: live input (microphone, USB audio)

> **Note added 2026-11-23 (PRESET-C):** packages from PRESET-C on keep the user's data (logs, run counter, `input/`, `recordings/`, `presets/`) in `/userdata/saves/audreybrick/` (on the SD card: `saves/audreybrick/`), no longer in `roms/ports/audreybrick/data/`. An old `roms/ports/audreybrick/data/` is not moved automatically: the launcher leaves it untouched and only notes in `stdout.log` that it still holds files (PRESET-D); move its files by hand. Replacing the `audreybrick` folder no longer deletes anything in `saves/audreybrick/`. The steps below are kept as written: for a newer package, read every `roms/ports/audreybrick/data/` (or `data/`) as `saves/audreybrick/`.

For the human. Written 2026-11-19 for the package built after M2b-B; the section "Built-in mic run" was added for the package built after M2b-B2 (the built-in microphone). **The built-in mic run has not been run yet.**

## Built-in mic run (package after M2b-B2)

**Gain memory (2026-11-20, applies to any package built after it):** the input gain no longer returns to -inf at every open. The first open of a device in a run is still at -inf; after that, the device comes back at the gain you left it at (each device has its own, and FILE has its own), fading in over a tenth of a second once it is open. Steps 7 and 8 below say what to expect with it; `docs/RECORDING_RUN_BRICK.md` ("Gain memory") has the steps that check it.

What changed: on the Brick the device list no longer has SYSTEM DEFAULT (it broke the mic until reboot). The built-in mic, `audiocodec, `, now opens directly on its sound card (the DEVICE row shows DIRECT), and AudreyBrick switches the mic's input mixer on before opening it and back to how it was after closing it. Headphones in, volume low, for every step except 6.

1. **Reboot the Brick first** if SYSTEM DEFAULT was chosen in AudreyBrick (or anything recorded through the audio server) since it last started. Otherwise the mic can stay blocked ("Device or resource busy").
2. **Save your logs,** then copy the new `AudreyBrick.sh` and `audreybrick` folder into `roms/ports`, replacing the old ones. Plug in the USB interface before starting.
3. **Start AudreyBrick.** L1 to INPUT, d-pad right twice (LIVE). Hold X and press down: the list should show `audiocodec, ` and the USB interface, and **no SYSTEM DEFAULT**. Photo of the list.
4. **Choose `audiocodec, `** (cursor on it, release X). The built-in mic is opened as mono, so expect `48000 Hz 1 CH 256 FR` with `DIRECT` (DIRECT sits on the DEVICE row's top line, next to the word DEVICE; the format line is below the name). GAIN shows -inf dB. If it shows `2 CH`, note it (the device gave two channels; it still works, mixed to one). If it says CANNOT OPEN, photograph the reason and skip to step 7.
5. **PRE bar, gain, clap.** Tap the Brick or speak near it: does the PRE bar move while the gain is still -inf? Raise the gain with d-pad up (or Y + up for 0 dB) at minimum feedback: do you hear the mic through the instrument? Note the gain you need for speech at arm's length. Clap once or twice sharply: none, slight or clearly late delay?
6. **Speaker howl test.** Feedback at its minimum (hold L2 until FEEDBACK stops falling), system volume low, input gain at -inf. Unplug the headphones and raise the input gain slowly. Note the gain at which it starts to ring or howl, then bring the gain down at once. (D-pad left also stops it by closing LIVE, but with gain memory the mic then comes back at the howling gain in steps 7 and 8; if you used it, lower the gain as soon as the mic reopens.) Headphones back in.
7. **USB still works.** Hold X, step to the USB interface, release. It should open by its name (no DIRECT) and sound as in the first run. Then choose `audiocodec, ` again: it should reopen (DIRECT). With a package from before 2026-11-20 the gain is back at -inf; with gain memory it comes back at the gain the mic had, with a short fade-in, and the USB interface keeps its own.
8. **Leave and return.** D-pad left (FILE), then right (LIVE): the mic reopens, no clicks. Before 2026-11-20 the gain is at -inf; with gain memory it is the gain the mic had.
9. **Quit** with Menu + Start. **Send back** the `roms/ports/audreybrick/data/` folder and your notes (the list photo, what step 4 showed, the gain for speech, the delay impression, the howl point, whether USB still worked).

The log shows `input: capture route: ...` lines (which route opened the device) and `input: mixer: ...` lines (the three mixer values read, set, and restored at each close).

## First run (M2b-B, done 2026-11-19)

The steps below were the first live-input run. They mention SYSTEM DEFAULT, which the Brick's list no longer offers. They also say the gain goes back to -inf at every open; since 2026-11-20 that holds only for the first open of each device in a run (gain memory, above).

## What is new

The INPUT tab has a third source, LIVE: a capture device (the Brick's built-in microphone, or your USB audio interface) feeds the instrument where the original takes its audio input. You choose the device by holding X, as with files. Every time a device opens, the input gain is set to -inf dB; you raise it yourself. While LIVE is selected the tab warns SPEAKER + MIC CAN HOWL. The main purpose of this run is to learn what the Brick reports: device names, what opening them gives, the mic level, the delay, and whether the capture and output clocks drift.

**Ears first.** Mic plus speaker plus a feedback instrument can howl. Do steps 4 to 9 on headphones at low volume. Only step 10 uses the speaker, at low volume and low gain.

## Steps

1. **Save your logs first.** Copy `roms/ports/audreybrick/data/` from the card to the Mac (replacing the folder deletes it).
2. **Copy the two items** from `dist/AudreyBrick/` into `roms/ports`, replacing the old ones: `AudreyBrick.sh` and the folder `audreybrick`.
3. **Plug in the USB interface before starting** (with a source connected to it, quiet at first). Put headphones in. Volume low.
4. **Start AudreyBrick.** L1 to INPUT. D-pad right twice: SOURCE shows LIVE, the DEVICE row says NO DEVICE CHOSEN, the red warning shows. Nothing should sound.
5. **The list.** Hold X and press down: the device list opens. Note every name shown (a photo is fine). Release X: with nothing open, the highlighted entry (SYSTEM DEFAULT) opens. The DEVICE row shows what it delivers (for example `48000 Hz 1 CH 256 FR`), GAIN shows -inf dB.
6. **Internal mic, SYSTEM DEFAULT.** Tap the Brick or speak near it: does the PRE bar move while the gain is still -inf? Press Y + up (0 dB): do you hear it through the instrument at minimum feedback? Note the input meter's level for speech at arm's length, and the gain you need.
7. **Internal mic by its name.** Hold X, step with right to the built-in codec's own name (something like `audiocodec, `), release. The gain jumps back to -inf. Raise it again. Same sound? Note if it says CANNOT OPEN instead (photo the reason).
8. **USB interface.** Choose it the same way. Note its name, what the row says it delivers, and whether it opens. Raise the gain with your source playing.
9. **Clap test (delay).** With the USB source or the mic at a usable gain and feedback at minimum, clap or tap sharply once or twice. Estimate the delay you hear between the clap and the sound in the headphones: none noticeable, slight (like a short slap), or clearly late.
10. **Speaker, carefully.** First: feedback at its minimum (hold L2 until the FEEDBACK value stops falling), system volume low (volume buttons), input gain at -inf. Then unplug the headphones and raise the input gain slowly with the mic open. Note at what gain it starts to ring or howl. Bring the gain down at once (or press d-pad left: LIVE closes).
11. **Plug in while running.** Headphones back in. With the mic open, unplug the USB interface; hold X and press down: is it gone from the list? Release, plug it in again, hold X and press down: is it listed again? Choose it.
12. **Unplug while playing.** With the USB interface open and sounding, pull the cable. The sound may stop abruptly at that moment, or fade out; either way there must be no crackle loop and no stuck sound, and within a second or so the row should say DEVICE LOST. Note which you heard. Plug it back in and choose it again.
13. **Five minutes of drift.** Open the USB interface (or the mic if USB failed), gain at a comfortable level, raise the feedback with R2 to a steady drone, and leave it for five minutes without touching anything. Listen for regular small dropouts or skips (note roughly how often).
14. **Back and forth.** Switch LIVE -> FILE -> LIVE a few times (left/right): no clicks; each return to LIVE reopens the same device with the gain at -inf.
15. **Quit** with Menu + Start. **Send back** the `roms/ports/audreybrick/data/` folder and your notes (device names, mic level and gain, delay impression, the howl point, what you heard when unplugging, dropouts heard in step 13).

## If something goes wrong

- **Howl:** press d-pad left at once (LIVE closes the device), or hold d-pad down to lower the gain.
- **Regular dropouts or a choppy sound with a device** (the `live:` part of the log shows underruns): AudreyBrick doubles its live buffer by itself after two dropouts within a second, or after one when the buffer had no room left for its corrections (the log says `live target raised`). If dropouts still happen, or to start with a larger buffer, edit `AUDREY_LIVE_TARGET="${AUDREY_LIVE_TARGET:-2}"` near the top of `AudreyBrick.sh` to 4 or 8 (1 to 32; more adds delay), run again and note the setting.
- **CANNOT OPEN:** photograph the reason and note the device. (The first run's advice to try SYSTEM DEFAULT no longer applies on the Brick: it is not offered any more.)
- **The mic is silent or says Device or resource busy:** reboot the Brick and try again before anything else.

## What the log will tell us

- `launcher: pactl list short sources` and `launcher: arecord -L` in `stdout.log`: PipeWire's and ALSA's names for the devices.
- `input: capture devices: ...` lines: what SDL listed, at start and on every change.
- `input: capture device opened: ...`: the spec asked for and obtained, and how long the open took; or `could not be opened` with SDL's reason.
- The `live:` part of each statistics line: buffer fill, its range and the target, underruns, skips (capture fast), repeats (capture slow), frames captured and played, and `capture clock ...x output`, which shows drift directly.

## Things I would like your opinion on

- Is the warning readable, and is -inf dB at every open right, or too cautious?
- Is the PRE bar enough to tell that the mic works before raising the gain?
- Is the delay acceptable for playing?
