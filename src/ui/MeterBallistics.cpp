// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "ui/MeterBallistics.h"

#include <algorithm>
#include <cmath>

namespace audreybrick
{
namespace ui
{
namespace
{
// Gain after falling for `seconds` at `dbPerSecond`.
float fallGain(double seconds, float dbPerSecond) noexcept
{
    return static_cast<float>(std::pow(10.0, -static_cast<double>(dbPerSecond) * seconds / 20.0));
}
} // namespace

float meterPositionDb(float db) noexcept
{
    if(std::isnan(db))
        return 0.0f;
    const float p = (db - kMeterFloorDb) / (0.0f - kMeterFloorDb);
    return std::clamp(p, 0.0f, 1.0f);
}

float meterPosition(float linear) noexcept
{
    if(std::isnan(linear) || linear <= 0.0f)
        return 0.0f;
    if(!std::isfinite(linear))
        return 1.0f;
    return meterPositionDb(20.0f * std::log10(linear));
}

void PeakMeter::reset() noexcept
{
    *this = PeakMeter{};
}

void PeakMeter::update(float peak, double timeSeconds) noexcept
{
    const bool bad = !std::isfinite(peak);
    float      p   = bad ? kClipLevel : std::fabs(peak);
    if(p < kSilence)
        p = 0.0f;

    const double dt = started_ ? std::max(0.0, timeSeconds - last_) : 0.0;
    if(!started_ || timeSeconds > last_)
        last_ = timeSeconds;
    started_ = true;
    const double now = last_;

    // Bar: fall, then instant attack.
    level_ *= fallGain(dt, kFallDbPerSecond);
    if(level_ < kSilence)
        level_ = 0.0f;
    if(p > level_)
        level_ = p;

    // Hold: falls only for the part of dt after the hold time ran out.
    if(p >= hold_)
    {
        hold_     = p;
        holdTime_ = now;
    }
    else
    {
        const double fallStart = holdTime_ + kHoldSeconds;
        const double falling   = std::min(dt, now - fallStart);
        if(falling > 0.0)
            hold_ *= fallGain(falling, kFallDbPerSecond);
        if(hold_ < kSilence)
            hold_ = 0.0f;
    }
    hold_ = std::max(hold_, level_);

    // Clip latch.
    if(bad || p >= kClipLevel)
    {
        clipTime_ = now;
        everClip_ = true;
    }
    clip_ = everClip_ && (now - clipTime_) < kClipHoldSeconds;
}

} // namespace ui
} // namespace audreybrick
