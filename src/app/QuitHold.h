// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// QuitHold: Start + Select held together for one second quits the app
// (docs/briefs/M3.md, M3-A, C.2). The built-in quit path that needs no keyboard
// and no system hotkey.
//
// It watches the same ButtonEvents the control model gets; it does not consume
// them. Select keeps acting as the fine modifier while held, and Start keeps
// doing whatever the model does with it (nothing in M2).
//
// Rules:
//   - The clock starts when the second of the two buttons goes down (the moment
//     both are down). Holding one of them alone never starts it.
//   - Releasing either button cancels the hold. Pressing it again starts a new
//     full second: holds are not added up.
//   - At kHoldSeconds of continuous holding, the hold triggers. It stays triggered.
//   - releaseAll() (focus lost, controller removed) cancels like a release.
// No SDL, no allocation; time is supplied by the caller (seconds, monotonic).

#pragma once

#include "app/Buttons.h"

namespace audreybrick
{

class QuitHold
{
  public:
    static constexpr double kHoldSeconds = 1.0;

    void onButton(const ButtonEvent& e, double t) noexcept;
    void releaseAll() noexcept;

    // Checks the hold at time t; returns triggered().
    bool update(double t) noexcept;

    bool triggered() const noexcept { return triggered_; }
    // Both buttons are down and the hold has not triggered yet: the screen shows
    // "HOLD TO QUIT".
    bool active() const noexcept { return select_ && start_ && !triggered_; }
    // Fraction of kHoldSeconds held so far at time t (0..1); 0 when not active.
    float progress(double t) const noexcept;

  private:
    bool   select_    = false;
    bool   start_     = false;
    bool   triggered_ = false;
    double since_     = 0.0; // when both went down
};

} // namespace audreybrick
