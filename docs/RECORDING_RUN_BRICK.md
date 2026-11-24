# Device run: recording (REC)

> **Note added 2026-11-23 (PRESET-C):** packages from PRESET-C on keep the user's data (logs, run counter, `input/`, `recordings/`, `presets/`) in `/userdata/saves/audreybrick/` (on the SD card: `saves/audreybrick/`), no longer in `roms/ports/audreybrick/data/`. An old `roms/ports/audreybrick/data/` is not moved automatically: the launcher leaves it untouched and only notes in `stdout.log` that it still holds files (PRESET-D); move its files by hand. Replacing the `audreybrick` folder no longer deletes anything in `saves/audreybrick/`. The steps below are kept as written: for a newer package, read every `roms/ports/audreybrick/data/` (or `data/`) as `saves/audreybrick/`.

For the human. Written 2026-11-19 for the package built after the REC task. **Not yet run.**

## What is new

**Start + Y** records what AudreyBrick plays, on any tab, to a 16-bit stereo 48 kHz WAV file in `roms/ports/audreybrick/data/recordings/` (`rec-0001.wav`, `rec-0002.wav`, ...). Start + Y again stops. While it records, a thin red frame runs round the whole screen and `REC 1:23` counts up in red under the feedback bar; after a stop the same place shows `SAVED REC-0003.WAV 1:23` for 2 seconds. Start alone still does nothing; Select + Start is still KNULLI's screenshot; Menu + Start still quits.

What the device has to tell us: whether the SD card keeps up (the app holds 4 seconds of sound in memory for slow moments), how long the card takes to write, and whether the files play on a computer.

## Before you start

Have headphones, a computer that can play WAV files, and, for step 7, a USB audio device or a few WAV files in `data/input` (as in the earlier input runs). At least 1 GB free on the card.

## Steps

1. **Save your logs first.** Copy `roms/ports/audreybrick/data/` from the card to the Mac. Replacing the folder deletes it (and any recordings in it).
2. **Copy the two items** from `dist/AudreyBrick/` into `roms/ports`, replacing the old ones: `AudreyBrick.sh` and the folder `audreybrick`.
3. **Start AudreyBrick** with the volume low. Raise the feedback with **R2** until it drones.
4. **First recording, on LOOP.** Hold **Start**, press **Y**, release both. The red frame should appear at once and `REC 0:00` start counting under the feedback bar. Let it run about 20 seconds, change a few LOOP parameters, then **Start + Y** again. Note: did the frame vanish and `SAVED REC-0001.WAV 0:20` (or so) show for about 2 seconds?
5. **The other order and every tab.** Hold **Y**, press **Start**: recording starts again. While it runs, go through every tab with **L1/R1** (LOOP, FILTER, REVERB, DELAY, OUT, INPUT), moving a parameter on each. The frame and the timer should stay on every tab. Stop with Start + Y. Check: after the chord, does **Y + d-pad** still reset only when Y is pressed again on its own?
6. **Input OFF.** On INPUT with the source OFF: record about 15 seconds of the drone alone. Stop.
7. **Input FILE and LIVE.** Select FILE and a file (hold X as before), raise the gain, record about 30 seconds; while recording, open the file list (X + down) and press Start + Y (keep X held, let go of the d-pad once the list is open, then Start + Y): the recording stops and the list stays open. Start + Y again, then switch the source to LIVE and open your USB device (or the built-in mic, with headphones: speaker and mic can howl). Record about 30 seconds, then stop. The recording should carry on across the source changes.
8. **Long recording.** Start a recording and leave the Brick droning untouched for **20 minutes** (the screen should stay on, as in the earlier runs). Glance at the timer now and then; note whether `DROP` ever appears next to it. Stop with Start + Y. Note how long `REC SAVING` shows, if you see it at all.
9. **Quit while recording.** Start a recording, wait about 30 seconds, then quit with **Menu + Start** while it runs.
10. **Busy press (optional).** Start AudreyBrick again (step 9 quit it) and start a recording. Stop it and immediately press Start + Y again: either a new recording starts, or `REC BUSY` shows for 2 seconds (press again).
11. **Copy the recordings off** (Brick off, card in the Mac): `roms/ports/audreybrick/data/recordings/`. There should be one file per recording, numbered without gaps from `rec-0001.wav`. Play each on the computer:
    - do they all open, and does each length match what the screen said?
    - do they sound like what you heard (no clicks at the start or end, no gaps, no crackle)?
    - does the quit-while-recording file (step 9) play to its end?
    - does the 20-minute file (step 8) play right through?
12. **Send back** the `roms/ports/audreybrick/data/` folder **without** the `recordings` folder (logs only: `audreybrick*.log`, `stdout*.log`), and your notes. Send a recording only if I ask for one or one sounds wrong.

## What the logs will show (for me)

- At start, stdout.log: the free space on the data folder and how many recordings the folder holds.
- On each start: the file, the free space and the ring size. Every 10 seconds while recording: frames written, the ring's highest fill, dropped frames, the longest single write. On each stop: frames, length, bytes, dropped frames, the highest fill, the time to finish the file, and why it stopped.

## Things I would like your opinion on

- Is the red frame visible enough without being distracting, and is `REC m:ss` under the feedback bar easy to read?
- Is Start + Y comfortable to press while playing?
- Should a stopped-by-the-card message (`REC STOPPED: CARD FULL`, `REC STOPPED: FILE LIMIT`) stay longer than 4 seconds?

## Gain memory

Added 2026-11-20 (the input gain is now remembered per source until you quit). Do these after step 11 or in the same session before quitting; headphones in, feedback low, USB interface plugged in.

1. **Mic gain kept across OFF and FILE.** On INPUT select LIVE and open `audiocodec, ` (the built-in mic). Raise the gain to a level you can hear and note the GAIN value. Select FILE (d-pad left): GAIN shows FILE's own value. Select OFF, then FILE, then LIVE again: the mic reopens and GAIN should show the value you noted, with the sound fading in (no click, no jump). Note anything you hear at the switch.
2. **USB and mic independent.** Hold X and open the USB interface: if it has not been opened since AudreyBrick started (step 7 opens it), it shows -inf dB; otherwise it comes back at the gain it had. Set it to a clearly different gain from the mic's. Switch back to the mic (hold X, choose it): its own value returns. Back to USB: its own value returns. Then FILE and back to LIVE: the device that was open comes back at its own value.
3. **First open still silent.** Quit (Menu + Start) and start AudreyBrick again. LIVE, open the mic: GAIN must show -inf dB and nothing of the mic is heard until you raise it (the memory lasts only until you quit). The log has a line `input: gain restored to ... for '...'` for each restore in steps 1 and 2.
