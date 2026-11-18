// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The tabs of the button scheme (docs/CONTROL_SCHEME_PROPOSAL.md section 0), M2
// subset: LOOP, FILTER, REVERB, DELAY, OUT. Each tab binds up to four knobs to
// the d-pad: left/right and up/down, and the same two axes with X held. Right
// and up increase. FeedbackGain is never bound (it lives on L2/R2).

#pragma once

#include <cstddef>

#include "core/ControlSource.h"

namespace audreybrick
{

enum class Tab : unsigned char
{
    Loop,
    Filter,
    Reverb,
    Delay,
    Out,
    Count
};

constexpr std::size_t kTabCount = static_cast<std::size_t>(Tab::Count);

// A d-pad axis.
enum class Axis : unsigned char
{
    LeftRight = 0,
    UpDown    = 1,
};

constexpr Knob kNoKnob = Knob::Count;

struct TabSpec
{
    const char* name;
    Knob        leftRight;  // kNoKnob = unbound
    Knob        upDown;
    Knob        xLeftRight; // with X held
    Knob        xUpDown;
};

const TabSpec& tabSpec(Tab t) noexcept;

// The knob an axis moves on tab `t`, with or without X held (kNoKnob if none).
Knob tabBinding(Tab t, Axis axis, bool xHeld) noexcept;

// The next or previous tab, wrapping.
Tab nextTab(Tab t) noexcept;
Tab previousTab(Tab t) noexcept;

} // namespace audreybrick
