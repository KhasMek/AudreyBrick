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
| **glibc** | The loader reports glibc **2.40** (Buildroot); the highest symbol version it exports is **`GLIBC_2.39`**. That symbol version is the build limit: a device binary must not need anything newer |
| **libstdc++** | `libstdc++.so.6.0.32`, highest `GLIBCXX_3.4.32` (GCC 13). A build with a newer GCC must link libstdc++ statically or avoid newer symbols |
| **SDL2** | **2.32.8** (`libSDL2-2.0.so.0.3200.8`), plus SDL2_ttf 2.24, SDL2_image 2.8, SDL2_mixer, SDL2_gfx, SDL2_net |
| SDL2 audio drivers built in | **`alsa`**, `dsp` (OSS), `disk`, `dummy`. **No `pipewire` and no `pulseaudio` driver.** So there is nothing to choose between: SDL uses ALSA |
| SDL2 video drivers built in | **`mali` only** ("Mali EGL Video Driver", a patched build that reads `/dev/fb0` and `/sys/class/graphics/fb0/modes`). **No dummy or offscreen video driver**, and no KMSDRM, Wayland or X11. (An earlier version of this table wrongly listed dummy/offscreen video; `dummy` exists only as an audio driver. Corrected 2026-11-17 after running the app against the device's libraries.) Expect a fullscreen 1024x768 GLES surface; windowing and resizing do not apply. The display path cannot be tested off the device |
| Audio server | PipeWire 1.4.6 with WirePlumber and `pipewire-pulse`, started by `/etc/init.d/S06audio`. ALSA's default PCM is the PipeWire plugin (`99-pipewire-default.conf`), for playback **and capture**. So SDL (ALSA) goes through PipeWire |
| Built-in sound card | WirePlumber rule `51-builtinspeaker.conf` forces the card to **S16LE, 48000 Hz, 2 channels, no mmap, software mixer**, with a comment that the driver does not publish its capabilities. 48 kHz is therefore the native rate; our float output is converted to 16-bit by the audio stack |
| Kernel | Module trees for `4.9.191`, `4.9.191_v90s` and `6.11.11` are present. The 4.9.191 tree has the A133 codec drivers (`sun50iw10-codec`, `snd-soc-ac107`) built in, so that is the Brick's kernel [inferred from the driver names] |
| **USB audio** | `snd-usb-audio` and `snd-usbmidi-lib` are **built into the 4.9.191 kernel**. So USB audio is supported at the driver level on this stable build, contrary to the worry in section 2. Whether capture and duplex work in practice is still [unknown] |
| Microphone | The `ac107` ADC codec driver is built in. No init script mentions the mic or capture. Whether a capture device appears and at what level is [unknown] |
| How Ports start | EmulationStation runs the port's `.sh` with `/bin/bash` through `configgen/generators/sh/shGenerator.py`. It writes `/tmp/gamecontrollerdb.txt` and **sets `SDL_GAMECONTROLLERCONFIG`** for the connected controllers before launching. So the SDL GameController API should see the Brick's buttons with standard names without us shipping a mapping |
| Exit hotkey context | The generator registers `exit` as Alt+F4 key events for shell ports, but `hotkeygen` is never started on this firmware, so nothing sends it (corrected 2026-11-18; see section 12, "System hotkeys", for how exit and screenshots really work) |
| PortMaster | Not preinstalled; `/usr/share/portmaster/Install.PortMaster.sh` is. `control.txt` and `gptokeyb` are not in the image. A port that relies on PortMaster's helper scripts needs PortMaster installed first; a self-contained `.sh` does not |
| Idle behaviour | `idlewatcher` daemon: after **300 s** idle the default mode is **dim**, and `system.batterysaver.mute` defaults to **1 (mute)**; after a further 900 s the default is suspend. Settings keys: `system.idlewatcher.timer`, `system.idlewatcher.extendedtimer`, `system.batterysaver.mode` (`dim`, `dispoff`, `suspend`, `shutdown`, `none`), `system.batterysaver.extendedmode`, `system.batterysaver.mute`. This confirms the 5-minute audio stop reported for GrainBrick. What counts as activity, and whether an app can inhibit it, is [unknown]; hooks live in `/etc/idlewatcher/active.d` and `extended.d` |
| Tools on the device | `ldd`, `aplay`, `arecord`, `amixer`, `pactl`, `pw-cli`, `wpctl`, `evtest`, `sdl-jstest`'s controller database, Python 3.12, dropbear SSH |

### What this settles

- Open questions 1 and 2 of section 3: glibc exports symbols up to 2.39; the audio server is PipeWire and SDL reaches it through ALSA only.
- The device build must target **glibc 2.39 or older and libstdc++ from GCC 13 or older**. The Debian 13 test image used in M1-D has glibc 2.41 and is too new for device binaries. Debian 12 (glibc 2.36, GCC 12) is a safe build base. GCC 12's library lacks `std::tanf`, which the upstream `DSPUtils.h` calls; `src/shim/std_cmath_compat.h` supplies it for libstdc++ older than 14 (same C library function, arithmetic unchanged: the Debian 12 build matches the same 17 of 25 macOS reference renders as Debian 13).
- The app is built against SDL 2.0.18-level API and runs against 2.32.8, the same minor line as the development Mac.
- The `--audio-driver` option is of little use on the device (only `alsa`), but buffer size still matters.
- The app must handle a fullscreen Mali surface: no window manager, no vsync guarantee, and the logical size should equal the screen.

### What still needs the device

Stable buffer size and real callback load on the A53; what the idle watcher treats as activity; whether SDL reports the buttons as expected (including L2/R2 as buttons or axes, and which physical button SDL calls A); mic and USB capture devices and duplex latency; legibility of 16 px text; and whether the Fn switch or Menu are delivered to apps.

## 11. Measured on the device (first run, 2026-11-18)

The human ran package `7af286f-dirty` on the Brick (KNULLI Scarab 20260511) twice, about three minutes and thirty seconds. Logs are kept outside the repo. Everything here is **[confirmed, device log]** unless marked.

| Item | Finding |
|---|---|
| Result | The app started, drew, played, responded to every button and quit cleanly both times. The human reports everything working as expected apart from A and B being swapped, CPU spikes, and possibly one crackle |
| Start-up | Window and renderer ready 0.28 s after launch, audio 0.37 s, first frame 0.43 s |
| Video | SDL driver `mali`, one display, 1024x768 at 60 Hz, `SDL_PIXELFORMAT_RGBX8888`. A `SDL_WINDOW_FULLSCREEN_DESKTOP` window works. Renderer **`opengles2`**, accelerated, vsync honoured (60.2 frames per second measured), render targets supported, maximum texture 8192x8192. `/dev/fb0` mode `U:1024x768p-60`. Render drivers available: opengles2, opengles, software |
| Audio out | SDL driver `alsa`, device "default", **48000 Hz, 2 channels, float32, 256 frames granted as requested**. Callback clock 1.000x real time. PipeWire's default sample spec is float32 48 kHz. Sink `alsa_output._sys_devices_platform_soc_sndcodec_sound_card0.playback.0.0` |
| Audio in | A capture device exists: ALSA card 0 `audiocodec` (SUNXI-CODEC sun50iw10codec), PipeWire default source `alsa_input..._sound_card0.capture.0.0`, and SDL lists one capture device. So the internal mic is reachable in principle; level and quality are [unknown] |
| Controller | "TRIMUI Brick Controller", GUID `030000005e0400008e02000014010000` (it presents as an Xbox 360 pad). KNULLI sets `SDL_GAMECONTROLLERCONFIG`: `b:b5,a:b4,x:b7,y:b6,leftshoulder:b8,rightshoulder:b9,lefttrigger:b10,righttrigger:b11,back:b12,start:b13,guide:b14,` d-pad on hat 0 |
| **A and B, X and Y** | SDL's `a` is raw button 4 and `b` is raw button 5, and the human reports that **the physical A and B come out swapped** relative to their labels. The human also reported X and Y as correct, but only one of the two was pressed in either run (SDL `y`, raw button 6, once), so that was never checked [reported, unverified]. From the firmware (`es_input.cfg`: a = BTN_EAST id 5, b = BTN_SOUTH id 4, x = BTN_NORTH id 6, y = BTN_WEST id 7; `configgen/controller.py`'s default mapping swaps a/b and x/y into SDL's positional Xbox naming), the Brick's Nintendo-style labels give: labelled A (right) = SDL `b`, B (bottom) = SDL `a`, **X (top) = SDL `y`, Y (left) = SDL `x`** [inferred, about 85 % confident]. So both pairs need swapping for labels to match. KNULLI's per-port option "SWITCH A/B and X/Y" does the same at the mapping level (flag file `/var/run/ports_xbox_layout.flag`) |
| L2 / R2 | Raw buttons 10 and 11; SDL delivers them as **trigger axes jumping to 32767**, which the app's axis threshold handles |
| Menu | Arrives as SDL `guide` (raw button 14). Not used by the app |
| Select, Start | SDL `back` and `start`. Start + Select held for a second quits, as designed |
| Process | Launched at nice -4 with `SDL_RENDER_VSYNC=1` in the environment, as read from the image |
| CPU governor | `schedutil`, range 408 to 1416 MHz |
| **Callback load** | With a 256-frame buffer the smoothed callback load was **30 to 36 % of the buffer time** for most of both runs, including in silence, with maxima of 42 to 48 % and single spikes of 80 % and 94 %; at the end of the second run the smoothed value read **11.9 %** (this is the smoothed load at the end of a window, a half-second average, not a window average; it coincided with the Start + Select hold). No callback overran its buffer (overrun counter 0 throughout). The likely explanation is the governor: `schedutil` runs the core slowly while the load is light, so the same work takes about three times longer than at full speed, and the 11.9 % reading is the core at or near 1416 MHz [inferred from one reading; the next build logs the frequency and a load histogram to confirm or refute]. If that holds, the engine costs roughly an eighth of one core at 256 frames at full speed |
| Idle watcher settings | `system.batterysaver.mode=dim`, `mute=1`, `aggressive=0`, `extendedmode=suspend`, timers 300 s and 900 s (the defaults). The keep-awake pause file was created and removed correctly. No run was long enough to show whether it prevents dimming |
| Clock | The device's clock read 1970-06-16, so it has no usable real-time clock without a network. The launcher's run counter kept the logs in order |
| Storage | `/userdata` is `/dev/mmcblk0p5`, 115 GB; the Mac could read the card, so it is not ext4 [inferred from the human copying the data folder directly] |

Still unknown after this run: whether the system exit hotkey reaches the app (both runs were ended with Start + Select); whether keep-awake prevents the five-minute dim and mute; legibility of the small text (the human did not complain); the Fn switch and volume buttons (nothing from them appears in the log, so they are handled by the system); mic level; USB audio.

### How KNULLI handles power mode and the governor (read from the image, 2026-11-18)

- `/etc/init.d/S18governor` sets the boot governor from `system.cpu.governor`, falling back to `schedutil`.
- Every game or port launch runs hook scripts at `gameStart` and `gameStop`. `powermode_launch_hooks.sh` calls `/usr/bin/knulli-power-mode` with the per-game, per-system or global power mode at start, and re-applies the global or default mode at stop. "High Performance" maps to the `performance` governor.
- The Ports system exposes this as a per-game option, **POWER MODE** (High Performance, Balanced, Power Saver). That is the supported way to run a port at full CPU speed, and KNULLI restores the governor itself afterwards.
- KNULLI's exit path: `emulatorlauncher` kills the launcher script with SIGKILL and then runs the `gameStop` hooks; `knulli-es-swissknife --emukill` sends SIGTERM down the process tree.
- The idle watcher's "aggressive" mode sets `powersave` and stops processes at nice -4; it is skipped while a `*.pause` file exists in `/var/run/battery-saver/`.

## 12. Measured on the device (second run, 2026-11-18)

Package `9b9d7db-dirty`. Two sessions: run 3 at KNULLI's defaults (about 15 minutes, mostly left droning untouched) and run 4 with the port's POWER MODE set to High Performance (about 4 minutes). Everything here is **[confirmed, device log or the human's report]**.

| Item | Finding |
|---|---|
| **Face buttons** | Confirmed, all four. Labelled X = SDL `y` (raw button 6), Y = SDL `x` (raw 7), A = SDL `b` (raw 5), B = SDL `a` (raw 4). With both swaps on, the human reports every button doing what its label says. The inference in section 11 was right |
| Other raw buttons | Raw joystick buttons 0 and 1 arrive (probably the volume buttons) and 14 is Menu (`guide`). None are mapped by the app; the volume buttons still change the system volume |
| **Keep-awake** | Works. Run 3 went 12 minutes with no input; the screen stayed on and the sound kept playing (the human's report). The idle watcher's settings were at their defaults (dim and mute after 300 s) |
| **Governor hypothesis** | Confirmed. With `schedutil` the CPU sat at **408 MHz in 76 of 89 ten-second samples** (600 MHz in 7, 816 in 1, 1416 in 5) and the callback load averaged **32 to 36 %** of a 256-frame buffer. With `performance` (KNULLI's POWER MODE: High Performance) the CPU stayed at 1416 MHz and the load was **11 to 12 %**, maximum 34 % |
| **Overruns** | At the default governor: **9 callbacks in about 15 minutes took longer than their buffer** (maxima 100 to 122 %), one in each of 9 of the 89 windows. In High Performance mode: **0**. The human heard no problems in either run. So late callbacks do occur at the default setting, roughly one every 100 seconds, and they are either inaudible or absorbed by the audio server's own buffering |
| Frame rate | 60.2 frames per second throughout both runs |
| KNULLI's per-port settings | `ports["AudreyBrick.sh"].powermode=highperformance` is how the POWER MODE option is stored; `xbox_layout` unset. One cpufreq policy (`policy0`, CPUs 0 to 3); available governors: interactive, conservative, userspace, powersave, ondemand, performance, schedutil |
| Battery | 83 % at the start of run 3, 79 % at the start of run 4, discharging (`axp2202-battery`) |
| Text | The human finds the small text acceptable |
| **System hotkeys** | Settled (firmware reading plus the human's tests on 2026-11-18). **Exit:** KNULLI's `triggerhappy` (`/etc/triggerhappy/triggers.d/multimedia_keys.conf`) runs `knulli-softreset` on Menu + Start; a **double press within half a second** sends SIGTERM down the game's process tree. The human confirmed this ends AudreyBrick. The single-press exit that emulators have comes from `evmapy`, which only starts when a `<rom>.keys` file exists; we ship none, and instead the app quits on Menu + Start itself (Menu is SDL `guide`, raw button 14). **Screenshot:** `triggerhappy` runs `knulli-screenshot` when **Start is pressed while Select is held**; it grabs `/dev/fb0` with `fbgrab` into `/userdata/screenshots/knulli-screenshot_1024x768_<date>.png`. The human confirmed these screenshots work and look right for our GLES app. Our former Start + Select hold-to-quit collided with it (every screenshot showed the quit banner), so that gesture was removed. `hotkeygen` is not used on this firmware: nothing starts it or sets its context, so the Alt+F4 exit context in `shGenerator.py` never fires |

Decision by the human (2026-11-18): leave the default governor as it is for now; revisit if later features need the headroom.

### Third run (2026-11-18)

Package `401ae63-dirty`, three sessions. **[confirmed, device log and the human's report]** One press of Menu + Start closes the app (`exit: Menu + Start`), in either press order, with the keep-awake file removed and a clean shutdown each time. KNULLI's Select + Start screenshot of the running app shows the normal screen; the file is a 1024x768 PNG of about 15 kB in the card's `screenshots` folder.

### File input run (2026-11-19)

Package built from the M2b-A3 tree (file input with the hold-X pop-up browser), default governor. **[confirmed, device log and the human's report]**

| Item | Finding |
|---|---|
| Result | The human reports the browser, crossfade and play modes working as expected and the pop-up readable |
| Folder scan | 7 entries on the card: 1.3 to 4.4 ms |
| Load time, 48 kHz files | 523 s stereo 24-bit (100 MB as mono float): **2.1 s**. 19.5 s stereo: 0.12 s. One-second mono files: 7 to 20 ms |
| Load time, converted | 197 s stereo 24-bit at 44.1 kHz: **5.4 s** (four loads, 5.41 to 5.49 s). Sample-rate conversion, not the card, dominates: about 27 ms per second of audio against about 4 ms per second unconverted |
| Memory | `MemAvailable` 678 MB at start-up; 578 MB with the 100 MB file loaded. The 10-minute cap (115 MB per file, two at most) fits comfortably |
| Callback load | Unchanged by a playing file: 33 to 36 % of a 256-frame buffer at the default governor; maxima up to 69 % while a file was loading. **0 overruns** in the run |

### Live input run and capture diagnosis (2026-11-19)

Package built from the M2b-B tree, then a diagnostic session over adb with the human's permission. **[confirmed, device log, `arecord` tests and kernel log]** unless marked.

| Item | Finding |
|---|---|
| Capture devices SDL lists | `audiocodec, ` (built-in, ALSA card 0, `hw:0,0`) and, with the human's USB interface plugged in, `JIANT FM, USB Audio` (card 1). The list updates on plug and unplug |
| **USB by name** | Works. Opened in 59 ms: 48000 Hz, 2 channels, 256 frames (float after SDL's conversion). About 17 minutes with no skips or repeats; 2 underruns at start; the capture buffer was raised from 512 to 1024 frames at start and to 2048 at 14 minutes. Capture arrives in bursts of 1024 frames. Unplugging is reported by SDL. The human reports it working perfectly |
| **Built-in mic by name (SDL, `sysdefault:CARD=audiocodec`)** | Fails: `Couldn't set hardware audio parameters`, kernel `can't set sun50iw10codec hw params: -22`. ALSA's plug layer picks a hardware format the codec rejects (`arecord -D plughw:0,0 -f FLOAT_LE` and `-D sysdefault:CARD=audiocodec` fail the same way) |
| **Built-in mic, direct** | `arecord -D hw:0,0 -f S16_LE -r 48000` with 1 or 2 channels works (also 44100 Hz, also a 256-frame period), **only with the mixer set**. The hardware advertises S16_LE and S24_LE, 1 to 2 channels |
| **Mixer** | Defaults after boot: `ADCL Input MIC1 Boost` off, `MIC1 gain volume` 31, `ADC volume` 0,0 (-119 dB). With these, direct capture delivers no frames and blocks. With boost on and `ADC volume` 160,160, capture delivers real audio. (GrainBrick sets the same three controls before recording from `hw:0,0`.) |
| **Capture through PipeWire (ALSA `default`, SDL's default device)** | **Broken, and harmful.** With the built-in mic as the default source nothing is delivered (also with plain `arecord -D default`), the kernel logs `invalid rate setting` / `machine hw_params failed: -22`, and PipeWire then keeps `/dev/snd/pcmC0D0c` open in a failed state, so every direct open is `Device or resource busy` until reboot. PipeWire lists the source as `s24-32le`, `audio.channels = 64`; the firmware's WirePlumber rule forces sane values for the output node only. With the USB interface as the default source it delivered silence at about a third of real time, then stalled. **Never open the default capture device on this firmware** |
| PipeWire and the capture PCM | On a fresh boot PipeWire holds only the playback PCM; the capture PCM is closed. Direct capture and PipeWire playback coexist |
| Output load with USB capture | Default governor (408 MHz almost throughout): 28 late output callbacks in 18 minutes, maxima 102 to 141 % of the buffer; average load 33 to 36 % as before |
| SDL and `AUDIODEV` | SDL 2.32.8's ALSA driver opens the ALSA name in the `AUDIODEV` environment variable when asked for the default device, and falls back from float to 16-bit on raw hardware. [confirmed, SDL source, and on the device: see the next table] |

### Built-in mic run (2026-11-19)

Package `8b45e8c-dirty` (the M2b-B2 tree), default governor, about 12 minutes. **[confirmed, device log and the human's report]** The human reports everything working as expected.

| Item | Finding |
|---|---|
| **Built-in mic, direct route** | Works. SDL opens `hw:0,0` through `AUDIODEV` with a NULL device name: asked 48000 Hz float 1 channel 256 frames, obtained the same (SDL converts from the codec's 16-bit). Open takes 82 to 85 ms, 188 to 194 ms including the mixer calls. Opened twice in the run |
| Mixer | `amixer -c 0 cget/cset name=...` works as called: 16 to 24 ms per call. Boost off -> on, gain 31 -> 31, ADC volume 0,0 -> 160,160 on open; all three restored on both closes (device change and quit) |
| Mic capture quality of service | 7 minutes continuous: 0 underruns, 0 skips, 0 repeats, capture clock 1.0000x the output clock, fill 512 to 768 frames at the 512-frame target (no raise needed). Capture arrives in 256-frame callbacks, unlike the USB interface's 1024-frame bursts |
| Mic level | The pre-gain peak reached 1.000 (full scale) with these mixer values, so the mic can clip before the app's gain. The human did not report a problem; lowering `ADC volume` is the remedy if it matters |
| Device list | `audiocodec, ` only at start; the USB interface appears on plug-in; no SYSTEM DEFAULT |
| USB after the change | Still opens by name (105 ms, 2 channels); the target was raised to 2048 frames within 0.2 s as before. Unplugging is reported as DEVICE LOST within 50 ms, and the mic reopens afterwards |
| Close time | 46 to 77 ms for either device |
| Output load | 31 to 37 % average as before; 1 late callback (115 %) in the run, at the moment the USB device was unplugged |

### Recording run (2026-11-19 and 2026-11-20)

Package `de2fc16-dirty` (the REC tree), default governor, three sessions, six recordings. **[confirmed, device log and the human's report]** The human reports recording working as expected and the files playing.

| Item | Finding |
|---|---|
| SD card writes | 205 ten-second windows of recording: longest single write **27.7 ms** (typically 8 ms); the 4-second ring never held more than **9728 frames (0.20 s)**; **0 dropped frames** in every recording |
| Longest recording | 27 min 52 s (`rec-0002.wav`, 321 MB), written without a drop |
| Finishing a file | 30 to 66 ms from the stop press to the file closed |
| Files | Six files; each size equals its logged data bytes plus the 44-byte header. Numbering continued across app restarts (`rec-0001` to `rec-0006`) |
| Free space | `statvfs` on the card reports about 92.7 GiB free; the launcher and app agree |
| Recording across sources | Recordings ran through source changes and capture-device opens with no drops |
| Quit while recording | Run on 2026-11-20 (package `3bf3139-dirty`): Menu + Start during `rec-0007.wav` saved 5.4 s with reason `quit`, finalised in 61 ms, exit code 0; the human reports the file plays |
| Not exercised | The file limit and card-full stops have only been tested with injected limits |

### Gain memory run (2026-11-20)

Package `3bf3139-dirty`. **[confirmed, device log and the human's report]** The human reports it working as expected.

| Item | Finding |
|---|---|
| First open | The built-in mic and the USB interface each opened at -inf dB the first time in a session, in both sessions |
| Restores | 12 restores in one session: the mic came back at 0.362 (-5.6 dB) five times and the USB interface (`LOOM-8 FM-1, USB Audio`) at 0.479 (-0.8 dB) four times, each independent of the other; FILE returned to its own 0.500 (0.0 dB) after every LIVE visit, including through OFF |
| Order | Every restoring open logs gain to 0, then the open, then the restore, as designed |
| Open times | Mic 211 to 226 ms (mixer included), USB 31 to 69 ms |
