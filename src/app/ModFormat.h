// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-B (docs/briefs/MOD.md): the MOD tab's names, the target list's order and
// the formatting of rate, release and depth. No SDL, no allocation: every
// formatter writes into a caller's char array, always NUL-terminates it
// (truncating if it is too small) and returns the length written. Used by the
// screen (upper case, the font's letters) and by ModPanel's log lines (lower
// case units).
//
// Rate (LFO shapes; modRateHz()): three significant digits, the tier chosen
// after rounding: below 1 Hz three decimals ("0.010 HZ", "0.500 HZ"), then
// "1.00 HZ" .. "9.99 HZ", "10.0 HZ" .. "99.9 HZ", "100 HZ" .. "999 HZ", and from
// 1 kHz "1.00 KHZ" .. "2.00 KHZ". Log: "Hz", "kHz".
// Release (Envelope; modReleaseSeconds()): three significant digits: "5.00 S"
// .. "1.00 S", then "995 MS" .. "100 MS", "99.5 MS" .. "10.0 MS", "9.95 MS" ..
// "5.00 MS". Log: "s", "ms".
// Depth: percent of the target's travel with one decimal and a sign, "+34.0%",
// "-100.0%"; zero is "0.0%" (never "+0.0%" or "-0.0%").
// Not finite: "--".
//
// Target order (the TARGET row steps through it, wrapping; MOD-D): None, then
// the knobs as the value strip shows them (FEEDBACK first, then LOOP's,
// FILTER's, REVERB's, DELAY's and OUT's), then INPUT GAIN and DELAY SWITCH. Display names are the knobs'
// on-screen names (ui::knobLongName; ui_layout_test checks they agree).

#pragma once

#include <cstddef>

#include "mod/ModTypes.h"

namespace audreybrick
{

constexpr std::size_t kModTargetListSize = kModTargetCount; // every target, None included

// The target at list position `i` (None for an out-of-range position).
ModTarget modTargetAt(std::size_t i) noexcept;
// The list position of `t` (0 for an unknown target).
std::size_t modTargetListIndex(ModTarget t) noexcept;

// "NONE", "FREQUENCY", ..., "INPUT GAIN", "DELAY SWITCH".
const char* modTargetDisplayName(ModTarget t) noexcept;
// "SINE", "TRIANGLE", "SQUARE", "RAMP UP", "RAMP DOWN", "EXP UP", "EXP DOWN",
// "S&H", "ENVELOPE" (MOD-C: the ramps).
const char* modShapeDisplayName(ModShape s) noexcept;
// MOD-C: "BIPOLAR", "UNIPOLAR" (the POLARITY row) and "+/-", "+" (the slot cards).
const char* modPolarityDisplayName(ModPolarity p) noexcept;
const char* modPolarityShortName(ModPolarity p) noexcept;

std::size_t formatModRateHz(char* out, std::size_t size, double hz, bool upper = true) noexcept;
std::size_t formatModRelease(char* out, std::size_t size, double seconds, bool upper = true) noexcept;
// The RATE row's value for a slot: the rate for the LFO shapes, the release
// time for Envelope.
std::size_t formatModRate(char* out, std::size_t size, float rateNorm, ModShape shape, bool upper = true) noexcept;
std::size_t formatModDepth(char* out, std::size_t size, float depth) noexcept;

} // namespace audreybrick
