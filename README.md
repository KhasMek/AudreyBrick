# AudreyBrick

A port of Synthux Academy's **Audrey II** "horrorscape" feedback synthesiser to the TrimUI Brick running KNULLI.

Audrey II is a droning feedback instrument: a plucked-string model fed with inaudible noise, then distortion, filters, reverb and a long body delay fed back into the string, and a final delay whose repeats can run away and saturate. Nothing sounds until you raise the feedback. AudreyBrick runs the original firmware's DSP code unchanged and replaces the eleven knobs and the switch with the Brick's buttons.

AudreyBrick is an unofficial port. It is not made or endorsed by Synthux Academy or Infrasonic Audio.

## Credits

Audrey II by [Synthux Academy](https://github.com/Synthux-Academy/Audrey-II).

- Concept and firmware: Nick Donaldson (Infrasonic Audio)
- Visual and hardware design: Roey Tsemah

Full notices for all third-party code are in [`THIRD_PARTY.md`](THIRD_PARTY.md) and [`licenses/`](licenses).

## What it does

- The complete Audrey II signal chain at 48 kHz, as in the firmware.
- Audio input, as on the original: from a WAV file, the Brick's built-in microphone or a USB audio interface.
- Four modulation slots (LFO shapes and an envelope) that can move any parameter.
- Presets, saved as plain JSON files.
- Recording of the output to a WAV file.

## Requirements

- TrimUI Brick running KNULLI. Tested on the "Scarab" release of 11 May 2026.
- Headphones are recommended. **Start with the volume low: the feedback can run away.** The built-in microphone and the speaker together can howl.

## Install

1. Download the zip from the [releases page](https://github.com/KhasMek/AudreyBrick/releases) and unpack it.
2. Copy `AudreyBrick.sh` and the folder `audreybrick` into `roms/ports` on the SD card.
3. In KNULLI, update the game lists, then start AudreyBrick from Ports.

To update, replace both with the new ones. Your recordings, presets, input files and logs are kept in `saves/audreybrick` on the card and are not touched by an update.

## Controls

| Button | Function |
|---|---|
| L1 / R1 | Previous / next tab |
| R2 / L2 (hold) | Feedback up / down |
| A (hold) | Feedback to maximum while held |
| B (hold) | Delay switch while held; Select + B latches it |
| D-pad | Change the current tab's parameters |
| X (hold) | The d-pad moves the tab's other parameter |
| Y + d-pad | Reset that parameter |
| Select (hold) | Fine steps |
| Start + Y | Start / stop recording |
| Start + A | Save a new preset |
| Menu + Start | Quit |

Tabs: LOOP, FILTER, REVERB, DELAY, OUT, INPUT, MOD, PRESET. The full manual is [`packaging/README.txt`](packaging/README.txt), which is also in the zip.

## Building

Requirements: CMake 3.20 or newer, a C++17 compiler and SDL2. Docker is needed for the device build.

Host build and tests (macOS or Linux):

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j
ctest --test-dir build/host
```

Add `-DAUDREY_WITH_SDL=OFF` to build only the DSP, the offline renderer and their tests.

Device build (Linux aarch64, in a container):

```sh
scripts/build_device.sh
scripts/package_port.sh
```

The package is written to `dist/`. `scripts/make_port.sh <KNULLI system image>` does the same and also tests the package against the firmware's own libraries.

## Documentation

- [`docs/PARAMETERS.md`](docs/PARAMETERS.md): every control and what it does.
- [`docs/SIGNAL_FLOW.md`](docs/SIGNAL_FLOW.md): the DSP chain.
- [`docs/SOURCE_STUDY.md`](docs/SOURCE_STUDY.md): notes on the original firmware.
- [`docs/PRESET_FORMAT.md`](docs/PRESET_FORMAT.md): the preset file format.
- [`docs/PLATFORM_NOTES.md`](docs/PLATFORM_NOTES.md): what was learned about KNULLI on the Brick.

## Licence

AudreyBrick's own code is under the MIT licence, see [`LICENSE`](LICENSE). The Audrey II firmware code is MIT (Synthux Academy; Infrasonic Audio, LLC). DaisySP's `ReverbSc` and `Tone` are treated as LGPL-2.1 and built as a separate shared library that can be replaced; see `THIRD_PARTY.md`.
