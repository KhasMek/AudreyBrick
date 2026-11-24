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
//   - REC: Start + Y (both orders, pad and keyboard) returns RecordToggle once
//     per chord; the chord's Y is no reset modifier (the model never sees it as
//     held) and, with X held on INPUT, does not toggle LOOP/ONCE; plain Y is
//     unchanged; Menu + Start still quits in both orders with Y held; Select +
//     Start + Y counts as Start + Y; self-test mode ignores real Start + Y while
//     routeScripted() (the self-test's path) applies the chord.
//   - PRESET-B: Start + A (both orders) asks the preset panel for a save once
//     per chord on any tab; the chord's A never reaches the model as MAX when
//     Start came first, and is released for the model when it came first;
//     Start + Y + A fires both chords; plain A is MAX; L1 from LOOP is PRESET,
//     where right reaches the preset panel and the model binds nothing;
//     routeScripted() applies the chord; ReleaseAll forgets it.
//   - PRESET-B review: Start + A while INPUT's file pop-up is open (X held):
//     the preset is saved, the pop-up stays open and the chord's A reaches
//     neither the model nor the INPUT panel.
// No SDL_Init: InputMap::handle needs none for these events.

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/InputRouter.h"
#include "app/MenuQuit.h"
#include "app/ModPanel.h"
#include "app/PresetPanel.h"
#include "app/RecordChord.h"
#include "control/ButtonControlSource.h"
#include "input/InputController.h"
#include "input/InputStage.h"
#include "platform/InputMap.h"
#include "platform_test_common.h"
#include "preset/PresetFs.h"
#include "preset/PresetStore.h"

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
SDL_Event key(Uint32 type, SDL_Scancode sc)
{
    SDL_Event e{};
    e.type                = type;
    e.key.type            = type;
    e.key.state           = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = sc;
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
    RecordChord                          chord; // REC
    InputRouter                          router;
    double                               t = 0.0;

    explicit Rig(bool selftest = false) : router(*model, quit, selftest, nullptr, &chord) {}

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
constexpr int kY      = SDL_CONTROLLER_BUTTON_Y;
constexpr int kX      = SDL_CONTROLLER_BUTTON_X;
constexpr int kUpPad  = SDL_CONTROLLER_BUTTON_DPAD_UP;
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

    // --- REC: Start + Y ------------------------------------------------------------------------------
    {
        Rig r;
        check(r.feed(down(kStart)) == R::Continue, "REC: Start down alone: nothing");
        check(r.feed(down(kY)) == R::RecordToggle, "REC: Start held, Y down: RecordToggle");
        check(!r.model->yHeld() && !r.model->isDown(Button::Y), "REC: the chord's Y never reaches the model (not held there)");
        const std::uint64_t sent = r.model->commandsSent();
        check(r.feed(down(kUpPad)) == R::Continue && r.model->commandsSent() == sent + 1 && r.model->heldWord() == 0,
              "REC: d-pad up with the chord's Y still held: a plain step (one command), not a reset");
        r.feed(up(kUpPad));
        check(r.feed(up(kY)) == R::Continue && r.feed(up(kStart)) == R::Continue, "REC: releases: nothing");
        check(r.feed(down(kY)) == R::Continue && r.model->yHeld(), "REC: Y pressed again alone: plain Y (held in the model)");
        r.feed(up(kY));
    }
    {
        Rig r;
        check(r.feed(down(kY)) == R::Continue && r.model->yHeld(), "REC: Y down first: a plain Y press");
        check(r.feed(down(kStart)) == R::RecordToggle, "REC: Y held, Start down: RecordToggle");
        check(!r.model->yHeld(), "REC: Y + Start: the model is told Y is up (no reset modifier from now on)");
        check(r.feed(up(kStart)) == R::Continue && r.feed(down(kStart)) == R::RecordToggle,
              "REC: Start released and pressed again with Y held: fires again (a new chord)");
        check(r.feed(up(kStart)) == R::Continue && r.feed(up(kY)) == R::Continue && !r.model->yHeld(),
              "REC: Y released: held back (the model already had its up)");
        check(r.feed(down(kY)) == R::Continue && r.model->yHeld(), "REC: Y pressed again without Start: a reset modifier again");
        r.feed(up(kY));
    }
    {
        Rig r;
        r.feed(down(kStart));
        int toggles = 0;
        for(int i = 0; i < 3; ++i)
        {
            ButtonEvent yd{Button::Y, true};
            toggles += r.router.route(InputMap::Action::Button, yd, 1.0) == R::RecordToggle ? 1 : 0; // a repeated down too
        }
        check(toggles == 1, "REC: fires once per chord (repeated Y downs do not fire again)");
    }
    {
        Rig r;
        check(r.feedAll({down(kSelect), down(kStart)}) == R::Continue, "REC: Select + Start: nothing (KNULLI's screenshot)");
        check(r.feed(down(kY)) == R::RecordToggle && r.model->fine(), "REC: Select + Start + Y: treated as Start + Y");
    }
    {
        Rig r;
        r.feed(down(kY));
        r.feed(down(kMenu));
        check(r.feed(down(kStart)) == R::MenuStart, "REC: Y and Menu held, Start down: quits (Menu + Start wins)");
    }
    {
        Rig r;
        r.feed(down(kStart));
        check(r.feed(down(kY)) == R::RecordToggle, "REC: Start + Y");
        check(r.feed(down(kMenu)) == R::MenuStart, "REC: Start and Y held, Menu down: quits");
    }
    {
        Rig r;
        check(r.feed(down(kY)) == R::Continue && r.model->yHeld(), "REC: plain Y: unchanged (a reset modifier)");
        check(r.feed(up(kY)) == R::Continue && !r.model->yHeld() && r.chord.fired() == 0, "REC: plain Y: no toggle");
    }
    {
        Rig r;
        r.input.setSwapXY(true); // the launcher's swap on the Brick: SDL 'x' acts as Y
        r.feed(down(kStart));
        check(r.feed(down(kX)) == R::RecordToggle, "REC: with --swap-xy, Start + the button that acts as Y");
    }
    {
        Rig r;
        check(r.feed(key(SDL_KEYDOWN, SDL_SCANCODE_RETURN)) == R::Continue &&
                  r.feed(key(SDL_KEYDOWN, SDL_SCANCODE_Y)) == R::RecordToggle,
              "REC: keyboard: Return (Start) held + Y key: RecordToggle");
    }
    {
        Rig r;
        r.feed(down(kStart));
        ButtonEvent none{};
        r.router.route(InputMap::Action::ReleaseAll, none, 1.0);
        check(r.feed(down(kY)) == R::Continue && r.model->yHeld(), "REC: ReleaseAll between Start and Y: no toggle");
    }
    {
        Rig r(true);
        check(r.feedAll({down(kStart), down(kY)}) == R::Continue && r.chord.fired() == 0,
              "REC: self-test mode: real Start + Y is ignored");
        check(r.router.routeScripted({Button::Start, true}, 1.0) == R::Continue &&
                  r.router.routeScripted({Button::Y, true}, 1.0) == R::RecordToggle && !r.model->yHeld(),
              "REC: routeScripted() (the self-test's path): Start + Y toggles, Y held back from the model");
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
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // L1: LOOP -> PRESET (PRESET-B)
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // L1: PRESET -> MOD (MOD-B)
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // L1: MOD -> INPUT
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        ctl.takeLog(log);
        bool scanned = false;
        for(const std::string& l : log)
            scanned = scanned || l.find("input: scan of") != std::string::npos;
        check(model->currentTab() == Tab::Input && scanned,
              "L1 three times from LOOP: INPUT, and entering it scans the folder");
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
        // REC: Start + Y with X held on INPUT (source FILE): no LOOP/ONCE toggle.
        {
            RecordChord chord;
            InputRouter rrouter(*model, quit, false, &ctl, &chord);
            const InputPlayMode before = ctl.playMode();
            ButtonEvent         be{};
            rrouter.route(InputMap::Action::Button, be = {Button::X, true}, t);
            rrouter.route(InputMap::Action::Button, be = {Button::Start, true}, t);
            const auto res = rrouter.route(InputMap::Action::Button, be = {Button::Y, true}, t);
            rrouter.route(InputMap::Action::Button, be = {Button::Y, false}, t);
            rrouter.route(InputMap::Action::Button, be = {Button::Start, false}, t);
            check(res == R::RecordToggle && ctl.playMode() == before,
                  "REC: X held on INPUT, Start + Y: RecordToggle and LOOP/ONCE unchanged");
            rrouter.route(InputMap::Action::Button, be = {Button::Y, true}, t);
            rrouter.route(InputMap::Action::Button, be = {Button::Y, false}, t);
            check(ctl.playMode() != before, "REC: X still held, plain Y afterwards: toggles LOOP/ONCE as before");
            rrouter.route(InputMap::Action::Button, be = {Button::Y, true}, t); // back
            rrouter.route(InputMap::Action::Button, be = {Button::Y, false}, t);
            rrouter.route(InputMap::Action::Button, be = {Button::X, false}, t);
        }
        // deliver(): the self-test's path.
        router.deliver({Button::DpadLeft, true}, t);
        router.deliver({Button::DpadLeft, false}, t);
        check(ctl.source() == InputSource::Off, "deliver(): d-pad left on INPUT selects OFF");
        router.deliver({Button::R1, true}, t); // INPUT -> MOD
        router.deliver({Button::R1, false}, t);
        router.deliver({Button::R1, true}, t); // MOD -> PRESET (PRESET-B)
        router.deliver({Button::R1, false}, t);
        router.deliver({Button::R1, true}, t); // PRESET -> LOOP
        router.deliver({Button::R1, false}, t);
        router.deliver({Button::DpadRight, true}, t);
        check(model->currentTab() == Tab::Loop && ctl.source() == InputSource::Off,
              "R1 three times from INPUT wraps to LOOP; the d-pad there does not reach the panel");
        stage.setSource(InputSource::Off);
    }

    // --- MOD-B: the MOD tab's panel gets every event after the model ------------------------------
    {
        InputMap      input;
        auto          src   = std::make_unique<ButtonControlSource>();
        auto          model = std::make_unique<ControlModel>(*src);
        MenuQuit      quit;
        RecordChord   chord;
        ModPanel      panel; // no decorator: every send is refused and kept
        InputRouter   router(*model, quit, false, nullptr, &chord, &panel);
        double        t    = 0.0;
        auto          feed = [&](const SDL_Event& e) {
            ButtonEvent be{};
            t += 0.01;
            return router.route(input.handle(e, be), be, t);
        };
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        check(panel.row() == ModRow::Target, "MOD-B: on LOOP the d-pad does not reach the mod panel");
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // L1: LOOP -> PRESET (PRESET-B)
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // L1: PRESET -> MOD
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        const std::uint32_t heldBefore = model->heldWord();
        const std::uint64_t sentBefore = model->commandsSent();
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        // While right is held, past the hold delay: the model holds no ramp
        // and sends nothing (it binds no knob on MOD).
        t += 1.0;
        model->tick(t);
        const bool heldSame = model->heldWord() == heldBefore && model->commandsSent() == sentBefore;
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        check(model->currentTab() == Tab::Mod && panel.row() == ModRow::Shape &&
                  panel.slot(0).shape == ModShape::Triangle && heldSame && model->commandsSent() == sentBefore,
              "MOD-B: L1 twice from LOOP is MOD; down moves the cursor to SHAPE, right steps it; the model binds nothing "
              "(held word unchanged while right is held, no command sent)");
        // MOD-D: X held selects the slot (wrapping), up / down do nothing, the cursor row is kept.
        feed(down(kX));
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        feed(down(kUpPad));
        feed(up(kUpPad));
        check(panel.choosing() && panel.currentSlot() == 3 && panel.row() == ModRow::Shape,
              "MOD-D: X + left on slot 1 wraps to slot 4; X + up does nothing; the cursor row kept");
        // L1 with X held: the tab changes and the X state is dropped.
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)); // MOD -> INPUT
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        const bool dropped = model->currentTab() == Tab::Input && !panel.choosing();
        feed(down(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)); // back to MOD, X still held
        feed(up(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        check(dropped && model->currentTab() == Tab::Mod && panel.currentSlot() == 3,
              "MOD-D: L1 with X held changes tab and drops the X state; back on MOD X + right does nothing");
        feed(up(kX));
        // On TARGET (up from SHAPE): right steps slot 4's target; left wraps.
        feed(down(kUpPad));
        feed(up(kUpPad));
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        check(panel.row() == ModRow::Target && panel.slot(3).target == ModTarget::DelaySwitch &&
                  panel.slot(0).target == ModTarget::None,
              "MOD-D: left on TARGET wraps None -> DELAY SWITCH (slot 4 only)");
        feed(down(kX));
        ButtonEvent none{};
        router.route(InputMap::Action::ReleaseAll, none, t);
        check(!panel.choosing() && panel.slot(3).target == ModTarget::DelaySwitch,
              "MOD-D: ReleaseAll drops the X state, the settings stay");
    }

    // --- PRESET-B: the PRESET tab's panel and the Start + A chord -----------------------------------
    {
        InputMap      input;
        auto          src   = std::make_unique<ButtonControlSource>();
        auto          model = std::make_unique<ControlModel>(*src);
        MenuQuit      quit;
        RecordChord   recChord;
        RecordChord   preChord(Button::A);
        ModPanel      mod;
        PosixPresetFs fs;
        PresetStore   store(fs, "", "test"); // no folder: actions fail, but the requests are seen
        PresetPanel   preset(store, *model, *src, mod);
        InputRouter   router(*model, quit, false, nullptr, &recChord, &mod, &preChord, &preset);
        double        t    = 0.0;
        auto          feed = [&](const SDL_Event& e) {
            ButtonEvent be{};
            t += 0.01;
            return router.route(input.handle(e, be), be, t);
        };
        const int kA     = SDL_CONTROLLER_BUTTON_A;
        const int kStart = SDL_CONTROLLER_BUTTON_START;
        const int kY     = SDL_CONTROLLER_BUTTON_Y;
        ControlFrame frame{};
        auto         settle = [&]() {
            src->read(frame); // the audio side applies what the model sent
            preset.tick(t, t); // runs the queued save (it fails: no folder), clears pending
        };
        // Start, then A: a save is asked for; the model never sees A (no MAX).
        feed(down(kStart));
        feed(down(kA));
        const bool pending1 = preset.pending();
        const bool noMax    = !model->feedbackMaxHeld() && !model->isDown(Button::A);
        feed(up(kA));
        feed(up(kStart));
        check(pending1 && noMax && preChord.fired() == 1 && !model->feedbackMaxHeld(),
              "PRESET-B: Start + A asks the preset panel for a save on LOOP; the model never sees that A (no MAX)");
        settle();
        check(!preset.pending() && preset.failures() == 1 && preset.lastMessage() == "SAVE FAILED: NO PRESET FOLDER",
              "PRESET-B: with no folder the save fails with SAVE FAILED: NO PRESET FOLDER");
        // A, then Start: MAX sounds until Start, then the model is told A is up.
        feed(down(kA));
        const bool maxBefore = model->feedbackMaxHeld();
        feed(down(kStart));
        const bool maxAfter = model->feedbackMaxHeld();
        const bool pending2 = preset.pending();
        feed(up(kA)); // held back
        feed(up(kStart));
        check(maxBefore && !maxAfter && pending2 && preChord.fired() == 2 && !model->isDown(Button::A),
              "PRESET-B: A then Start: MAX until the chord, then A up for the model; a save is asked for");
        settle();
        // Once per chord: A pressed again while Start stays held fires again only after A was released.
        feed(down(kStart));
        feed(down(kA));
        feed(down(kA)); // a repeated down
        feed(up(kA));
        feed(down(kA));
        feed(up(kA));
        feed(up(kStart));
        check(preChord.fired() == 4, "PRESET-B: once per chord (a repeated down does not fire; a new press does)");
        settle();
        // Start + Y + A: both chords fire on the one Start press.
        feed(down(kY));
        feed(down(kA));
        const InputRouter::Result both = feed(down(kStart));
        feed(up(kY));
        feed(up(kA));
        feed(up(kStart));
        check(both == R::RecordToggle && preChord.fired() == 5 && recChord.fired() == 1 && !model->isDown(Button::A) &&
                  !model->isDown(Button::Y),
              "PRESET-B: Y and A held, then Start: RecordToggle and a preset save on the same press");
        settle();
        // Plain A still means MAX.
        feed(down(kA));
        const bool plainMax = model->feedbackMaxHeld();
        feed(up(kA));
        check(plainMax && !model->feedbackMaxHeld() && preChord.fired() == 5, "PRESET-B: plain A is MAX as before");
        // The PRESET tab: L1 from LOOP; d-pad there reaches the preset panel, not elsewhere.
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        const bool loopRight = preset.pending(); // on LOOP: nothing for the preset panel
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        settle();
        check(!loopRight, "PRESET-B: on LOOP the d-pad does not reach the preset panel");
        const std::uint64_t failsLoop = preset.failures();
        feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        const std::uint32_t heldBefore = model->heldWord();
        const std::uint64_t sentBefore = model->commandsSent();
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)); // on SAVE NEW: a save (it fails: no folder)
        const bool pendingTab = preset.pending();
        t += 1.0;
        model->tick(t);
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        settle();
        check(model->currentTab() == Tab::Preset && preset.failures() == failsLoop + 1 && pendingTab &&
                  model->heldWord() == heldBefore && model->commandsSent() == sentBefore,
              "PRESET-B: L1 from LOOP is PRESET; right there reaches the panel (SAVE NEW); the model binds nothing");
        // Self-test path: routeScripted applies the chord too.
        const unsigned before = preChord.fired();
        router.routeScripted({Button::Start, true}, t);
        router.routeScripted({Button::A, true}, t);
        const bool scripted = preset.pending() && !model->isDown(Button::A);
        router.routeScripted({Button::A, false}, t);
        router.routeScripted({Button::Start, false}, t);
        check(scripted && preChord.fired() == before + 1, "PRESET-B: routeScripted() applies Start + A");
        settle();
        // ReleaseAll resets the chord: Start held, focus lost, A pressed: no save.
        feed(down(kStart));
        ButtonEvent none{};
        router.route(InputMap::Action::ReleaseAll, none, t);
        feed(down(kA));
        const bool maxAgain = model->feedbackMaxHeld();
        feed(up(kA));
        check(preChord.fired() == before + 1 && !preset.pending() && maxAgain,
              "PRESET-B: ReleaseAll forgets Start: a later A is plain MAX");
    }

    // --- PRESET-B review: Start + A with INPUT's pop-up open (X held) -------------------------------
    {
        const std::string dir = std::string(AB_ROUTER_TMP) + "/presets";
        std::error_code   ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        InputMap      input;
        auto          src   = std::make_unique<ButtonControlSource>();
        auto          model = std::make_unique<ControlModel>(*src);
        MenuQuit      quit;
        InputStage    stage;
        stage.prepare(256);
        InputController ctl(stage, ""); // no folder: the pop-up still opens (as above)
        RecordChord   recChord;
        RecordChord   preChord(Button::A);
        ModPanel      mod;
        PosixPresetFs fs;
        PresetStore   store(fs, dir, "test");
        PresetPanel   preset(store, *model, *src, mod);
        preset.scanAtStart();
        InputRouter   router(*model, quit, false, &ctl, &recChord, &mod, &preChord, &preset);
        double        t    = 0.0;
        auto          feed = [&](const SDL_Event& e) {
            ButtonEvent be{};
            t += 0.01;
            return router.route(input.handle(e, be), be, t);
        };
        for(int i = 0; i < 3; ++i) // L1 three times from LOOP: INPUT
        {
            feed(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
            feed(up(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        }
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)); // FILE
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        // Hold X (the pad's X is our X here: no swap in InputMap by default), up: the pop-up.
        feed(down(SDL_CONTROLLER_BUTTON_X));
        feed(down(SDL_CONTROLLER_BUTTON_DPAD_UP));
        feed(up(SDL_CONTROLLER_BUTTON_DPAD_UP));
        ctl.tick(t);
        const bool open = model->currentTab() == Tab::Input && ctl.source() == InputSource::File && ctl.popupOpen() &&
                          ctl.choosing() && model->isDown(Button::X);
        const int           cursorBefore = ctl.cursor();
        const InputPlayMode modeBefore   = ctl.playMode();
        const std::uint64_t sentBefore   = model->commandsSent();
        std::vector<std::string> inputLog;
        ctl.takeLog(inputLog);
        // Start first, then A, X still held.
        feed(down(SDL_CONTROLLER_BUTTON_START));
        feed(down(SDL_CONTROLLER_BUTTON_A));
        const bool asked  = preset.pending() && preChord.fired() == 1;
        const bool aHidden = !model->isDown(Button::A) && !model->feedbackMaxHeld();
        ctl.tick(t);
        const bool stillOpen = ctl.popupOpen() && ctl.choosing();
        ControlFrame frame{};
        src->read(frame); // the audio side applies what the model sent
        preset.tick(t, t);
        feed(up(SDL_CONTROLLER_BUTTON_A));
        feed(up(SDL_CONTROLLER_BUTTON_START));
        ctl.tick(t);
        inputLog.clear();
        ctl.takeLog(inputLog);
        check(open, "PRESET-B review: on INPUT (FILE), X held + up opens the pop-up");
        check(asked && preset.saves() == 1 && std::filesystem::exists(dir + "/preset-0001.json") &&
                  preset.lastMessage() == "SAVED PRESET-0001",
              "PRESET-B review: Start + A with the pop-up open saves preset-0001.json (" + preset.lastMessage() + ")");
        check(stillOpen && ctl.popupOpen() && ctl.choosing() && ctl.cursor() == cursorBefore &&
                  ctl.playMode() == modeBefore && inputLog.empty(),
              "PRESET-B review: the pop-up stays open, its cursor and play mode unchanged, the INPUT panel logs nothing");
        check(aHidden && !model->isDown(Button::A) && model->commandsSent() == sentBefore && recChord.fired() == 0,
              "PRESET-B review: the chord's A reaches neither the model (no MAX, no command) nor, through it, the "
              "INPUT panel (deliver() is the one path to both)");
        feed(up(SDL_CONTROLLER_BUTTON_X));
        check(!ctl.popupOpen(), "PRESET-B review: releasing X closes the pop-up afterwards");
        std::filesystem::remove_all(dir, ec);
    }
    return ptest::finish();
}
