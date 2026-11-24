// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/InputRouter.h"

namespace audreybrick
{

void InputRouter::deliver(const ButtonEvent& be, double t) noexcept
{
    const Tab before = model_.currentTab();
    model_.onButton(be, t);
    if(input_ != nullptr)
    {
        const bool onInput = model_.currentTab() == Tab::Input;
        input_->onButton(be, t, onInput);
        if(onInput && before != Tab::Input)
            input_->onTabEntered();
    }
    if(mod_ != nullptr)
        mod_->onButton(be, t, model_.currentTab() == Tab::Mod); // MOD-B
    if(preset_ != nullptr)
    {
        // PRESET-B
        const bool onPreset = model_.currentTab() == Tab::Preset;
        preset_->onButton(be, t, onPreset);
        if(onPreset && before != Tab::Preset)
            preset_->onTabEntered();
    }
}

void InputRouter::presetChordAndDeliver(const ButtonEvent& be, double t) noexcept
{
    if(presetChord_ == nullptr)
    {
        deliver(be, t);
        return;
    }
    const RecordChord::Out o = presetChord_->onButton(be);
    if(o.releaseY)
        deliver(ButtonEvent{presetChord_->button(), false}, t); // MAX ends (A was delivered as a press)
    if(o.deliver)
        deliver(be, t);
    if(o.toggle && preset_ != nullptr)
        preset_->requestSaveNew(PresetPanel::Origin::Chord); // Start + A: on any tab
}

bool InputRouter::chordAndDeliver(const ButtonEvent& be, double t) noexcept
{
    if(record_ == nullptr)
    {
        presetChordAndDeliver(be, t);
        return false;
    }
    const RecordChord::Out o = record_->onButton(be);
    if(o.releaseY)
        presetChordAndDeliver(ButtonEvent{record_->button(), false}, t);
    if(o.deliver)
        presetChordAndDeliver(be, t);
    return o.toggle;
}

InputRouter::Result InputRouter::routeScripted(const ButtonEvent& be, double t) noexcept
{
    return chordAndDeliver(be, t) ? Result::RecordToggle : Result::Continue;
}

InputRouter::Result InputRouter::route(InputMap::Action action, const ButtonEvent& be, double t) noexcept
{
    if(action == InputMap::Action::Quit)
        return Result::Quit;
    if(selftest_)
        return Result::Continue; // self-test: real input is ignored
    bool toggled = false;
    switch(action)
    {
        case InputMap::Action::Button:
            toggled = chordAndDeliver(be, t); // REC: Start + Y first
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
            if(mod_ != nullptr)
                mod_->releaseAll();
            if(preset_ != nullptr)
                preset_->releaseAll(); // PRESET-B
            if(presetChord_ != nullptr)
                presetChord_->releaseAll();
            quit_.releaseAll();
            if(record_ != nullptr)
                record_->releaseAll();
            break;
        case InputMap::Action::None:
        case InputMap::Action::Quit: break;
    }
    if(quit_.triggered())
        return Result::MenuStart;
    return toggled ? Result::RecordToggle : Result::Continue;
}

} // namespace audreybrick
