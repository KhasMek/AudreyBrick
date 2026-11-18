// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ControlModel: the UI-thread half of the button control scheme
// (docs/CONTROL_SCHEME_PROPOSAL.md section 0; docs/briefs/M2.md, M2-A).
//
// Button events and time go in; commands and the held word go out to a
// ButtonControlSource. The model is the only producer on its command queue and
// the only writer of its held word. It never holds a knob value: the audio side
// owns those and publishes them for display.
//
// Time is passed in by the caller (seconds, monotonic, double). The model never
// reads a clock, so a test that feeds the same events and times gets the same
// output.
//
// Behaviour:
//   - D-pad press: one Step at once for the knob bound to that direction on the
//     current tab (the X binding while X is held). If the press is still held
//     tuning::kHoldDelaySeconds later (checked in tick() and onButton()), the axis
//     is "held" in the held word and the audio side ramps it until release.
//   - Opposite directions of one axis down together: no movement on that axis,
//     and the second press sends no Step.
//   - L1 / R1: previous / next tab, wrapping. Any d-pad direction that is down
//     becomes inert until it is released (no ramp, no further effect).
//   - X: the shift to the third and fourth bindings. Pressing or releasing X also
//     makes any d-pad direction that is down inert, so a ramp never jumps from one
//     knob to another.
//   - Y + d-pad press: Reset for the knob bound to that direction (X binding with X
//     held); no Step, and that press never ramps.
//   - Select: fine (taps and ramps of the d-pad and L2/R2 at the fine sizes).
//   - Select + B: ToggleSwitchLatch; that B press does not hold the switch.
//   - B alone: switch held while down.
//   - A: feedback maximum while down.
//   - L2 / R2: one feedback Step at once; if still held after the hold delay, the
//     feedback ramp runs until release. Both down: no movement, and the second
//     press sends no Step.
//   - Start: nothing in M2.
// A repeated "down" for a button already down, or an "up" for a button that is
// up, is ignored.

#pragma once

#include <cstddef>
#include <cstdint>

#include "app/Buttons.h"
#include "app/Tabs.h"
#include "control/ButtonControlSource.h"
#include "control/ControlCommands.h"

namespace audreybrick
{

class ControlModel
{
  public:
    // Start-up state: tab LOOP, nothing held. Writes a held word of 0.
    explicit ControlModel(ButtonControlSource& sink) noexcept;

    void onButton(const ButtonEvent& e, double timeSeconds) noexcept;
    // Call regularly (every UI frame): starts ramps whose hold delay has passed.
    void tick(double timeSeconds) noexcept;
    // As if an "up" had arrived for every button that is down: ramps stop, feedback
    // maximum and the momentary switch release, modifiers clear. Sends no command;
    // the tab and the switch latch are unchanged. A later "up" for those buttons is
    // ignored; a later "down" works normally. For focus loss and controller removal.
    void releaseAll(double timeSeconds) noexcept;

    // --- State for the UI ------------------------------------------------------
    Tab  currentTab() const noexcept { return tab_; }
    bool isDown(Button b) const noexcept { return press_[idx(b)].down; }
    bool fine() const noexcept { return isDown(Button::Select); }
    bool xHeld() const noexcept { return isDown(Button::X); }
    bool yHeld() const noexcept { return isDown(Button::Y); }
    bool feedbackMaxHeld() const noexcept { return isDown(Button::A); }
    bool switchHeld() const noexcept { return press_[idx(Button::B)].down && !press_[idx(Button::B)].inert; }
    // The held word last written (layout in ControlCommands.h).
    std::uint32_t heldWord() const noexcept { return held_; }

    // Commands the model tried to send, and how many of those the full queue
    // dropped. sent = delivered + dropped.
    std::uint64_t commandsSent() const noexcept { return sent_; }
    std::uint64_t commandsDropped() const noexcept { return dropped_; }

  private:
    struct Press
    {
        bool   down  = false;
        bool   inert = false; // down, but must not ramp (consumed by Y, a tab or X change, or Select+B)
        double time  = 0.0;   // when it went down
    };

    static std::size_t idx(Button b) noexcept { return static_cast<std::size_t>(b); }

    void send(const ControlCommand& c) noexcept;
    void onDpad(Button b, double t) noexcept;
    void onFeedbackButton(Button b, double t) noexcept;
    void makeDpadInert() noexcept;
    // -1, 0 or +1 for a pair of opposite buttons: the direction ramping at time t.
    int  rampDir(Button neg, Button pos, double t) const noexcept;
    void publishHeld(double t) noexcept;

    ButtonControlSource& sink_;
    Press                press_[kButtonCount];
    Tab                  tab_     = Tab::Loop;
    std::uint32_t        held_    = 0;
    std::uint64_t        sent_    = 0;
    std::uint64_t        dropped_ = 0;
};

} // namespace audreybrick
