# Device run: live input (microphone, USB audio)

For the human. Written 2026-11-19 for the package built after M2b-B. **Not yet run.**

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
- **Regular dropouts or a choppy sound with a device** (the `live:` part of the log shows underruns): AudreyBrick doubles its live buffer by itself after two dropouts within a second (the log says `live target raised`). If dropouts still happen, or to start with a larger buffer, edit `AUDREY_LIVE_TARGET="${AUDREY_LIVE_TARGET:-2}"` near the top of `AudreyBrick.sh` to 4 or 8 (1 to 32; more adds delay), run again and note the setting.
- **CANNOT OPEN:** photograph the reason, try SYSTEM DEFAULT and the device's own name, and note which works.

## What the log will tell us

- `launcher: pactl list short sources` and `launcher: arecord -L` in `stdout.log`: PipeWire's and ALSA's names for the devices.
- `input: capture devices: ...` lines: what SDL listed, at start and on every change.
- `input: capture device opened: ...`: the spec asked for and obtained, and how long the open took; or `could not be opened` with SDL's reason.
- The `live:` part of each statistics line: buffer fill, its range and the target, underruns, skips (capture fast), repeats (capture slow), frames captured and played, and `capture clock ...x output`, which shows drift directly.

## Things I would like your opinion on

- Is the warning readable, and is -inf dB at every open right, or too cautious?
- Is the PRE bar enough to tell that the mic works before raising the gain?
- Is the delay acceptable for playing?
