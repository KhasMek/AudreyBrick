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
//   Action::Button      -> deliver(): the control model, then the input panel
//                          (M2b-A, if one is attached), then MenuQuit (which
//                          watches Start)
//   Action::MenuDown/Up -> MenuQuit only (Menu has no other function)
//   Action::ReleaseAll  -> the model, the input panel and MenuQuit forget every
//                          held button
// Then, outside self-test mode, Result::MenuStart once MenuQuit has triggered
// (Menu + Start, app/MenuQuit.h).
//
// deliver() (M2b-A): one button event to the model and then to the input panel,
// with whether the INPUT tab is current after the model has seen it (so the tab
// L1/R1 lands on counts); when the event made INPUT current, the panel is told
// (it rescans the input folder). The self-test's scripted events use deliver()
// directly (real input is ignored there, as before). The model never sees the
// panel: on INPUT it binds no knob, so its d-pad, X and Y do nothing there.
//
// UI thread only. No allocation (apart from what the input panel does when it
// rescans or logs, M2b-A). Nothing throws: everything route() and deliver() call
// is noexcept (the input panel catches at its own boundary).

#pragma once

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/MenuQuit.h"
#include "input/InputController.h"
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

    // `input`: the INPUT tab's panel (M2b-A), or nullptr for none.
    InputRouter(ControlModel& model, MenuQuit& quit, bool selftest, InputController* input = nullptr) noexcept
        : model_(model), quit_(quit), selftest_(selftest), input_(input)
    {
    }

    Result route(InputMap::Action action, const ButtonEvent& be, double t) noexcept;
    // One button event to the model and the input panel (not to MenuQuit).
    void deliver(const ButtonEvent& be, double t) noexcept;

  private:
    ControlModel&    model_;
    MenuQuit&        quit_;
    bool             selftest_;
    InputController* input_;
};

} // namespace audreybrick
