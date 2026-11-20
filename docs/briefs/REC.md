# REC: recording the output to a file

Requested by the human on 2026-11-18, decisions made on 2026-11-19. One implementation task, then a review. Read `CLAUDE.md`, `docs/PLATFORM_NOTES.md` (sections 6, 7 and 12) and `docs/CONTROL_SCHEME_PROPOSAL.md` (sections 00 and 000) first.

## Goal

Start + Y, on any tab, starts and stops recording the instrument's final stereo output to a 16-bit WAV file in `data/recordings/`. A thin red border round the whole screen and a `REC mm:ss` readout show that a recording is running.

## The human's decisions (do not change)

- **Combo: Start + Y.** Start does nothing by itself and stays that way.
- **16-bit PCM, stereo, 48 kHz** (the engine's rate; use the rate the output device was opened at).
- **No user-facing maximum length.** The only stops are the WAV size limit and the card running low (below).
- **Indicator:** a thin red border round the entire screen on every tab, plus a small `REC mm:ss` (hours when needed: `h:mm:ss`) readout in the header.
- Files are named by counter, `rec-0001.wav`, `rec-0002.wav`, ... (the Brick's clock reads 1970, so no dates). The next number is one more than the highest `rec-NNNN.wav` present when recording starts; never overwrite a file.
- What is recorded is exactly what goes to the output device (after everything, including the processed input), not the dry input.
- Recording carries on across tab changes and source changes. Quitting (Menu + Start, SIGTERM, Escape, the parent dying) finishes the file properly.

## Design

### Audio thread

- A `Recorder` (suggested: `src/record/`, its own small library with no SDL dependency, like `src/input/`) owns a preallocated lock-free SPSC ring of interleaved stereo float frames, sized for about 4 seconds (the device's SD card can stall for a while; 2.1 s to read 100 MB was the best case, writes are slower). Allocate it once at start-up, never on Start + Y.
- At the end of `AudioRender::render()` (or wherever the final output block is complete, after meters; find the right place and say where), if an atomic "armed" flag is set, copy the block into the ring. A memory copy and two atomic index updates; nothing else. No allocation, lock, log, file call or wait, as everywhere on this thread.
- If the ring is full, drop the whole block and count the dropped frames in an atomic. Never block and never write a partial block. The writer inserts nothing for dropped blocks (the file is simply shorter); the count is logged and shown (below).
- Start and stop must be click-free in the file and sample-exact at block boundaries: the first and last recorded blocks are whole blocks. No fade is applied to the file (it is a faithful record of the output).
- Recording must not change the output by one bit. Add a test: render the same script with and without recording armed and compare the output buffers exactly.

### Writer thread

- One thread, started with the app or on first use (say which and why), that drains the ring, converts float to 16-bit and appends to the file. Conversion: clamp to [-1, 1], scale by 32767, round to nearest. No dither. (The device's own output path does the same conversion to S16; say in the report whether you can confirm its scaling from the firmware notes, and do not guess.) NaN becomes 0.
- Use dr_wav only if it can do the following cleanly; otherwise write the 44-byte PCM header yourself (it is trivial and ours, MIT). Whatever is used:
  - **Crash-safe header:** every 2 seconds, and on stop, rewrite the RIFF and data sizes in the header so that a file cut off by a flat battery, SIGKILL (KNULLI's exit path can SIGKILL the launcher) or a crash is a valid WAV up to the last update. Do not `fsync` on every block; one `fsync`/`fdatasync` with each header update is right. Seek back to the end afterwards. Test by killing a recording process with SIGKILL and opening the file.
  - **Size limit:** stop cleanly before the file would pass 4 GB (stay at or below `0xFFFFFFFF - header`; simplest is to stop at 4,000,000,000 data bytes, about 5 h 47 min). No RF64, no automatic continuation in a second file.
  - **Card space:** check free space on the recordings folder's filesystem (`statvfs`) before starting and every 2 seconds while recording. Refuse to start below 200 MB free; stop cleanly below 100 MB free.
  - **Write errors:** a failed or short write stops the recording, finishes the header as far as possible and reports the error. Never retry in a loop.
- `data/recordings/` is created if missing (by the app and by the launcher `packaging/AudreyBrick.sh`, next to how `data/input` is handled). It follows the same data-folder rule as the rest: found relative to the working directory the same way `data/input` is.
- Stopping: the UI thread clears the armed flag; the writer drains what is left in the ring, finalises the header, closes the file and reports the result (frames, duration, bytes, dropped frames, largest ring fill seen, time to finalise). Stop must not freeze the UI: the UI thread only sets flags and reads state; shutdown joins the writer with the ring drained (bounded: at most the ring's 4 seconds of data).
- Starting while the previous file is still being finalised: ignore the press and show `REC BUSY` briefly, or queue it; choose the simpler one that cannot lose a file, and say which.

### Controls

- `Button::Start` held and `Button::Y` pressed, in either order (as Menu + Start does in `src/app/MenuQuit.h`; read it and `InputRouter` and follow the same pattern), toggles recording. It fires once per chord: one of the two buttons must be released before it can fire again. Works on every tab, and during the pop-up lists on INPUT (it does not close them).
- The Y press that toggles recording must have **no other effect**: it is not a reset modifier (Y + d-pad) until Y has been released and pressed again, and with X held on INPUT it does not toggle LOOP/ONCE. Y pressed without Start behaves exactly as today.
- Start pressed while Menu is held still quits, and Menu pressed while Start is held still quits, including while Y is held. Quitting while recording finishes the file first.
- Select + Start stays untouched (KNULLI's screenshot). Start + Select + Y: treat as Start + Y; do not special-case it.
- Keyboard on the host: Return is Start and whatever key is already Y; no new key is needed. Update the `--help` key list and the `Start (no function by itself)` line.
- The scripted button source used by the self-tests (`ButtonScript`) must be able to drive Start + Y.

### Screen

- **Border:** a 4-pixel solid red frame on the outermost pixels of the 1024x768 screen, drawn last (over everything, including the pop-up overlay layer in `Layout`/`DrawList`), only while recording or finalising. Check that nothing on any tab is drawn within those 4 pixels today; if something is, say so and do not move it without reporting.
- **Readout:** `REC 0:00` style elapsed time in the header, red, at the existing text size, in a position that is free on every tab (look at `src/ui/Layout.cpp` and choose; report where). Elapsed time comes from frames written to the ring divided by the rate, not from a wall clock. If frames have been dropped, append the count of dropped frames in a short form (for example `REC 1:23 DROP 512`).
- For 2 seconds after a stop, show the saved file's name and length in the same place (`SAVED REC-0003.WAV 1:23`). For a failure or refusal, show the reason for 4 seconds: `REC: CARD FULL`, `REC: CANNOT CREATE FILE`, `REC: WRITE ERROR`, `REC STOPPED: FILE LIMIT`, `REC STOPPED: CARD FULL`. The built-in font is upper case; keep to its character set.
- No other UI: no file browser for recordings, no playback, no delete.

### Logging

On start: the file path, free space, ring size. Every statistics window (the existing 10 s line) while recording: frames written, ring fill maximum for the window, dropped frames, longest single write call in milliseconds. On stop: the result line above and the reason (button, quit, file limit, card full, write error). In the launcher's device-state block add the free space on the data folder.

### Self-test

Extend the app's self-test mode so a scripted run can record: a test that starts the app with `SDL_AUDIODRIVER=dummy` (and the dummy or offscreen video path the existing self-tests use), plays a script that presses Start + Y, waits, and presses it again, then checks that the WAV exists, has the expected format, a plausible length, and is not silent when the script raises the feedback. Recordings made by tests go to a temporary folder under the build tree or the scratchpad, never to the repo's `data/`.

## Tests required (at least)

- Ring: wrap-around, full ring drops whole blocks and counts them, concurrent producer and consumer under TSan.
- Float to 16-bit: 0, ±1, beyond ±1, tiny values, NaN and infinities.
- Header: correct for an empty recording (start then stop at once), for a short one, after a periodic update with the file still open, and after SIGKILL (fork a child that records and kill it).
- File numbering: empty folder, gaps, `rec-9999.wav` present (go to five digits), unrelated files, never overwrites.
- Size limit and low space, through an injected file/space interface (do not write 4 GB): refuses to start, stops cleanly, header valid.
- Write error injected mid-recording.
- Bit-identical output with and without recording.
- Chord handling: both orders, fires once per chord, Y has no reset or LOOP/ONCE effect when used in the chord, Menu + Start still quits in both orders with Y held, plain Y unchanged.
- Layout: the border is the last thing drawn and covers the outer 4 pixels on every tab and with a pop-up open; the readout does not overlap anything on any tab (use the existing overlap checks in `tests/ui/`).
- Shutdown while recording: file finalised, writer joined, no leak (ASan), no race (TSan).

## Also update

`docs/CONTROL_SCHEME_PROPOSAL.md` (button table and section 00: Start + Y records; Start alone still does nothing), `packaging/README.txt` (how to record, where files go, how to copy them off, the limits), `packaging/AudreyBrick.sh` (create `data/recordings`, log free space), `THIRD_PARTY.md` only if a dependency changes (none is expected), and a new numbered human checklist `docs/RECORDING_RUN_BRICK.md`: record on each tab, with input OFF/FILE/LIVE, a long recording (20 minutes) left droning, quit while recording, check the files play on a computer, and send back `data/` logs (not the recordings themselves unless asked).

## Rules and verification

No git state changes of any kind. Nothing under `third_party/`, `src/core`, `src/shim`. No `docs/STATUS.md` or `docs/PLATFORM_NOTES.md` edits (the orchestrator does those). Never access the user's personal folders or media library (Music, Movies, Pictures, Documents, Desktop, Downloads) and never run filesystem-wide searches; stay in the repo, the session scratchpad and read-only `/opt/homebrew`. Test WAVs, if needed, are in `~/.cache/audreybrick-device/data/input` (read-only). No network access is needed. Do not use adb or touch the device in any way. Never open a real audio device on the Mac (`SDL_AUDIODRIVER=dummy` or no SDL). Leave no background processes and no stray recordings.

Run the whole verification list and report the numbers: macOS Release, ASan + UBSan, TSan (`-R "control|platform|ui|input|record"`), no-SDL (`-DAUDREY_WITH_SDL=OFF`), `scripts/verify_vendor.sh`, `scripts/make_reference.sh --check` (all 25 hashes must still match: recording must not change rendering), `scripts/build_linux_arm64.sh`, `scripts/make_port.sh ~/.cache/audreybrick-device/knulli-scarab-20260511.sqfs`.

Report back: files changed, where the tap is in the audio path and why there, the choices left to you above and what you chose, test counts for each build, anything you could not verify, and what only the device can confirm (SD card write stalls, ring size, free-space figures).

---

# GAIN: remember the input gain per source for the session

Requested by the human on 2026-11-20 after the recording run. One small implementation task, then a review. Read `CLAUDE.md` ("Audio input"), `docs/CONTROL_SCHEME_PROPOSAL.md` section 000, and the header comment of `src/input/InputController.h` first.

## Problem

There is one input gain (`InputController::gain_`). Every capture-device open forces it to 0 (-inf dB) as the howl safeguard (`InputController.cpp`, "before opening a capture device"). So a player who has set the mic's gain, switches to OFF or FILE and comes back to LIVE has to raise the gain again every time, and the FILE gain is lost too (it is the same variable, left at whatever LIVE had). The human wants to switch between sources on the fly: "retain the position for both mic and usb independently throughout the entire session after the user increases the gain the first time".

## Behaviour wanted

- The gain is remembered **per source, for the running session only** (nothing is written to disk; a new launch starts as today):
  - one slot for FILE (starts at today's start-up default);
  - one slot per capture device, keyed by the device's name as listed (the same string the chooser shows and opens). Each starts at 0 (-inf dB).
- **The first open of a given device in a session still starts at -inf dB.** That is the howl safeguard and it stays. "First" means no gain has been remembered for that name yet in this session.
- A later open of the same device (after leaving LIVE and coming back, after choosing another device and returning, after DEVICE LOST and the device being listed again and chosen) restores that device's remembered gain.
- The restore must be as safe as the current open: the gain is still set to 0 before the old device closes and the new session starts, exactly as now, so nothing of the old or new device is heard at the wrong gain. Only once the open has succeeded is the remembered gain applied, and it must rise through a click-free ramp (use the stage's existing gain smoothing or fades; say what the ramp length is; if it is shorter than 100 ms, lengthen it for this case only if that is simple, otherwise report). A failed open leaves the gain at 0 and does not change the remembered value.
- Any gain change the player makes (up/down, Y + up/down to unity, fine steps, ramps) updates the slot of the current source at once. With the source LIVE and no device open (failed open, lost device, none chosen), gain changes behave as today and are not stored against any device.
- Switching to FILE applies the FILE slot; switching to OFF behaves as today. Switching FILE -> LIVE -> FILE returns FILE's gain. Check what else reads the gain when the source changes (the stage's fades on source change) so that the change of gain between sources never clicks: the old source must have faded out before the gain jumps, or the gain must ramp.
- A bounded table: at most 8 remembered device names (fixed-size, no growth without limit; when full, forget the least recently opened). Names are compared exactly.
- The howl warning banner and everything else on the LIVE screen stay as they are. The gain row simply shows the restored value.
- Log each restore (`input: gain restored to 0.608 (-x.x dB) for '<name>'`) and keep the existing "gain ... -> 0 before opening" line.

## Tests required (at least)

In `tests/input/` with the existing fake backend and `LiveSim`: first open of a device is at 0; raise, leave to OFF and to FILE, return: restored; two devices keep independent gains; FILE keeps its own gain across LIVE visits; a failed open leaves 0 and keeps the remembered value for the next successful open; DEVICE LOST then reopen restores; gain changes with no device open are not stored; the table's bound (9 names); the order of events on a restoring open (gain 0 before close and new session, restore only after success), and a rendered check through `InputStage` that a restoring open and a FILE <-> LIVE switch produce no step in the output larger than the existing click limits used by the input tests. Update any existing test that asserts "gain to 0 again" on reopen.

## Also update

The header comment in `InputController.h`, `docs/CONTROL_SCHEME_PROPOSAL.md` section 000, `packaging/README.txt`, `docs/LIVE_INPUT_RUN_BRICK.md` where they say the gain returns to -inf on every open, and add three steps to the end of `docs/RECORDING_RUN_BRICK.md` under a heading "Gain memory" (mic gain kept across OFF/FILE, USB and mic independent, first open of a device still silent).

## Rules and verification

The working tree already holds the uncommitted, reviewed REC change; do not disturb it, and keep this change to `src/input/`, `tests/input/`, the docs named above, and `src/app` or `src/ui` only if something there truly has to change (say why). No git state changes of any kind. Nothing under `third_party/`, `src/core`, `src/shim`. No `docs/STATUS.md` or `docs/PLATFORM_NOTES.md` edits. Never access the user's personal folders or media library and never run filesystem-wide searches; stay in the repo, the session scratchpad and read-only `/opt/homebrew`. No adb, no device. Never open a real audio device on the Mac. Nothing new on the audio thread may allocate, lock, log, do I/O or wait. Leave no background processes.

Run and report numbers for: macOS Release, ASan + UBSan, TSan (`-R "control|platform|ui|input|record"`), no-SDL, `scripts/verify_vendor.sh`, `scripts/make_reference.sh --check`. The Docker scripts (`build_linux_arm64.sh`, `make_port.sh`) are run by the orchestrator afterwards; you need not run them.
