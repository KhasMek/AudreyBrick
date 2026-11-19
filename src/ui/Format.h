// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Display formatting of parameter values for the screen (docs/briefs/M2.md,
// M2-C). No SDL, no allocation: every function writes into a caller's char
// array, always NUL-terminates it (truncating if it is too small) and returns the
// length written. A value that is not finite is shown as "--".
//
// Rules (the unit is ParamSpec::unit, carried in UiKnobView::unit):
//   "note"  Frequency's MIDI note number: note name, cents when off the note,
//           then Hz: "E2 82.4 Hz", "E2+10c 82.9 Hz", "F2-40c 85.3 Hz".
//           Sharps, octave numbers as MIDI 60 = C4.
//   "Hz"    below 100 Hz one decimal ("25.3 Hz"), below 1 kHz whole hertz
//           ("250 Hz"), below 10 kHz two decimals in kHz ("4.00 kHz"), above
//           that one decimal ("18.0 kHz"). The tier is chosen after rounding, so
//           99.96 Hz reads "100 Hz", never "100.0 Hz".
//   "s"     milliseconds below one second ("1.00 ms", "45.3 ms", "500 ms"),
//           seconds from one second ("1.00 s", "5.00 s"); tiers chosen after
//           rounding, so 0.9996 s reads "1.00 s".
//   "dB"    one decimal with a sign ("-10.0 dB", "+12.0 dB"); zero is "0.0 dB"
//           (never "-0.0").
//   ""      (reverb dry/wet, reverb decay, delay mix, repeats, volume) the
//           plain ENGINE value with two decimals and no unit: dry/wet
//           "0.00".."1.00", decay "0.20".."1.00" (the ReverbSc feedback
//           coefficient), mix "0.00".."1.00" (the send gain), repeats
//           "0.00".."1.50", volume "0.00".."1.00" (the output gain). Never
//           "-0.00". (Decided by the human, 2026-11-17; the bars for these
//           five show the same engine value, see ui/Layout.cpp.)

#pragma once

#include <cstddef>

#include "app/UiSnapshot.h"

namespace audreybrick
{
namespace ui
{

// 440 * 2^((note - 69) / 12).
double midiNoteToHz(double note) noexcept;

// "E2", "E2+10c", "F2-40c". Cents are the distance to the nearest note, rounded
// to whole cents; within half a cent the cents are left out. A half-semitone
// (cents rounding to +50) is shown as -50 of the upper note, so the range of
// cents is -50..+49.
std::size_t formatNoteName(char* out, std::size_t size, float midiNote) noexcept;
// Note name and Hz: "E2 82.4 Hz", "E2+10c 82.9 Hz".
std::size_t formatNote(char* out, std::size_t size, float midiNote) noexcept;
std::size_t formatHz(char* out, std::size_t size, float hz) noexcept;
std::size_t formatSeconds(char* out, std::size_t size, float seconds) noexcept;
std::size_t formatDb(char* out, std::size_t size, float db) noexcept;
// `fraction` 1.0 = "100 %".
std::size_t formatPercent(char* out, std::size_t size, float fraction) noexcept;
// A plain number with two decimals, no unit: "0.85", "1.50"; never "-0.00".
std::size_t formatPlain(char* out, std::size_t size, float value) noexcept;
// CPU load (1.0 = 100 %): one decimal below 10 % ("3.2 %"), whole numbers above.
std::size_t formatCpu(char* out, std::size_t size, float load) noexcept;

// M2b-A. The input gain for a knob position n (gain = (2n)^2): "-inf dB" at 0,
// otherwise its level by formatDb ("0.0 dB" at 0.5, "+12.0 dB" at 1, "-6.0 dB"
// near 0.354).
std::size_t formatInputGain(char* out, std::size_t size, float norm) noexcept;
// A length as minutes, seconds and tenths, rounded to the tenth: "0:02.0",
// "1:05.3", "5:00.0". Negative or not finite: "--".
std::size_t formatClock(char* out, std::size_t size, float seconds) noexcept;

// M2b-A2. A time as whole minutes and seconds, rounded down: "0:00", "1:23",
// "10:00". Negative or not finite: "--".
std::size_t formatMinSec(char* out, std::size_t size, float seconds) noexcept;

// A knob's engine value in its unit, by the rules above ("" = plain, two decimals).
std::size_t formatEngineValue(char* out, std::size_t size, float engine, const char* unit) noexcept;
// What the screen shows for a knob: its engine value by the rules above.
std::size_t formatKnob(char* out, std::size_t size, const UiKnobView& k) noexcept;

} // namespace ui
} // namespace audreybrick
