# Second run on the Brick

> **Note added 2026-11-23 (PRESET-C):** packages from PRESET-C on keep the user's data (logs, run counter, `input/`, `recordings/`, `presets/`) in `/userdata/saves/audreybrick/` (on the SD card: `saves/audreybrick/`), no longer in `roms/ports/audreybrick/data/`. An old `roms/ports/audreybrick/data/` is not moved automatically: the launcher leaves it untouched and only notes in `stdout.log` that it still holds files (PRESET-D); move its files by hand. Replacing the `audreybrick` folder no longer deletes anything in `saves/audreybrick/`. The steps below are kept as written: for a newer package, read every `roms/ports/audreybrick/data/` (or `data/`) as `saves/audreybrick/`.

> **Historical.** This run happened on 2026-11-18; results are in `PLATFORM_NOTES.md` section 12. Quitting is now **Menu + Start**, not Select + Start. The current checklist is `THIRD_RUN_BRICK.md`.

For the human. Written 2026-11-18 for the package built by `scripts/make_port.sh` after M3-B. **Not yet run.** The first run's results are in `PLATFORM_NOTES.md` section 11.

## What changed since the first run

- **A, B, X and Y should now match their labels.** The first run showed A and B swapped. The firmware's controller tables say X and Y are swapped the same way, but only one of them was pressed last time, so this run checks all four.
- **The log now records the processor's speed** next to the audio load, to find out whether the load figures and spikes come from KNULLI slowing the processor down.
- **Nothing changes the processor's speed by default.** Run B below uses KNULLI's own POWER MODE option to compare.
- The launcher and app were hardened against being stopped at awkward moments.

## Steps

1. **Save your logs first.** Copy `roms/ports/audreybrick/data/` from the card to the Mac. Replacing the folder deletes it.
2. **Copy the two items.** Copy `AudreyBrick.sh` and the folder `audreybrick` from `dist/AudreyBrick/` into `roms/ports`, replacing the old ones. Do not copy the outer folder. On a FAT or exFAT card, run `rm "/Volumes/<card name>/roms/ports/"._*` in Terminal, then eject.
3. **Run A: KNULLI's options for the port left at their defaults.** System volume low. Start AudreyBrick.
   - Press **A, B, X, Y** once each, about two seconds apart, **in that order**. For each, note whether it did what the footer says: A = MAX, B = SWITCH, X = SHIFT, Y + d-pad = RESET. (Each footer entry lights while its button is held.)
   - Flip the **Fn switch** both ways and press each **volume button** once.
   - Raise BODY (d-pad up on LOOP) and feedback (R2), and listen for a minute.
   - Quit with **KNULLI's own exit combination** for games and ports. Note whether the app closes and how quickly.
4. **Run B: POWER MODE set to High Performance.**
   - In the Ports list, highlight AudreyBrick and open its game options (the help bar at the bottom shows the button; it may need holding). Go to ADVANCED GAME OPTIONS, then POWER MODE, and choose **High Performance**.
   - If you cannot find it per game: START > GAME SETTINGS > PER SYSTEM ADVANCED CONFIGURATION > PORTS > POWER MODE. That applies to all ports.
   - Start AudreyBrick, make a drone, listen for a minute, and quit with **SELECT + START**.
   - **Set POWER MODE back to its default afterwards.**
5. **Idle test, six minutes.** Start, make a drone, and leave the Brick untouched for six minutes. Note whether the screen dims or the sound stops at about five minutes. It should do neither. Quit with SELECT + START.
6. **Notes to keep:** whether the charger was connected in each run; roughly when any crackle happened; whether the small text (the footer, the names in the value strip) is comfortable to read.
7. **Send back** the whole `roms/ports/audreybrick/data/` folder with your notes.

## If something goes wrong

The same as the first run: hold SELECT + START for a second, then the system exit combination, then (over SSH) `killall audreybrick`, then a long press on power. The logs survive. See `FIRST_RUN_BRICK.md`.

## What the logs will show

- **Run A:** which button each of A, B, X, Y really is; what the Fn switch and volume buttons send, if anything; whether the system exit reaches the app; and the processor's speed and the audio load under KNULLI's normal power mode.
- **Run B:** the same with the processor held at full speed. Comparing the two settles whether the governor explains the load figures.
- **Idle test:** whether keep-awake works.

## Results

| Date | Package version | What happened |
|---|---|---|
| | | |
