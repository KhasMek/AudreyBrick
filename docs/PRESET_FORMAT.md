# Preset file format (version 1)

A preset is one JSON file in the presets folder. It holds **the sound**: the eleven knobs' stored positions, the latched state of the delay switch and the four modulation slots. It holds nothing about the audio input (source, device, file, play mode, input gain) and nothing momentary (A = MAX held, B held). Loading a preset never changes the input.

Code: `src/preset/` (`PresetFormat.h` is the format, `Json.h` the reader, `PresetStore.h` the folder, `PresetState.h` capture and apply). Decided in `docs/briefs/PRESET.md`.

## Complete example

This is exactly what the app writes (the test `preset_format_test` checks this text byte for byte): the start-up state with the feedback at half travel, the switch latched on, slot 1 a triangle on the lowpass and slot 2 an envelope on the input gain.

```json
{
  "format": "audreybrick-preset",
  "version": 1,
  "app": "abc1234",
  "knobs": {
    "Frequency": 0.428571433,
    "FeedbackGain": 0.5,
    "FeedbackBody": 0,
    "FeedbackLPFCutoff": 1,
    "FeedbackHPFCutoff": 0.537242174,
    "ReverbMix": 0,
    "ReverbDecay": 0,
    "EchoDelaySend": 0,
    "EchoDelayTime": 0.301511347,
    "EchoDelayFeedback": 0,
    "OutputVolume": 0.707106769
  },
  "delaySwitch": true,
  "mod": [
    { "shape": "triangle", "target": "FeedbackLPFCutoff", "rate": 0.25, "depth": 0.5, "polarity": "bipolar" },
    { "shape": "envelope", "target": "InputGain", "rate": 0.377285182, "depth": -0.25, "polarity": "unipolar" },
    { "shape": "sine", "target": "None", "rate": 0.377285182, "depth": 0, "polarity": "bipolar" },
    { "shape": "sine", "target": "None", "rate": 0.377285182, "depth": 0, "polarity": "bipolar" }
  ]
}
```

The smallest valid preset is `{"format": "audreybrick-preset", "version": 1}`: every field then takes its start-up value.

## Keys

| Key | Type | Meaning |
|---|---|---|
| `format` | string, required | Exactly `"audreybrick-preset"` (case matters). |
| `version` | number, required | The format version, a whole number. This document is version **1**. |
| `app` | string, optional | The version of the app that wrote the file (`git describe` output; `audrey-render <version>` from the render tool). Informational: never read. |
| `knobs` | object, optional | Knob name to stored position, 0..1 (below). |
| `delaySwitch` | boolean, optional | The switch's **latch** (Select + B). B held momentarily is not part of it. |
| `mod` | array, optional | The four modulation slots, slot 1 first (below). |

### Knob names

