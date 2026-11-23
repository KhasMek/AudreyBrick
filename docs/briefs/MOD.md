# MOD: modulation

Requested by the human on 2026-11-17, designed with the human on 2026-11-20. The human decided on 2026-11-20 to build modulation before presets (presets must store modulation settings) and before M4. Two implementation tasks, each followed by a review: **MOD-A** (the engine side, this section) and **MOD-B** (the MOD tab and its controls; brief at the end of this file).

Read first: `CLAUDE.md`, `docs/PLATFORM_NOTES.md` section 6, `docs/PARAMETERS.md` (the smoothing column and the curves), `docs/SIGNAL_FLOW.md`, `src/core/ControlSource.h`, `src/core/AudreyVoice.{h,cpp}`, `src/core/ParamTable.{h,cpp}`, `src/control/` (how the UI thread's commands reach the audio thread), `src/platform/AudioRender.{h,cpp}`, `src/input/InputStage.h`.

## The human's decisions (do not change)

- **Four modulation slots.** Each has a shape, a rate, a depth and one target.
- **Shapes:** sine, triangle, square, sample-and-hold (random steps), and an **envelope follower on the audio input**.
- **Targets:** any setting. That is all eleven knobs (`Knob::Frequency` ... `Knob::OutputVolume`, including the feedback gain and the volume), the input gain, and the delay switch. Plus "none".
- **Rate:** the whole range from slow drift to audio rate.
- With nothing modulating, the instrument is exactly the faithful port.

## Design constraints found by the orchestrator

1. **The original smooths every knob** (`docs/PARAMETERS.md`): a one-pole per control block with time constants from 7.2 ms (most knobs) to 29 ms (frequency) and 145 ms (body), and two more per-sample smoothers inside the engine (body delay 0.2 s, echo time 0.5 s). A modulator that only moved the knob value (`ControlFrame::knob[]`, before the smoother) could never reach audio rate: everything above roughly 20 Hz would be flattened. So modulation must be applied **after** the original's parameter smoother. `AudreyVoice` (ours, `src/core`) owns the registry and the handlers that pass each smoothed value to the engine's setter, so this can be done there without touching the vendored code.
2. **The vendored engine is not modified** (`scripts/verify_vendor.sh` must still pass). The two smoothers inside the engine (body delay length, echo time) therefore stay: modulating those two targets is slow by nature (that is the original's pitch-sweep character). Document it; do not work around it.
3. **The control rate is 12 kHz** (one control block per 4 samples). Modulators run once per control block. That is the "audio rate" available: rates up to 2 kHz are meaningful. Naive (non-band-limited) square and sample-and-hold shapes are acceptable.

## What to build (MOD-A)

### 1. Post-smoothing modulation in `AudreyVoice`

- Extend the control path so that each control block carries, besides the eleven knob values and the switch, **one modulation offset per knob in normalised knob units** (-1 to +1 of the knob's travel; 0 = none) and a modulated switch request. Suggested: new fields on `ControlFrame` (`float knobMod[kKnobCount]`, `bool delaySwitchMod`), written by every `ControlSource` (existing sources write zeros and false). Choose another shape if it is cleaner, and say why.
- For a knob whose offset is exactly 0 the handler path must be **bit-for-bit what it is today** (same calls, same values, same order). For a non-zero offset the value given to the engine's setter must be what the original mapping would give for the knob position `clamp(n_s + offset, 0, 1)`, where `n_s` is the knob position that corresponds to the smoothed value (i.e. invert the curve of that parameter, add, clamp, map again; or the mathematically equal operation in the mapped domain, e.g. adding semitones for the frequency, multiplying by a ratio for the exponential cutoffs). Take the curves from `ParamTable` and `docs/PARAMETERS.md`, including the pre-shaping of `ReverbDecay` and the switch scaling of `EchoDelayTime`. Test every knob: offset 0 identical; `map(unmap(v) + o)` matches a direct `map(n + o)` within float tolerance across the range; clamping at both ends; no NaN or inf for any input.
- The modulated delay switch: the engine sees the switch on if the manual switch is on **or** the modulated request is on. With no slot targeting the switch it is exactly the manual switch.
- The existing 25 reference renders must keep their hashes (`scripts/make_reference.sh --check`) and the Linux build must still match the same 17.

### 2. The modulators (new library, suggested `src/mod/`, no SDL, MIT, ours)

- `ModSlot` settings: `shape` (Sine, Triangle, Square, SampleHold, Envelope), `target` (None, each `Knob`, InputGain, DelaySwitch), `rateNorm` 0..1, `depth` -1..+1.
- **Rate mapping:** exponential from **0.01 Hz** (`rateNorm` 0, a 100 s cycle) to **2000 Hz** (`rateNorm` 1). For the Envelope shape the same control sets the follower's **release time**, from 5 s (`rateNorm` 0) to 5 ms (`rateNorm` 1); attack fixed at 2 ms.
- **Outputs:** the four LFO shapes are bipolar, -1..+1; a slot's contribution is `depth * value`. The envelope is unipolar, 0..1 (follower of the absolute input level, clamped at 1); contribution `depth * value`. Several slots on one target add; the sum is clamped by the voice as above.
- Phase accumulators in double or in fixed point so that a 0.01 Hz cycle is exact enough over hours; free-running (no reset on parameter changes); a rate change continues from the current phase without a jump. Sample-and-hold takes a new value from a small deterministic PRNG (seeded per slot, so renders are reproducible) at each cycle start. A reset function for tests and for the render tool.
- **Depth and target changes must not click more than the modulation itself does:** smooth `depth` with a short one-pole (about 10 ms) at the control rate; when the target changes, the old target's contribution goes to zero and the new one starts from zero through the same smoothing (i.e. smooth per-target contributions or cross-fade; choose and explain). When a smoothed contribution has decayed to below 1e-6 and its goal is zero, snap it to exactly 0 so the bit-identical path is taken again.
- **Envelope source:** the mono input signal the engine receives for that callback (after the input stage's gain and fades, before any InputGain modulation). With the input OFF it is silence, so the envelope is 0. Computed per sample or per control block (max of |x| over the 4 samples, then attack/release); say which.
- **InputGain target:** a multiplicative gain on the mono input applied per control block with per-sample linear interpolation across the block (no steps), equal to the input stage's own gain law evaluated at `clamp(g + offset)` divided by the law at `g`, where `g` is the current normalised input gain; when `g` is 0 (-inf dB) the input stays silent (do not divide by zero; a modulator cannot open a closed input: this keeps the howl safeguard). Find the law in `src/input/` (`(2n)^2`) and reuse it, do not copy constants. With no slot on InputGain the input buffer is untouched (bit-identical).
- **DelaySwitch target:** request on while the slot's smoothed contribution is > 0.
- Everything on the audio thread: no allocation, lock, log, I/O or wait. Settings arrive from the UI thread the way the existing control commands do (`src/control/SpscQueue.h`, `ControlCommands.h`) or through atomics; read them once per callback. Provide a lock-free read-back for the UI: per slot the current output value (for an activity display) and per target the current summed offset (for showing the moving value), as relaxed atomics updated once per callback.
- **Wiring:** a `ControlSource` decorator (or equivalent) that wraps the existing source and fills the modulation fields per control block, and the hook in `AudioRender` (or the app's audio path) that gives it the callback's input buffer and applies the InputGain modulation before the voice runs. Callback sizes that are not multiples of 4 must work (the voice carries its block position across calls; tests use 64, 256 and 1000 frames): the result must not depend on how the stream is cut into callbacks. Add a test for that (same script rendered at 64, 256, 1000 and 1 frame per call gives identical output).
- For MOD-A the app needs no UI: add a temporary way to set slots for testing, **only** as command-line options on the app's self-test path and script commands in the render tool (below). Do not add a tab; MOD-B does that. Leave a small, documented API on the app/control side that MOD-B will call (`setModSlot(index, settings)` on the UI thread, and the read-back).

### 3. Render tool and reference renders

- `audrey_render` scripts (`src/tools/`, `scripts/*.txt`) get commands to set a slot's shape, target, rate and depth at a time. Existing scripts and their output must not change.
- Add modulation scripts under `scripts/` and renders checked like the others, but **kept separate from the existing 25** (their own list/hash file or a clearly separate section, whichever fits `make_reference.sh` with the least change; the count "25" and "17 of 25" for the old set must stay reportable). At least: slow sine on frequency (vibrato/drift); 200 Hz sine on the lowpass (audio-rate); square on the delay switch; sample-and-hold on reverb mix; triangle on feedback gain across the point where the loop starts to sing; envelope on the lowpass with an input burst file; all four slots on one target at full depth with the knob at each end (clamp); modulated volume to zero and back. Every render checked by `wavcheck` for non-finite samples and DC; click limits may need their own thresholds for the audio-rate cases (say what you chose and why).
- A stability sweep test: random slot settings (all shapes, targets, rates, depths, changed every few hundred milliseconds) for several minutes of audio at extreme knob settings: output always finite, bounded by the original's own limiter behaviour, no denormal slowdown (check the time taken).

### 4. Cost

Measure on the Mac the callback cost with 0, 1 and 4 active slots (audio-rate sine on the frequency, the lowpass, the echo time and the feedback) with the existing benchmark or a new one, and report the percentage increase. The Brick runs this engine at about a third of one core at its default CPU speed, so report anything above 10 % extra as a concern.

## Tests required

As listed in each part above, plus: all-slots-off is bit-identical to today for the full existing parity/reference checks; TSan for the settings hand-over and the read-back; ASan/UBSan; a counting `operator new` test over the audio path with slots active (follow `tests/record/record_render_test.cpp`).

## Rules and verification

No git state changes of any kind. Nothing under `third_party/` or `src/shim`. `src/core` may be changed for part 1 only. No `docs/STATUS.md` or `docs/PLATFORM_NOTES.md` edits. Never access the user's personal folders or media library (Music, Movies, Pictures, Documents, Desktop, Downloads) and never run filesystem-wide searches; stay in the repo, the session scratchpad and read-only `/opt/homebrew`. Test WAVs, if needed, are in `~/.cache/audreybrick-device/data/input` (read-only). No network. No adb, no device. Never open a real audio device on the Mac (`SDL_AUDIODRIVER=dummy` or no SDL). Run everything in the foreground; leave no background processes and no stray files.

Update `docs/SIGNAL_FLOW.md` (where modulation enters), `docs/PARAMETERS.md` (a short section: modulation offsets and which targets are slow because of the engine's own smoothers), `THIRD_PARTY.md` only if a dependency changes (none expected).

Run and report numbers for: macOS Release, ASan + UBSan, TSan (`-R "control|platform|ui|input|record|mod|core"`, adjusted to the test names you add), no-SDL (`-DAUDREY_WITH_SDL=OFF`), `scripts/verify_vendor.sh`, `scripts/make_reference.sh --check` (old set: all 25), the new modulation renders, and `scripts/build_linux_arm64.sh` (old set must still say 17 of 25). `scripts/make_port.sh` is run by the orchestrator.

Report back: files changed; the interface you chose between the control source, the modulators and the voice, and the API left for MOD-B; how each curve is inverted; every choice the brief left to you; test counts per build; the cost measurement; anything you could not verify; and what only the device can confirm.

---

# MOD-B: the MOD tab

Written 2026-11-20 after MOD-A. The layout below was shown to the human on 2026-11-20 and not objected to; treat the gestures as decided and the visual details as yours.

Read first: everything listed for MOD-A, plus `src/mod/ModControlSource.h` (the "API for MOD-B" comment) and `src/mod/ModTypes.h`, `docs/CONTROL_SCHEME_PROPOSAL.md` sections 00 and 0, `src/app/Tabs.h`, `ControlModel.h`, `InputRouter.h`, `UiSnapshot.h`, `App.cpp` (how the INPUT tab's panel is built, ticked, drawn and scripted), `src/input/InputController.{h,cpp}` (the model for a tab that binds no knob and owns its own state, including the hold-X pop-up), `src/ui/Layout.{h,cpp}` and `DrawList.h`, `tests/ui/`, `src/app/ButtonScript.h`.

## What the human gets

A seventh tab, **MOD**, after INPUT (L1/R1 wrap through it). It shows all four slots at once, one highlighted as current, each with its target, shape, rate, depth and a live activity indicator. Settings last until the app quits (presets come later and will store them).

### Gestures on the MOD tab (decided)

| Gesture | Does |
|---|---|
| D-pad up / down | Move the cursor between the rows **SLOT, SHAPE, RATE, DEPTH** (no wrap) |
| D-pad left / right | Change the row's value: SLOT 1 to 4 (which slot the other rows edit), SHAPE (sine, triangle, square, sample-and-hold, envelope), RATE, DEPTH. Right increases |
| Hold left / right on RATE or DEPTH | Ramp after the usual hold delay, as knobs do |
| Select held | Fine steps for RATE and DEPTH (taps and ramps) |
| X + left / right | Step the current slot's **target** backwards / forwards through the list |
| X + up / down | Open the **target pop-up** (a list of all targets, current one highlighted); up/down move while X stays held; **releasing X chooses**. Follow the INPUT tab's file pop-up for look, scrolling and how to leave without choosing |
| Y + left / right | Reset the current row to its default: SHAPE sine, RATE 1 Hz, DEPTH 0; on the SLOT row, the whole slot (target None too) |
| Y + X + left / right | Target back to None |

Unchanged everywhere, including on MOD: L2/R2 feedback, A feedback maximum, B and Select + B the delay switch, L1/R1 tabs, Start + Y record, Menu + Start quit. The knob d-pad bindings do nothing on MOD (it binds no knob, like INPUT). Changing tab, or pressing/releasing X, makes held d-pad directions inert as on the other tabs, so nothing ramps by accident. The cursor row and the current slot are remembered when leaving and returning.

### Values

- **Target order** in the list and pop-up: None, then the knobs in tab order as they appear on screen (LOOP's, FILTER's, REVERB's, DELAY's, OUT's, with the feedback gain where LOOP shows it), then INPUT GAIN, then DELAY SWITCH. Use the on-screen knob names, not the ParamTable ids. Stepping does not wrap. A newly chosen target takes effect at once (the engine cross-fades it).
- **RATE:** tap = 1/100 of the control's travel (about 13 % in frequency), fine tap = 1/1000, ramps at the same speeds the knob ramps use in normalised travel (`src/control/ControlTuning.h`); clamped 0..1. Shown in Hz with three significant digits (`0.010 HZ`, `1.00 HZ`, `440 HZ`, `2.00 KHZ`). For the Envelope shape the same row is labelled **RELEASE** and shown as time (`5.00 S` ... `5 MS`). The default is the control position for 1 Hz (`modRateNormFromHz(1.0)`).
- **DEPTH:** -100 % to +100 % of the target's travel, tap 2 %, fine tap 0.2 %, shown signed (`+34.0%`). Steps are on a grid so exactly 0 is reachable from either side; a ramp passing through 0 does not stop there.
- **Activity indicator** per slot: a small horizontal bar centred on zero showing `slotOutput(i) * depth` (so it is still when depth is 0 or the target is None; unipolar for the envelope), read once per UI frame. A fast LFO will simply flicker; that is fine.
- A slot whose target is None, or whose depth is 0, is drawn dimmed.

### On the other tabs

Wherever a knob is drawn, if at least one slot with non-zero depth targets it: show a small tag with the slot numbers (e.g. `M1`, `M13`) and a second, thinner marker on the value bar at `clamp(knob + targetOffset(target), 0, 1)` (the moving value), read once per UI frame. The printed value stays the knob's own setting. The INPUT tab's meter reads the input before the InputGain modulation: when a slot targets the gain, show the effective level (meter value times `inputGainRatio()`) so the howl safeguard is not misleading. The same marker for the INPUT tab's gain (from `inputGainRatio()` or the offset, whichever is honest and simple) and the delay-switch indicator (lit differently when only the modulation holds it on: `switchRequested()`). With no slot active every existing screen must be pixel-identical to today (the existing screenshot tests must pass unchanged).

## Structure

- A UI-thread class owning the four `ModSlotSettings`, the cursor and the pop-up state (suggested `src/app/ModPanel.{h,cpp}` or beside the mod library if it needs no app types; no SDL in its logic so it is testable). Time is passed in, as in `ControlModel`. It calls `ModControlSource::setModSlot()` when a slot changes; if that returns false it sends the latest settings again on a later tick (never lose the final value of a ramp). It exposes `slot(i)` / `setSlot(i, settings)` for the coming presets task and for `--mod`.
- `Tabs`: add `Tab::Mod` after `Input`, no knob bindings. `InputRouter::deliver()` passes events to the panel as it does for the input panel.
- Drawing through `UiSnapshot` / `Layout` / `DrawList` like every other tab; no allocation per frame (`tests/ui/ui_render_alloc_test.cpp` pattern). 1024x768, the existing font and sizes; the four slots must be readable at a glance.
- The self-test's `--mod` option stays and now goes through the panel so the screen shows it. Button scripts (`ButtonScript`) must be able to drive the tab.
- Log on the UI thread, not per step: one line per slot when its settings have been stable for 0.5 s after a change (`mod: slot 2 triangle -> FeedbackLPFCutoff 3.20 Hz depth +34.0%`), so a device log shows what was played.
- Nothing in `src/core`, `src/mod`'s audio-thread code, `third_party/` or `src/shim` should need to change. If you find you need an engine-side change, stop and report instead.

## Tests required

Panel logic (every gesture in the table, clamps, defaults, resets, inert rules on tab change and X press/release, pop-up open/move/choose/leave, fine and ramp step sizes, resend after a full queue, cursor memory); formatting of rate, release and depth; layout and screenshot tests for the MOD tab (empty, one slot active, four active, pop-up open) and for a modulated knob's tag and marker on another tab; existing screenshots unchanged; an app self-test that sets slots by scripted button presses and checks the read-back (`targetOffset` non-zero) and that the audio stays finite; allocation test for drawing; TSan over the panel-to-audio hand-over.

## Documents

`docs/CONTROL_SCHEME_PROPOSAL.md` (replace the "MOD reserved" row and note with what is built), `packaging/README.txt` (a short "Modulation" section for players, including: fast deep modulation of the two filters gets shallower as the rate rises; body and echo time respond slowly by the instrument's nature; modulation cannot open an input whose gain is at -inf), and a new `docs/MOD_RUN_BRICK.md`: a numbered checklist for the human in the style of `docs/RECORDING_RUN_BRICK.md` (save logs, copy the package, then steps that try: each shape on a slow and an audio-rate target, the pop-up, resets, four slots at once at audio rate while watching for crackle, envelope from the mic/USB/file input, modulated delay switch, modulated volume and feedback, recording while modulating; what to send back; opinions asked for: step sizes, readability, whether the envelope should follow the input before the gain).

## Rules and verification

As MOD-A's rules section, word for word (no git state changes; nothing under `third_party/` or `src/shim`; no `docs/STATUS.md` or `docs/PLATFORM_NOTES.md` edits; no personal folders, no filesystem-wide searches; no network, adb or device; `SDL_AUDIODRIVER=dummy`; foreground only; no stray files).

Run and report numbers for: macOS Release, ASan + UBSan, TSan (`-R "control|platform|ui|input|record|mod|core|app"` adjusted to your test names), no-SDL, `scripts/verify_vendor.sh`, `scripts/make_reference.sh --check` (25) and `--set mod --check`, `scripts/build_linux_arm64.sh`. Attach nothing; say where the MOD tab screenshots from the tests are written so the orchestrator can look at them.

Report back: files changed; every choice left to you; anything in the gesture table that turned out awkward or ambiguous in code; test counts per build; what only the device can confirm.

---

# MOD-C: changes after the human's first device run (2026-11-21)

The human ran the MOD build on the Brick: it works; the tab's UX needs more thought (to come later, after more testing; switching slots felt unintuitive). Three changes are wanted now, before the commit.

## 1. The envelope follows the input before the gain (decided)

The Envelope shape follows the selected input source **before the INPUT tab's gain** (and before the InputGain modulation), so a player can turn the input down, even to -inf dB, and still modulate targets with the incoming audio without hearing it.

- The signal followed is what the source delivers after the stage's source fades and crossfades (so switching source does not make the envelope jump) but without the gain: for FILE the file's samples, for LIVE the captured samples (as the PRE meter shows them). Source OFF: silence, envelope 0.
- It must work at gain exactly 0 and on a capture device's first open (which starts at -inf dB): find out what `InputStage::pull()` does today when the gain is 0 or nothing is audible (returns nullptr? skips reading the source?) and make the pre-gain signal available whenever a source is selected and delivering, without changing the stage's audible output or its timing (file position, live ring reads) in any case. If a file does not advance while inaudible today, keep that and say so.
- The howl safeguards stay: a modulator still cannot open an input whose gain is 0 (the InputGain rule is unchanged); only the envelope's *source* moves.
- Audio-thread rules, chunk-size independence and the idle pass-through (no slot active = exact copies, voice reads the stage's buffer) hold as before. With no Envelope slot active nothing extra should be computed beyond a negligible cost.

## 2. Four more shapes (decided)

Ramp up, ramp down, exponential ramp up, exponential ramp down. Bipolar -1..+1 like the other LFO shapes, same rate range, naive (not band-limited), free-running phase.

- Ramp up: -1 rising linearly to +1 over the cycle, then jumping back. Ramp down: the mirror.
- Exponential ramp down: starts at +1, falls quickly then ever more slowly to -1 by the end of the cycle (a decay curve: normalised `exp(-k*u)`, u = phase 0..1, k about 5; choose k, state it, make the end points exact). Exponential ramp up: its mirror in time (slow start, fast finish, ends at +1).
- Shape order on screen and in the enum: SINE, TRIANGLE, SQUARE, RAMP UP, RAMP DOWN, EXP UP, EXP DOWN, S&H, ENVELOPE. Nothing persists shapes yet, so the enum may be reordered; scripts use names (add `rampup`, `rampdown`, `expup`, `expdown`).

## 3. Polarity per slot (the human's wish; the orchestrator's design, to be judged on the device)

Each slot gets a polarity: **BIPOLAR** (default, as now: the value swings both ways round the knob's setting) or **UNIPOLAR** (the LFO value is mapped to 0..+1, `(v + 1) / 2`, so with a positive depth the target only moves above the knob's setting and with a negative depth only below).

- `ModSlotSettings` gains the field; default bipolar; the Envelope shape is always unipolar whatever the setting (the row shows `UNIPOLAR` dimmed and left/right do nothing there).
- On the MOD tab: a fifth row **POLARITY** under DEPTH (up/down reach it; left/right toggle, no wrap needed for two values; Y + left/right resets to BIPOLAR). Fit the five rows above the slot cards without shrinking the text; each slot card shows the polarity compactly (e.g. `+/-` or `+` beside the depth) only if it fits cleanly.
- A polarity change must not click more than a target change does: switch through the existing smoothing (say how).
- The activity bar and `slotOutput()` show the value after the polarity mapping.
- Script syntax: `mod<n> polarity bipolar|unipolar`; the self-test `--mod` option accepts it optionally (your choice of form; existing uses must keep working).

## Tests, documents, rules

Tests for each part (the envelope responding at gain 0 and at first open with the input silent in the output; pre-gain independence from the gain knob; the four shapes' values, end points, continuity of phase on rate changes; polarity mapping, smoothing on change, envelope ignoring it; the panel's new row and formatting; layout/screenshots incl. the new shapes and row; scripts). The 25 old reference hashes must not change. The mod set may be regenerated where a hash changes for an explained reason (say which and why); add a render or extend one to cover a ramp shape and a unipolar slot. `mod_stability_test` must cover the new shapes and polarity.

Update `docs/PARAMETERS.md` section 6, `docs/SIGNAL_FLOW.md` section 6, `docs/CONTROL_SCHEME_PROPOSAL.md`, `packaging/README.txt`, and `docs/MOD_RUN_BRICK.md` (new shapes, the POLARITY row, step 9 rewritten for the pre-gain envelope including "gain at -inf, the filter still follows the mic", and the opinions list: drop the envelope-source question).

Rules and the verification list as MOD-B. `src/core` should not need to change; `src/mod` and `src/input/InputStage` may. `docker/knulli_rootfs_tests.sh` may be touched only if a button script in it needs adapting.

# MOD-D: the tab's gestures reworked (the human's design after the second device run, 2026-11-21)

Read MOD-B and MOD-C above first; everything there holds except what this section changes. The engine (`src/mod`, `src/core`, `src/input`, `src/platform`) does not change: this is the tab only (`src/app/ModPanel`, `ModFormat`, `ModView`, `InputRouter`, `App`, `src/ui/Layout`), its tests, and the documents. The 25 reference hashes and the 12 mod-set hashes must not change.

## What the human decided (do not change)

The human found switching slots unintuitive: the slot was a row among the slot's own properties. The new model: **the rows are one slot's properties, the cards are the four slots, and holding X turns the d-pad to the cards.**

1. **Rows:** TARGET, SHAPE, RATE (RELEASE), DEPTH, POLARITY. The SLOT row is gone; TARGET takes its place at the top. Left/right on TARGET step through the targets (NONE, the eleven knobs, INPUT GAIN, DELAY SWITCH), **wrapping at both ends**, and repeating while held at a rate that makes 14 entries comfortable (state the rate).
2. **Cards:** normally dimmed as a group, like the dimmed rows on the DELAY tab (look at how that tab dims, and reuse its colours). **While X is held** the cards are shown at full brightness and the five rows are dimmed; left/right select slot 1 to 4, **wrapping** (4 -> 1, 1 -> 4), one step per press (no auto-repeat, or a slow one: your choice, say which). Up/down do nothing while X is held.
3. **The selected card stays clearly marked when the cards are dimmed** (a bright frame and a bright slot number, or what reads best: it is now the only thing that says which slot the rows edit). The activity bars keep moving and stay readable in both states. A slot that is active (target set, depth not 0) must still be distinguishable from an idle one while the group is dimmed.
4. **The cursor row is kept when the slot changes** (on DEPTH, X + right lands on the next slot's DEPTH).
5. **The target list pop-up is removed** (X + up/down no longer opens anything). Remove its code, its draw commands, its log lines and its tests rather than leaving it unreachable.
6. **Resets:** Y + left/right on a row resets that row as before; on TARGET that means target NONE. **Y + X + left/right resets the whole selected slot** (all five properties to their defaults). The old Y + X + left/right (target NONE) and the old Y + left/right on the SLOT row are replaced by these.

## Details left to you (say what you chose)

- Everything about held directions across modifier changes stays as careful as now: pressing or releasing X or Y makes held directions inert until released; a value ramp must not continue into slot selection or the reverse; DEPTH's snap at ramp end stays.
- SELECT (fine) has no meaning on TARGET or with X held.
- The hint line(s) on the tab say what X does (`X: SLOT` or similar) and change while X is held if that helps.
- The half-second `mod: slot ...` log line stays; log slot selection only if cheap and not noisy.
- X on other tabs is untouched (INPUT's hold-X browser in particular). L1/R1 while X is held on MOD: change tab as normal and drop the X state cleanly.
- POLARITY under ENVELOPE stays as in MOD-C, including the Y no-op. Y + X whole-slot reset does reset the stored polarity.

## Tests, documents, rules

Rewrite the panel, router and layout tests for the new gestures (every item above, including wrap both ways for targets and slots, cursor row kept, both resets, inert directions across X and Y changes, tab change with X held, the pop-up's absence). Update the UI snapshots / screenshots (normal state, X held, a dimmed-but-active card, the selected card's mark) and `tests/mod/mod_tab.txt`; adapt any button script that used X + left/right for targets, the SLOT row or the pop-up (including `docker/knulli_rootfs_tests.sh` only if one of its scripts needs it; note that the number of rows is unchanged, so up/down counts in existing scripts shift only where they started from SLOT).

Update `docs/CONTROL_SCHEME_PROPOSAL.md`, `packaging/README.txt` (Modulation section) and `docs/MOD_RUN_BRICK.md` (the buttons table and every step that names a gesture; say at the top that it is updated for MOD-D and not yet run; in the opinions list replace the pop-up related points with: is holding X for slots comfortable, is the selected card obvious when dimmed, are 14 targets on left/right enough without a list). `docs/PARAMETERS.md` / `SIGNAL_FLOW.md` only if they mention the gestures.

Rules as MOD-B: no git state changes; vendored code untouched; no real audio device on the Mac (`SDL_AUDIODRIVER=dummy`); stay inside the repo, the session scratchpad and read-only `/opt/homebrew`, never personal folders or filesystem-wide searches; no adb or device access. Build and run the full host test suite (Release, and ASan+UBSan at least for the ui / mod / platform tests) in build directories of your own under `build/` and delete them when done. Report: what you changed per file, the choices you made, test counts, and anything you are unsure of.
