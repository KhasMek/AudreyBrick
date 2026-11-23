// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The screen layout (docs/briefs/M2.md, M2-C): turns a UiSnapshot and the meter
// state into a DrawList. No SDL, no allocation. 1024 x 768 logical pixels, top to
// bottom, each block a panel in the DrawList:
//
//   tab bar        L1 key, the six tabs (current one in inverse video; each as
//                  wide as its name plus an equal margin), R1 key
//   feedback band  stored feedback in dB, bar between L2 and R2 keys with a 0 dB
//                  tick; while A is held the bar is striped, a plate reads
//                  "MAX +12.0 dB" in the half away from the stored-value marker,
//                  and "STORED" labels the big number.
//                  REC: under the bar's left part (x 20, y 158, 16 px, red, at
//                  most 30 characters, so it ends before the "0 dB" label) the
//                  recording readout: REC m:ss [DROP n], REC SAVING, SAVED
//                  <NAME> m:ss, or a REC message (record/RecordView.h).
//   switch box     inverse and "ON" when on, LATCHED / HELD / LATCH+HELD, B key
//                  (lit only for the momentary hold).
//   current tab    one row per bound d-pad binding: arrows (with an X key for X
//                  bindings), name and value at 32 px or more, a position bar.
//                  Rows for the other X state are dimmed with outline arrows;
//                  unbound directions are not shown. While the delay switch is
//                  on, TIME carries a "B" mark. REPEATS has a tick at unity gain.
//                  Unitless values show the plain engine value ("0.85") and
//                  their bars show the same value against its maximum, so
//                  number and bar agree.
//   meter panel    L/R output meters with a dB scale, peak hold, CLIP caps.
//                  M2b-A: whenever the input source is not OFF, a slim IN bar
//                  along the bottom (the input level after gain; the IN label
//                  in inverse video while the input clips).
//   INPUT tab      (M2b-A, in place of the rows) SOURCE row (left/right: OFF and
//                  FILE as boxes, the current one in inverse video), GAIN row
//                  (up/down: the gain in dB at 40 px and a bar with a 0 dB
//                  tick), FILE row (dimmed while OFF: "FILE", "n OF m", the file
//                  name cut with "..", its length, and a status line), dimmed
//                  with outline arrows unless the source is FILE; to their right
//                  a large input meter with a dB scale, peak hold, CLIP cap and
//                  the held peak in dB.
//                  M2b-A3, with the source FILE: the same three rows; the
//                  FILE row shows "n OF m", the name (the cursor's
//                  file while X is held, else the loaded one, or NOTHING LOADED
//                  with HOLD X: CHOOSE FILE), a progress line (bar, "elapsed /
//                  length", LOOP or ONCE, ENDED) and one status line (while
//                  choosing another file: PLAYING: <name>; else CANNOT LOAD
//                  <name>: <reason>, LOADING <name> or CUT TO 10 MIN). "n OF m"
//                  is the position of the file on the name line.
//   pop-up list    (M2b-A3) while X is held after an up/down press, an overlay
//                  panel (PanelItem::overlay) built last, x 8..712 y 184..656:
//                  header FILES n OF m and the mode, eight 32 px names around
//                  the cursor (cursor row in inverse video with ">", PLAY on the
//                  loaded file; other names use the full width), one status
//                  line (CANNOT LOAD <name>: <reason> / LOADING <name> /
//                  PLAYING: <name>), hints X RELEASE: LOAD, Y MODE, SELECT PLAY
//                  AGAIN. It covers the tab's rows and the first three columns
//                  of the value strip, whole rows; the tab bar, feedback band,
//                  switch box, meters and status line stay.
//   INPUT, LIVE    (M2b-B) compact rows: SOURCE y 184 (52 px), GAIN y 242 (72
//                  px, the dB value still at 40 px), DEVICE y 320 (94 px): "DEVICE",
//                  "n OF m", the device name at 24 px (the cursor's while X is
//                  held, else the open one, or NO DEVICE CHOSEN), and one 16 px
//                  line: OPEN: <name> while choosing another device; else, with a
//                  device open, what it delivers ("48000 Hz 1 CH 256 FR") and a
//                  PRE bar (the pre-gain level, so a live device shows while the
//                  gain is at zero); else CANNOT OPEN <name>: <reason> or DEVICE
//                  LOST in inverse video; else HOLD X: CHOOSE DEVICE. Under the
//                  rows and the input meter (shortened to y 184..414) the warning
//                  panel, x 8..872 y 420..472: SPEAKER + MIC CAN HOWL and USE
//                  HEADPHONES OR KEEP GAIN LOW at 24 px, bg on the clip colour
//                  (as the errors), whether or not a device is open.
//                  The SOURCE choices (OFF, FILE, LIVE) are boxes at 24 px on
//                  every source since M2b-B (three no longer fit at 32 px).
//                  The pop-up with the source LIVE lists the devices: DEVICES n
//                  OF m, OPEN on the open device, status CANNOT OPEN / DEVICE
//                  LOST / OPEN: <name>, one hint X RELEASE: OPEN.
//   value strip    all eleven values (tab, name, value, small bar), the current
//                  tab's cells framed; the twelfth cell is the CPU meter.
//   status line    audio spec, dropped commands, controller / keyboard hint;
//                  cut names end in "..".
//   REC border     DrawList::recBorder while recording or finalising: a 4-pixel
//                  red frame on the screen's outermost pixels, drawn by the
//                  renderer after everything (also over the pop-up). Nothing
//                  else is drawn within 4 pixels of the edge.
//   footer         the global buttons; an entry is drawn in inverse video while
//                  its button is held (this is also where FINE, X and Y show).
//   MOD tab        (MOD-B, in place of the rows; x 8..872, y 184..472)
//                  five editor rows (ModRow panels, MOD-C: y 184/222/260/298/
//                  336, 36 px): TARGET (MOD-D, in the SLOT row's place), SHAPE,
//                  RATE (RELEASE for the envelope), DEPTH, POLARITY (MOD-C):
//                  the selected slot's properties. The cursor row is bright
//                  with an accent frame and filled left/right arrows; the
//                  others dimmed with outline arrows. Label at 24 px (x 92),
//                  value at 32 px (x 300). TARGET: the target's name (NONE a
//                  step dimmer), the knob's tab at 16 px after it, and at the
//                  right an X cap with SLOT (x 760..856). RATE: a position bar;
//                  DEPTH: a bar centred on zero (bars x 576..856). POLARITY:
//                  BIPOLAR or UNIPOLAR; with the envelope UNIPOLAR dimmed,
//                  outline arrows even as the cursor row, and FIXED BY
//                  ENVELOPE at 16 px.
//                  Under them four slot cards (ModSlot panels, x 8/226/444/662,
//                  y 378, 210 x 94): the number (inverse on the selected slot,
//                  whose card has the accent frame), shape, target ("NO
//                  TARGET"), rate ("REL" and the release for the envelope),
//                  depth with the polarity at its right ("+/-" or "+") at 16 px,
//                  and the activity bar (6 px) centred on zero showing output *
//                  depth (the output after the polarity), in the accent colour
//                  on an active card (target set, depth not 0).
//                  An active card is on the panel colour, an idle one dark.
//                  MOD-D: normally the cards are dimmed as a group (all text
//                  faint). While X is held (pressed on MOD) the cards are lit
//                  (an active card's text bright, an idle one's dim) and
//                  every row is dimmed as the DELAY tab's other-X rows (dark,
//                  faint text, outline arrows; the cursor row keeps a thin
//                  faint frame), and the X cap on TARGET is lit. The selected
//                  card's frame and number are bright in both states.
//   modulated knob (MOD-B, every tab) while at least one slot with a non-zero
//                  depth targets a knob: a tag in the modulation colour with
//                  the slot numbers ("M1", "M13") at 16 px beside its name
//                  (tab rows: under the name; strip: on the label line or the
//                  value line, wherever it fits; feedback band: after
//                  FEEDBACK), and a thin bar in that colour along the bottom
//                  of its value bar to the moving value clamp(knob + offset)
//                  (RectTag::ModMarker). The printed value stays the knob's
//                  own setting; the marker is at least a 4 px stub (a value
//                  clamped at 0). The strip falls back to "M" and the number
//                  of slots ("M3"), then "M", where the full tag does not
//                  fit. INPUT's gain row the same (the tag under GAIN;
//                  beside it in LIVE's compact row), the marker at
//                  clamp(knob + offset), none moved while the knob is 0. The
//                  switch box: tag after SWITCH; while only the modulation
//                  holds the switch on, the box is dark with a green frame,
//                  ON and MOD in green. With no active slot nothing changes.

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
inline constexpr Color mod{214, 150, 255, 255}; // MOD-B: modulation tags and moving-value markers
} // namespace palette

struct MeterView
{
    float level[2] = {0.0f, 0.0f}; // L, R (linear)
    float hold[2]  = {0.0f, 0.0f};
    bool  clip[2]  = {false, false};
    // M2b-A: the input meter (after gain), with the same ballistics.
    float inLevel = 0.0f;
    float inHold  = 0.0f;
    bool  inClip  = false;
    // M2b-B: the live input's pre-gain level (the mono mix as captured).
    float preLevel = 0.0f;
    float preHold  = 0.0f;
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
