# Platform notes: KNULLI on the TrimUI Brick

Shared ground truth for BrickLoop and VectorBrick. Written from desk research in November 2026. **Nothing here has been tested on hardware by us yet.** Every claim carries a confidence tag:

- **[confirmed]** stated by a primary source (README, changelog, issue, official docs)
- **[reported]** claimed by a developer or user, not independently verified
- **[inferred]** our reasoning from adjacent evidence
- **[unknown]** must be checked on the device before relying on it

When you learn something on real hardware, update this file and change the tag.

## 1. Target hardware

| Item | Value |
|---|---|
| SoC | Allwinner A133P, 4x Cortex-A53, aarch64 [confirmed] |
| RAM | 1 GB [confirmed] |
| Screen | 1024x768 [confirmed], no touchscreen |
| Inputs | D-pad, A/B/X/Y, L1/R1, L2/R2 shelf buttons, Select/Start/Menu, Fn switch. **No analog sticks.** |
| Audio out | Speaker, 3.5 mm headphone jack |
| Audio in | **Mono internal mic only. No line-in.** External audio in needs a USB-C audio interface. |
| USB | One USB-C port (rear USB enablement is in recent KNULLI development builds, see section 3) |

Design consequence: every control must work with digital buttons only. Layers, shift keys and tabs are the main tools for fitting features onto the controls.

## 2. Software environment

- KNULLI is a Batocera fork with a Linux userland. Apps are launched as "Ports" (a `.sh` script plus a folder) from `/userdata/roms/ports/`. [confirmed]
- SDL2 is on the system (updated to 2.30 in the Firefly release). [reported]
- Python 3.12 + pygame are reported on current stable. Development builds move to Python 3.14, which has already broken GrainBrick. **We do not use Python on the device.** [reported]
- NumPy is not shipped. [reported]
- Pure Data, Csound, JACK and LOVE are not confirmed in the system image. Assume we would have to bundle them. We are not planning to use them. [unknown]
- Audio stack: Batocera moved to PipeWire, so KNULLI probably uses it, but no KNULLI document confirms this for the Brick. SDL2's default audio driver should work. **Test both the `pipewire` and `alsa` SDL audio drivers.** [inferred]

### Known audio problems on the Brick

