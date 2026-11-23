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

// MOD-A: the inverse of knobToEngine. The knob position whose mapping is the
// engine value v (computed in double): v is clamped to the parameter's range,
// the curve is inverted (linear; square root for EXP; log ratio for LOG), then
// the pre-shaping (ReverbDecay: the inverse of ftension(n, -3), i.e.
// log1p(t * expm1(-3)) / -3; EchoDelayTime: divided by the switch scale when
// the switch is on). For EchoDelayTime with the switch on the result can lie
// above 1 (up to 1 / kDelaySwitchScale = 2): that is a value the smoother is
// still carrying from before the switch turned on. Never NaN for a finite v.
double engineToKnob(Knob knob, float v, bool delaySwitch) noexcept;

// MOD-A: the engine value for the smoothed engine value `smoothed` moved by
// `offset` in normalised knob travel. With offset 0 (or NaN) returns `smoothed`
// unchanged. Otherwise n_s = engineToKnob(smoothed), n = clamp(n_s + offset,
// min(0, n_s), max(1, n_s)), and the result is knobToEngine(n): the original
// mapping of the moved knob position. The clamp range is [0, 1] whenever n_s
// is (always, except EchoDelayTime while the smoother still carries a value
// from before the switch turned on, where it widens to include n_s so that a
// small offset gives a small change rather than a jump).
float modulateEngineValue(Knob knob, float smoothed, float offset, bool delaySwitch) noexcept;

} // namespace audreybrick
