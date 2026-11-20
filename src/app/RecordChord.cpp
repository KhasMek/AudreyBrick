// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/RecordChord.h"

namespace audreybrick
{

RecordChord::Out RecordChord::onButton(const ButtonEvent& e) noexcept
{
    Out o;
    if(e.button == Button::Start)
    {
        if(e.pressed && !start_ && y_)
        {
            o.toggle = true;
            ++fired_;
            if(!yHeldBack_)
            {
                o.releaseY = true; // Y was delivered as a press: tell them it is up
                yHeldBack_ = true; // and hold back its real up
            }
        }
        start_ = e.pressed;
        return o; // Start always goes on
    }
    if(e.button != Button::Y)
        return o;
    if(e.pressed)
    {
        if(y_)
        {
            o.deliver = !yHeldBack_; // a repeated down: as the first one was
            return o;
        }
        y_ = true;
        if(start_)
        {
            o.toggle   = true;
            o.deliver  = false;
            yHeldBack_ = true;
            ++fired_;
        }
        return o;
    }
    // Y up.
    if(!y_)
    {
        o.deliver = !yHeldBack_;
        return o;
    }
    y_         = false;
    o.deliver  = !yHeldBack_;
    yHeldBack_ = false;
    return o;
}

void RecordChord::releaseAll() noexcept
{
    start_     = false;
    y_         = false;
    yHeldBack_ = false;
}

} // namespace audreybrick
