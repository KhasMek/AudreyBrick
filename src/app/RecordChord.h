// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// RecordChord: Start + Y starts and stops recording (docs/briefs/REC.md). Like
// MenuQuit (app/MenuQuit.h) it watches the button events before the control
// model and the INPUT tab's panel get them; unlike MenuQuit it can hold back a
// Y event, so that the Y of the chord has no other effect. No SDL, no allocation.
//
// Rules:
//   - It fires (Out::toggle) when a press makes Start and Y both down: Y pressed
//     while Start is held, or Start pressed while Y is held. Once per chord: a
//     repeated down of a button already down is not a press, so one of the two
//     must be released before it can fire again. Other buttons held at the same
//     time do not matter (Start + Select + Y is Start + Y).
//   - Start + then Y: that Y down is held back (Out::deliver false), and so is its
//     up. The model and the panel never see this Y: it is no reset modifier, and
//     with X held on INPUT it does not toggle LOOP/ONCE.
//   - Y + then Start: Y went down before the chord existed, so the model and the
//     panel have already had it as a plain Y press (whatever that did when it was
//     pressed stays done: a Y press does nothing by itself; with X held on INPUT
//     it toggled LOOP/ONCE at that moment). When Start completes the chord they
//     are told Y is up (Out::releaseY: deliver a Y up first), so from then on Y is
//     no reset modifier; the real Y up later is held back.
//   - In both cases Y counts as a reset modifier again only after it has been
//     released and pressed again (without Start).
//   - Start events are always delivered (Start does nothing in the model;
//     MenuQuit watches it, so Menu + Start still quits with Y held).
//   - releaseAll() (focus lost, controller removed) forgets everything.

#pragma once

#include "app/Buttons.h"

namespace audreybrick
{

class RecordChord
{
  public:
    struct Out
    {
        bool deliver  = true;  // pass this event on to the model and the panel
        bool releaseY = false; // first deliver a Y up to the model and the panel
        bool toggle   = false; // start or stop recording
    };

    Out  onButton(const ButtonEvent& e) noexcept;
    void releaseAll() noexcept;

    bool startHeld() const noexcept { return start_; }
    bool yHeld() const noexcept { return y_; }
    unsigned fired() const noexcept { return fired_; }

  private:
    bool     start_    = false;
    bool     y_        = false;
    bool     yHeldBack_ = false; // the current Y press is the chord's: its up is held back
    unsigned fired_    = 0;
};

} // namespace audreybrick
