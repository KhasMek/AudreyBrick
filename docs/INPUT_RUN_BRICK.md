# Device run: file input

> **Note added 2026-11-23 (PRESET-C):** packages from PRESET-C on keep the user's data (logs, run counter, `input/`, `recordings/`, `presets/`) in `/userdata/saves/audreybrick/` (on the SD card: `saves/audreybrick/`), no longer in `roms/ports/audreybrick/data/`. An old `roms/ports/audreybrick/data/` is not moved automatically: the launcher leaves it untouched and only notes in `stdout.log` that it still holds files (PRESET-D); move its files by hand. Replacing the `audreybrick` folder no longer deletes anything in `saves/audreybrick/`. The steps below are kept as written: for a newer package, read every `roms/ports/audreybrick/data/` (or `data/`) as `saves/audreybrick/`.

For the human. Written 2026-11-18 for the package built after M2b-A, updated for M2b-A2 (crossfade, LOOP / ONCE, 10-minute cap) and M2b-A3 (choosing a file by holding X, with a large pop-up list). **Not yet run.**

## What is new

A sixth tab, INPUT. A WAV file from the card plays into the instrument at the point where the original Audrey II takes its audio input. With the source OFF the instrument behaves exactly as before.

Since M2b-A2 and A3: you choose a file by holding X, and nothing loads until you release X; changing file crossfades; each file plays in LOOP or ONCE mode; files up to 10 minutes are used whole.

## Before you start

Pick three to six WAV files. Good choices: a short loop or drone, something rhythmic, a short one-shot sound (for ONCE), and one long file (over ten minutes) to see the cut. Any sample rate and mono or stereo are fine. More than five files lets you see the list scroll.

## Steps

1. **Save your logs first.** Copy `roms/ports/audreybrick/data/` from the card to the Mac. Replacing the folder deletes it.
2. **Copy the two items** from `dist/AudreyBrick/` into `roms/ports`, replacing the old ones: `AudreyBrick.sh` and the folder `audreybrick`.
3. **Put your WAV files in** `roms/ports/audreybrick/data/input/` on the card. Keep the originals on the Mac too. On a FAT or exFAT card run `rm "/Volumes/<card name>/roms/ports/"._*` in Terminal, then eject. (The app ignores the hidden `._` files macOS adds inside `data/input`.)
4. **Start AudreyBrick** with the volume low. Press **L1** once to reach INPUT. SOURCE shows OFF, GAIN 0.0 dB.
5. **Select the file source.** Press d-pad **right**. The FILE row says NOTHING LOADED and HOLD X: CHOOSE FILE. Nothing should sound yet.
6. **First file.** Hold **X**, press **down** (the list opens on the first file), release X: with nothing playing yet, the highlighted file loads (LOADING briefly) and plays. Listen at minimum feedback (do not touch R2 yet).
   Then hold **X** and press **right** and **left**: the FILE row shows each name and "n OF m". Release X on another file: it loads.
7. **The list.** Hold **X** and press **down**: a large list opens over the screen, the playing file marked PLAY and named under the list. Releasing X now, without moving, only closes it. Move with up/down (hold to scroll, about 8 a second), release X on another file. Is the list easy to read at arm's length?
8. **Crossfade.** Choose another file a few times, quickly. The old file should keep playing until the new one is ready, then blend into it over about a tenth of a second. Note any click or gap.
9. **Nothing by accident.** Hold X and release it without touching the d-pad: nothing changes. Hold X, press right, then press **L1**: the choice is cancelled (and the tab changes). Choose a long file and, while it shows LOADING, choose the playing file again: the long one is dropped and the playing file carries on.
10. **Play again and ONCE.** **X + SELECT** plays the file again from the start. **X + Y** switches LOOP to ONCE (shown in the FILE row); let the file run out: it fades out and shows ENDED. X + SELECT plays it again. X + Y back to LOOP.
11. **Long file.** Choose the file over ten minutes. Note roughly how long LOADING shows. The FILE row should say CUT TO 10 MIN and the total time 10:00.
12. **Gain and feedback.** D-pad **up/down** moves the gain (Y + up returns to 0 dB); watch the input meter. Raise feedback with **R2**; try **A** briefly.
13. **Other tabs.** Press **R1** to go round to LOOP. The small IN bar under the output meters should show the input level. Change a few parameters while a file plays.
14. **Source off.** Back on INPUT, press d-pad **left**. The input fades out and the FILE row dims. Press **right** again: the loaded file plays from its start.
15. **Listen for trouble** throughout: crackles, dropouts, a stuck sound. Glance at the CPU cell during a crossfade and with the source off.
16. **Quit** with Menu + Start. **Send back** the `roms/ports/audreybrick/data/` folder (not the input files) and your notes.

## Things I would like your opinion on

- Is the 100 ms crossfade the right length?
- Is the pop-up list readable (eight large names, the highlight, PLAY), and is 8 files a second a good scroll speed?
- Should ONCE be remembered per file, or is one mode for all files right?

## Notes

- Switching LOOP -> ONCE within the last 10 ms of a file plays it one more time before it stops (the loop crossfade already under way completes; this is deliberate, so the switch cannot click).
- With a file playing, opening the list (X + up or down) and releasing X without moving only looks; with nothing playing it loads the highlighted file.
- A file asked for while a crossfade is still running shows LOADING and starts loading a moment later, once the crossfade is over (this keeps at most two long files in memory).

## If something goes wrong

Quit with Menu + Start; if the app does not respond, press Menu + Start twice quickly, or as a last resort hold the power button. The logs survive.

## What the logs will show

How long the folder scan and each file load took on the SD card, the format of each file, whether it was converted or cut and its size in memory, the free memory (MemAvailable) at start and after each load, mode changes and restarts, the input level in each ten-second line, and the audio callback load with a file playing.

## Results

| Date | Package version | What happened |
|---|---|---|
| | | |
