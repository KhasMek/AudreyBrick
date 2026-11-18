// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputMap: SDL events to Button events (docs/briefs/M2.md, M2-B).
//
// Sources, all in one binding table (InputMap.cpp, kBindings):
//   - Game controllers, through the SDL GameController API only (never raw joystick
//     indices): buttons by SDL_GameControllerButton, the triggers L2/R2 by
//     SDL_GameControllerAxis. A trigger is pressed when its axis rises above 0.5 of
//     full scale and released when it falls below 0.3 (hysteresis).
//   - The keyboard, for development: arrows = d-pad; Q / W = L1 / R1; A / S = L2 /
//     R2; Space = A; B, X, Y = B, X, Y; Left Shift = Select; Return = Start;
//     Escape = quit; Alt+F4 = quit (M3: KNULLI's exit hotkey for shell ports sends
//     Left Alt + F4 as key events; F4 without Alt does nothing). Keys are matched
//     by SDL scancode (physical key position), so
//     the binding does not depend on the keyboard layout or language: on a French
//     AZERTY keyboard "Q" (L1) is the key labelled A, and so on. Key repeat is
//     ignored.
//
// A/B and X/Y swaps (M3-B, --swap-ab, --swap-xy): KNULLI's default port mapping
// names the Brick's buttons by position, Xbox style, while the Brick is labelled
// Nintendo style, so the button labelled A arrives as SDL 'b', B as 'a', and
// (expected from the firmware's mapping, to be confirmed on the device) X as 'y'
// and Y as 'x'. With setSwapAB(true), SDL controller button 'a' produces Button::B
// and 'b' produces Button::A; with setSwapXY(true), 'x' produces Button::Y and 'y'
// produces Button::X; for presses and releases alike. Nothing else changes: not
// the shoulders, triggers, d-pad, Start/Select, and not the keyboard binding. The
// swaps are two flags applied in one place (controllerButton() in InputMap.cpp).
// Changing them while a swapped button is held would pair a press with the other
// button's release; the app sets them once, before the first event.
//
// One controller is active at a time: the first one found at start, or the first
// one plugged in while none is active. If the active controller is removed, the
// next available one is opened, and the caller is told to release all buttons.
//
// Other events that matter to the app are reported too: quit (window close,
// SDL_QUIT, Escape, Alt+F4) and release-all (window focus lost, active controller
// removed).
// Whenever release-all is returned, the trigger pressed states are cleared as
// well, so the next trigger press above the threshold is reported as a press.

#pragma once

#include <SDL.h>

#include <string>

#include "app/Buttons.h"

namespace audreybrick
{

class InputMap
{
  public:
    // Full-scale fractions for the trigger axes.
    static constexpr float kTriggerPressFraction   = 0.5f;
    static constexpr float kTriggerReleaseFraction = 0.3f;

    enum class Action
    {
        None,       // nothing for the app (unknown or ignored event)
        Button,     // `out` holds a button event
        Quit,       // window close, SDL_QUIT, Escape or Alt+F4
        ReleaseAll, // focus lost or the active controller was removed: release every button
    };

    InputMap() = default;
    ~InputMap() { closeController(); }
    InputMap(const InputMap&)            = delete;
    InputMap& operator=(const InputMap&) = delete;

    // Opens the first available game controller, if any. Needs
    // SDL_INIT_GAMECONTROLLER. Returns true if one is open afterwards.
    bool openFirstController();
    void closeController() noexcept;

    // Translates one event. Controller hot-plug events open or close controllers
    // as a side effect (SDL only delivers controller button and axis events for
    // controllers that are open, and only one is opened). Never blocks.
    Action handle(const SDL_Event& e, ButtonEvent& out);

    // --swap-ab, --swap-xy: see above. Off by default (SDL's own naming).
    void setSwapAB(bool on) noexcept { swapAB_ = on; }
    bool swapAB() const noexcept { return swapAB_; }
    void setSwapXY(bool on) noexcept { swapXY_ = on; }
    bool swapXY() const noexcept { return swapXY_; }
    // For the diagnostic log: "[A/B swapped]" or "[X/Y swapped]" if `sdlButton`
    // (an SDL_GameControllerButton) produces a Button other than the one of the
    // same name because of a swap; "" otherwise.
    const char* swapNote(int sdlButton) const noexcept;
    bool        swapsControllerButton(int sdlButton) const noexcept { return swapNote(sdlButton)[0] != '\0'; }

    bool               controllerConnected() const noexcept { return controller_ != nullptr; }
    const std::string& controllerName() const noexcept { return controllerName_; }

    // The trigger thresholds in raw SDL axis units (0..32767): pressed when the
    // value is above the first, released when it is below the second.
    static float triggerPressThreshold() noexcept;
    static float triggerReleaseThreshold() noexcept;

    // True if the keyboard binding uses this scancode (a button or Escape). F4 is
    // not: only Alt+F4 quits, so F4 alone (or its release) is reported by the
    // diagnostic log like any other unmapped key.
    static bool mapsScancode(int scancode) noexcept;

  private:
    Action onAxis(const SDL_ControllerAxisEvent& a, ButtonEvent& out) noexcept;

    SDL_GameController* controller_ = nullptr;
    SDL_JoystickID      instance_   = -1;
    std::string         controllerName_;
    bool                triggerDown_[2] = {false, false}; // L2, R2
    bool                swapAB_         = false;
    bool                swapXY_         = false;
};

} // namespace audreybrick
