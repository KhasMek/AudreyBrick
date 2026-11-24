# Device run: presets (PRESET) and the new data folder

For the human. Written 2026-11-23 for the package built after PRESET-A, PRESET-B, PRESET-C and PRESET-D. **Not yet run.**

## What is new

- **Your files live outside the app folder** (PRESET-C, PRESET-D). Logs, the run counter, `input/`, `recordings/` and the new `presets/` live in `/userdata/saves/audreybrick/` on the Brick, which should be `saves/audreybrick/` on the SD card (your card's files are already there). Replacing the `audreybrick` folder on an update no longer touches them. The launcher never moves anything by itself any more: if an old `roms/ports/audreybrick/data/` folder is on the card and holds anything, every start leaves it untouched and writes one line `launcher: old data folder: ...` to `stdout.log` (and one to stderr) saying so; move such files by hand.
- **Presets** (PRESET-B). An eighth tab, **PRESET**, after MOD. A preset is one small file (`preset-0001.json`, ...) holding the sound: the eleven settings (the feedback as set with L2/R2, not A's MAX), the delay switch latch, and the four modulation slots. It holds nothing about the audio input and nothing momentary. Nothing is loaded at start.
- **Start + A** on any tab saves a new preset (the "freeze" of the original plan), like Start + Y records. A message such as `SAVED PRESET-0008` appears under the feedback bar's left end, where `REC m:ss` shows.
- **The tab bar scrolls.** Eight tabs no longer fit, so the bar shows the current tab with at least its neighbour on each side; a small arrow on a tab stub at either end of the bar means more tabs that way. L1 from LOOP now goes to PRESET (MOD is a second L1).

The buttons on PRESET:

| Buttons | Does |
|---|---|
| Up / down | move through the list; hold to scroll (8 rows a second); a press at either end goes round (up from `+ SAVE NEW` goes to the bottom, the last entry in the list) |
| Right on `+ SAVE NEW` | save a new preset |
| Right on a preset | load it, at once |
| X held, then right held about 1 s | overwrite the highlighted preset with the current sound (an orange bar fills) |
| Y held, then right held about 1 s | delete the highlighted preset (a red bar fills) |
| Start + A (any tab) | save a new preset |

Letting go of right early, or pressing or letting go of X or Y, or pressing up/down or L1/R1 during a hold, cancels it and nothing happens; so does a save (Start + A) finishing during the hold. An action that arrives while a save is still waiting for the audio side is not done: the screen says `BUSY, TRY AGAIN` in red; do it again. The preset loaded or saved last is marked **LAST** in the list. On PRESET the footer's X and Y cells read `X + d-pad OVERWRITE` and `Y + d-pad DELETE` (right, held; the hint line above them says HOLD).

## Before you start

Headphones, the volume low (loading a preset jumps straight to its sound, which can be loud from silence). The Mac with a card reader.

## Steps

### Part 1: install and check the data folder

1. **Optional backup.** With the card in the Mac, copy `saves/audreybrick/` to the Mac. Note how many files are in `saves/audreybrick/recordings/` and the number in `saves/audreybrick/run-counter`.
2. **Look in `roms/ports/audreybrick/data/` first.** If it exists and holds anything, copy it to the Mac and note what it is: the next step deletes it. Then **replace the `audreybrick` folder** in `roms/ports` with the package's `dist/AudreyBrick/audreybrick/` (Finder's "Replace" is fine once that folder is empty or gone). The package has no `data/` folder.
3. **Replace `AudreyBrick.sh`** in `roms/ports` with the new one.
4. **Start AudreyBrick once**, check that it plays, and quit with **Menu + Start**.
5. **Check on the Mac** (card in the reader) that `saves/audreybrick/` holds the files:
   - `input/`, `recordings/`, `presets/` (empty) and the logs are there; `recordings/` has the same files as in step 1, `input/` your input WAV files.
   - `run-counter` holds the number from step 1 **plus one** (the same as in `stdout.log`'s first line, `launcher: AudreyBrick run N, ...`).
   - `stdout.log` has `launcher: data folder: /userdata/saves/audreybrick (it exists and is writable)`.
   - `roms/ports/audreybrick/data` should not exist (step 2 replaced the folder), and `stdout.log` should have no `launcher: old data folder: ...` line. That line appears only when an old `data/` with content is on the card; the launcher then leaves it alone.
6. **Start AudreyBrick again.** Record a few seconds (**Start + Y**, then **Start + Y**): the readout says `SAVED REC-NNNN.WAV` with the number after your highest recording. Go to INPUT (**L1** three times), choose FILE (**right**), then hold **X** and press **up**: the file list pops up and shows your WAV files. Release X (with nothing playing yet this loads the highlighted file, which is fine). Quit.
7. **On the Mac:** the new recording is in `saves/audreybrick/recordings/`.

### Part 2: presets

8. **The tab bar on every tab.** Start, raise the feedback with **R2** until it drones, keep the volume low. Press **R1** through all eight tabs and back: on each, the current tab and the tabs either side of it are shown whole; an arrow on a small stub at the left and/or right end appears exactly where tabs are hidden (none on the left on LOOP, none on the right on PRESET). Press **L1** on LOOP: you land on PRESET. Note whether the jumping bar and the markers are easy to follow.
9. **The empty tab.** On PRESET: the title `PRESETS`, `0 PRESETS`, `+ SAVE NEW` highlighted, `NO PRESETS YET` under it, and a hint line at the bottom.
10. **Save.** Set a sound you will recognise: on LOOP move FREQUENCY and BODY, on FILTER the LOWPASS, and on MOD give slot 1 a target and a depth (for example SINE on LOWPASS, 1 HZ, +30 %), so it moves audibly. Go to PRESET and press **right** on `+ SAVE NEW`: `SAVED PRESET-0001` in green, the new row `preset-0001` highlighted and marked **LAST**, `1 PRESET`.
11. **Load restores the sound and the modulation.** Change the sound clearly (another FREQUENCY, LOWPASS, slot 1's depth back to 0 with **Y + left** on MOD's DEPTH row). Back on PRESET, with `preset-0001` highlighted, press **right**: `LOADED PRESET-0001`, the sound jumps back, and slot 1 modulates again (check on MOD: target, shape, rate, depth and polarity as saved). The INPUT tab's source and gain must not change (set the input to FILE or LIVE before loading to check).
12. **Start + A on several tabs.** On LOOP press **Start + A** (hold Start, press A): `SAVED PRESET-0002` under the feedback bar. Then on MOD hold **A** first (the feedback jumps to MAX) and press **Start**: MAX stops at once, `SAVED PRESET-0003`. Then on INPUT with the source FILE, hold **X** and press **up** so the file list pops up; while still holding X, press **Start first, then A**: `SAVED PRESET-0004`, the list stays open (and A does not jump the feedback to MAX). Go to PRESET: four presets, the last saved marked LAST.
13. **Start + A while recording.** Start a recording (**Start + Y**), then press **Start + A** on any tab: the preset message replaces `REC m:ss` for 2 seconds while the red frame stays, then `REC m:ss` comes back. Stop the recording; the file must play normally later.
14. **Overwrite.** Change the sound. On PRESET, highlight `preset-0002`, hold **X**, then hold **right**: the bottom panel says `OVERWRITE PRESET-0002` and an orange bar fills over about a second; at the end `OVERWROTE PRESET-0002`. Then try again but let go of right half way: nothing happens. Load `preset-0002`: it is the overwritten sound.
15. **Delete.** Highlight `preset-0003`, hold **Y**, hold **right** about a second (red bar): `DELETED PRESET-0003`, the highlight moves to the next preset. Also try: Y + right on `+ SAVE NEW` (nothing), X + Y + right (nothing), X held on LOOP then L1 to PRESET and right (nothing until X is pressed again on PRESET).
16. **A loud preset from silence.** Save a loud, droning sound (feedback high, volume where you like it) as a new preset. Turn the feedback all the way down with **L2** until it is silent. Load the loud preset: it should come in at once (knob smoothing only, no fade) without a crack or a click beyond the sound itself. Keep the headphone level low for this one.
17. **Quit and find them again.** Quit with **Menu + Start**, start again: the PRESET tab lists the same presets (nothing is loaded at start: the sound is the usual start-up sound). On the Mac, `saves/audreybrick/presets/` holds the `preset-NNNN.json` files.
18. **A renamed file and a broken one (on the Mac).** In `saves/audreybrick/presets/`: rename `preset-0001.json` to `Night drone.json`; copy `preset-0002.json` to `Broken.json`, open `Broken.json` in a text editor and delete its last line (the closing `}`), save. Put the card back, start, go to PRESET: `Broken` and `Night drone` are listed (by name, with `.json` hidden). Loading `Night drone` works. Loading `Broken` shows `LOAD FAILED: SYNTAX ERROR` in red and changes nothing. Saving new now takes the number after the highest remaining one.
19. **Many presets for scrolling.** On the Mac copy one preset about 30 times (`Copy 1.json`, ... or Finder's duplicates; any names ending in `.json`). On PRESET: hold **down** to scroll (8 rows a second, it stops at the bottom), the arrows at the right of the list show there is more above or below, **up** on `+ SAVE NEW` jumps to the bottom. Delete the copies on the Mac afterwards if you like.
20. **Quit** with Menu + Start.
21. **Send back** the logs from `saves/audreybrick/` (`audreybrick*.log`, `stdout*.log`; not the recordings) and the `presets/` folder, with your notes from step 5.

## What the logs will show (for me)

- `stdout.log`: `launcher: data folder: ...` and `launcher: presets folder: ... (N preset file(s))` on every start, and `launcher: old data folder: ...` only while an old `roms/ports/audreybrick/data/` holds anything.
- `audreybrick.log`: at start `preset: folder /userdata/saves/audreybrick/presets: N preset(s) listed; next new preset preset-NNNN.json`; one line per action, for example `preset: saved preset-0002.json (Start + A); 2 preset(s) in the folder; file work 8.3 ms`, `preset: loaded preset-0001.json`, `preset: overwrote preset-0002.json (X + right held); file work 7.9 ms`, `preset: deleted preset-0003.json (Y + right held); 3 preset(s) left; file work 2.1 ms` (the `file work` figure is how long the card write, its fsync and the rescan took on the UI thread), every failure with its reason and detail (`preset: load Broken.json failed: syntax error (line 25, column 1: unexpected end of text)`: a saved preset has 25 lines, so removing the last one ends the text at line 25), and `preset: ... ignored: the previous save is still waiting` for anything that showed BUSY, TRY AGAIN. After a load, the `mod: slot ...` lines show the slots that were loaded.

## Things I would like your opinion on

- **The gestures:** right to load, X + right held to overwrite, Y + right held to delete. Is right-to-load too easy to hit by accident (it changes the sound at once)? Would load on release, or a short hold, be better?
- **The hold time:** is about a second right for overwrite and delete? Is the bar clear enough?
- **The scrolling tab bar:** is it easy to see where you are, and are the end markers noticeable? Would you rather the bar kept its place and only moved when needed?
- **The list:** five rows at the large text size; readable? Enough rows? Is the LAST mark useful?
- **Start + A:** comfortable? Does the MAX you hear when you press A first bother you?
- **Messages:** are the green / red messages under the feedback bar on the other tabs noticeable, and long enough?
- **Messages while recording:** on the other tabs a preset message (Start + A's `SAVED ...`, or a failure) takes the place of the recording timer `REC m:ss` (and of `DROP`, if frames were dropped) for 2 seconds (4 for a failure), while the red frame stays. Is it acceptable that the timer and DROP are hidden for that long?
- **Wrapping:** up from `+ SAVE NEW` going to the bottom (the last entry in the list): useful or confusing?
- **The footer on PRESET:** `X + d-pad OVERWRITE` / `Y + d-pad DELETE` (the word HOLD does not fit in the cell; the hint line says it). Clear enough?
