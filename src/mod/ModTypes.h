// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A (docs/briefs/MOD.md): the settings of a modulation slot, the names used
// by scripts and the command line, and the rate mapping. Header only, no SDL,
// no allocation. Ours (not derived from Audrey II).
//
// A slot has a shape, a target, a rate, a depth and a polarity:
//   - shape: Sine, Triangle, Square, RampUp, RampDown, ExpUp, ExpDown (MOD-C),
//     SampleHold (random steps) or Envelope (a follower of the absolute input
//     level, before the input gain: MOD-C).
//   - target: None, one of the eleven knobs, InputGain or DelaySwitch.
//   - rateNorm 0..1: the LFO rate, exponential from kModMinRateHz (0.01 Hz, a
//     100 s cycle) at 0 to kModMaxRateHz (2000 Hz) at 1. For Envelope the same
//     control sets the follower's release time, exponential from 5 s at 0 to
//     5 ms at 1 (attack fixed at 2 ms).
//   - depth -1..+1: the slot's contribution is depth * value, value -1..+1 for
//     the LFO shapes and 0..1 for Envelope, in normalised knob travel.
//   - polarity (MOD-C): Bipolar (the LFO value as it is, -1..+1) or Unipolar
//     (the LFO value mapped to 0..1, (v + 1) / 2, so a positive depth only
//     moves the target above the knob and a negative one only below). The
//     Envelope is always unipolar, whatever the setting (which is kept, so a
//     change back to an LFO shape finds it again).
//
// The LFO shapes at phase u (0..1, one cycle), all -1..+1, naive (not
// band-limited):
//   Sine sin(2 pi u); Triangle 0 at u = 0 rising to +1 at 0.25, -1 at 0.75;
//   Square +1 for u < 0.5, else -1; RampUp 2u - 1; RampDown 1 - 2u;
//   ExpDown 2 (exp(-k u) - exp(-k)) / (1 - exp(-k)) - 1 with k = kModExpK (5):
//   +1 at u = 0, falling fast and then ever more slowly to -1 at u = 1;
//   ExpUp(u) = ExpDown(1 - u): -1 at u = 0, slow start, fast finish, +1 at 1.
//
// DelaySwitch: the request is on while the target's summed offset is above 0.
// An Envelope slot with positive depth on DelaySwitch is therefore on whenever
// the follower is above its floor (1e-12): a gate with no threshold. The
// INPUT tab's input meter reads before the InputGain modulation.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/ControlSource.h"
#include "core/ParamTable.h"

