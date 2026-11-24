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
// REC (docs/briefs/REC.md): with a RecordChord attached, every button event goes
// through it first (app/RecordChord.h): it may hold back a Y event or ask for a
// Y up to be delivered first, and it reports Start + Y as Result::RecordToggle
// (MenuStart wins if both happen on one event: the app quits, which finishes
// the recording). routeScripted() is the self-test's path: the chord, then
// deliver(), no MenuQuit. Action::ReleaseAll also resets the chord.
//
// deliver() (M2b-A): one button event to the model and then to the input panel,
// with whether the INPUT tab is current after the model has seen it (so the tab
// L1/R1 lands on counts); when the event made INPUT current, the panel is told
// (it rescans the input folder). The self-test's scripted events use deliver()
// directly (real input is ignored there, as before). The model never sees the
// panel: on INPUT it binds no knob, so its d-pad, X and Y do nothing there.
//
// MOD-B: with a ModPanel attached, deliver() passes every event to it last,
// with whether the MOD tab is current after the model has seen it; ReleaseAll
// reaches it too. Like INPUT, MOD binds no knob in the model.
//
// PRESET-B: with a PresetPanel attached, deliver() passes every event to it
// last (after the mod panel), with whether the PRESET tab is current after the
// model has seen it; when the event made PRESET current the panel is told (it
// rescans the presets folder). With a second chord attached (Start + A, a
// RecordChord built with Button::A), every button event goes through the Start
// + Y chord first and then through the Start + A chord, each of which may hold
// back its own button's events or ask for an up of it to be delivered first
// (an A up asked for by the A chord goes to deliver(); a Y up asked for by the
// Y chord goes through the A chord, which passes it on). When the A chord fires
// the router calls PresetPanel::requestSaveNew() (Origin::Chord) at once, on
// any tab, in self-test mode through routeScripted() as well. MenuStart and
// RecordToggle are reported as before; both chords can fire on one Start
// press (Start + Y + A). ReleaseAll resets both chords and the preset panel.
//
// UI thread only. No allocation (apart from what the input panel does when it
// rescans or logs, M2b-A, and the preset panel's rescans and log lines,
// PRESET-B). Nothing throws: everything route() and deliver() call is noexcept
// (the panels catch at their own boundaries).

#pragma once

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/MenuQuit.h"
#include "app/ModPanel.h"
#include "app/PresetPanel.h"
#include "app/RecordChord.h"
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
        RecordToggle, // REC: Start + Y: start or stop recording
    };

    // `input`: the INPUT tab's panel (M2b-A), or nullptr for none. `record`: the
    // Start + Y chord (REC), or nullptr for none. `mod`: the MOD tab's panel
    // (MOD-B), or nullptr for none.
    // PRESET-B: `presetChord`: the Start + A chord (a RecordChord built with
    // Button::A), or nullptr; `preset`: the PRESET tab's panel, or nullptr.
    InputRouter(ControlModel& model, MenuQuit& quit, bool selftest, InputController* input = nullptr,
                RecordChord* record = nullptr, ModPanel* mod = nullptr, RecordChord* presetChord = nullptr,
                PresetPanel* preset = nullptr) noexcept
        : model_(model), quit_(quit), selftest_(selftest), input_(input), record_(record), mod_(mod),
          presetChord_(presetChord), preset_(preset)
    {
    }

    Result route(InputMap::Action action, const ButtonEvent& be, double t) noexcept;
    // One button event to the model, the input panel, the mod panel and the
    // preset panel (not to MenuQuit, not through the chords).
    void deliver(const ButtonEvent& be, double t) noexcept;
    // REC: a scripted (self-test) button event: the chord, then deliver().
    // Returns RecordToggle or Continue.
    Result routeScripted(const ButtonEvent& be, double t) noexcept;

  private:
    ControlModel&    model_;
    MenuQuit&        quit_;
    bool             selftest_;
    InputController* input_;
    RecordChord*     record_;
    ModPanel*        mod_;
    RecordChord*     presetChord_;
    PresetPanel*     preset_;

    // The chords and deliver(); true if the Start + Y chord fired (the Start +
    // A chord asks the preset panel for a save itself).
    bool chordAndDeliver(const ButtonEvent& be, double t) noexcept;
    // The Start + A chord, then deliver().
    void presetChordAndDeliver(const ButtonEvent& be, double t) noexcept;
};

} // namespace audreybrick
