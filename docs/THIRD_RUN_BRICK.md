# Third run on the Brick

> **Note added 2026-11-23 (PRESET-C):** packages from PRESET-C on keep the user's data (logs, run counter, `input/`, `recordings/`, `presets/`) in `/userdata/saves/audreybrick/` (on the SD card: `saves/audreybrick/`), no longer in `roms/ports/audreybrick/data/`. An old `roms/ports/audreybrick/data/` is not moved automatically: the launcher leaves it untouched and only notes in `stdout.log` that it still holds files (PRESET-D); move its files by hand. Replacing the `audreybrick` folder no longer deletes anything in `saves/audreybrick/`. The steps below are kept as written: for a newer package, read every `roms/ports/audreybrick/data/` (or `data/`) as `saves/audreybrick/`.

For the human. Written 2026-11-18 for the package built after M3-C. **Run on 2026-11-18; see Results.** A short check of two changes.

## What changed

- **Quit is now Menu + Start**, pressed once, like other KNULLI games.
- **Select + Start no longer quits.** It is KNULLI's screenshot, and the "HOLD TO QUIT" banner is gone, so screenshots show the normal screen.
- A small `MENU+START QUIT` reminder sat in the status line in the package that was tested. It was removed afterwards at the human's request.

## Steps

1. **Save your logs first.** Copy `roms/ports/audreybrick/data/` from the card to the Mac. Replacing the folder deletes it.
2. **Copy the two items** from `dist/AudreyBrick/` into `roms/ports`, replacing the old ones: `AudreyBrick.sh` and the folder `audreybrick`. On a FAT or exFAT card, run `rm "/Volumes/<card name>/roms/ports/"._*`, then eject.
3. **Quit with one press.** Start AudreyBrick, make a drone, then hold **Menu** and press **Start once**. The app should close at once and return to the Ports list.
4. **Check it does not quit by accident.** Start it again. Hold Menu alone for five seconds, release; hold Start alone for five seconds, release. Nothing should happen.
5. **Take a KNULLI screenshot.** Hold **Select** and press **Start**. The app should keep running and playing, with no banner. Look in the card's `screenshots` folder for the new `knulli-screenshot_1024x768_...png` and check it shows the normal screen.
6. **Try the habitual double press.** Press Menu + Start twice quickly. The app closes on the first press. Note whether anything unexpected happens after that (for example the menu system restarting), since KNULLI's own soft reset also listens for that double press.
7. **Send back** the `roms/ports/audreybrick/data/` folder, one of the new screenshots, and your notes.

## If something goes wrong

If Menu + Start does not close the app: press it twice quickly (KNULLI's soft reset, which you confirmed works), or as a last resort hold the power button. The logs survive.

## What the logs will show

The exit line should read `exit: Menu + Start`, followed by the keep-awake file being removed and `shut down cleanly`.

## Results

| Date | Package version | What happened |
|---|---|---|
| 2026-11-18 | 401ae63-dirty | Three sessions. Menu + Start closed the app every time (log: `exit: Menu + Start`, keep-awake file removed, clean shutdown), in both press orders. The KNULLI screenshot shows the normal screen with no banner. The human reports everything else working as expected and asked for the status-line quit hint to be removed, which was done. Not reported: what a quick double press does after the app has closed |
