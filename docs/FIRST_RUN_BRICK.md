# First run on the Brick

> **Note added 2026-11-23 (PRESET-C):** packages from PRESET-C on keep the user's data (logs, run counter, `input/`, `recordings/`, `presets/`) in `/userdata/saves/audreybrick/` (on the SD card: `saves/audreybrick/`), no longer in `roms/ports/audreybrick/data/`. An old `roms/ports/audreybrick/data/` is not moved automatically: the launcher leaves it untouched and only notes in `stdout.log` that it still holds files (PRESET-D); move its files by hand. Replacing the `audreybrick` folder no longer deletes anything in `saves/audreybrick/`. The steps below are kept as written: for a newer package, read every `roms/ports/audreybrick/data/` (or `data/`) as `saves/audreybrick/`.

> **Historical.** This run happened on 2026-11-18 and worked. Two things below have since changed: quitting is now **Menu + Start** (Select + Start is KNULLI's screenshot and no longer quits), and A/B/X/Y are swapped by the launcher to match their labels. Current facts are in `PLATFORM_NOTES.md` sections 11 and 12; the current checklist is `THIRD_RUN_BRICK.md`.

For the human. Written 2026-11-18 for the package built by `scripts/make_port.sh` (M3-A). **Not yet run.**

What has been tested off the device: the binary loads against the firmware's own glibc, C++ library and SDL 2.32.8; audio processing, controls, logging, the launcher and quitting on signals all work in a container built from the firmware image. **What has never been tested: the screen (the Mali video driver) and sound output through ALSA.** This run is their first test, so a failure here is useful information, not a setback.

If the screen cannot start, the app exits within a fraction of a second and you are back in the Ports menu. The logs say why.

## Before you start

- The package is `dist/AudreyBrick/` in the project folder. It holds two items: the file `AudreyBrick.sh` and the folder `audreybrick`.
- To rebuild it: `scripts/make_port.sh ~/.cache/audreybrick-device/knulli-scarab-20260511.sqfs` (Docker must be running; about three minutes).

## Steps

1. **Copy the two items to the card.** Switch the Brick off, take out the SD card that holds KNULLI's games, and put it in the Mac. Open `roms/ports` on the card. Copy `AudreyBrick.sh` and the folder `audreybrick` from `dist/AudreyBrick` into `roms/ports`. Copy the two items, **not** the outer `AudreyBrick` folder.
   - If the Mac cannot read the card, its games partition is ext4. Put the card back, switch on the Brick's Wi-Fi, and in Finder use Go > Connect to Server, `smb://<the Brick's IP address>`, share `share`, folder `roms/ports`. Note which way you used.
2. **If the card is FAT or exFAT,** Finder leaves hidden `._` files. In Terminal: `rm "/Volumes/<card name>/roms/ports/"._*` (an error saying there are none is fine). Then eject the card in Finder.
3. **Put the card back and switch on.** Press START for the main menu, then Game Settings > Update Gamelists (the wording may differ). "AudreyBrick" should appear under Ports.
4. **Turn the system volume low.** Use the speaker for the first run, not headphones.
5. **Launch AudreyBrick from Ports.** Note how many seconds until the screen appears, or that nothing appears and you return to the menu.
6. **Lower the app's volume first.** Press L1 once to reach the OUT tab and hold d-pad down for a second. Then press R1 to go back to LOOP.
7. **Press every button once, slowly,** and note anything unexpected on screen: d-pad up, down, left, right; A, B, X, Y; L1, R1, L2, R2; SELECT; START; MENU; the Fn switch both ways; the volume buttons. The footer entries light up while their button is held.
8. **Make sound.** On LOOP, hold d-pad up for about three seconds to raise BODY. Hold R2 for about three seconds until a drone starts. L2 lowers it. Hold A for maximum, then release. Step through the tabs with L1 and R1 and move a few parameters.
9. **Leave it playing untouched for at least 30 seconds.** Note any crackles or dropouts, and whether the small text is readable.
10. **Quit by holding SELECT and START together** for one second. "HOLD TO QUIT" appears and the app closes.
11. **Launch it again and quit with KNULLI's usual exit combination** for games and ports, so both ways are logged.
12. **Optional, six minutes:** launch, raise a drone, and leave the Brick untouched for six minutes. The app asks KNULLI not to dim or mute while it runs, so the screen should **not** dim and the sound should **not** stop at five minutes. Note what happens. Quit with SELECT + START.
13. **Fetch the logs.** Switch off and take out the card (or use the network share). Copy the whole folder `roms/ports/audreybrick/data/` to the Mac and send it with your notes. `audreybrick.log` and `stdout.log` are the latest run; the numbered files are earlier runs (the last eight are kept).

## If something goes wrong

| What you see | What to do |
|---|---|
| You are returned to the menu at once | The screen could not start. Fetch the logs (step 13); they hold the reason |
| A black or garbled screen | Try step 10, then step 11's exit combination. Fetch the logs |
| The app does not respond to anything | In this order: hold SELECT + START for a second; the system exit combination; over SSH, `killall audreybrick`; as a last resort hold the power button. The logs survive all of these: every line is written to the card as it happens |
| Sound crackles | Note it. To try a larger buffer, edit `roms/ports/AudreyBrick.sh` on the card and change `256` on the `AUDREY_BUFFER` line to `512`, then to `1024` |
| It is too loud | Lower the system volume. Feedback maximum (A) at the app's default volume is loud |
| No logs in `data/` | The card may have been read-only: over SSH, look in `/tmp/audreybrick-data` before switching off (it is lost on reboot). Also KNULLI's own launch logs under `system/logs/` on the card |

**Before installing a newer package:** replacing the `audreybrick` folder deletes `data/`. Copy the logs off first.

**To turn off keep-awake** for a comparison: on the card, edit `AudreyBrick.sh` and change `AUDREY_KEEP_AWAKE="${AUDREY_KEEP_AWAKE:-1}"` to end in `:-0}"`. Change it back afterwards.

## What the logs will tell us

Which SDL button each physical button produces; whether L2 and R2 arrive as buttons or axes; whether MENU, Fn and the exit hotkey reach the app; the display size, renderer and frame rate; the audio buffer obtained, the callback load every ten seconds and a count of late callbacks; the sound cards and capture devices the system lists (this tells us about the microphone and any USB audio interface); the CPU governor and frequency; and KNULLI's battery-saver settings.

## Results

| Date | Package version | What happened |
|---|---|---|
| | | |