The ids of `src/core/ParamTable.cpp` (the firmware's enum names, also used by scripts), defined once in `presetKnobName()`. Each value is the knob's **stored** normalised position 0..1 (for `FeedbackGain` the stored feedback, which A = MAX does not change).

| Name | Panel | Start-up value |
|---|---|---|
| `Frequency` | frequency | 0.42857143 |
| `FeedbackGain` | feedback | 0 |
| `FeedbackBody` | body | 0 |
| `FeedbackLPFCutoff` | lowpass | 1 |
| `FeedbackHPFCutoff` | highpass | 0.53724216 |
| `ReverbMix` | reverb dry/wet | 0 |
| `ReverbDecay` | reverb decay | 0 |
| `EchoDelaySend` | delay mix | 0 |
| `EchoDelayTime` | delay time | 0.30151134 |
| `EchoDelayFeedback` | delay repeats | 0 |
| `OutputVolume` | volume | 0.70710678 |

### A modulation slot

| Key | Type | Values (written as) | Start-up value |
|---|---|---|---|
| `shape` | string | `sine`, `triangle`, `square`, `rampup`, `rampdown`, `expup`, `expdown`, `samplehold`, `envelope` | `sine` |
| `target` | string | `None`, a knob name from the table above, `InputGain`, `DelaySwitch` | `None` |
| `rate` | number | The normalised rate 0..1 (LFO: 0.01 Hz at 0 to 2000 Hz at 1, exponential; envelope: the release, 5 s at 0 to 5 ms at 1). | 0.377285182 (1 Hz) |
| `depth` | number | -1..+1 of the target's travel | 0 |
| `polarity` | string | `bipolar`, `unipolar` (the envelope is always unipolar; the stored setting is kept) | `bipolar` |

The start-up slot is `ModPanel::defaultSlot()`. The names are those of `src/mod/ModTypes.h`, the same as script `mod` lines and the `--mod` option.

## Reading rules

1. **Parse completely, then apply.** The whole file is read and checked first; a file that fails changes nothing at all.
2. **Size.** A file larger than 64 KiB fails ("too large").
3. **JSON is strict** (RFC 8259): no comments, no trailing commas, no single quotes, no `NaN` or `Infinity`, no leading `+` or zeros, nothing after the closing brace but whitespace. Line endings may be LF or CRLF. A UTF-8 byte order mark at the very start is ignored. Strings must be valid UTF-8. Nesting deeper than 32 levels, more than 16384 values, or more than 256 keys in one object fail (the last bounds the duplicate-key check: a preset's largest object has 15 keys). A syntax error fails with its line and column in the log.
4. **Numbers** are read as doubles in any locale. A number too large for a double (`1e999`) fails; a number below 1e-300 in size reads as 0.
5. **The marker and version.** A missing or different `format` fails ("not a preset"); so does a file that is not a JSON object. A missing, fractional or non-positive `version` fails ("bad version"); a version above 1 fails ("made by a newer version").
6. **Missing fields take their start-up value**, not the value currently set: a knob, the switch, a whole slot or a single slot field. Loading a preset therefore always gives the same settings, whatever was set before.
7. **Unknown keys are ignored** at every level, whatever their type. Elements of `mod` after the fourth are ignored.
8. **Wrong types fail**: wherever this document defines a key, a value of another type (a knob as a string, `delaySwitch` as `1`, a slot that is not an object, `app` that is not a string) fails ("bad value").
9. **Out of range values are clamped**: knobs and `rate` to 0..1, `depth` to -1..+1. A depth smaller than 1e-6 in size becomes 0 (as the MOD tab stores it).
10. **Names.** Knob names are matched without regard to case (as in scripts): `frequency` is `Frequency`. Every other key is matched exactly. `shape`, `target` and `polarity` accept every spelling `ModTypes.h` accepts, in any case and with its aliases (`sh` and `s&h` for samplehold, `env` for envelope, `switch` for DelaySwitch, `bi`, `uni`); any other name fails ("bad value"). The app always writes the canonical spelling shown in the tables.
11. **Duplicate keys fail.** A key repeated in one object is a syntax error; two keys naming the same knob (`Frequency` and `frequency`) fail ("bad value"). Neither "first wins" nor "last wins": a file that says two things is not loaded.

## Writing rules

- The text is canonical: UTF-8, LF line endings, two-space indent, the keys in the order of the example, every knob and every slot written, numbers with `%.9g` in the C locale (so reading returns the identical float: save then load restores every value bit for bit), names in the canonical spelling. Invalid values (NaN, out of range) are made valid before writing, so a written file always reads back.
- **Write, then rename.** The text goes to a temporary file `.<name>.tmp` in the same folder, which is flushed (`fsync`) and closed, then renamed over `<name>`; then the folder is flushed (best effort). If something that is not a regular file already has the temporary name (a symbolic link, a folder, a FIFO), `.1.<name>.tmp`, `.2.<name>.tmp` and `.3.<name>.tmp` are tried; if all four are blocked the save fails ("temporary name blocked"). If any step fails, the temporary file is removed and the reason is reported. A stale temporary file (a regular file whose name starts with `.` and ends in `.json.tmp`) left by a power cut is removed at the next scan of the folder.
- **What a power cut can do.** On a file system where `rename()` replaces the target atomically (ext4 and other POSIX file systems, so the KNULLI SHARE partition when it is ext4), a power cut leaves either the old file or the new one, never half a file under a `.json` name. On an exFAT or FAT card, which the Brick's card may be, write-then-rename is the best technique available, but the file system does not promise atomic replacement: a power cut during the rename can still lose or damage that one file. Other presets are not touched by a save.
- **Failure reasons** (short, shown on the screen; the full detail goes to the log):
  - any operation: "no preset folder" (the app was started without `--preset-dir`), "bad name";
  - load: "not found", "not a file", "too large", "unreadable", and the reading reasons above ("syntax error", "not a preset", "bad version", "made by a newer version", "bad value", "out of memory");
  - save new: "no folder" (it could not be created or listed), "read-only", "not allowed", "numbers used up" (see Numbering), "name taken" (another program created the new name between the scan and the rename), and the write reasons;
  - overwrite: "folder missing", "unreadable", "not found" (the file has gone), and the write reasons;
  - write reasons: "card full", "read-only", "not allowed", "folder missing", "write failed", "temporary name blocked";
  - delete: "not found", "read-only", "not allowed", "folder missing", "write failed".

## The presets folder

- **Listed:** regular files (symbolic links are not followed) whose name ends in `.json` in any case and does not start with `.`. Hidden files are skipped, which also skips macOS's `._` files on exFAT cards and our temporary files. Sub-folders are not entered.
- **Names on screen:** the file name without `.json`, so a file renamed on a computer shows its new name.
- **Order:** by the name shown (without `.json`), case-insensitive, with runs of digits compared as numbers (`preset-2` before `preset-10`, `Drone 9` before `Drone 10`, `Drone` before `Drone 2`, `preset-0007` before `preset-0007 (copy)`). Names that are equal this way (`Drone`, `drone`) are ordered by their bytes, and two files with the same shown name (`a.json`, `a.JSON`, possible only on a case-sensitive file system) by the file name, so the order is the same everywhere.
- **Cap:** at most 2000 entries are listed (the first 2000 in that order); the store reports the total and that the list is cut. Numbering still counts every file.
- **Numbering:** a new preset is `preset-NNNN.json` (four digits, then wider: `preset-10000.json` follows `preset-9999.json`), one more than the highest `preset-<1 to 9 digits>.json` in the folder (case-insensitive; files and folders alike; 1 when there is none). Deleting the highest one frees its number. Numbers stop at `preset-999999999.json`: while that name exists, saving a new preset fails with "numbers used up" and writes nothing; rename or delete it.
- **Missing folder:** an empty list and a "missing" state, not an error. Saving creates the folder and its parents if it can.
- **Overwrite** replaces only an existing file; if it has gone (renamed or deleted on a computer) it fails ("not found") and creates nothing.

## Loading and saving in the app

PRESET-B wires this to the PRESET tab and to Start + A (`src/app/PresetPanel.h`; the gestures are in `docs/CONTROL_SCHEME_PROPOSAL.md` section 0b). The app's presets folder is `--preset-dir` (the launcher passes `presets/` in the data folder); without it saving reports "no preset folder". Every save (SAVE NEW, overwrite, Start + A) waits until the audio side has applied every change made before it (normally one audio buffer) and then captures; if that has not happened within 0.5 s of wall clock the save fails with "audio not running" and nothing is written, so a file never holds stale values. What PRESET-A provides (`src/preset/PresetState.h`):

- **Capture** reads the stored knob values and the latch as the audio side last published them (`ButtonControlSource`) and the slots from the MOD tab's panel (`ModPanel`). It reports "pending" while a command (for example a load in the same frame) has not reached the audio side yet; save after it reports "ok".
- **Apply** goes through the same paths as a user's change: one `SetAll` command on the control queue (after stopping any d-pad or L2/R2 ramp in progress) and `ModPanel::setSlot()` for each slot (after stopping any MOD ramp). Every value jumps; the knob smoothing and the modulation smoothing do the rest (no extra fade): loading is audible by design. A slot that was already live blends to a new polarity over the modulation's smoothing, as when the setting is changed by hand. A and B held stay held. If the control queue is full, nothing is applied.
- All file work runs on the UI thread. A preset is a few hundred bytes; the slowest single write measured on the Brick's card was 28 ms, which can cost a frame but never audio.

## The render tool

`audrey-render --write-preset <out.json> <script> <out.wav>` writes the state at the end of the render as a preset (the knobs and switch of the last control block, the four slots at the end). `audrey-render --preset <in.json> <script> <out.wav>` starts the render from a preset instead of the start-up state; the script's events then apply on top (a ramp starts from the preset's value). A script that sets every value at time 0 renders byte for byte the same as an empty script rendered from the preset it wrote (`preset_tool_test`).

## Changing the format

A new version only when old readers would misread a file. Adding optional keys does not need one: old readers ignore unknown keys (rule 7). A reader must refuse a version it does not know (rule 5).
