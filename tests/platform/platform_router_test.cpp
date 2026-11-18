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
//   - with the A/B and X/Y swaps on, Menu + Start still quits.
// No SDL_Init: InputMap::handle needs none for these events.

#include <SDL.h>

#include <initializer_list>
#include <memory>
#include <string>

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/InputRouter.h"
#include "app/MenuQuit.h"
#include "control/ButtonControlSource.h"
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
    return ptest::finish();
}
