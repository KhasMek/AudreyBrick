// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The screen layout (docs/briefs/M2.md, M2-C): turns a UiSnapshot and the meter
// state into a DrawList. No SDL, no allocation. 1024 x 768 logical pixels, top to
// bottom, each block a panel in the DrawList:
//
//   tab bar        L1 key, the five tabs (current one in inverse video), R1 key
//   feedback band  stored feedback in dB, bar between L2 and R2 keys with a 0 dB
//                  tick; while A is held the bar is striped, a plate reads
//                  "MAX +12.0 dB" in the half away from the stored-value marker,
//                  and "STORED" labels the big number.
//   switch box     inverse and "ON" when on, LATCHED / HELD / LATCH+HELD, B key
//                  (lit only for the momentary hold).
//   quit banner    only while Start + Select are held (UiSnapshot::quitHoldActive):
//                  "HOLD TO QUIT", a bar filling over the one-second hold and
//                  "SELECT + START  RELEASE TO CANCEL", drawn in place of the
//                  current tab's rows (the area below), so nothing overlaps.
//   current tab    one row per bound d-pad binding: arrows (with an X key for X
//                  bindings), name and value at 32 px or more, a position bar.
//                  Rows for the other X state are dimmed with outline arrows;
//                  unbound directions are not shown. While the delay switch is
//                  on, TIME carries a "B" mark. REPEATS has a tick at unity gain.
//                  Unitless values show the plain engine value ("0.85") and
//                  their bars show the same value against its maximum, so
//                  number and bar agree.
//   meter panel    L/R output meters with a dB scale, peak hold, CLIP caps.
//   value strip    all eleven values (tab, name, value, small bar), the current
//                  tab's cells framed; the twelfth cell is the CPU meter.
//   status line    audio spec, dropped commands, controller / keyboard hint;
//                  cut names end in "..".
//   footer         the global buttons; an entry is drawn in inverse video while
//                  its button is held (this is also where FINE, X and Y show).

#pragma once

#include <cstddef>

#include "app/UiSnapshot.h"
#include "core/ControlSource.h"
#include "ui/DrawList.h"

namespace audreybrick
{
namespace ui
{

// The palette. State never relies on colour alone: the current tab and held
// buttons are in inverse video, the current tab's rows and strip cells have a
// thick frame, inactive rows use outline arrows, maximum and clip carry the
// words MAX and CLIP, the switch says ON / OFF. Text colours on their panels
// reach a contrast ratio of at least 4.5:1 (ui_layout_test checks it).
namespace palette
{
inline constexpr Color bg{12, 14, 18, 255};
inline constexpr Color panel{30, 34, 42, 255};
inline constexpr Color panelDark{20, 23, 29, 255};
inline constexpr Color edge{72, 80, 94, 255};
inline constexpr Color text{242, 242, 236, 255};
inline constexpr Color dim{170, 178, 192, 255};
inline constexpr Color faint{136, 144, 160, 255};
inline constexpr Color accent{86, 200, 236, 255};
inline constexpr Color feedback{255, 172, 48, 255};
inline constexpr Color maxFill{255, 86, 64, 255};
inline constexpr Color maxStripe{176, 44, 34, 255};
inline constexpr Color switchOn{140, 228, 112, 255};
inline constexpr Color meter{110, 214, 130, 255};
inline constexpr Color meterHot{240, 210, 80, 255};
inline constexpr Color clip{255, 64, 64, 255};
} // namespace palette

struct MeterView
{
    float level[2] = {0.0f, 0.0f}; // L, R (linear)
    float hold[2]  = {0.0f, 0.0f};
    bool  clip[2]  = {false, false};
};

// Display names: the long one is used in the current tab when it fits, the short
// one otherwise and in the value strip.
const char* knobLongName(Knob k) noexcept;
const char* knobShortName(Knob k) noexcept;
// The tab a knob lives on ("LOOP", ...), or "" for FeedbackGain (L2/R2 everywhere).
const char* knobTabName(Knob k) noexcept;

// The strip cell's value text for a knob: the stored value in dB for
// FeedbackGain, the note name only for Frequency (Hz is in the LOOP row), the
// formatted engine value otherwise.
std::size_t formatStripValue(char* out, std::size_t size, const UiSnapshot& s, Knob k) noexcept;

// Where unity gain (engine value 1.0) sits on the REPEATS bars (0..1): 1.0 /
// the engine maximum from ParamTable, since those bars are linear in the engine
// value. The REPEATS bars carry a tick there.
float repeatsUnityPosition() noexcept;

void buildLayout(const UiSnapshot& s, const MeterView& meters, DrawList& out) noexcept;

} // namespace ui
} // namespace audreybrick
