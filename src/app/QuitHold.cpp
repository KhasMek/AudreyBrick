// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/QuitHold.h"

namespace audreybrick
{

void QuitHold::onButton(const ButtonEvent& e, double t) noexcept
{
    if(e.button != Button::Select && e.button != Button::Start)
        return;
    const bool before = select_ && start_;
    if(e.button == Button::Select)
        select_ = e.pressed;
    else
        start_ = e.pressed;
    if(!before && select_ && start_)
        since_ = t;
}

void QuitHold::releaseAll() noexcept
{
    select_ = false;
    start_  = false;
}

bool QuitHold::update(double t) noexcept
{
    if(select_ && start_ && t - since_ >= kHoldSeconds)
        triggered_ = true;
    return triggered_;
}

float QuitHold::progress(double t) const noexcept
{
    if(!active())
        return 0.0f;
    const double p = (t - since_) / kHoldSeconds;
    if(!(p > 0.0))
        return 0.0f;
    return p >= 1.0 ? 1.0f : static_cast<float>(p);
}

} // namespace audreybrick
