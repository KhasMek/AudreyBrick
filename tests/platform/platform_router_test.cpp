// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-C: the app's input wiring, without a window or an audio device. Synthetic
// SDL events go through InputMap::handle() into InputRouter::route() (the code
// App's main loop runs), with a real ControlModel and MenuQuit:
//   - guide (Menu) down then start down quits; start then guide quits; either
//     alone does not;
//   - focus lost between Menu down and Start down prevents the quit, and so does
//     the active controller being removed (InputMap reports that as ReleaseAll
//     only for an open controller, which needs a device, so that case routes
//     Action::ReleaseAll directly);
//   - Select + Start does not quit, and Select reaches the model (fine);
//   - in self-test mode real input neither reaches the model nor quits (Escape
//     still quits);
//   - with the A/B and X/Y swaps on, Menu + Start still quits;
//   - M2b-A: with the input panel attached, the d-pad on the INPUT tab reaches
//     the panel (source, gain) and not the model's knobs; on other tabs it does
//     not reach the panel; entering INPUT rescans; ReleaseAll stops a gain ramp;
//     deliver() (the self-test's path) drives the INPUT tab the same way;
//   - M2b-A3: X + Y, X + Select and X + d-pad on INPUT reach the panel and make
//     the model send no command.
// No SDL_Init: InputMap::handle needs none for these events.

#include <SDL.h>

#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/InputRouter.h"
#include "app/MenuQuit.h"
#include "control/ButtonControlSource.h"
#include "input/InputController.h"
#include "input/InputStage.h"
#include "platform/InputMap.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
SDL_Event cbutton(Uint32 type, int button)
{
    SDL_Event e{};
    e.type           = type;
    e.cbutton.type   = type;
    e.cbutton.button = static_cast<Uint8>(button);
    e.cbutton.state  = type == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    return e;
}
SDL_Event down(int b)
{
    return cbutton(SDL_CONTROLLERBUTTONDOWN, b);
}
SDL_Event up(int b)
{
    return cbutton(SDL_CONTROLLERBUTTONUP, b);
}
SDL_Event focusLost()
{
    SDL_Event e{};
    e.type         = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    return e;
}
SDL_Event escape()
{
    SDL_Event e{};
    e.type                = SDL_KEYDOWN;
    e.key.type            = SDL_KEYDOWN;
    e.key.state           = SDL_PRESSED;
    e.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    return e;
}

// The app's wiring: InputMap -> InputRouter -> model and MenuQuit.
struct Rig
{
    InputMap                             input;
    std::unique_ptr<ButtonControlSource> src   = std::make_unique<ButtonControlSource>();
    std::unique_ptr<ControlModel>        model = std::make_unique<ControlModel>(*src);
    MenuQuit                             quit;
    InputRouter                          router;
    double                               t = 0.0;

    explicit Rig(bool selftest = false) : router(*model, quit, selftest) {}

    InputRouter::Result feed(const SDL_Event& e)
    {
        ButtonEvent be{};
        t += 0.01;
        return router.route(input.handle(e, be), be, t);
    }
    // Feeds the events in order; returns the last result.
    InputRouter::Result feedAll(std::initializer_list<SDL_Event> evs)
    {
        InputRouter::Result r = InputRouter::Result::Continue;
        for(const SDL_Event& e : evs)
            r = feed(e);
        return r;
    }
};

constexpr int kMenu   = SDL_CONTROLLER_BUTTON_GUIDE;
constexpr int kStart  = SDL_CONTROLLER_BUTTON_START;
constexpr int kSelect = SDL_CONTROLLER_BUTTON_BACK;
using R               = InputRouter::Result;
} // namespace

