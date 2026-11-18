// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The diagnostic log's SDL parts (docs/briefs/M3.md, M3-A, C.3): what the app
// writes about SDL, the window, the renderer, the audio device and every kind of
// input, so that one run on the Brick answers the questions in
// docs/PLATFORM_NOTES.md section 10 ("What still needs the device"):
//
//   - which SDL button each physical press produced: "button first seen" lines,
//     one per Button, naming the SDL event behind it;
//   - whether L2/R2 arrive as trigger axes or as buttons: "controller axis first
//     moved" and "controller button first pressed" lines, one per SDL button or
//     axis, mapped or not (a press of an unmapped button such as guide or misc1
//     is still logged);
//   - what the raw joystick reports for buttons the controller mapping does not
//     cover (Menu, Fn): "joystick button/axis/hat first" lines. These are
//     diagnostics only; the controls never use raw joystick indices;
//   - what keyboard events arrive that the app does not map (Menu, Fn, the exit
//     hotkey): "unmapped key" lines, at most 10 per 10 seconds, the rest counted;
//   - controllers and joysticks added or removed, with name, GUID and the mapping
//     string SDL uses; window focus and other window events; render resets.
//
// Everything here runs on the UI thread. Nothing is called from the audio callback.

#pragma once

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "app/Buttons.h"
#include "app/DiagLog.h"
#include "platform/InputMap.h"

namespace audreybrick
{

// One-line description of an SDL input or device event, e.g.
//   "controller button 'a' (0) down", "controller axis 'lefttrigger' (4) = 32767",
//   "key scancode 225 'Left Shift' down, keycode 0x400000e1, mod 0x0001".
// Writes at most `size` bytes including the NUL; returns the length.
std::size_t describeEvent(char* out, std::size_t size, const SDL_Event& e) noexcept;

// Why an event that InputMap reports as Quit quits: "SDL_QUIT (...)",
// "window closed", "Escape key", "Alt+F4 (the KNULLI exit hotkey)".
const char* quitReason(const SDL_Event& e) noexcept;

// The name of an SDL event type ("SDL_MOUSEMOTION", ...) or nullptr if unknown.
const char* eventTypeName(Uint32 type) noexcept;

// Start-up lines: SDL compiled and linked versions, revision, platform, CPU
// count, RAM, the video and audio drivers SDL was built with, and the
// environment variables that steer SDL.
void logSdlInfo(DiagLog& log, double t);

// Before SDL_Init: SDL_VIDEODRIVER, the video drivers compiled into the linked
// SDL, whether /dev/fb0 exists and is readable/writable (stat/access), and the
// modes in /sys/class/graphics/fb0/modes (what the Mali driver reads). Every line
// also goes to `echo` (stdout, so stdout.log) when given.
void logVideoEnvironment(DiagLog& log, double t, std::FILE* echo);

// The video driver in use, the display count, and per display the current and
// desktop modes, bounds and the list of modes (or "not initialised"). Also on
// `echo`. Called on success (from logVideoInfo) and after a window or renderer failure.
void logVideoDisplays(DiagLog& log, double t, std::FILE* echo);

// The window, renderer and display as obtained: logVideoDisplays, then the window
// size and flags, renderer name and flags, output size, logical size and integer
// scaling, vsync, and the render drivers available. Also on `echo`.
void logVideoInfo(DiagLog& log, double t, SDL_Window* window, SDL_Renderer* renderer, std::FILE* echo = nullptr);

// The audio drivers SDL was built with, and the output and capture device names
// the current audio driver reports (SDL_GetNumAudioDevices / SDL_GetAudioDeviceName).
// Call with the audio subsystem initialised.
void logAudioDevices(DiagLog& log, double t);

// Every joystick SDL sees right now: name, GUID, whether it is a game
// controller, and its mapping string; and SDL_GAMECONTROLLERCONFIG if set.
void logJoysticks(DiagLog& log, double t);

class EventLogger
{
  public:
    static constexpr int    kKeyLinesPerWindow = 10;
    static constexpr double kKeyWindowSeconds  = 10.0;

    explicit EventLogger(DiagLog& log) noexcept : log_(log) {}

    // Call for every SDL event, after InputMap::handle() produced `action` and,
    // for Action::Button, `be`. `input` is consulted for the active controller.
    void onEvent(const SDL_Event& e, InputMap::Action action, const ButtonEvent& be, const InputMap& input,
                 double t) noexcept;

    // The inputs that can quit the app or are part of a quit combination (Escape,
    // Left/Right Alt, F4 as keys; Start and Select as Buttons) are logged the first
    // time each is pressed ("quit-path input first seen"), so an unexpected quit can
    // be traced.

    // Unmapped-key lines suppressed by the rate limit since the last call.
    std::uint64_t takeKeysSuppressed() noexcept { return keys_.takeSuppressed(); }

  private:
    void noteDevice(SDL_JoystickID id, const char* name) noexcept;
    const char* deviceName(SDL_JoystickID id) const noexcept;
    void otherEvent(const SDL_Event& e, double t) noexcept;

    DiagLog&                            log_;
    SeenSet<kButtonCount>               buttons_;
    SeenSet<SDL_CONTROLLER_BUTTON_MAX>  controllerButtons_;
    SeenSet<SDL_CONTROLLER_AXIS_MAX>    controllerAxes_;
    SeenSet<64>                         joyButtons_;
    SeenSet<16>                         joyAxes_;
    SeenSet<4 * 16>                     joyHats_; // hat index * 16 + value
    SeenSet<6>                          quitInputs_; // Escape, LAlt, RAlt, F4, Start, Select
    RateLimiter                         keys_{kKeyLinesPerWindow, kKeyWindowSeconds};
    RateLimiter                         window_{20, 10.0};
    Uint32                              otherTypes_[48] = {};
    std::size_t                         otherCount_     = 0;

    struct Device
    {
        SDL_JoystickID id = -1;
        char           name[96] = {};
    };
    Device      devices_[8];
    std::size_t nextDevice_ = 0;
};

} // namespace audreybrick
