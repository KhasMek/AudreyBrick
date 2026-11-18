// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MenuQuit: Menu + Start quits the app at once (task M3-C). The Brick's Menu
// button arrives as SDL's game controller button 'guide'; it has no other
// function and is not one of the control model's Buttons (app/Buttons.h), so
// InputMap reports it separately (InputMap::Action::MenuDown / MenuUp) and only
// this class looks at it.
//
// Rules:
//   - Start pressed while Menu is held quits; so does Menu pressed while Start is
//     held. No hold time.
//   - Menu alone or Start alone never quits, however long. A repeated down of a
//     button that is already down is not a new press.
//   - Releasing a button forgets it; releaseAll() (focus lost, controller
//     removed) forgets both.
//   - Once triggered it stays triggered.
// It watches Start in the same ButtonEvents the control model gets and does not
// consume them (Start has no function in the model). No SDL, no allocation.
//
// KNULLI's own soft reset (Menu + Start pressed twice quickly, run by
// triggerhappy) ends the port with SIGTERM; the app handles that separately
// (app/Signals.h).

#pragma once

#include "app/Buttons.h"

namespace audreybrick
{

class MenuQuit
{
  public:
    void onMenu(bool pressed) noexcept;
    void onButton(const ButtonEvent& e) noexcept;
    void releaseAll() noexcept;

    bool triggered() const noexcept { return triggered_; }
    bool menuHeld() const noexcept { return menu_; }

  private:
    bool menu_      = false;
    bool start_     = false;
    bool triggered_ = false;
};

} // namespace audreybrick
