# On-device checklist

For the human to run on the TrimUI Brick. **Status: not run. The probe app does not exist yet.**

Items 1 to 3 need only a shell on the device (SSH over Wi-Fi). Items 4 onward need a minimal test app, called `brickprobe` here: one SDL2 program, packaged as a Port, that opens an audio device, plays a test tone, runs the Audrey engine at a fixed setting, shows a CPU meter and prints what SDL reports. Building it is part of M3 (or earlier, if you approve it as a separate small task once M1 exists).

Record every result in `PLATFORM_NOTES.md` and change the tag to [confirmed].

**Safety first:** start every audio test at low volume. Do not wear headphones for items 12 to 14.

## A. Shell only

1. **glibc version.** Run `ldd --version` and `uname -a`. Write down both. Also `cat /etc/os-release` for the KNULLI version.
2. **Audio server.** Run `ps aux | grep -E 'pipewire|pulse|wireplumber'`, then `aplay -l` and `arecord -l`. Note which server runs and which playback and capture cards exist.
3. **SDL2 version.** Run `ls -l /usr/lib/libSDL2*`. Note the version in the file name.

## B. With `brickprobe`, output

4. **Output rate and format.** Launch the probe with the default SDL audio driver. Note the driver name, the sample rate, the format and the buffer size SDL actually granted when 48000 Hz, float, stereo, 256 frames were requested. Repeat with `SDL_AUDIODRIVER=alsa` and `SDL_AUDIODRIVER=pipewire`.
5. **Buffer size on the speaker.** Play the test tone for 2 minutes at 1024, 512, 256 and 128 frames. For each, note whether you hear crackles or dropouts and what the under-run counter shows.
6. **Buffer size on headphones.** Repeat item 5 with wired headphones. Also note whether left and right are the correct way round, and whether audio cuts out at low volume.
7. **CPU headroom.** Run the engine page (feedback up, reverb and delay up) for 2 minutes at the smallest stable buffer. Note the average and the peak callback load in percent, and the CPU governor and frequency shown. Repeat once after setting the CPU to 1416 MHz if that option exists.
8. **Buttons.** On the input page press every button once: d-pad, A, B, X, Y, L1, R1, L2, R2, Select, Start, Menu, and flip the Fn switch. Note the SDL GameController name shown for each, which ones show nothing, and which ones trigger a system action (menu overlay, volume, exit). Repeat with KNULLI's A/B swap option enabled.
9. **Screen dim and mute.** Leave the tone playing and touch nothing for 7 minutes. Note when the screen dims and whether the sound stops. Then enable the probe's "keep awake" option and repeat.

## C. With `brickprobe`, input

10. **Internal mic capture.** Open the capture device. Note the rate, format and channel count granted. Speak and watch the meter. Note the idle noise level and whether shouting clips.
11. **USB audio interface.** Plug in a class-compliant USB-C interface. Note whether it appears for playback, for capture, and whether both can be opened at once. Note the rates granted. Say which KNULLI build this was.
12. **Round-trip latency.** With a cable from the headphone output to the USB interface input (or, lacking that, the speaker and the internal mic at low volume), run the probe's click test. Note the measured delay in milliseconds at each stable buffer size.
13. **Speaker or headphones detection.** Plug and unplug headphones while the probe shows its output-route line. Note whether the app sees a change. Run `amixer contents | grep -i -E 'jack|headphone|speaker'` before and after and note any difference.
14. **Speaker feedback behaviour.** Volume low, speaker on, internal mic enabled at the lowest input gain, engine feedback at minimum. Raise the input gain slowly. Note the gain at which howling starts and whether the limiter indicator lights. Stop immediately if it gets loud.

## D. Packaging

15. **Launch as a Port.** Copy the folder and `.sh` to `/userdata/roms/ports/`, refresh the game list, launch, and exit with the exit hotkey. Note any error and whether control returns to the menu cleanly.
16. **Storage.** Note whether the SD card's share partition is ext4 or exFAT. Confirm that a preset saved by the probe is still there after relaunching.

## Results

| Item | Date | KNULLI build | Result |
|---|---|---|---|
| | | | |
