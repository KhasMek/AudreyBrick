// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/InputRouter.h"

namespace audreybrick
{

void InputRouter::deliver(const ButtonEvent& be, double t) noexcept
{
    const Tab before = model_.currentTab();
    model_.onButton(be, t);
    if(input_ == nullptr)
        return;
    const bool onInput = model_.currentTab() == Tab::Input;
    input_->onButton(be, t, onInput);
    if(onInput && before != Tab::Input)
        input_->onTabEntered();
}

InputRouter::Result InputRouter::route(InputMap::Action action, const ButtonEvent& be, double t) noexcept
{
    if(action == InputMap::Action::Quit)
        return Result::Quit;
    if(selftest_)
        return Result::Continue; // self-test: real input is ignored
    switch(action)
    {
        case InputMap::Action::Button:
            deliver(be, t);
            quit_.onButton(be); // watches Start; the model gets it too
            break;
        case InputMap::Action::MenuDown:
        case InputMap::Action::MenuUp:
            quit_.onMenu(action == InputMap::Action::MenuDown); // Menu has no other function
            break;
        case InputMap::Action::ReleaseAll:
            model_.releaseAll(t);
            if(input_ != nullptr)
                input_->releaseAll();
            quit_.releaseAll();
            break;
        case InputMap::Action::None:
        case InputMap::Action::Quit: break;
    }
    return quit_.triggered() ? Result::MenuStart : Result::Continue;
}

} // namespace audreybrick
