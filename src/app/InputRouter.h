// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputRouter: where the app sends what InputMap made of an SDL event (task M3-C).
// App's main loop calls route() for every event, right after InputMap::handle();
// tests drive the same code with synthetic SDL events, without a window or an
// audio device (tests/platform/platform_router_test.cpp).
//
//   Action::Quit        -> Result::Quit (window close, SDL_QUIT, Escape, Alt+F4)
//   self-test mode      -> anything else is ignored (real input never reaches the
//                          model and never quits; scripted events go to the model
//                          directly)
//   Action::Button      -> the control model, and MenuQuit (which watches Start)
//   Action::MenuDown/Up -> MenuQuit only (Menu has no other function)
//   Action::ReleaseAll  -> the model and MenuQuit forget every held button
// Then, outside self-test mode, Result::MenuStart once MenuQuit has triggered
// (Menu + Start, app/MenuQuit.h).
//
// UI thread only. No allocation.

#pragma once

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/MenuQuit.h"
#include "platform/InputMap.h"

namespace audreybrick
{

class InputRouter
{
  public:
    enum class Result
    {
        Continue,  // nothing for the main loop to do
        Quit,      // InputMap reported a quit
        MenuStart, // Menu + Start on a pad: quit
    };

    InputRouter(ControlModel& model, MenuQuit& quit, bool selftest) noexcept
        : model_(model), quit_(quit), selftest_(selftest)
    {
    }

    Result route(InputMap::Action action, const ButtonEvent& be, double t) noexcept;

  private:
    ControlModel& model_;
    MenuQuit&     quit_;
    bool          selftest_;
};

} // namespace audreybrick