- Early builds cut out at low headphone volume. Workaround: `audio.headphone=5` in `batocera.conf`. [confirmed, knulli-cfw/distribution #267]
- Gladiator II added automatic volume attenuation on headphones for A133 devices and fixed reversed L/R channels. [confirmed, changelog]
- "Auto" output mode sometimes picks the wrong device, and audio can be missing after reboot. [reported, #92 and #497]
- KNULLI's default battery saver dims the screen after 5 minutes idle and may also mute audio. This is the suspected cause of GrainBrick's audio stopping after about 5 minutes. **Our apps must either keep the device awake while playing or document the setting.** [reported, GrainBrick issues #10 and #17]
- Recommended CPU setting from GrainBrick's author: 1416 MHz. [reported]

### USB audio and MIDI

- USB DAC support arrived in the Gladiator release. [confirmed]
- A knulli-linux pull request (#131, merged 8 Nov 2026 into the development branch) enables the rear USB port and `SND_USB_AUDIO`. Stable builds may lack this. [confirmed for development branch]
- Third-party apps report USB audio and USB MIDI working on current builds. [reported]
- Duplex use (USB audio in and out at once) and its latency are untested. [unknown]

## 3. Open questions to settle on the device first

1. Which glibc version does the device have? (`ldd --version`) This sets the build floor.
2. Which audio server is running, and which SDL audio driver gives the lowest stable latency?
3. What buffer size is stable on the speaker, headphones and USB?
4. Does the app need to prevent screen dim and audio mute? How?
5. What SDL GameController mapping does the Brick report on KNULLI?
6. Does the Fn switch or the Menu button need to be reserved for the system?
7. What is the real CPU headroom? Measure, do not estimate.

## 4. Toolchain

- **Language:** C++17. Reason: Mutable Instruments DSP is C++, SDL2 and miniaudio have clean C/C++ APIs, and Faust emits C++.
- **Build system:** CMake.
- **Libraries:** SDL2 for window, input and audio callback (or miniaudio for audio if SDL's backend proves unreliable), dr_wav for WAV I/O. Keep dependencies few and vendored where licences allow.
- **Dev machine:** Mac, Apple Silicon. Two build targets:
  1. **Host build** (macOS, clang, SDL2 from Homebrew). For fast iteration, unit tests and render-to-WAV. The DSP core must build and run here with no SDL dependency.
  2. **Device build** (Linux aarch64). Options: PortMaster's builder Docker image (`portmaster-builder:aarch64-latest`), or a Linux arm64 container, which should run natively on Apple Silicon (verify). `zig cc` with a pinned glibc floor is a fallback, as used by PocketEuclidean. Link against the device's own libSDL2 at runtime. [inferred]
- The device build must not depend on a newer glibc than the device has (see open question 1).

## 5. Packaging as a Port

Follow PortMaster conventions so the app runs on KNULLI and, for free, on other CFWs.

- Layout: `Your App.sh` next to a folder `yourapp/`, copied into `/userdata/roms/ports/`.
- The `.sh` sources `control.txt` and `mod_${CFW_NAME}.txt`, runs `get_controls`, exports `SDL_GAMECONTROLLERCONFIG`, sets `LD_LIBRARY_PATH` to the bundled `libs.aarch64`, and ends with `pm_finish`.
- Use `gptokeyb` only for an exit hotkey, if at all. Read buttons through the SDL GameController API, **never raw joystick button indices**. (GrainBrick's raw indices are why community forks had to remap buttons.)
- For PortMaster submission include `port.json`, `gameinfo.xml`, `README.md`, a screenshot and `licenses/`.
- Keep user data (samples, recordings, projects, wavetables, presets) **outside the code folder**, e.g. `yourapp/data/`, so updating the app never overwrites it.
- exFAT cards cannot hold symlinks. The default KNULLI ext4 SHARE partition can. If exFAT support is wanted, use PortMaster's `bind_directories`.

## 6. Real-time audio rules (apply to every app)

1. **The audio callback never allocates, locks, logs, touches the filesystem, or waits.** No `new`, `malloc`, `std::vector` growth, `std::mutex`, `printf`, file I/O.
2. Communicate with the UI thread through lock-free single-producer single-consumer queues and atomics only.
3. Preallocate all buffers at startup. Loops and wavetables load on a background thread and are swapped in atomically.
4. Internal processing is 32-bit float. Target 48 kHz, with a user-selectable lower rate for heavy patches.
5. Denormals are flushed to zero. All parameter changes are smoothed to avoid clicks.
6. Budget: the audio callback should stay under about 50% of one core at the chosen buffer size. Measure on the device and expose a CPU meter in the UI during development.
7. Starting targets (estimates, **not measurements**): 128 to 256 frame buffers at 48 kHz, 10 to 20 ms button-to-sound latency.

## 7. Input rules

- Use SDL GameController names (A, B, X, Y, shoulders, triggers, dpad, start, back). Allow remapping in a config file.
- KNULLI has an optional A/B and X/Y swap for Ports. Check for it and do not fight it.
- Every function reachable with buttons alone. Hold-to-modify and tab switching are preferred over deep menus.
- Always show the current layer or modifier state on screen.

## 8. Licensing rules

- Mutable Instruments STM32-era code (Braids, Plaits, Clouds, Beads, stmlib) is MIT. AVR-era code is GPL-3.0. **Confirm the licence of each directory you vendor.** Do not use the "Mutable Instruments" name or branding on derivatives.
- GPL-3.0 projects (Pocket Acid, LittleGPTracker, PocketTracker): reading them for ideas is fine. Copying code makes our app GPL-3.0. Do not copy without an explicit decision from the human.
- GrainBrick is closed source. Do not decompile or reuse it.
- Keep a `THIRD_PARTY.md` listing every vendored library, its licence and its source URL. Ship licence texts in `licenses/`.

## 9. Existing apps worth knowing (for UX reference and to avoid duplication)

GrainBrick (granular, closed source, Python), Brickchord (chord synth, Python), ChipNomad and ChooChooTracker (trackers, SDL2), PocketTracker (GPL), LittleGPTracker (GPL-3.0), 4TRKmini (commercial groovebox, native C++, USB MIDI), MTRX, Pocket Acid (GPL-3.0, Zig), m8c, and several NextUI-only instruments by boorch (BRIXEQ, EUCLIDTRACK, Grampus, Dronage Terminal, PolyBrik). Use them as UX references. Their latency and CPU claims are developer statements, not benchmarks. [reported]

## 10. Read from the firmware image (KNULLI "Scarab", 20260511, TrimUI Brick)

The human's Brick runs `knulli-a133-trimui-brick-scarab-20260511.img.gz` (stable release of 11 May 2026; SHA-256 of the download `2c6e059b0f7db9d6b0fce3c383e72ca989b1c1224f4b3e81af11c66ebea5421f`). On 2026-11-17 the image was downloaded and its system squashfs inspected. Everything below is **[confirmed, read from the image]** unless marked otherwise. It describes what is installed, not how it behaves when running.

| Item | Finding |
|---|---|
| Base | Batocera 42, Buildroot 2024.11, `OS_NAME=knulli` |
| **glibc** | **2.39** (highest symbol version `GLIBC_2.39`). This is the build floor: a device binary must not need anything newer |
| **libstdc++** | `libstdc++.so.6.0.32`, highest `GLIBCXX_3.4.32` (GCC 13). A build with a newer GCC must link libstdc++ statically or avoid newer symbols |
| **SDL2** | **2.32.8** (`libSDL2-2.0.so.0.3200.8`), plus SDL2_ttf 2.24, SDL2_image 2.8, SDL2_mixer, SDL2_gfx, SDL2_net |
| SDL2 audio drivers built in | **`alsa`**, `disk`, `dummy`. **No `pipewire` and no `pulseaudio` driver.** So there is nothing to choose between: SDL uses ALSA |
| SDL2 video drivers built in | **`mali`** (Mali framebuffer), plus dummy/offscreen. No KMSDRM, Wayland or X11. Expect a fullscreen 1024x768 GLES surface; windowing and resizing do not apply |
| Audio server | PipeWire 1.4.6 with WirePlumber and `pipewire-pulse`, started by `/etc/init.d/S06audio`. ALSA's default PCM is the PipeWire plugin (`99-pipewire-default.conf`), for playback **and capture**. So SDL (ALSA) goes through PipeWire |
| Built-in sound card | WirePlumber rule `51-builtinspeaker.conf` forces the card to **S16LE, 48000 Hz, 2 channels, no mmap, software mixer**, with a comment that the driver does not publish its capabilities. 48 kHz is therefore the native rate; our float output is converted to 16-bit by the audio stack |
| Kernel | Module trees for `4.9.191`, `4.9.191_v90s` and `6.11.11` are present. The 4.9.191 tree has the A133 codec drivers (`sun50iw10-codec`, `snd-soc-ac107`) built in, so that is the Brick's kernel [inferred from the driver names] |
| **USB audio** | `snd-usb-audio` and `snd-usbmidi-lib` are **built into the 4.9.191 kernel**. So USB audio is supported at the driver level on this stable build, contrary to the worry in section 2. Whether capture and duplex work in practice is still [unknown] |
| Microphone | The `ac107` ADC codec driver is built in. No init script mentions the mic or capture. Whether a capture device appears and at what level is [unknown] |
| How Ports start | EmulationStation runs the port's `.sh` with `/bin/bash` through `configgen/generators/sh/shGenerator.py`. It writes `/tmp/gamecontrollerdb.txt` and **sets `SDL_GAMECONTROLLERCONFIG`** for the connected controllers before launching. So the SDL GameController API should see the Brick's buttons with standard names without us shipping a mapping |
| Exit hotkey context | The generator registers `exit` as Alt+F4 key events for shell ports, so the system's hotkey sends Alt+F4. The app should quit on that (SDL delivers it as a key event or `SDL_QUIT`) [inferred] |
| PortMaster | Not preinstalled; `/usr/share/portmaster/Install.PortMaster.sh` is. `control.txt` and `gptokeyb` are not in the image. A port that relies on PortMaster's helper scripts needs PortMaster installed first; a self-contained `.sh` does not |
| Idle behaviour | `idlewatcher` daemon: after **300 s** idle the default mode is **dim**, and `system.batterysaver.mute` defaults to **1 (mute)**; after a further 900 s the default is suspend. Settings keys: `system.idlewatcher.timer`, `system.idlewatcher.extendedtimer`, `system.batterysaver.mode` (`dim`, `dispoff`, `suspend`, `shutdown`, `none`), `system.batterysaver.extendedmode`, `system.batterysaver.mute`. This confirms the 5-minute audio stop reported for GrainBrick. What counts as activity, and whether an app can inhibit it, is [unknown]; hooks live in `/etc/idlewatcher/active.d` and `extended.d` |
| Tools on the device | `ldd`, `aplay`, `arecord`, `amixer`, `pactl`, `pw-cli`, `wpctl`, `evtest`, `sdl-jstest`'s controller database, Python 3.12, dropbear SSH |

### What this settles

- Open questions 1 and 2 of section 3: glibc is 2.39; the audio server is PipeWire and SDL reaches it through ALSA only.
- The device build must target **glibc 2.39 or older and libstdc++ from GCC 13 or older**. The Debian 13 test image used in M1-D has glibc 2.41 and is too new for device binaries. Debian 12 (glibc 2.36, GCC 12) is a safe build base.
- The app is built against SDL 2.0.18-level API and runs against 2.32.8, the same minor line as the development Mac.
- The `--audio-driver` option is of little use on the device (only `alsa`), but buffer size still matters.
- The app must handle a fullscreen Mali surface: no window manager, no vsync guarantee, and the logical size should equal the screen.

### What still needs the device

Stable buffer size and real callback load on the A53; what the idle watcher treats as activity; whether SDL reports the buttons as expected (including L2/R2 as buttons or axes, and which physical button SDL calls A); mic and USB capture devices and duplex latency; legibility of 16 px text; and whether the Fn switch or Menu are delivered to apps.
