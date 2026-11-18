// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Criterion 3: InputMap maps each key and controller button to the right Button,
// the trigger axes press above 0.5 and release below 0.3 of full scale
// (hysteresis), key repeat is ignored, and unknown events are ignored. Events are
// built by hand; no window and no SDL_Init are needed for these.
//
// Then, if no real game controller is connected, a hot-plug test with an SDL
// virtual joystick (SDL_JoystickAttachVirtual, SDL 2.0.14): plugging it in opens
// it, its buttons and trigger arrive through the GameController API, and
// unplugging it asks for release-all. Skipped (and said so) if a real controller
// is present or the virtual controller is not recognised as a game controller.

#include <SDL.h>

#include <cstdio>
#include <string>
#include <utility>

#include "app/Buttons.h"
#include "platform/InputMap.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
SDL_Event key(Uint32 type, SDL_Scancode sc, Uint8 repeat = 0)
{
    SDL_Event e;
    SDL_zero(e);
    e.type                = type;
    e.key.type            = type;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym      = SDL_GetKeyFromScancode(sc);
    e.key.repeat          = repeat;
    e.key.state           = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    return e;
}

SDL_Event cbutton(Uint32 type, int b)
{
    SDL_Event e;
    SDL_zero(e);
    e.type           = type;
    e.cbutton.type   = type;
    e.cbutton.button = static_cast<Uint8>(b);
    e.cbutton.state  = type == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    return e;
}

SDL_Event caxis(int axis, int value)
{
    SDL_Event e;
    SDL_zero(e);
    e.type        = SDL_CONTROLLERAXISMOTION;
    e.caxis.type  = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis  = static_cast<Uint8>(axis);
    e.caxis.value = static_cast<Sint16>(value);
    return e;
}

SDL_Event window(Uint8 ev)
{
    SDL_Event e;
    SDL_zero(e);
    e.type         = SDL_WINDOWEVENT;
    e.window.type  = SDL_WINDOWEVENT;
    e.window.event = ev;
    return e;
}

bool isButton(InputMap& m, const SDL_Event& e, Button b, bool pressed)
{
    ButtonEvent out{Button::Start, !pressed};
    return m.handle(e, out) == InputMap::Action::Button && out.button == b && out.pressed == pressed;
}

bool isNone(InputMap& m, const SDL_Event& e)
{
    ButtonEvent out{};
    return m.handle(e, out) == InputMap::Action::None;
}

void virtualControllerTest()
{
    if(SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
    {
        std::printf("(skipped) virtual controller test: SDL_Init failed: %s\n", SDL_GetError());
        return;
    }
    if(SDL_NumJoysticks() > 0)
    {
        std::printf("(skipped) virtual controller test: a real joystick or controller is connected\n");
        SDL_Quit();
        return;
    }
    InputMap  m;
    ButtonEvent out{};
    check(!m.openFirstController() && !m.controllerConnected(), "no controller at start");

    const int idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                              SDL_CONTROLLER_BUTTON_MAX, 0);
    if(idx < 0 || !SDL_IsGameController(idx))
    {
        std::printf("(skipped) virtual controller test: virtual joystick not usable as a game controller (%s)\n",
                    idx < 0 ? SDL_GetError() : "no mapping");
        if(idx >= 0)
            SDL_JoystickDetachVirtual(idx);
        SDL_Quit();
        return;
    }

    // Hot-plug add: the ADDED event opens it.
    SDL_Event e;
    bool      added = false;
    SDL_PumpEvents();
    while(SDL_PollEvent(&e))
        if(e.type == SDL_CONTROLLERDEVICEADDED)
        {
            added = true;
            m.handle(e, out);
        }
    check(added && m.controllerConnected(), "hot-plug: the added controller is opened");
    std::printf("virtual controller name: %s\n", m.controllerName().c_str());

    SDL_Joystick* js = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(idx));
    auto collect = [&](Button want, bool pressed) {
        bool got = false;
        SDL_PumpEvents();
        while(SDL_PollEvent(&e))
        {
            ButtonEvent b{};
            if(m.handle(e, b) == InputMap::Action::Button && b.button == want && b.pressed == pressed)
                got = true;
        }
        return got;
    };
    // The virtual joystick's raw button/axis numbers equal the GameController
    // enums under SDL's default virtual mapping; the app never sees these numbers.
    SDL_JoystickSetVirtualButton(js, SDL_CONTROLLER_BUTTON_A, 1);
    SDL_JoystickUpdate();
    check(collect(Button::A, true), "virtual controller: A down arrives as Button::A");
    SDL_JoystickSetVirtualButton(js, SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickUpdate();
    check(collect(Button::A, false), "virtual controller: A up");
    SDL_JoystickSetVirtualAxis(js, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000);
    SDL_JoystickUpdate();
    check(collect(Button::R2, true), "virtual controller: right trigger pressed = R2 down");

    // Unplug: release-all.
    SDL_JoystickDetachVirtual(idx);
    bool releaseAll = false;
    SDL_PumpEvents();
    while(SDL_PollEvent(&e))
    {
        ButtonEvent b{};
        if(m.handle(e, b) == InputMap::Action::ReleaseAll)
            releaseAll = true;
    }
    check(releaseAll && !m.controllerConnected(), "hot-plug: removing the active controller asks for release-all");
    m.closeController();
    SDL_Quit();
}
} // namespace

