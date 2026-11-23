// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/ModFormat.h"

#include <cmath>
#include <cstdio>

namespace audreybrick
{
namespace
{
constexpr ModTarget kOrder[kModTargetListSize] = {
    ModTarget::None,
    ModTarget::FeedbackGain,
    ModTarget::Frequency,
    ModTarget::FeedbackBody,
    ModTarget::FeedbackLPFCutoff,
    ModTarget::FeedbackHPFCutoff,
    ModTarget::ReverbMix,
    ModTarget::ReverbDecay,
    ModTarget::EchoDelayTime,
    ModTarget::EchoDelaySend,
    ModTarget::EchoDelayFeedback,
    ModTarget::OutputVolume,
    ModTarget::InputGain,
    ModTarget::DelaySwitch,
};

std::size_t put(char* out, std::size_t size, const char* fmt, double v, const char* unit) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    const int n = std::snprintf(out, size, fmt, v, unit);
    if(n < 0)
    {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1;
}

std::size_t notFinite(char* out, std::size_t size) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    std::snprintf(out, size, "--");
    return size > 2 ? 2 : size - 1;
}

double roundTo(double v, double scale) noexcept
{
    return std::round(v * scale) / scale;
}
} // namespace

ModTarget modTargetAt(std::size_t i) noexcept
{
    return i < kModTargetListSize ? kOrder[i] : ModTarget::None;
}

std::size_t modTargetListIndex(ModTarget t) noexcept
{
    for(std::size_t i = 0; i < kModTargetListSize; ++i)
        if(kOrder[i] == t)
            return i;
    return 0;
}

const char* modTargetDisplayName(ModTarget t) noexcept
{
    switch(t)
    {
        case ModTarget::None: return "NONE";
        case ModTarget::Frequency: return "FREQUENCY";
        case ModTarget::FeedbackGain: return "FEEDBACK";
        case ModTarget::FeedbackBody: return "BODY";
        case ModTarget::FeedbackLPFCutoff: return "LOWPASS";
        case ModTarget::FeedbackHPFCutoff: return "HIGHPASS";
        case ModTarget::ReverbMix: return "DRY/WET";
        case ModTarget::ReverbDecay: return "DECAY";
        case ModTarget::EchoDelaySend: return "MIX";
        case ModTarget::EchoDelayTime: return "TIME";
        case ModTarget::EchoDelayFeedback: return "REPEATS";
        case ModTarget::OutputVolume: return "VOLUME";
        case ModTarget::InputGain: return "INPUT GAIN";
        case ModTarget::DelaySwitch: return "DELAY SWITCH";
        case ModTarget::Count: break;
    }
    return "?";
}

const char* modShapeDisplayName(ModShape s) noexcept
{
    switch(s)
    {
        case ModShape::Sine: return "SINE";
        case ModShape::Triangle: return "TRIANGLE";
        case ModShape::Square: return "SQUARE";
        case ModShape::RampUp: return "RAMP UP";
        case ModShape::RampDown: return "RAMP DOWN";
        case ModShape::ExpUp: return "EXP UP";
        case ModShape::ExpDown: return "EXP DOWN";
        case ModShape::SampleHold: return "S&H";
        case ModShape::Envelope: return "ENVELOPE";
        case ModShape::Count: break;
    }
    return "?";
}

const char* modPolarityDisplayName(ModPolarity p) noexcept
{
    switch(p)
    {
        case ModPolarity::Bipolar: return "BIPOLAR";
        case ModPolarity::Unipolar: return "UNIPOLAR";
        case ModPolarity::Count: break;
    }
    return "?";
}

const char* modPolarityShortName(ModPolarity p) noexcept
{
    switch(p)
    {
        case ModPolarity::Bipolar: return "+/-";
        case ModPolarity::Unipolar: return "+";
        case ModPolarity::Count: break;
    }
    return "?";
}

std::size_t formatModRateHz(char* out, std::size_t size, double hz, bool upper) noexcept
{
    if(!std::isfinite(hz))
        return notFinite(out, size);
    const char* unitHz  = upper ? "HZ" : "Hz";
    const char* unitKHz = upper ? "KHZ" : "kHz";
    if(roundTo(hz, 1000.0) < 1.0)
        return put(out, size, "%.3f %s", hz, unitHz);
    if(roundTo(hz, 100.0) < 10.0)
        return put(out, size, "%.2f %s", hz, unitHz);
    if(roundTo(hz, 10.0) < 100.0)
        return put(out, size, "%.1f %s", hz, unitHz);
    if(std::round(hz) < 1000.0)
        return put(out, size, "%.0f %s", hz, unitHz);
    return put(out, size, "%.2f %s", hz / 1000.0, unitKHz);
}

std::size_t formatModRelease(char* out, std::size_t size, double seconds, bool upper) noexcept
{
    if(!std::isfinite(seconds))
        return notFinite(out, size);
    const char* unitS  = upper ? "S" : "s";
    const char* unitMs = upper ? "MS" : "ms";
    if(roundTo(seconds, 100.0) >= 1.0)
        return put(out, size, "%.2f %s", seconds, unitS);
    const double ms = seconds * 1000.0;
    if(roundTo(ms, 100.0) < 10.0)
        return put(out, size, "%.2f %s", ms, unitMs);
    if(roundTo(ms, 10.0) < 100.0)
        return put(out, size, "%.1f %s", ms, unitMs);
    return put(out, size, "%.0f %s", ms, unitMs);
}

std::size_t formatModRate(char* out, std::size_t size, float rateNorm, ModShape shape, bool upper) noexcept
{
    if(!std::isfinite(rateNorm))
        return notFinite(out, size);
    if(shape == ModShape::Envelope)
        return formatModRelease(out, size, modReleaseSeconds(rateNorm), upper);
    return formatModRateHz(out, size, modRateHz(rateNorm), upper);
}

std::size_t formatModDepth(char* out, std::size_t size, float depth) noexcept
{
    if(!std::isfinite(depth))
        return notFinite(out, size);
    const double pct = roundTo(static_cast<double>(depth) * 100.0, 10.0);
    if(pct == 0.0)
        return put(out, size, "%.1f%s", 0.0, "%");
    return put(out, size, "%+.1f%s", pct, "%");
}

} // namespace audreybrick
