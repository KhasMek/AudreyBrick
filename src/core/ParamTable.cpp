// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Parameter values transcribed from upstream Audrey II FeedbackSynthControls.cpp
// (commit e5ab799), which carries this notice:
//
//   MIT License
//
//   Copyright (c) 2026 Synthux Academy
//   Copyright (c) 2022 Infrasonic Audio, LLC
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.

#include "core/ParamTable.h"

#include <cmath>

#include "DSPUtils.h"

namespace audreybrick
{
namespace
{
using daisysp::Mapping;

// Order is the Knob enum. Columns: id, panel, unit, min, max, curve, smooth time,
// registry initial, default knob position.
//
// Default knob positions (docs/PARAMETERS.md section 4) are the inverse mapping of
// the registry initial values: (40-16)/56; 0; 0; 1; log(25)/log(400); 0; 0; 0;
// sqrt(0.45/4.95); 0; sqrt(0.5).
const ParamSpec kTable[kKnobCount] = {
    {"Frequency",         "frequency",       "note", 16.0f,   72.0f,    Mapping::LINEAR, 0.2f,  40.0f,    0.42857143f},
    {"FeedbackGain",      "feedback",        "dB",   -60.0f,  12.0f,    Mapping::LINEAR, 0.05f, -60.0f,   0.0f},
    {"FeedbackBody",      "body",            "s",    0.001f,  0.1f,     Mapping::EXP,    1.0f,  0.001f,   0.0f},
    {"FeedbackLPFCutoff", "lowpass",         "Hz",   100.0f,  18000.0f, Mapping::LOG,    0.05f, 18000.0f, 1.0f},
    {"FeedbackHPFCutoff", "highpass",        "Hz",   10.0f,   4000.0f,  Mapping::LOG,    0.05f, 250.0f,   0.53724216f},
    {"ReverbMix",         "reverb dry/wet",  "",     0.0f,    1.0f,     Mapping::LINEAR, 0.05f, 0.0f,     0.0f},
    {"ReverbDecay",       "reverb decay",    "",     0.2f,    1.0f,     Mapping::LINEAR, 0.05f, 0.2f,     0.0f},
    {"EchoDelaySend",     "delay mix",       "",     0.0f,    1.0f,     Mapping::EXP,    0.05f, 0.0f,     0.0f},
    {"EchoDelayTime",     "delay time",      "s",    0.05f,   5.0f,     Mapping::EXP,    0.1f,  0.5f,     0.30151134f},
    {"EchoDelayFeedback", "delay repeats",   "",     0.0f,    1.5f,     Mapping::LINEAR, 0.05f, 0.0f,     0.0f},
    {"OutputVolume",      "volume",          "",     0.0f,    1.0f,     Mapping::EXP,    0.05f, 0.5f,     0.70710678f},
};

char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if(a.size() != b.size())
        return false;
    for(std::size_t i = 0; i < a.size(); ++i)
        if(lower(a[i]) != lower(b[i]))
            return false;
    return true;
}
} // namespace

const ParamSpec& paramSpec(Knob knob) noexcept
{
    return kTable[static_cast<std::size_t>(knob)];
}

bool knobFromId(std::string_view id, Knob& out) noexcept
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        if(equalsIgnoreCase(id, kTable[i].id))
        {
            out = static_cast<Knob>(i);
            return true;
        }
    }
    return false;
}

float preShape(Knob knob, float n, bool delaySwitch) noexcept
{
    switch(knob)
    {
        // "Special mapping for reverb feedback/decay (anti-exponential tension curve)"
        case Knob::ReverbDecay: return infrasonic::ftension(n, kReverbDecayTension);
        // "Delay switch doubles or halves delay time instantly for doppler warp"
        case Knob::EchoDelayTime: return n * (delaySwitch ? kDelaySwitchScale : 1.0f);
        default: return n;
    }
}

float knobToEngine(Knob knob, float n, bool delaySwitch) noexcept
{
    // Same expression as ParameterRegistry::UpdateNormalized.
    const ParamSpec& s = paramSpec(knob);
    return daisysp::fmap(daisysp::fclamp(preShape(knob, n, delaySwitch), 0.0f, 1.0f), s.min, s.max, s.curve);
}

double engineToKnob(Knob knob, float v, bool delaySwitch) noexcept
{
    const ParamSpec& s   = paramSpec(knob);
    const double     lo  = s.min;
    const double     hi  = s.max;
    double           x   = static_cast<double>(v);
    if(!(x >= lo)) // also NaN
        x = lo;
    if(x > hi)
        x = hi;

    // The registry's fmap, inverted: the pre-shaped value p in 0..1.
    double p = 0.0;
    switch(s.curve)
    {
        case daisysp::Mapping::EXP: p = std::sqrt((x - lo) / (hi - lo)); break;
        case daisysp::Mapping::LOG: p = std::log(x / lo) / std::log(hi / lo); break;
        case daisysp::Mapping::LINEAR:
        default: p = (x - lo) / (hi - lo); break;
    }
    p = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);

    // preShape, inverted.
    switch(knob)
    {
        case Knob::ReverbDecay:
        {
            // p = expm1(k n) / expm1(k)  =>  n = log1p(p expm1(k)) / k
            const double k = static_cast<double>(kReverbDecayTension);
            return std::log1p(p * std::expm1(k)) / k;
        }
        case Knob::EchoDelayTime: return delaySwitch ? p / static_cast<double>(kDelaySwitchScale) : p;
        default: return p;
    }
}

float modulateEngineValue(Knob knob, float smoothed, float offset, bool delaySwitch) noexcept
{
    if(offset == 0.0f || offset != offset) // 0, -0 or NaN: not modulated
        return smoothed;
    const ParamSpec& s = paramSpec(knob);
    if(std::isfinite(smoothed) && smoothed >= s.min && smoothed <= s.max && knob != Knob::ReverbDecay)
    {
        // The same result computed in the mapped domain (cheaper): for a linear
        // knob an offset adds offset * (max - min); for a LOG knob it multiplies
        // by (max / min)^offset; then the range clamp, which is the knob clamp.
        if(s.curve == daisysp::Mapping::LINEAR)
        {
            const float v = smoothed + offset * (s.max - s.min);
            return v < s.min ? s.min : (v > s.max ? s.max : v);
        }
        if(s.curve == daisysp::Mapping::LOG)
        {
            const float o = offset < -2.0f ? -2.0f : (offset > 2.0f ? 2.0f : offset); // inf-safe, beyond the clamp
            const float v = smoothed * std::exp2(o * std::log2(s.max / s.min));
            return v < s.min ? s.min : (v > s.max ? s.max : v);
        }
    }
    const double ns = engineToKnob(knob, smoothed, delaySwitch);
    const double lo = ns < 0.0 ? ns : 0.0;
    const double hi = ns > 1.0 ? ns : 1.0;
    double       n  = ns + static_cast<double>(offset);
    n               = n < lo ? lo : (n > hi ? hi : n);
    return knobToEngine(knob, static_cast<float>(n), delaySwitch);
}

} // namespace audreybrick