int main()
{
    {
        Rig r;
        check(r.feed(down(kMenu)) == R::Continue, "Menu down alone: no quit");
        check(r.feed(down(kStart)) == R::MenuStart, "Menu held, Start down: quit (Menu + Start)");
    }
    {
        Rig r;
        check(r.feed(down(kStart)) == R::Continue && r.feed(down(kMenu)) == R::MenuStart,
              "Start held, Menu down: quit");
    }
    {
        Rig r;
        check(r.feedAll({down(kMenu), up(kMenu), down(kMenu), up(kMenu)}) == R::Continue, "Menu alone, twice: no quit");
        check(r.feedAll({down(kStart), up(kStart)}) == R::Continue, "Start alone (after Menu was released): no quit");
        check(r.feedAll({down(kStart)}) == R::Continue && r.model->isDown(Button::Start),
              "Start held alone: no quit, and Start reaches the model");
    }
    {
        Rig r;
        r.feed(down(kMenu));
        check(r.feed(focusLost()) == R::Continue && r.feed(down(kStart)) == R::Continue,
              "focus lost between Menu down and Start down: no quit");
    }
    {
        Rig r;
        r.feed(down(kMenu));
        ButtonEvent none{};
        check(r.router.route(InputMap::Action::ReleaseAll, none, 1.0) == R::Continue && r.feed(down(kStart)) == R::Continue,
              "controller removed (ReleaseAll) between Menu down and Start down: no quit");
    }
    {
        Rig r;
        check(r.feed(down(kSelect)) == R::Continue && r.model->fine(), "Select reaches the model: FINE");
        check(r.feed(down(kStart)) == R::Continue && r.model->fine(), "Select + Start: no quit, still FINE");
        check(r.feedAll({up(kStart), up(kSelect)}) == R::Continue && !r.model->fine(), "released: FINE off, no quit");
    }
    {
        Rig r(true);
        check(r.feedAll({down(kMenu), down(kStart)}) == R::Continue && !r.quit.triggered(),
              "self-test mode: Menu + Start from real input does not quit");
        check(r.feed(down(kSelect)) == R::Continue && !r.model->fine() && !r.model->isDown(Button::Start),
              "self-test mode: real input does not reach the model");
        check(r.feed(escape()) == R::Quit, "self-test mode: Escape still quits");
    }
    {
        Rig r;
        r.input.setSwapAB(true);
        r.input.setSwapXY(true);
        check(r.feedAll({down(SDL_CONTROLLER_BUTTON_A), down(SDL_CONTROLLER_BUTTON_X)}) == R::Continue,
              "swaps on: face buttons do not quit");
        check(r.feed(down(kMenu)) == R::Continue && r.feed(down(kStart)) == R::MenuStart,
              "swaps on (--swap-ab --swap-xy): Menu + Start still quits");
    }
    {
        Rig r;
        check(r.feed(escape()) == R::Quit, "Escape: Quit (InputMap's quit passes through the router)");
    }

    // --- M2b-A: the INPUT tab ----------------------------------------------------------------------
    static_assert(noexcept(std::declval<InputRouter&>().route(InputMap::Action::None, ButtonEvent{}, 0.0)),
                  "route() is noexcept: the input panel catches at its own boundary");
    static_assert(noexcept(std::declval<InputRouter&>().deliver(ButtonEvent{}, 0.0)), "deliver() is noexcept");
    {
        InputMap                             input;
        auto                                 src   = std::make_unique<ButtonControlSource>();
        auto                                 model = std::make_unique<ControlModel>(*src);
        MenuQuit                             quit;
        InputStage                           stage;
        stage.prepare(256);
        InputController                      ctl(stage, ""); // no folder
        InputRouter                          router(*model, quit, false, &ctl);
        double                               t = 0.0;
        auto feed = [&](const SDL_Event& e) {
            ButtonEvent be{};
            t += 0.01;
            return router.route(input.handle(e, be), be, t);
        };
        const int kRight = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        const int kUp    = SDL_CONTROLLER_BUTTON_DPAD_UP;
        // On LOOP the d-pad moves frequency, not the panel.
        feed(down(kRight));
        feed(up(kRight));
        check(ctl.source() == InputSource::Off, "LOOP: d-pad right does not reach the input panel");
        std::vector<std::string> log;
        ctl.takeLog(log);
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // L1: LOOP -> INPUT
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        ctl.takeLog(log);
        bool scanned = false;
        for(const std::string& l : log)
            scanned = scanned || l.find("input: scan of") != std::string::npos;
        check(model->currentTab() == Tab::Input && scanned, "L1 from LOOP: INPUT, and entering it scans the folder");
        const std::uint32_t heldBefore = model->heldWord();
        feed(down(kRight));
        feed(up(kRight));
        check(ctl.source() == InputSource::File, "INPUT: d-pad right selects FILE");
        feed(down(kUp));
        ctl.tick(t); // the app ticks the panel every frame
        check(std::fabs(ctl.gainNorm() - (0.5f + 1.0f / 128.0f)) < 1e-6f && model->heldWord() == heldBefore,
              "INPUT: d-pad up steps the input gain; the model binds nothing there (held word unchanged)");
        t += 1.0;
        ctl.tick(t);
        const float ramped = ctl.gainNorm();
        ButtonEvent none{};
        router.route(InputMap::Action::ReleaseAll, none, t);
        t += 1.0;
        ctl.tick(t);
        check(ramped > 0.6f && ctl.gainNorm() == ramped, "a held d-pad ramps the gain; ReleaseAll stops it");
        // M2b-A3: X + Y, X + Select and X + d-pad on INPUT reach the panel (mode,
        // play again, the cursor) and send the model no command (no reset, no
        // step: INPUT binds no knob).
        {
            const std::uint64_t sent = model->commandsSent();
            const InputPlayMode before = ctl.playMode();
            router.deliver({Button::X, true}, t);
            router.deliver({Button::Y, true}, t);
            router.deliver({Button::DpadUp, true}, t); // Y + up with X held: the pop-up, not a gain reset
            router.deliver({Button::DpadUp, false}, t);
            router.deliver({Button::Y, false}, t);
            router.deliver({Button::Select, true}, t);
            router.deliver({Button::DpadRight, true}, t);
            router.deliver({Button::DpadRight, false}, t);
            router.deliver({Button::Select, false}, t);
            const bool popup = ctl.popupOpen();
            router.deliver({Button::X, false}, t);
            check(model->commandsSent() == sent && ctl.playMode() != before && popup && !ctl.popupOpen(),
                  "INPUT: X + Y toggles the play mode, X + up opens the pop-up, X + Select and X + right reach the "
                  "panel; the model sends no command (no reset, no step)");
            router.deliver({Button::X, true}, t);
            router.deliver({Button::Y, true}, t); // back to LOOP
            router.deliver({Button::Y, false}, t);
            router.deliver({Button::X, false}, t);
        }
        // deliver(): the self-test's path.
        router.deliver({Button::DpadLeft, true}, t);
        router.deliver({Button::DpadLeft, false}, t);
        check(ctl.source() == InputSource::Off, "deliver(): d-pad left on INPUT selects OFF");
        router.deliver({Button::R1, true}, t); // INPUT -> LOOP
        router.deliver({Button::R1, false}, t);
        router.deliver({Button::DpadRight, true}, t);
        check(model->currentTab() == Tab::Loop && ctl.source() == InputSource::Off,
              "R1 from INPUT wraps to LOOP; the d-pad there does not reach the panel");
        stage.setSource(InputSource::Off);
    }
    return ptest::finish();
}
