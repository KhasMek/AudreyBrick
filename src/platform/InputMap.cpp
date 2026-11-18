// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "platform/InputMap.h"

#include <SDL.h>

#include <cstddef>
#include <string>

namespace audreybrick
{
namespace
{
enum class Source
{
    Key,               // code = SDL_Scancode
    ControllerButton,  // code = SDL_GameControllerButton
    ControllerTrigger, // code = SDL_GameControllerAxis
};

struct Binding
{
    Source source;
    int    code;
    bool   quit;   // true: this input quits the app (button ignored)
    Button button;
};

// Every binding in one table. Button names are SDL GameController names (A is the
// bottom face button in SDL's naming); which physical Brick button that is gets
// settled by the device checklist (M3).
constexpr Binding kBindings[] = {
    // Game controller buttons.
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_DPAD_UP, false, Button::DpadUp},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false, Button::DpadDown},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false, Button::DpadLeft},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false, Button::DpadRight},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_A, false, Button::A},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_B, false, Button::B},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_X, false, Button::X},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_Y, false, Button::Y},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false, Button::L1},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, false, Button::R1},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_BACK, false, Button::Select},
    {Source::ControllerButton, SDL_CONTROLLER_BUTTON_START, false, Button::Start},
    // Game controller triggers (axes, with hysteresis).
    {Source::ControllerTrigger, SDL_CONTROLLER_AXIS_TRIGGERLEFT, false, Button::L2},
    {Source::ControllerTrigger, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, false, Button::R2},
    // Keyboard, by scancode (physical position, layout-independent).
    {Source::Key, SDL_SCANCODE_UP, false, Button::DpadUp},
    {Source::Key, SDL_SCANCODE_DOWN, false, Button::DpadDown},
    {Source::Key, SDL_SCANCODE_LEFT, false, Button::DpadLeft},
    {Source::Key, SDL_SCANCODE_RIGHT, false, Button::DpadRight},
    {Source::Key, SDL_SCANCODE_Q, false, Button::L1},
    {Source::Key, SDL_SCANCODE_W, false, Button::R1},
    {Source::Key, SDL_SCANCODE_A, false, Button::L2},
    {Source::Key, SDL_SCANCODE_S, false, Button::R2},
    {Source::Key, SDL_SCANCODE_SPACE, false, Button::A},
    {Source::Key, SDL_SCANCODE_B, false, Button::B},
    {Source::Key, SDL_SCANCODE_X, false, Button::X},
    {Source::Key, SDL_SCANCODE_Y, false, Button::Y},
    {Source::Key, SDL_SCANCODE_LSHIFT, false, Button::Select},
    {Source::Key, SDL_SCANCODE_RETURN, false, Button::Start},
    {Source::Key, SDL_SCANCODE_ESCAPE, true, Button::Start},
};

const Binding* find(Source s, int code) noexcept
{
    for(const Binding& b : kBindings)
        if(b.source == s && b.code == code)
            return &b;
    return nullptr;
}

constexpr float kAxisFullScale = 32767.0f;
} // namespace

float InputMap::triggerPressThreshold() noexcept
{
    return kTriggerPressFraction * kAxisFullScale; // 16383.5: pressed from 16384 up
}

float InputMap::triggerReleaseThreshold() noexcept
{
    return kTriggerReleaseFraction * kAxisFullScale; // 9830.1: released from 9830 down
}

bool InputMap::openFirstController()
{
    if(controller_ != nullptr)
        return true;
    const int n = SDL_NumJoysticks();
    for(int i = 0; i < n; ++i)
    {
        if(!SDL_IsGameController(i))
            continue;
        SDL_GameController* c = SDL_GameControllerOpen(i);
        if(c == nullptr)
            continue;
        controller_     = c;
        instance_       = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
        const char* nm  = SDL_GameControllerName(c);
        controllerName_ = nm ? nm : "(unnamed controller)";
        triggerDown_[0] = triggerDown_[1] = false;
        return true;
    }
    return false;
}

void InputMap::closeController() noexcept
{
    if(controller_ != nullptr)
        SDL_GameControllerClose(controller_);
    controller_ = nullptr;
    instance_   = -1;
    controllerName_.clear();
    triggerDown_[0] = triggerDown_[1] = false;
}

InputMap::Action InputMap::onAxis(const SDL_ControllerAxisEvent& a, ButtonEvent& out) noexcept
{
    const Binding* b = find(Source::ControllerTrigger, a.axis);
    if(b == nullptr)
        return Action::None; // stick axes are not used
    const int i     = b->button == Button::L2 ? 0 : 1;
    const float value = static_cast<float>(a.value);
    if(!triggerDown_[i] && value > triggerPressThreshold())
    {
        triggerDown_[i] = true;
        out             = {b->button, true};
        return Action::Button;
    }
    if(triggerDown_[i] && value < triggerReleaseThreshold())
    {
        triggerDown_[i] = false;
        out             = {b->button, false};
        return Action::Button;
    }
    return Action::None;
}

InputMap::Action InputMap::handle(const SDL_Event& e, ButtonEvent& out)
{
    switch(e.type)
    {
        case SDL_QUIT: return Action::Quit;

        case SDL_WINDOWEVENT:
            if(e.window.event == SDL_WINDOWEVENT_CLOSE)
                return Action::Quit;
            if(e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                // The model forgets every held button, so the triggers must too:
                // otherwise the first trigger press after focus returns would be
                // swallowed as "already down".
                triggerDown_[0] = triggerDown_[1] = false;
                return Action::ReleaseAll;
            }
            return Action::None;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            if(e.key.repeat != 0)
                return Action::None;
            const Binding* b = find(Source::Key, e.key.keysym.scancode);
            if(b == nullptr)
                return Action::None;
            if(b->quit)
                return e.type == SDL_KEYDOWN ? Action::Quit : Action::None;
            out = {b->button, e.type == SDL_KEYDOWN};
            return Action::Button;
        }

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        {
            const Binding* b = find(Source::ControllerButton, e.cbutton.button);
            if(b == nullptr)
                return Action::None;
            out = {b->button, e.type == SDL_CONTROLLERBUTTONDOWN};
            return Action::Button;
        }

        case SDL_CONTROLLERAXISMOTION: return onAxis(e.caxis, out);

        case SDL_CONTROLLERDEVICEADDED:
            if(controller_ == nullptr)
                openFirstController();
            return Action::None;

        case SDL_CONTROLLERDEVICEREMOVED:
            if(controller_ != nullptr && e.cdevice.which == instance_)
            {
                closeController(); // also clears the trigger states
                openFirstController();
                triggerDown_[0] = triggerDown_[1] = false;
                return Action::ReleaseAll;
            }
            return Action::None;

        default: return Action::None;
    }
}

} // namespace audreybrick
