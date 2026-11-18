// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "control/ButtonControlSource.h"

#include <cstddef>
#include <cstdint>

#include "core/ParamTable.h"

namespace audreybrick
{
namespace
{
constexpr std::size_t kFb = static_cast<std::size_t>(Knob::FeedbackGain);

// Clamps to 0..1; NaN becomes 0 so a bad command can never poison a knob.
double clamp01(double v) noexcept
{
    if(!(v >= 0.0))
        return 0.0;
    return v > 1.0 ? 1.0 : v;
}
} // namespace

ButtonControlSource::ButtonControlSource(std::size_t queueCapacity) : queue_(queueCapacity)
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        value_[i] = paramSpec(static_cast<Knob>(i)).defaultNorm;
        knob_[i].store(static_cast<float>(value_[i]), std::memory_order_relaxed);
    }
    storedFeedback_.store(static_cast<float>(value_[kFb]), std::memory_order_relaxed);
}

float ButtonControlSource::publishedKnob(Knob k) const noexcept
{
    const auto i = static_cast<std::size_t>(k);
    return i < kKnobCount ? knob_[i].load(std::memory_order_relaxed) : 0.0f;
}

void ButtonControlSource::apply(const ControlCommand& c) noexcept
{
    const auto k = static_cast<std::size_t>(c.knob);
    switch(c.type)
    {
        case CommandType::Step:
            if(k < kKnobCount)
                value_[k] = clamp01(value_[k] + c.delta);
            break;
        case CommandType::Reset:
            if(k < kKnobCount)
                value_[k] = paramSpec(c.knob).defaultNorm;
            break;
        case CommandType::SetAll:
            for(std::size_t i = 0; i < kKnobCount; ++i)
                value_[i] = clamp01(c.values[i]);
            latch_ = c.switchLatch;
            break;
        case CommandType::ToggleSwitchLatch: latch_ = !latch_; break;
    }
}

void ButtonControlSource::read(ControlFrame& out) noexcept
{
    // 1. Commands, bounded per block.
    ControlCommand c;
    for(int i = 0; i < tuning::kMaxCommandsPerBlock && queue_.pop(c); ++i)
    {
        apply(c);
        ++processedLocal_;
    }

    // 2. One load of the held word per block.
    const HeldState h = unpackHeld(held_.load(std::memory_order_relaxed));

    // 3. D-pad ramps.
    constexpr double kAccelBlocks = tuning::kRampAccelSeconds * tuning::kBlocksPerSecond;
    for(int a = 0; a < 2; ++a)
    {
        AxisRamp& r = axis_[a];
        const Knob k = h.axisKnob[a];
        const int  d = h.axisDir[a];
        if(k == Knob::Count || k == Knob::FeedbackGain || d == 0)
        {
            r = AxisRamp{};
            continue;
        }
        if(k != r.knob || d != r.dir)
        {
            r.knob   = k;
            r.dir    = static_cast<std::int8_t>(d);
            r.blocks = 0;
        }
        const double t    = r.blocks < kAccelBlocks ? static_cast<double>(r.blocks) / kAccelBlocks : 1.0;
        double       rate = tuning::kRampStartRate + (tuning::kRampEndRate - tuning::kRampStartRate) * t;
        if(h.fine)
            rate *= tuning::kFineRampScale;
        const auto i = static_cast<std::size_t>(k);
        value_[i]    = clamp01(value_[i] + d * rate / tuning::kBlocksPerSecond);
        if(r.blocks < static_cast<std::int64_t>(kAccelBlocks))
            ++r.blocks;
    }

    // 4. Feedback ramp, constant rate.
    if(h.feedbackDir != 0)
    {
        const double rate = h.fine ? tuning::kFbRampRateFine : tuning::kFbRampRate;
        value_[kFb]       = clamp01(value_[kFb] + h.feedbackDir * rate / tuning::kBlocksPerSecond);
    }

    // 5. The frame.
    for(std::size_t i = 0; i < kKnobCount; ++i)
        out.knob[i] = static_cast<float>(value_[i]);
    if(h.feedbackMax)
        out.knob[kFb] = 1.0f;
    out.delaySwitch = latch_ != h.switchHeld;

    // 6. Publish.
    publish(out);
    feedbackMax_.store(h.feedbackMax, std::memory_order_relaxed);
}

void ButtonControlSource::publish(const ControlFrame& f) noexcept
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
        knob_[i].store(f.knob[i], std::memory_order_relaxed);
    storedFeedback_.store(static_cast<float>(value_[kFb]), std::memory_order_relaxed);
    switchOn_.store(f.delaySwitch, std::memory_order_relaxed);
    switchLatch_.store(latch_, std::memory_order_relaxed);
    processed_.store(processedLocal_, std::memory_order_relaxed);
}

} // namespace audreybrick
