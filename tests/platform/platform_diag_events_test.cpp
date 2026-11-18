// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-A, acceptance criterion 4 (first-seen Button logging) and the new quit key:
// synthetic SDL events go through InputMap and EventLogger exactly as in the app's
// loop, into a DiagLog that is read back. Checks:
//   - the first press of each Button logs one "button first seen" line naming the
//     SDL event (controller button, trigger axis, key); later presses log nothing;
//   - SDL controller buttons and axes log their first use, mapped or not (guide
//     is "not mapped"; a trigger axis says "L2 (trigger axis)");
//   - raw joystick buttons, axes and hats log their first use (diagnostics);
//   - unmapped keys are logged, at most 10 per 10 s, the rest counted; mapped keys
//     and key repeats are not "unmapped";
//   - Alt+F4 quits (F4 alone does not, and is logged as an unmapped key), with the
//     reason text; Escape too; quit-path inputs (Escape, Alt, F4, Start, Select)
//     are logged the first time each is pressed;
//   - focus changes, render resets and the first event of an unknown type are logged.
// No SDL_Init: these SDL calls work without it.

#include <SDL.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/DiagLog.h"
#include "app/SdlDiagnostics.h"
#include "platform/InputMap.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
SDL_Event key(Uint32 type, SDL_Scancode sc, Uint16 mod = 0, Uint8 repeat = 0)
{
    SDL_Event e;
    SDL_zero(e);
    e.type                = type;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym      = SDL_GetKeyFromScancode(sc);
    e.key.keysym.mod      = mod;
    e.key.repeat          = repeat;
    e.key.state           = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    return e;
}

SDL_Event cbutton(Uint32 type, int b)
{
    SDL_Event e;
    SDL_zero(e);
    e.type           = type;
    e.cbutton.button = static_cast<Uint8>(b);
    e.cbutton.state  = type == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    return e;
}

SDL_Event caxis(int axis, int value)
{
    SDL_Event e;
    SDL_zero(e);
    e.type        = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis  = static_cast<Uint8>(axis);
    e.caxis.value = static_cast<Sint16>(value);
    return e;
}

SDL_Event typed(Uint32 type)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    return e;
}