namespace audreybrick
{

constexpr std::size_t kModSlots = 4;

// The order on screen (MOD-C): nothing stores shapes by number (scripts and the
// command line use names), so the enum follows the screen.
enum class ModShape : std::uint8_t
{
    Sine,
    Triangle,
    Square,
    RampUp,
    RampDown,
    ExpUp,
    ExpDown,
    SampleHold,
    Envelope,
    Count
};
constexpr std::size_t kModShapeCount = static_cast<std::size_t>(ModShape::Count);

// MOD-C: how an LFO value is applied (see the top of this file).
enum class ModPolarity : std::uint8_t
{
    Bipolar,
    Unipolar,
    Count
};
constexpr std::size_t kModPolarityCount = static_cast<std::size_t>(ModPolarity::Count);

// None, then the eleven knobs in Knob order (value = Knob index + 1), then
// InputGain and DelaySwitch.
enum class ModTarget : std::uint8_t
{
    None = 0,
    Frequency,
    FeedbackGain,
    FeedbackBody,
    FeedbackLPFCutoff,
    FeedbackHPFCutoff,
    ReverbMix,
    ReverbDecay,
    EchoDelaySend,
    EchoDelayTime,
    EchoDelayFeedback,
    OutputVolume,
    InputGain,
    DelaySwitch,
    Count
};
constexpr std::size_t kModTargetCount = static_cast<std::size_t>(ModTarget::Count);
static_assert(static_cast<std::size_t>(ModTarget::OutputVolume) == kKnobCount, "knob targets follow the Knob enum");

constexpr ModTarget modTargetFromKnob(Knob k) noexcept
{
    return static_cast<ModTarget>(static_cast<std::uint8_t>(k) + 1u);
}
constexpr bool modTargetIsKnob(ModTarget t) noexcept
{
    return t != ModTarget::None && static_cast<std::size_t>(t) <= kKnobCount;
}
// Only for modTargetIsKnob(t).
constexpr Knob modTargetKnob(ModTarget t) noexcept
{
    return static_cast<Knob>(static_cast<std::uint8_t>(t) - 1u);
}

struct ModSlotSettings
{
    ModShape    shape    = ModShape::Sine;
    ModTarget   target   = ModTarget::None;
    float       rateNorm = 0.5f; // 0..1
    float       depth    = 0.0f; // -1..+1
    ModPolarity polarity = ModPolarity::Bipolar; // MOD-C; ignored by Envelope
};

// MOD-C: the polarity in force: Unipolar for Envelope whatever the setting.
constexpr ModPolarity modEffectivePolarity(const ModSlotSettings& s) noexcept
{
    return s.shape == ModShape::Envelope ? ModPolarity::Unipolar : s.polarity;
}

// --- The exponential ramps (MOD-C) -----------------------------------------------------
// k of exp(-k u): 5 makes the decay fall to 1/e of its travel in a fifth of the
// cycle (99.3 % of it by the end, before the normalisation that makes the end
// points exact).
constexpr double kModExpK = 5.0;
// ExpDown at phase u (0..1): exactly +1 at u = 0 and exactly -1 at u = 1 (the
// end points are set, not left to the rounding of exp), clamped to -1..+1,
// falling monotonically in between. No static state (audio thread).
inline double modExpDown(double u) noexcept
{
    if(!(u > 0.0))
        return 1.0; // also NaN
    if(u >= 1.0)
        return -1.0;
    const double e = std::exp(-kModExpK);
    const double v = 2.0 * (std::exp(-kModExpK * u) - e) / (1.0 - e) - 1.0;
    return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
}
// ExpUp at phase u: ExpDown mirrored in time (-1 at u = 0, +1 at u = 1).
inline double modExpUp(double u) noexcept
{
    return modExpDown(1.0 - u);
}

// --- Rate ---------------------------------------------------------------------------
constexpr double kModMinRateHz         = 0.01;
constexpr double kModMaxRateHz         = 2000.0;
constexpr double kModMaxReleaseSeconds = 5.0;   // Envelope release at rateNorm 0
constexpr double kModMinReleaseSeconds = 0.005; // Envelope release at rateNorm 1
constexpr double kModAttackSeconds     = 0.002; // Envelope attack, fixed
// Depth and target changes: a one-pole of this time constant per control block.
constexpr double kModDepthSmoothingSeconds = 0.010;
// A smoothed contribution whose goal is 0 snaps to exactly 0 below this.
constexpr float kModSnapThreshold = 1.0e-6f;

constexpr float clampModRate(float r) noexcept
{
    return r > 0.0f ? (r < 1.0f ? r : 1.0f) : 0.0f; // NaN -> 0
}
constexpr float clampModDepth(float d) noexcept
{
    return d > -1.0f ? (d < 1.0f ? d : 1.0f) : (d <= -1.0f ? -1.0f : 0.0f); // NaN -> 0
}

// LFO rate in Hz: kModMinRateHz * (kModMaxRateHz / kModMinRateHz)^rateNorm.
inline double modRateHz(float rateNorm) noexcept
{
    return kModMinRateHz * std::pow(kModMaxRateHz / kModMinRateHz, static_cast<double>(clampModRate(rateNorm)));
}
// The inverse (clamped to 0..1): the rateNorm whose rate is `hz`.
inline float modRateNormFromHz(double hz) noexcept
{
    if(!(hz > kModMinRateHz))
        return 0.0f;
    if(hz >= kModMaxRateHz)
        return 1.0f;
    return static_cast<float>(std::log(hz / kModMinRateHz) / std::log(kModMaxRateHz / kModMinRateHz));
}
// Envelope release in seconds: 5 s * (0.005 / 5)^rateNorm.
inline double modReleaseSeconds(float rateNorm) noexcept
{
    return kModMaxReleaseSeconds *
           std::pow(kModMinReleaseSeconds / kModMaxReleaseSeconds, static_cast<double>(clampModRate(rateNorm)));
}

// Any settings made valid: an unknown shape becomes Sine, an unknown target
// None, an unknown polarity Bipolar, rate and depth are clamped (NaN -> 0), and
// a depth below kModSnapThreshold in size is 0 (no denormal weights).
constexpr ModSlotSettings sanitizeModSlot(const ModSlotSettings& s) noexcept
{
    ModSlotSettings o;
    o.shape    = static_cast<std::size_t>(s.shape) < kModShapeCount ? s.shape : ModShape::Sine;
    o.target   = static_cast<std::size_t>(s.target) < kModTargetCount ? s.target : ModTarget::None;
    o.rateNorm = clampModRate(s.rateNorm);
    o.depth    = clampModDepth(s.depth);
    if(o.depth > -kModSnapThreshold && o.depth < kModSnapThreshold)
        o.depth = 0.0f;
    o.polarity = static_cast<std::size_t>(s.polarity) < kModPolarityCount ? s.polarity : ModPolarity::Bipolar;
    return o;
}

// --- Names (scripts, the command line, the log) --------------------------------------
constexpr const char* modShapeName(ModShape s) noexcept
{
    switch(s)
    {
        case ModShape::Sine: return "sine";
        case ModShape::Triangle: return "triangle";
        case ModShape::Square: return "square";
        case ModShape::RampUp: return "rampup";
        case ModShape::RampDown: return "rampdown";
        case ModShape::ExpUp: return "expup";
        case ModShape::ExpDown: return "expdown";
        case ModShape::SampleHold: return "samplehold";
        case ModShape::Envelope: return "envelope";
        case ModShape::Count: break;
    }
    return "?";
}

constexpr const char* modPolarityName(ModPolarity p) noexcept
{
    switch(p)
    {
        case ModPolarity::Bipolar: return "bipolar";
        case ModPolarity::Unipolar: return "unipolar";
        case ModPolarity::Count: break;
    }
    return "?";
}

// "None", a knob's ParamTable id (e.g. "FeedbackLPFCutoff"), "InputGain" or "DelaySwitch".
inline const char* modTargetName(ModTarget t) noexcept
{
    if(t == ModTarget::None)
        return "None";
    if(modTargetIsKnob(t))
        return paramSpec(modTargetKnob(t)).id;
    if(t == ModTarget::InputGain)
        return "InputGain";
    if(t == ModTarget::DelaySwitch)
        return "DelaySwitch";
    return "?";
}

namespace moddetail
{
constexpr char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
constexpr bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if(a.size() != b.size())
        return false;
    for(std::size_t i = 0; i < a.size(); ++i)
        if(lower(a[i]) != lower(b[i]))
            return false;
    return true;
}
} // namespace moddetail