int main()
{
    InputMap m;

    // Keyboard, by scancode.
    const struct
    {
        SDL_Scancode sc;
        Button       b;
    } keys[] = {
        {SDL_SCANCODE_UP, Button::DpadUp},     {SDL_SCANCODE_DOWN, Button::DpadDown}, {SDL_SCANCODE_LEFT, Button::DpadLeft},
        {SDL_SCANCODE_RIGHT, Button::DpadRight}, {SDL_SCANCODE_Q, Button::L1},       {SDL_SCANCODE_W, Button::R1},
        {SDL_SCANCODE_A, Button::L2},           {SDL_SCANCODE_S, Button::R2},         {SDL_SCANCODE_SPACE, Button::A},
        {SDL_SCANCODE_B, Button::B},            {SDL_SCANCODE_X, Button::X},          {SDL_SCANCODE_Y, Button::Y},
        {SDL_SCANCODE_LSHIFT, Button::Select},  {SDL_SCANCODE_RETURN, Button::Start},
    };
    for(const auto& k : keys)
    {
        const std::string name = std::string(SDL_GetScancodeName(k.sc)) + " -> " + buttonName(k.b);
        check(isButton(m, key(SDL_KEYDOWN, k.sc), k.b, true), "key down " + name);
        check(isButton(m, key(SDL_KEYUP, k.sc), k.b, false), "key up " + name);
        check(isNone(m, key(SDL_KEYDOWN, k.sc, 1)), "key repeat ignored: " + name);
    }
    {
        ButtonEvent out{};
        check(m.handle(key(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE), out) == InputMap::Action::Quit, "Escape down quits");
        check(m.handle(key(SDL_KEYUP, SDL_SCANCODE_ESCAPE), out) == InputMap::Action::None, "Escape up does nothing");
        check(m.handle(key(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, 1), out) == InputMap::Action::None,
              "Escape repeat does nothing");
    }
    check(isNone(m, key(SDL_KEYDOWN, SDL_SCANCODE_Z)), "unbound key Z ignored");
    check(isNone(m, key(SDL_KEYDOWN, SDL_SCANCODE_RSHIFT)), "Right Shift is not Select");
    check(isNone(m, key(SDL_KEYDOWN, SDL_SCANCODE_KP_ENTER)), "keypad Enter is not Start");

    // Controller buttons.
    const struct
    {
        int    cb;
        Button b;
    } pads[] = {
        {SDL_CONTROLLER_BUTTON_DPAD_UP, Button::DpadUp},     {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Button::DpadDown},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Button::DpadLeft}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Button::DpadRight},
        {SDL_CONTROLLER_BUTTON_A, Button::A},                {SDL_CONTROLLER_BUTTON_B, Button::B},
        {SDL_CONTROLLER_BUTTON_X, Button::X},                {SDL_CONTROLLER_BUTTON_Y, Button::Y},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Button::L1},    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Button::R1},
        {SDL_CONTROLLER_BUTTON_BACK, Button::Select},        {SDL_CONTROLLER_BUTTON_START, Button::Start},
    };
    for(const auto& p : pads)
    {
        const std::string name = std::string(SDL_GameControllerGetStringForButton(
                                     static_cast<SDL_GameControllerButton>(p.cb))) +
                                 " -> " + buttonName(p.b);
        check(isButton(m, cbutton(SDL_CONTROLLERBUTTONDOWN, p.cb), p.b, true), "controller down " + name);
        check(isButton(m, cbutton(SDL_CONTROLLERBUTTONUP, p.cb), p.b, false), "controller up " + name);
    }
    check(isNone(m, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE)), "Guide ignored");

    // --swap-ab (M3-B): controller 'a' -> B and 'b' -> A, presses and releases;
    // nothing else changes (X/Y, shoulders, d-pad, Start/Select, triggers, keyboard).
    {
        InputMap s;
        check(!s.swapAB() && !s.swapsControllerButton(SDL_CONTROLLER_BUTTON_A), "swap off by default");
        s.setSwapAB(true);
        check(s.swapAB(), "setSwapAB(true)");
        check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A), Button::B, true) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_A), Button::B, false),
              "swap on: controller 'a' down/up -> B down/up");
        check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B), Button::A, true) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B), Button::A, false),
              "swap on: controller 'b' down/up -> A down/up");
        check(s.swapsControllerButton(SDL_CONTROLLER_BUTTON_A) && s.swapsControllerButton(SDL_CONTROLLER_BUTTON_B) &&
                  !s.swapsControllerButton(SDL_CONTROLLER_BUTTON_X) && !s.swapsControllerButton(SDL_CONTROLLER_BUTTON_GUIDE),
              "swapsControllerButton: only 'a' and 'b'");
        check(std::string(s.swapNote(SDL_CONTROLLER_BUTTON_A)) == "[A/B swapped]" &&
                  std::string(s.swapNote(SDL_CONTROLLER_BUTTON_Y)).empty(),
              "swapNote: [A/B swapped] for 'a', nothing for 'y'");
        for(const auto& p : pads)
        {
            if(p.cb == SDL_CONTROLLER_BUTTON_A || p.cb == SDL_CONTROLLER_BUTTON_B)
                continue;
            const std::string name = std::string(SDL_GameControllerGetStringForButton(
                                         static_cast<SDL_GameControllerButton>(p.cb))) +
                                     " -> " + buttonName(p.b);
            check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, p.cb), p.b, true) &&
                      isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, p.cb), p.b, false),
                  "swap on, unchanged: controller " + name);
        }
        check(isButton(s, caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000), Button::L2, true) &&
                  isButton(s, caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0), Button::L2, false) &&
                  isButton(s, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000), Button::R2, true) &&
                  isButton(s, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0), Button::R2, false),
              "swap on, unchanged: triggers L2/R2");
        for(const auto& k : keys)
        {
            const std::string name = std::string(SDL_GetScancodeName(k.sc)) + " -> " + buttonName(k.b);
            check(isButton(s, key(SDL_KEYDOWN, k.sc), k.b, true) && isButton(s, key(SDL_KEYUP, k.sc), k.b, false),
                  "swap on, keyboard unchanged: " + name);
        }
        check(isNone(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE)), "swap on: Guide still ignored");
        s.setSwapAB(false);
        check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A), Button::A, true) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_A), Button::A, false) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B), Button::B, true) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B), Button::B, false),
              "swap off again: 'a' -> A and 'b' -> B, press and release");
    }
    // --swap-xy (M3-B review): 'x' -> Y and 'y' -> X; the mirror of the A/B tests.
    {
        InputMap s;
        check(!s.swapXY(), "X/Y swap off by default");
        s.setSwapXY(true);
        check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_X), Button::Y, true) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_X), Button::Y, false),
              "X/Y swap on: controller 'x' down/up -> Y down/up");
        check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_Y), Button::X, true) &&
                  isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_Y), Button::X, false),
              "X/Y swap on: controller 'y' down/up -> X down/up");
        check(std::string(s.swapNote(SDL_CONTROLLER_BUTTON_X)) == "[X/Y swapped]" &&
                  std::string(s.swapNote(SDL_CONTROLLER_BUTTON_A)).empty(),
              "swapNote: [X/Y swapped] for 'x'; 'a' not swapped by --swap-xy alone");
        for(const auto& p : pads)
        {
            if(p.cb == SDL_CONTROLLER_BUTTON_X || p.cb == SDL_CONTROLLER_BUTTON_Y)
                continue;
            const std::string name = std::string(SDL_GameControllerGetStringForButton(
                                         static_cast<SDL_GameControllerButton>(p.cb))) +
                                     " -> " + buttonName(p.b);
            check(isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, p.cb), p.b, true) &&
                      isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, p.cb), p.b, false),
                  "X/Y swap on, unchanged: controller " + name);
        }
        for(const auto& k : keys)
            check(isButton(s, key(SDL_KEYDOWN, k.sc), k.b, true) && isButton(s, key(SDL_KEYUP, k.sc), k.b, false),
                  std::string("X/Y swap on, keyboard unchanged: ") + SDL_GetScancodeName(k.sc));
        check(isButton(s, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000), Button::R2, true) &&
                  isButton(s, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0), Button::R2, false),
              "X/Y swap on, unchanged: trigger R2");

        // Both swaps on (the launcher's default on the Brick).
        s.setSwapAB(true);
        const struct
        {
            int    cb;
            Button b;
        } both[] = {{SDL_CONTROLLER_BUTTON_A, Button::B}, {SDL_CONTROLLER_BUTTON_B, Button::A},
                    {SDL_CONTROLLER_BUTTON_X, Button::Y}, {SDL_CONTROLLER_BUTTON_Y, Button::X},
                    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Button::L1}, {SDL_CONTROLLER_BUTTON_BACK, Button::Select},
                    {SDL_CONTROLLER_BUTTON_START, Button::Start}, {SDL_CONTROLLER_BUTTON_DPAD_UP, Button::DpadUp}};
        bool ok = true;
        for(const auto& p : both)
            ok = ok && isButton(s, cbutton(SDL_CONTROLLERBUTTONDOWN, p.cb), p.b, true) &&
                 isButton(s, cbutton(SDL_CONTROLLERBUTTONUP, p.cb), p.b, false);
        check(ok, "both swaps on: a->B, b->A, x->Y, y->X; shoulders, Select, Start, d-pad unchanged");
        check(isButton(s, key(SDL_KEYDOWN, SDL_SCANCODE_SPACE), Button::A, true) &&
                  isButton(s, key(SDL_KEYDOWN, SDL_SCANCODE_B), Button::B, true) &&
                  isButton(s, key(SDL_KEYDOWN, SDL_SCANCODE_X), Button::X, true) &&
                  isButton(s, key(SDL_KEYDOWN, SDL_SCANCODE_Y), Button::Y, true),
              "both swaps on: keyboard Space/B/X/Y unchanged");
    }
    check(isNone(m, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_LEFTSTICK)), "stick click ignored");
    check(isNone(m, cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_MISC1)), "Misc1 ignored");

    // Trigger axes with hysteresis.
    std::printf("trigger thresholds: press above %.1f, release below %.1f\n",
                static_cast<double>(InputMap::triggerPressThreshold()),
                static_cast<double>(InputMap::triggerReleaseThreshold()));
    for(const auto& t : {std::pair<int, Button>{SDL_CONTROLLER_AXIS_TRIGGERLEFT, Button::L2},
                         std::pair<int, Button>{SDL_CONTROLLER_AXIS_TRIGGERRIGHT, Button::R2}})
    {
        const std::string n = buttonName(t.second);
        check(isNone(m, caxis(t.first, 0)), n + ": 0 = nothing");
        check(isNone(m, caxis(t.first, 16383)), n + ": 16383 (just below 0.5) = nothing");
        check(isButton(m, caxis(t.first, 16384), t.second, true), n + ": 16384 (above 0.5) = down");
        check(isNone(m, caxis(t.first, 32767)), n + ": further travel while down = nothing");
        check(isNone(m, caxis(t.first, 12000)), n + ": 12000 (between 0.3 and 0.5) = still down, nothing");
        check(isNone(m, caxis(t.first, 9831)), n + ": 9831 (just above 0.3) = nothing");
        check(isButton(m, caxis(t.first, 9830), t.second, false), n + ": 9830 (below 0.3) = up");
        check(isNone(m, caxis(t.first, 12000)), n + ": 12000 while up = nothing (hysteresis)");
        check(isNone(m, caxis(t.first, 0)), n + ": 0 while up = nothing");
    }
    // Release-all clears the trigger states: after focus returns, the first press
    // of a trigger that was down when focus was lost is reported.
    {
        ButtonEvent out{};
        check(isButton(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 30000), Button::R2, true), "R2 pressed");
        check(isButton(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000), Button::L2, true), "L2 pressed");
        check(m.handle(window(SDL_WINDOWEVENT_FOCUS_LOST), out) == InputMap::Action::ReleaseAll,
              "focus lost while both triggers are down: release-all");
        check(isButton(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 31000), Button::R2, true),
              "after release-all, R2 above the threshold is reported as a press again");
        check(isButton(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 31000), Button::L2, true),
              "after release-all, L2 above the threshold is reported as a press again");
        check(isButton(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0), Button::R2, false), "R2 released");
        check(isButton(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0), Button::L2, false), "L2 released");
        check(m.handle(window(SDL_WINDOWEVENT_FOCUS_LOST), out) == InputMap::Action::ReleaseAll,
              "focus lost with the triggers up: release-all");
        check(isNone(m, caxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 5000)), "and a light touch afterwards is nothing");
    }
    check(isNone(m, caxis(SDL_CONTROLLER_AXIS_LEFTX, 32767)), "left stick X ignored");
    check(isNone(m, caxis(SDL_CONTROLLER_AXIS_RIGHTY, -32768)), "right stick Y ignored");

    // Window and app events.
    {
        ButtonEvent out{};
        SDL_Event   q;
        SDL_zero(q);
        q.type = SDL_QUIT;
        check(m.handle(q, out) == InputMap::Action::Quit, "SDL_QUIT quits");
        check(m.handle(window(SDL_WINDOWEVENT_CLOSE), out) == InputMap::Action::Quit, "window close quits");
        check(m.handle(window(SDL_WINDOWEVENT_FOCUS_LOST), out) == InputMap::Action::ReleaseAll,
              "focus lost releases all");
        check(m.handle(window(SDL_WINDOWEVENT_FOCUS_GAINED), out) == InputMap::Action::None, "focus gained ignored");
        SDL_Event mm;
        SDL_zero(mm);
        mm.type = SDL_MOUSEMOTION;
        check(m.handle(mm, out) == InputMap::Action::None, "mouse motion ignored");
        SDL_Event rm;
        SDL_zero(rm);
        rm.type          = SDL_CONTROLLERDEVICEREMOVED;
        rm.cdevice.which = 1234;
        check(m.handle(rm, out) == InputMap::Action::None, "removal of a controller that is not open is ignored");
        SDL_Event jb;
        SDL_zero(jb);
        jb.type = SDL_JOYBUTTONDOWN; // raw joystick events are never used
        check(m.handle(jb, out) == InputMap::Action::None, "raw joystick button ignored");
    }

    virtualControllerTest();
    return ptest::finish();
}
