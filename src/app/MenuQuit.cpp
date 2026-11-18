// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/MenuQuit.h"

namespace audreybrick
{

void MenuQuit::onMenu(bool pressed) noexcept
{
    if(pressed && !menu_ && start_)
        triggered_ = true;
    menu_ = pressed;
}

void MenuQuit::onButton(const ButtonEvent& e) noexcept
{
    if(e.button != Button::Start)
        return;
    if(e.pressed && !start_ && menu_)
        triggered_ = true;
    start_ = e.pressed;
}

void MenuQuit::releaseAll() noexcept
{
    menu_  = false;
    start_ = false;
}

} // namespace audreybrick