// Case-insensitive. "sh" and "s&h" are accepted for samplehold, "env" for envelope.
inline bool modShapeFromName(std::string_view name, ModShape& out) noexcept
{
    using moddetail::equalsIgnoreCase;
    for(std::size_t i = 0; i < kModShapeCount; ++i)
    {
        if(equalsIgnoreCase(name, modShapeName(static_cast<ModShape>(i))))
        {
            out = static_cast<ModShape>(i);
            return true;
        }
    }
    if(equalsIgnoreCase(name, "sh") || equalsIgnoreCase(name, "s&h"))
    {
        out = ModShape::SampleHold;
        return true;
    }
    if(equalsIgnoreCase(name, "env"))
    {
        out = ModShape::Envelope;
        return true;
    }
    return false;
}

// Case-insensitive: "bipolar" (or "bi"), "unipolar" (or "uni").
inline bool modPolarityFromName(std::string_view name, ModPolarity& out) noexcept
{
    using moddetail::equalsIgnoreCase;
    if(equalsIgnoreCase(name, "bipolar") || equalsIgnoreCase(name, "bi"))
        out = ModPolarity::Bipolar;
    else if(equalsIgnoreCase(name, "unipolar") || equalsIgnoreCase(name, "uni"))
        out = ModPolarity::Unipolar;
    else
        return false;
    return true;
}

// Case-insensitive: "none", a knob id, "inputgain", "delayswitch" (or "switch").
inline bool modTargetFromName(std::string_view name, ModTarget& out) noexcept
{
    using moddetail::equalsIgnoreCase;
    Knob k;
    if(knobFromId(name, k))
    {
        out = modTargetFromKnob(k);
        return true;
    }
    if(equalsIgnoreCase(name, "none"))
        out = ModTarget::None;
    else if(equalsIgnoreCase(name, "inputgain"))
        out = ModTarget::InputGain;
    else if(equalsIgnoreCase(name, "delayswitch") || equalsIgnoreCase(name, "switch"))
        out = ModTarget::DelaySwitch;
    else
        return false;
    return true;
}

} // namespace audreybrick
