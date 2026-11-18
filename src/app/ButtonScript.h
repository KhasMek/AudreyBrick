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
// Parsing allocates; do it before audio starts.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "app/Buttons.h"

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

// Parses a script. On success, replaces `out` and returns true. On failure,
// leaves `out` unchanged, sets `error` to a message with the line number and
// returns false.
bool parseButtonScript(const std::string& text, std::vector<TimedButtonEvent>& out, std::string& error);
bool loadButtonScriptFile(const std::string& path, std::vector<TimedButtonEvent>& out, std::string& error);

} // namespace audreybrick
