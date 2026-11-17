// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The control abstraction that replaces the Audrey II hardware knobs and switch
// (docs/PLAN.md section 4).

#pragma once

#include <cstddef>
#include <cstdint>

namespace audreybrick
{

// Same order as the firmware's Parameter enum (FeedbackSynthControls.h), which is
// also its ADC channel order.
enum class Knob : std::uint8_t
{
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
    Count
};

constexpr std::size_t kKnobCount = static_cast<std::size_t>(Knob::Count);

struct ControlFrame
{
    float knob[static_cast<int>(Knob::Count)]; // 0..1, the firmware's (1 - adc) value
    bool  delaySwitch;                         // true = switch on
};

class ControlSource
{
  public:
    virtual ~ControlSource() = default;
    // Called on the audio thread once per control block (12 kHz).
    // Must not allocate, lock, block or do I/O.
    // Must write every field of `out`.
    virtual void read(ControlFrame& out) noexcept = 0;
};

} // namespace audreybrick
