// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The Brick's buttons as the control logic sees them, named after the SDL
// GameController buttons (A is the bottom face button in SDL's naming). No SDL
// types: the platform layer translates SDL events into ButtonEvents.

#pragma once

#include <cstddef>

namespace audreybrick
{

// A scoped enum: bare enumerators named A, B, X, Y, Select and Start at namespace
// scope would collide too easily.
enum class Button : unsigned char
{
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    A,
    B,
    X,
    Y,
    L1,
    R1,
    L2,
    R2,
    Select,
    Start,
};

constexpr std::size_t kButtonCount = static_cast<std::size_t>(Button::Start) + 1;

struct ButtonEvent
{
    Button button;
    bool   pressed; // true = down, false = up
};

// The enum name ("DpadUp", "A", "L2", "Select", ...). Used by the button script
// format (ButtonScript.h) and for display.
constexpr const char* buttonName(Button b) noexcept
{
    constexpr const char* kNames[kButtonCount] = {"DpadUp", "DpadDown", "DpadLeft", "DpadRight", "A",      "B",
                                                  "X",      "Y",        "L1",       "R1",        "L2",     "R2",
                                                  "Select", "Start"};
    const auto            i = static_cast<std::size_t>(b);
    return i < kButtonCount ? kNames[i] : "?";
}

} // namespace audreybrick