std::string readAll(const std::string& path)
{
    std::ifstream     in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int count(const std::string& hay, const std::string& needle)
{
    int         n   = 0;
    std::size_t pos = 0;
    while((pos = hay.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

// Lines of `hay` that contain both `a` and `b`.
int countLines(const std::string& hay, const std::string& a, const std::string& b)
{
    std::stringstream ss(hay);
    std::string       l;
    int               n = 0;
    while(std::getline(ss, l))
        n += (l.find(a) != std::string::npos && l.find(b) != std::string::npos) ? 1 : 0;
    return n;
}

struct Harness
{
    DiagLog     log;
    EventLogger events{log};
    InputMap    input;
    double      t = 0.0;

    InputMap::Action feed(const SDL_Event& e)
    {
        ButtonEvent            be{Button::Start, false};
        const InputMap::Action a = input.handle(e, be);
        events.onEvent(e, a, be, input, t);
        t += 0.01;
        return a;
    }
};
} // namespace

int main()
{
    char        tmpl[] = "/tmp/audreybrick-events-XXXXXX";
    const char* dir    = mkdtemp(tmpl);
    if(dir == nullptr)
    {
        check(false, "temporary directory");
        return ptest::finish();
    }
    const std::string path = std::string(dir) + "/events.log";
    Harness           h;
    std::string       err;
    check(h.log.open(path, err), "log opened: " + err);

    // --- First-seen Buttons ---------------------------------------------------------
    h.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A));
    h.feed(cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_A));
    h.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A));
    h.feed(caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767));
    h.feed(caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0));
    h.feed(caxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767));
    h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_LSHIFT));
    h.feed(key(SDL_KEYUP, SDL_SCANCODE_LSHIFT));
    h.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_BACK)); // Select again, from the pad
    h.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE));
    std::string s = readAll(path);
    check(count(s, "button first seen: A <- controller button 'a' (0) down") == 1,
          "A: one first-seen line naming SDL controller button 'a'");
    check(count(s, "button first seen: L2 <- controller axis 'lefttrigger' (4) = 32767") == 1,
          "L2: one first-seen line naming the trigger axis");
    check(count(s, "button first seen: Select <- key scancode 225 'Left Shift' down") == 1,
          "Select: first seen from the keyboard, logged once (the later pad press is not first)");
    check(count(s, "button first seen:") == 3, "exactly three first-seen Button lines");
    check(count(s, "controller button first pressed: 'a' (0) -> A") == 1 &&
              count(s, "controller button first pressed: 'back' (4) -> Select") == 1,
          "SDL controller buttons: first press logged with the Button they map to");
    check(count(s, "controller button first pressed: 'guide' (5) -> not mapped") == 1,
          "an unmapped controller button (guide) is still logged");
    check(count(s, "controller axis first moved: 'lefttrigger' (4) = 32767 -> L2 (trigger axis)") == 1,
          "the trigger axis: logged once as L2 (trigger axis)");

    // --- Raw joystick events (diagnostics) ---------------------------------------------
    {
        SDL_Event jb = typed(SDL_JOYBUTTONDOWN);
        jb.jbutton.button = 9;
        h.feed(jb);
        h.feed(jb);
        SDL_Event ja = typed(SDL_JOYAXISMOTION);
        ja.jaxis.axis  = 2;
        ja.jaxis.value = 100; // below the threshold: not logged
        h.feed(ja);
        ja.jaxis.value = -32768;
        h.feed(ja);
        SDL_Event jh = typed(SDL_JOYHATMOTION);
        jh.jhat.value = SDL_HAT_UP;
        h.feed(jh);
        h.feed(jh);
        s = readAll(path);
        check(count(s, "joystick button first pressed (raw, diagnostics only): joystick button 9 down") == 1,
              "raw joystick button 9: logged once");
        check(count(s, "joystick axis first moved (raw, diagnostics only): joystick axis 2 = -32768") == 1,
              "raw joystick axis: logged once, above the threshold only");
        check(count(s, "joystick hat first moved (raw, diagnostics only): joystick hat 0 = 0x1") == 1,
              "raw joystick hat: logged once");
    }

    // --- Unmapped keys, rate limited -----------------------------------------------------
    {
        h.t = 100.0;
        h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_Q));        // L1: mapped
        h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_Q, 0, 1));  // repeat
        h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_LALT));     // unmapped: 1
        for(int i = 0; i < 7; ++i)                       // unmapped: 2..15 (down and up)
        {
            h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_H));
            h.feed(key(SDL_KEYUP, SDL_SCANCODE_H));
        }
        h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_H, 0, 1)); // a repeat of an unmapped key: not logged
        s = readAll(path);
        check(count(s, "unmapped key: key scancode 226 'Left Alt' down") == 1, "Left Alt logged as unmapped");
        check(count(s, "unmapped key:") == EventLogger::kKeyLinesPerWindow,
              "unmapped keys: " + std::to_string(EventLogger::kKeyLinesPerWindow) + " lines in the 10 s window (got " +
                  std::to_string(count(s, "unmapped key:")) + ")");
        check(count(s, "unmapped key: key scancode 20 'Q'") == 0 && count(s, "button first seen: L1 <- key scancode 20 'Q' down") == 1, "a mapped key (Q = L1) is not logged as unmapped");
        check(h.events.takeKeysSuppressed() == 5 && h.events.takeKeysSuppressed() == 0,
              "the 5 lines over the limit are counted as suppressed");
        h.t = 111.0;
        h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_F5));
        check(count(readAll(path), "unmapped key: key scancode 62 'F5' down") == 1, "a new window logs again");
    }

    // --- Quit keys ---------------------------------------------------------------------
    {
        check(h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_F4)) == InputMap::Action::None, "F4 alone does not quit");
        const SDL_Event altF4 = key(SDL_KEYDOWN, SDL_SCANCODE_F4, KMOD_LALT);
        check(h.feed(altF4) == InputMap::Action::Quit, "Left Alt + F4 quits");
        check(h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_F4, KMOD_RALT)) == InputMap::Action::Quit, "Right Alt + F4 quits");
        check(h.feed(key(SDL_KEYUP, SDL_SCANCODE_F4, KMOD_LALT)) == InputMap::Action::None, "Alt + F4 release does nothing");
        check(h.feed(key(SDL_KEYDOWN, SDL_SCANCODE_F4, KMOD_LALT, 1)) == InputMap::Action::None, "Alt + F4 repeat ignored");
        check(std::strstr(quitReason(altF4), "Alt+F4") != nullptr, std::string("reason: ") + quitReason(altF4));
        const SDL_Event esc = key(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE);
        check(h.feed(esc) == InputMap::Action::Quit && std::strcmp(quitReason(esc), "Escape key") == 0, "Escape quits");
        check(std::strstr(quitReason(typed(SDL_QUIT)), "SDL_QUIT") != nullptr, "SDL_QUIT reason");
        check(!InputMap::mapsScancode(SDL_SCANCODE_F4) && InputMap::mapsScancode(SDL_SCANCODE_ESCAPE) &&
                  InputMap::mapsScancode(SDL_SCANCODE_SPACE) && !InputMap::mapsScancode(SDL_SCANCODE_LALT),
              "mapsScancode: Escape, Space mapped; F4 and Left Alt not");
        const std::string q = readAll(path);
        check(countLines(q, "unmapped key: key scancode 61 'F4' down", "mod 0x0000") == 1,
              "F4 without Alt is logged as an unmapped key (and does not quit)");
        check(countLines(q, "unmapped key: key scancode 61 'F4' down", "mod 0x0100") == 0,
              "Alt+F4 (a quit) is not logged as an unmapped key");
        check(countLines(q, "quit-path input first seen: key scancode 41 'Escape' down", "(quits)") == 1,
              "Escape: quit-path first-seen line, marked (quits)");
        check(count(q, "quit-path input first seen: key scancode 226 'Left Alt' down") == 1, "Left Alt: quit-path first seen");
        check(count(q, "quit-path input first seen: key scancode 61 'F4' down") == 1, "F4: quit-path first seen, once");
        check(count(q, "quit-path input first seen: key scancode 225 'Left Shift' down") == 1,
              "Select (from Left Shift): quit-path first seen");
        check(count(q, "quit-path input first seen:") == 4, "four quit-path inputs seen so far (Start not yet)");
    }

    // --- Focus, render resets, other events ---------------------------------------------
    {
        SDL_Event w = typed(SDL_WINDOWEVENT);
        w.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        check(h.feed(w) == InputMap::Action::ReleaseAll, "focus lost releases all");
        w.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
        h.feed(w);
        w.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
        w.window.data1 = 1024;
        w.window.data2 = 768;
        h.feed(w);
        h.feed(typed(SDL_RENDER_TARGETS_RESET));
        h.feed(typed(SDL_RENDER_DEVICE_RESET));
        h.feed(typed(SDL_MOUSEMOTION));
        h.feed(typed(SDL_MOUSEMOTION));
        h.feed(typed(SDL_USEREVENT + 7));
        s = readAll(path);
        check(count(s, "focus lost") == 1 && count(s, "focus gained") == 1, "focus lost and gained logged");
        check(count(s, "window event size changed (6), data 1024 768") == 1, "other window events logged");
        check(count(s, "render targets reset") == 1 && count(s, "render device reset") == 1, "render resets logged");
        check(count(s, "event type first seen: SDL_MOUSEMOTION") == 1, "an unhandled event type: first time only");
        check(count(s, "event type first seen: 0x") == 1, "an unknown event type by number");
    }

    // Every line has the time prefix.
    {
        std::stringstream ss(readAll(path));
        std::string       l;
        int               bad = 0;
        int               n   = 0;
        while(std::getline(ss, l))
        {
            ++n;
            if(l.size() < 13 || l[0] != '[' || l[11] != ']' || l[12] != ' ')
                ++bad;
        }
        check(n > 20 && bad == 0, std::to_string(n) + " lines, all with the [time] prefix");
    }
    h.log.close();
    if(std::getenv("KEEP_LOG") == nullptr)
        std::remove(path.c_str());

    // --- M3-B: --swap-ab / --swap-xy keep the first-seen lines truthful; Menu once only --
    {
        const std::string swapPath = std::string(dir) + "/swap.log";
        Harness           w;
        check(w.log.open(swapPath, err), "swap log opened: " + err);
        w.input.setSwapAB(true);
        w.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B));
        w.feed(cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B));
        w.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A));
        w.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_X));
        w.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        for(int i = 0; i < 5; ++i)
        {
            w.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE));
            w.feed(cbutton(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_GUIDE));
        }
        const std::string ws = readAll(swapPath);
        check(countLines(ws, "button first seen: A <- controller button 'b' (1) down", "[A/B swapped]") == 1,
              "swap: 'b' logged as A, marked [A/B swapped]");
        check(countLines(ws, "button first seen: B <- controller button 'a' (0) down", "[A/B swapped]") == 1,
              "swap: 'a' logged as B, marked [A/B swapped]");
        check(count(ws, "controller button first pressed: 'b' (1) -> A [A/B swapped]") == 1 &&
                  count(ws, "controller button first pressed: 'a' (0) -> B [A/B swapped]") == 1,
              "swap: the SDL-button lines name the resulting Button and the swap");
        check(count(ws, "button first seen: X <- controller button 'x' (2) down") == 1 &&
                  countLines(ws, "button first seen: X", "swapped]") == 0,
              "A/B swap only: X is unchanged and not marked");
        w.log.close();
        // Both swaps (a fresh logger, so first-seen lines appear again).
        Harness b;
        check(b.log.open(swapPath, err), "swap log reopened: " + err);
        b.input.setSwapAB(true);
        b.input.setSwapXY(true);
        b.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_Y));
        b.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_X));
        b.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B));
        b.feed(cbutton(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        const std::string bs = readAll(swapPath);
        check(countLines(bs, "button first seen: X <- controller button 'y' (3) down", "[X/Y swapped]") == 1 &&
                  countLines(bs, "button first seen: Y <- controller button 'x' (2) down", "[X/Y swapped]") == 1,
              "X/Y swap: 'y' logged as X and 'x' as Y, marked [X/Y swapped]");
        check(count(bs, "controller button first pressed: 'y' (3) -> X [X/Y swapped]") == 1 &&
                  countLines(bs, "button first seen: A <- controller button 'b' (1) down", "[A/B swapped]") == 1,
              "both swaps: the SDL-button line names X; A/B still marked [A/B swapped]");
        check(count(bs, "button first seen: L1 <- controller button 'leftshoulder'") == 1 &&
                  countLines(bs, "button first seen: L1", "swapped]") == 0,
              "both swaps: L1 unchanged and not marked");
        b.log.close();
        check(count(ws, "'guide'") == 1 && count(ws, "controller button first pressed: 'guide' (5) -> not mapped") == 1,
              "Menu (guide) pressed five times: one first-seen line, nothing else");
        if(std::getenv("KEEP_LOG") == nullptr)
            std::remove(swapPath.c_str());
    }
    rmdir(dir);
    return ptest::finish();
}
