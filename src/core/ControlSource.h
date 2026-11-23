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

    // MOD-A (docs/briefs/MOD.md): modulation, applied by AudreyVoice AFTER the
    // original's parameter smoother. knobMod[k] is an offset in normalised knob
    // travel (-1..+1 of the knob's range; the voice clamps the sum) added to the
    // knob position that the smoothed value corresponds to; exactly 0 (or -0)
    // means "not modulated" and the voice then runs the unmodified path, bit for
    // bit. delaySwitchMod: the engine sees the switch on if delaySwitch OR this
    // is true. Sources without modulation write 0 and false.
    float knobMod[static_cast<int>(Knob::Count)];
    bool  delaySwitchMod;
};

class ControlSource
{
  public:
    virtual ~ControlSource() = default;
    // Called on the audio thread once per control block (12 kHz).
    // Must not allocate, lock, block or do I/O.
    // Must write every field of `out` (knobMod all 0 and delaySwitchMod false
    // when the source does not modulate).
    virtual void read(ControlFrame& out) noexcept = 0;
};

} // namespace audreybrick
