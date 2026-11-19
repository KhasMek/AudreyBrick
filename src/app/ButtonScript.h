// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ButtonScript: a list of timed button events, for tests and the app's headless
// self-test (M2-B, --selftest).
//
// Format, one event per line:
//
//   <time_s> <button> <down|up>
//
//   - '#' starts a comment that runs to the end of the line. Blank lines are ignored.
//   - time_s: seconds from the start, a plain non-negative decimal: digits[.digits]
//     (e.g. 0, 1.5, 0.25). No sign, exponent, hex, inf or nan. Locale-independent.
//   - button: a Button name as buttonName() spells it (DpadUp, DpadDown, DpadLeft,
//     DpadRight, A, B, X, Y, L1, R1, L2, R2, Select, Start), matched
//     case-insensitively.
//   - down / up (case-insensitive): pressed or released.
//   - Fields are separated by spaces or tabs; extra fields are an error.
//   - Events are sorted by time, keeping file order for equal times.
//
// M2b-B, simulated capture (the self-test's live input; input/LiveSim.h). Lines
// whose first field is `capture` declare a simulated capture device, in list order:
//
//   capture device <name> <channels> <tone_hz> <amplitude> <rate_hz>
//   capture device <name> fail              (opening it fails)
//
// and timed lines plug or unplug one:
//
//   <time_s> capture unplug <name>
//   <time_s> capture plug <name>
//
//   - <name>: one field (no spaces), unique; the device list shows it as written.
//   - channels 1 to 8; tone_hz above 0 and below 24000; amplitude 0 to 8;
//     rate_hz 24000 to 96000 (48000: the capture clock matches the output's;
//     48240: 0.5 % fast). Numbers: plain decimals as for times.
//   - A device whose first scripted event is `plug` is absent at the start.
//   - A capture event must name a declared device.
// Scripts with capture lines are for the app's self-test (which then uses the
// simulated system instead of SDL's capture devices); parseButtonScript() without
// the capture argument rejects them.
//
// Parsing allocates; do it before audio starts.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "app/Buttons.h"
#include "input/LiveSim.h"

namespace audreybrick
{

struct TimedButtonEvent
{
    double      timeSeconds;
    ButtonEvent event;
    int         line; // 1-based script line
};

// Looks a button up by its name, case-insensitively. Returns false if there is none.
bool buttonFromName(std::string_view name, Button& out) noexcept;

// The simulated capture part of a script (M2b-B).
struct CaptureScript
{
    std::vector<SimCaptureDevice> devices; // in declaration order
    std::vector<SimCaptureEvent>  events;  // sorted by time, file order for equal times
    bool any() const noexcept { return !devices.empty(); }
};

// Parses a script. On success, replaces `out` (and `capture`) and returns true.
// On failure, leaves them unchanged, sets `error` to a message with the line
// number and returns false. Without `capture`, a capture line is an error.
bool parseButtonScript(const std::string& text, std::vector<TimedButtonEvent>& out, std::string& error);
bool parseButtonScript(const std::string& text, std::vector<TimedButtonEvent>& out, CaptureScript& capture,
                       std::string& error);
bool loadButtonScriptFile(const std::string& path, std::vector<TimedButtonEvent>& out, std::string& error);
bool loadButtonScriptFile(const std::string& path, std::vector<TimedButtonEvent>& out, CaptureScript& capture,
                          std::string& error);

} // namespace audreybrick
