// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/InputRouter.h"

namespace audreybrick
{

InputRouter::Result InputRouter::route(InputMap::Action action, const ButtonEvent& be, double t) noexcept
{
    if(action == InputMap::Action::Quit)
        return Result::Quit;
    if(selftest_)
        return Result::Continue; // self-test: real input is ignored
    switch(action)
    {
        case InputMap::Action::Button:
            model_.onButton(be, t);
            quit_.onButton(be); // watches Start; the model gets it too
            break;
        case InputMap::Action::MenuDown:
        case InputMap::Action::MenuUp:
            quit_.onMenu(action == InputMap::Action::MenuDown); // Menu has no other function
            break;
        case InputMap::Action::ReleaseAll:
            model_.releaseAll(t);
            quit_.releaseAll();
            break;
        case InputMap::Action::None:
        case InputMap::Action::Quit: break;
    }
    return quit_.triggered() ? Result::MenuStart : Result::Continue;
}

} // namespace audreybrick
