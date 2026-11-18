// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The protocol between the UI thread (ControlModel, the only producer) and the
// audio thread (ButtonControlSource::read, the only consumer):
//
//   1. ControlCommand: discrete events, sent through a lock-free SPSC queue.
//   2. The held word: one std::atomic<std::uint32_t> describing which ramps are
//      running right now. Written only by the UI thread, read once per control
//      block by the audio thread.
//
// Because the held word carries the knob each d-pad axis is bound to, the audio
// side knows nothing about tabs.

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "core/ControlSource.h"

namespace audreybrick
{

enum class CommandType : std::uint8_t
{
    Step,              // value[knob] += delta, clamped to 0..1
    Reset,             // value[knob] = ParamTable default position
    SetAll,            // all eleven values and the switch latch at once (presets, later)
    ToggleSwitchLatch, // latch = !latch
};

// Fixed-size, trivially copyable. Fields a type does not use are ignored.
struct ControlCommand
{
    CommandType type        = CommandType::Step;
    Knob        knob        = Knob::Count; // Step, Reset
    bool        switchLatch = false;       // SetAll
    // Step: a signed change in normalised knob travel (+ = up/right). The UI picks
    // the size (ordinary, semitone, feedback; coarse or fine, see ControlTuning.h),
    // so the audio side needs no per-knob step table.
    double delta = 0.0;
    float  values[kKnobCount] = {}; // SetAll: normalised knob positions

    static ControlCommand step(Knob k, double d) noexcept
    {
        ControlCommand c;
        c.type  = CommandType::Step;
        c.knob  = k;
        c.delta = d;
        return c;
    }
    static ControlCommand reset(Knob k) noexcept
    {
        ControlCommand c;
        c.type = CommandType::Reset;
        c.knob = k;
        return c;
    }
    static ControlCommand setAll(const float (&v)[kKnobCount], bool latch) noexcept
    {
        ControlCommand c;
        c.type        = CommandType::SetAll;
        c.switchLatch = latch;
        for(std::size_t i = 0; i < kKnobCount; ++i)
            c.values[i] = v[i];
        return c;
    }
    static ControlCommand toggleSwitchLatch() noexcept
    {
        ControlCommand c;
        c.type = CommandType::ToggleSwitchLatch;
        return c;
    }
};
static_assert(std::is_trivially_copyable_v<ControlCommand>, "commands travel through a lock-free queue");

// ---------------------------------------------------------------------------
// The held word.
//
// Bit layout (bit 0 = least significant). All other bits are zero.
//
//   bits  0-3   axis 0 (d-pad left/right) knob: 0 = none, 1..11 = Knob index + 1
//   bits  4-5   axis 0 direction: 0 = none, 1 = +1 (right), 2 = -1 (left)
//   bits  8-11  axis 1 (d-pad up/down) knob, same coding
//   bits 12-13  axis 1 direction: 0 = none, 1 = +1 (up), 2 = -1 (down)
//   bits 16-17  feedback direction: 0 = none, 1 = +1 (R2), 2 = -1 (L2)
//   bit  24     fine (Select held)
//   bit  25     feedback maximum held (A)
//   bit  26     switch held (B)
//
// Codes outside these (knob 12..15, direction 3) decode as none / 0. A word of 0
// means nothing is held. An axis is ramping only when it has both a knob and a
// non-zero direction.
// ---------------------------------------------------------------------------
struct HeldState
{
    Knob        axisKnob[2]  = {Knob::Count, Knob::Count}; // Knob::Count = none
    std::int8_t axisDir[2]   = {0, 0};                      // -1, 0, +1
    std::int8_t feedbackDir  = 0;                           // -1, 0, +1
    bool        fine         = false;
    bool        feedbackMax  = false;
    bool        switchHeld   = false;
};

namespace held
{
constexpr std::uint32_t kAxisShift[2]   = {0, 8};
constexpr std::uint32_t kKnobBits       = 0xFu; // within an axis field
constexpr std::uint32_t kDirShiftInAxis = 4;
constexpr std::uint32_t kDirBits        = 0x3u;
constexpr std::uint32_t kFeedbackShift  = 16;
constexpr std::uint32_t kFineBit        = 1u << 24;
constexpr std::uint32_t kFeedbackMaxBit = 1u << 25;
constexpr std::uint32_t kSwitchBit      = 1u << 26;

constexpr std::uint32_t encodeDir(int d) noexcept { return d > 0 ? 1u : (d < 0 ? 2u : 0u); }
constexpr std::int8_t   decodeDir(std::uint32_t c) noexcept
{
    return c == 1u ? std::int8_t{1} : (c == 2u ? std::int8_t{-1} : std::int8_t{0});
}
} // namespace held

constexpr std::uint32_t packHeld(const HeldState& s) noexcept
{
    std::uint32_t w = 0;
    for(int a = 0; a < 2; ++a)
    {
        const auto k = static_cast<std::uint32_t>(s.axisKnob[a]);
        const std::uint32_t kc = k < kKnobCount ? k + 1u : 0u;
        w |= (kc | (held::encodeDir(s.axisDir[a]) << held::kDirShiftInAxis)) << held::kAxisShift[a];
    }
    w |= held::encodeDir(s.feedbackDir) << held::kFeedbackShift;
    if(s.fine)
        w |= held::kFineBit;
    if(s.feedbackMax)
        w |= held::kFeedbackMaxBit;
    if(s.switchHeld)
        w |= held::kSwitchBit;
    return w;
}

constexpr HeldState unpackHeld(std::uint32_t w) noexcept
{
    HeldState s;
    for(int a = 0; a < 2; ++a)
    {
        const std::uint32_t field = w >> held::kAxisShift[a];
        const std::uint32_t kc    = field & held::kKnobBits;
        s.axisKnob[a] = (kc >= 1u && kc <= kKnobCount) ? static_cast<Knob>(kc - 1u) : Knob::Count;
        s.axisDir[a]  = held::decodeDir((field >> held::kDirShiftInAxis) & held::kDirBits);
    }
    s.feedbackDir = held::decodeDir((w >> held::kFeedbackShift) & held::kDirBits);
    s.fine        = (w & held::kFineBit) != 0;
    s.feedbackMax = (w & held::kFeedbackMaxBit) != 0;
    s.switchHeld  = (w & held::kSwitchBit) != 0;
    return s;
}

} // namespace audreybrick
