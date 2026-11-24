// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A (docs/briefs/PRESET.md): capture a preset from the live state and
// apply one to it. The seam PRESET-B calls; no SDL, no I/O. UI thread only
// (where ControlModel and ModPanel live). Nothing here runs on the audio thread
// or adds anything to it: applying sends the same commands a user's change
// sends (ControlModel's command queue and held word, ModPanel's settings queue),
// which the audio thread already drains.
//
// Where the state lives:
//   - The eleven stored knob values and the switch latch: on the audio side,
//     in ButtonControlSource, which publishes them (publishedKnob(), and
//     publishedStoredFeedback() for the feedback, which is the stored value even
//     while A holds MAX; publishedSwitchLatch(), the latch without B's
//     momentary hold). They change only through ControlModel's commands and
//     held word.
//   - The four modulation slots: ModPanel (UI side) owns their settings and
//     sends them to the ModControlSource.
//
// capturePreset(): reads the above. Returns Pending, with `out` still filled
//   from what is published, while a command the model sent has not been
//   applied by the audio side yet (ControlModel::commandsDelivered() vs
//   ButtonControlSource::commandsProcessed()): for example a preset loaded in
//   the same frame, or audio not running. A caller that saves should wait for
//   Ok (a frame or two while audio runs). With a ramp held, the values are a
//   snapshot of a moving state; the eleven values may come from two adjacent
//   control blocks (see ButtonControlSource.h), which is harmless.
//
// applyPreset(): sanitises the preset (sanitizePreset), then:
//   1. ControlModel::setAll(): stops every d-pad and L2 / R2 ramp, then sends
//      one SetAll command with the eleven values and the latch. A and B held
//      stay held (momentary: not stored, not changed): with A held the feedback
//      stays at MAX until A is released, then it is the loaded value.
//   2. Only if the command was accepted: ModPanel::cancelRamps(), then
//      ModPanel::setSlot() for each of the four slots (a refused send is retried
//      by ModPanel::tick(), so the slots always arrive; the panel logs each
//      changed slot once it has been stable for 0.5 s, as for a user's change).
//   Returns false, and changes nothing but stopping the ramps, if the command
//   queue was full. Every setting jumps to its new value; the knobs' smoothing
//   and the modulation's smoothing do the rest (no extra fade). The audio input
//   is not touched.

#pragma once

#include "app/ControlModel.h"
#include "app/ModPanel.h"
#include "control/ButtonControlSource.h"
#include "preset/PresetData.h"

namespace audreybrick
{

enum class PresetCapture
{
    Ok,
    Pending, // a sent command is not applied yet: capture again a little later
};

PresetCapture capturePreset(const ControlModel& model, const ButtonControlSource& source, const ModPanel& mod,
                            PresetData& out) noexcept;

bool applyPreset(const PresetData& preset, ControlModel& model, ModPanel& mod, double timeSeconds) noexcept;

} // namespace audreybrick
