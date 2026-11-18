// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/ControlModel.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "control/ControlTuning.h"

namespace audreybrick
{
namespace
{
struct DpadInfo
{
    Axis axis;
    int  dir;
};

DpadInfo dpadInfo(Button b) noexcept
{
    switch(b)
    {
        case Button::DpadLeft: return {Axis::LeftRight, -1};
        case Button::DpadRight: return {Axis::LeftRight, +1};
        case Button::DpadDown: return {Axis::UpDown, -1};
        default: return {Axis::UpDown, +1}; // DpadUp
    }
}

Button opposite(Button b) noexcept
{
    switch(b)
    {
        case Button::DpadLeft: return Button::DpadRight;
        case Button::DpadRight: return Button::DpadLeft;
        case Button::DpadDown: return Button::DpadUp;
        case Button::DpadUp: return Button::DpadDown;
        case Button::L2: return Button::R2;
        default: return Button::L2; // R2
    }
}

bool isDpad(Button b) noexcept
{
    return b == Button::DpadUp || b == Button::DpadDown || b == Button::DpadLeft || b == Button::DpadRight;
}
} // namespace

ControlModel::ControlModel(ButtonControlSource& sink) noexcept : sink_(sink)
{
    sink_.setHeld(held_);
}

void ControlModel::send(const ControlCommand& c) noexcept
{
    ++sent_;
    if(!sink_.post(c))
        ++dropped_;
}

void ControlModel::makeDpadInert() noexcept
{
    for(Button b : {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight})
        if(press_[idx(b)].down)
            press_[idx(b)].inert = true;
}

void ControlModel::onDpad(Button b, double t) noexcept
{
    Press& p = press_[idx(b)];
    p.down   = true;
    p.inert  = false;
    p.time   = t;

    const DpadInfo info = dpadInfo(b);
    const Knob     knob = tabBinding(tab_, info.axis, xHeld());

    if(yHeld())
    {
        p.inert = true; // a reset press never ramps
        if(knob != kNoKnob)
            send(ControlCommand::reset(knob));
        return;
    }
    if(knob == kNoKnob)
        return;
    if(press_[idx(opposite(b))].down)
        return; // both directions down: no movement
    send(ControlCommand::step(knob, info.dir * tuning::tapStep(knob, fine())));
}

void ControlModel::onFeedbackButton(Button b, double t) noexcept
{
    Press& p = press_[idx(b)];
    p.down   = true;
    p.inert  = false;
    p.time   = t;
    if(press_[idx(opposite(b))].down)
        return; // both down: no movement
    const int dir = b == Button::R2 ? +1 : -1;
    send(ControlCommand::step(Knob::FeedbackGain, dir * tuning::tapStep(Knob::FeedbackGain, fine())));
}

void ControlModel::onButton(const ButtonEvent& e, double t) noexcept
{
    const std::size_t i = idx(e.button);
    if(i >= kButtonCount)
        return;
    Press& p = press_[i];

    if(!e.pressed)
    {
        if(!p.down)
            return;
        p = Press{};
        if(e.button == Button::X)
            makeDpadInert();
        publishHeld(t);
        return;
    }

    if(p.down)
        return; // repeated down
    if(isDpad(e.button))
    {
        onDpad(e.button, t);
    }
    else if(e.button == Button::L2 || e.button == Button::R2)
    {
        onFeedbackButton(e.button, t);
    }
    else
    {
        p.down  = true;
        p.inert = false;
        p.time  = t;
        switch(e.button)
        {
            case Button::L1:
            case Button::R1:
                tab_ = e.button == Button::R1 ? nextTab(tab_) : previousTab(tab_);
                makeDpadInert();
                break;
            case Button::X: makeDpadInert(); break;
            case Button::B:
                if(fine())
                {
                    send(ControlCommand::toggleSwitchLatch());
                    p.inert = true; // no momentary hold for this press
                }
                break;
            default: break; // A, Y, Select, Start: state only
        }
    }
    publishHeld(t);
}

void ControlModel::releaseAll(double t) noexcept
{
    for(Press& p : press_)
        p = Press{};
    publishHeld(t);
}

void ControlModel::tick(double t) noexcept
{
    publishHeld(t);
}

int ControlModel::rampDir(Button neg, Button pos, double t) const noexcept
{
    const Press& n = press_[idx(neg)];
    const Press& p = press_[idx(pos)];
    if(n.down == p.down)
        return 0; // neither, or both: no movement
    const Press& d = p.down ? p : n;
    if(d.inert || t - d.time < tuning::kHoldDelaySeconds - tuning::kTimeEpsilon)
        return 0;
    return p.down ? +1 : -1;
}

void ControlModel::publishHeld(double t) noexcept
{
    HeldState s;
    const int lr = rampDir(Button::DpadLeft, Button::DpadRight, t);
    const int ud = rampDir(Button::DpadDown, Button::DpadUp, t);
    for(int a = 0; a < 2; ++a)
    {
        const int  dir  = a == 0 ? lr : ud;
        const Knob knob = tabBinding(tab_, a == 0 ? Axis::LeftRight : Axis::UpDown, xHeld());
        if(dir != 0 && knob != kNoKnob)
        {
            s.axisKnob[a] = knob;
            s.axisDir[a]  = static_cast<std::int8_t>(dir);
        }
    }
    s.feedbackDir = static_cast<std::int8_t>(rampDir(Button::L2, Button::R2, t));
    s.fine        = fine();
    s.feedbackMax = feedbackMaxHeld();
    s.switchHeld  = switchHeld();

    held_ = packHeld(s);
    sink_.setHeld(held_);
}

} // namespace audreybrick
