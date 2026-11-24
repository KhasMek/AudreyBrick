// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/Tabs.h"

#include <cstddef>

namespace audreybrick
{
namespace
{
// docs/CONTROL_SCHEME_PROPOSAL.md section 0, "Tabs". OUT's left/right is the
// safety limiter (M4), unbound in M2. INPUT (M2b-A), MOD (MOD-B) and PRESET
// (PRESET-B) bind no knob.
const TabSpec kTabs[kTabCount] = {
    {"LOOP", Knob::Frequency, Knob::FeedbackBody, kNoKnob, kNoKnob},
    {"FILTER", Knob::FeedbackLPFCutoff, Knob::FeedbackHPFCutoff, kNoKnob, kNoKnob},
    {"REVERB", Knob::ReverbMix, Knob::ReverbDecay, kNoKnob, kNoKnob},
    {"DELAY", Knob::EchoDelayTime, Knob::EchoDelaySend, kNoKnob, Knob::EchoDelayFeedback},
    {"OUT", kNoKnob, Knob::OutputVolume, kNoKnob, kNoKnob},
    {"INPUT", kNoKnob, kNoKnob, kNoKnob, kNoKnob},
    {"MOD", kNoKnob, kNoKnob, kNoKnob, kNoKnob},
    {"PRESET", kNoKnob, kNoKnob, kNoKnob, kNoKnob},
};

std::size_t index(Tab t) noexcept
{
    const auto i = static_cast<std::size_t>(t);
    return i < kTabCount ? i : 0;
}
} // namespace

const TabSpec& tabSpec(Tab t) noexcept
{
    return kTabs[index(t)];
}

Knob tabBinding(Tab t, Axis axis, bool xHeld) noexcept
{
    const TabSpec& s = tabSpec(t);
    if(axis == Axis::LeftRight)
        return xHeld ? s.xLeftRight : s.leftRight;
    return xHeld ? s.xUpDown : s.upDown;
}

Tab nextTab(Tab t) noexcept
{
    return static_cast<Tab>((index(t) + 1) % kTabCount);
}

Tab previousTab(Tab t) noexcept
{
    return static_cast<Tab>((index(t) + kTabCount - 1) % kTabCount);
}

} // namespace audreybrick
