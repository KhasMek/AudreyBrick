// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The Audrey II parameter table: the single place in our code where the knob
// ranges, curves, smoothing times, registry initial values and default knob
// positions appear. Values transcribed from Controls::registerParams and
// Controls::Update in upstream FeedbackSynthControls.cpp at commit e5ab799
// (see docs/PARAMETERS.md sections 2 and 4).

#pragma once

#include <daisysp.h>

#include <string_view>

#include "core/ControlSource.h"

namespace audreybrick
{

struct ParamSpec
{
    const char*      id;           // script and preset identifier (the firmware enum name)
    const char*      panel;        // faceplate label
    const char*      unit;         // unit of the engine value
    float            min;          // engine value at n = 0
    float            max;          // engine value at n = 1
    daisysp::Mapping curve;        // ParameterRegistry / daisysp::fmap curve
    float            smoothTime;   // ParameterRegistry smooth_time, seconds (t60)
    float            initial;      // ParameterRegistry initial value (engine units)
    float            defaultNorm;  // normalised knob position for reset-to-default
};

// Pre-shaping applied in Controls::Update before UpdateNormalized.
constexpr float kReverbDecayTension = -3.0f; // ftension(n, -3) on ReverbDecay
constexpr float kDelaySwitchScale   = 0.5f;  // EchoDelayTime knob value scale when the switch is on

const ParamSpec& paramSpec(Knob knob) noexcept;

// Looks a knob up by its id, case-insensitively. Returns false if there is none.
bool knobFromId(std::string_view id, Knob& out) noexcept;

// The value handed to ParameterRegistry::UpdateNormalized for knob position n:
// ftension for ReverbDecay, the switch scale for EchoDelayTime, n otherwise.
float preShape(Knob knob, float n, bool delaySwitch) noexcept;

// The engine value that knob position n maps to (the smoothing target), for
// display: preShape, then the registry's clamp and daisysp::fmap.
float knobToEngine(Knob knob, float n, bool delaySwitch) noexcept;

} // namespace audreybrick
