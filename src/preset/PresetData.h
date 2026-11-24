// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A (docs/briefs/PRESET.md, docs/PRESET_FORMAT.md): what a preset holds,
// as plain data. Independent of the app's classes (no ControlModel, no
// ButtonControlSource, no ModPanel); capture and apply live in
// preset/PresetState.h. No SDL, no allocation.
//
// A preset is the sound: the eleven knobs' stored positions (the stored
// feedback, not the momentary MAX value), the delay switch's latch, and the
// four modulation slots. Nothing about the audio input, nothing momentary.

#pragma once

#include <cstddef>

#include "core/ControlSource.h"
#include "mod/ModTypes.h"

namespace audreybrick
{

struct PresetData
{
    float           knobs[kKnobCount] = {}; // normalised knob positions 0..1, Knob order
    bool            delaySwitch       = false; // the latch (B's momentary hold is not part of it)
    ModSlotSettings mod[kModSlots];          // sanitised (sanitizeModSlot)
};

// The start-up state (what a missing field reads as): every knob at its
// ParamTable default position, the latch off, every slot ModPanel::defaultSlot()
// (sine, target None, the 1 Hz rate, depth 0, bipolar). Equal to what the app
// starts with when no --mod option is given.
PresetData presetDefaults() noexcept;

// The preset's name for knob k: the ParamTable id ("Frequency", "FeedbackGain",
// ..., "OutputVolume"), the same names scripts use.
const char* presetKnobName(Knob k) noexcept;

// Every value made valid, as reading and applying do: knobs clamped to 0..1
// (NaN -> 0), each slot through sanitizeModSlot.
PresetData sanitizePreset(const PresetData& p) noexcept;

// Field by field, floats compared bit for bit (so -0 != +0 and NaN == NaN of
// the same bits). For the tests and the round-trip checks.
bool presetBitsEqual(const PresetData& a, const PresetData& b) noexcept;

} // namespace audreybrick
