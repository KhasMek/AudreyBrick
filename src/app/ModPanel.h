// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ModPanel: the UI-thread model of the MOD tab (MOD-B, docs/briefs/MOD.md; the
// gestures as reworked in MOD-D). It owns the four slots' settings
// (ModSlotSettings) and the editor's cursor, and sends every changed slot to
// the modulation decorator through a ModUiPort (mod/ModControlSource.h:
// setModSlot() and the read-back; nothing else of the audio side). No SDL.
// Time is passed in (seconds, monotonic), as in ControlModel and
// InputController. Like InputController it binds no knob: InputRouter sends it
// every button event after the ControlModel (which binds nothing on MOD) and
// the INPUT panel have seen it.
//
// MOD-D: the rows are the selected slot's properties, the cards are the four
// slots, and holding X turns left / right to the cards.
//
// Buttons (presses made while the MOD tab is current act; a d-pad press made on
// another tab never does):
//   - d-pad up / down: the cursor between the rows TARGET, SHAPE, RATE, DEPTH,
//     POLARITY (up towards TARGET), one row per press, no wrap, no repeat. A
//     left/right that is held becomes inert (the ramp or repeat stops; it does
//     not move to the new row).
//   - d-pad left / right: the cursor row's value, right increases:
//       TARGET the slot's target through the list (app/ModFormat.h:
//              modTargetAt: NONE, the eleven knobs, INPUT GAIN, DELAY SWITCH),
//              one per press, wrapping at both ends (DELAY SWITCH -> NONE,
//              NONE -> DELAY SWITCH); a held direction repeats from
//              tuning::kHoldDelaySeconds after its press at kTargetRepeatHz (8
//              per second, as the INPUT tab's lists): the n-th repeat at press +
//              delay + (n - 1) / 8 s, so the whole list of 14 goes round in
//              about 1.9 s; at most one step per tick (after a UI stall the
//              backlog is dropped and the next repeat is one period later).
//              Both held: no movement and no backlog. Select
//              (fine) means nothing here. A new target takes effect at once.
//       SHAPE  sine, triangle, square, ramp up, ramp down, exp up, exp down,
//              sample-and-hold, envelope (MOD-C order), no wrap, no repeat;
//       RATE   a tap moves kRateTap (1/100 of the control's travel, about 13 %
//              in frequency), kRateTapFine with Select held; held past
//              tuning::kHoldDelaySeconds it ramps at the knobs' ramp speeds in
//              travel (tuning::kRampStartRate rising to kRampEndRate over
//              kRampAccelSeconds, times kFineRampScale with Select). With the
//              shape Envelope the row is RELEASE and right makes the release
//              LONGER (it moves rateNorm down): right increases the value shown.
//              Clamped 0..1.
//       DEPTH  -1..+1: a tap moves to the next point of a grid of kDepthTap
//              (0.02, 2 %; kDepthTapFine 0.002 with Select) in that direction,
//              so exactly 0 is reached from either side; held it ramps at
//              twice the knob ramp (the depth control's travel is 2: full
//              travel takes as long as a knob's), continuously, through 0;
//              where the ramp ends (release, or made inert) the depth goes to
//              the nearest point of the fine grid (0.2 %), so "0.0%" is always
//              exactly 0.
//       POLARITY (MOD-C) right UNIPOLAR, left BIPOLAR, one step per press (no
//              wrap: right on UNIPOLAR stays). With the shape Envelope the row
//              shows UNIPOLAR dimmed and left / right do nothing (the stored
//              setting is kept for when the shape changes back).
//   - X held (pressed on the MOD tab): left / right select the slot the rows
//     edit, 1..4, wrapping (4 -> 1, 1 -> 4), one step per press (no repeat);
//     up / down do nothing. The cursor row is kept (on DEPTH, X + right lands
//     on the next slot's DEPTH). Select means nothing with X.
//   - Y + left / right: the cursor row back to its default: TARGET None, SHAPE
//     sine, RATE the 1 Hz position (modRateNormFromHz(1.0)), DEPTH 0, POLARITY
//     bipolar (nothing under Envelope, where the row is fixed). Y + up / down:
//     nothing.
//   - Y + X + left / right (either held first): the whole selected slot back to
//     its default (defaultSlot(): all five properties, the stored polarity too);
//     the slot stays selected.
//   - Both directions of an axis down: the second press does nothing.
//   - L1 / R1, and pressing or releasing X or Y, make every d-pad direction
//     that is down inert until it is released (no ramp, no repeat). A Y press
//     made with a direction is inert too (a reset never ramps); so is every
//     direction pressed with X (a value ramp never continues into slot
//     selection, nor the reverse). L1 / R1 while X is held also drops the X
//     state (the cards go back to dimmed; X must be pressed again on MOD).
//   - X pressed on another tab does not count: X + a direction does nothing on
//     MOD until X is released and pressed again there. The same for Y: while a
//     Y pressed on another tab (or held across L1 / R1) is down, a direction
//     does nothing on MOD (no reset, no value step, no slot selection, with or
//     without X) until Y is released and pressed again there.
// The cursor row and the selected slot are kept when the tab is left.
//
// Sending: every change stores the sanitised settings (sanitizeModSlot) and
// sends the slot's complete settings at once; if the port refuses (the queue is
// full) the slot stays "unsent" and tick() sends the latest settings again, so
// the final value of a ramp is never lost.
//
// Log (UI thread, collected for the app's diagnostic log; takeLog()): one line
// per slot once its settings have been unchanged for kLogStableSeconds (0.5 s)
// after a change, e.g.
//   mod: slot 2 triangle -> FeedbackLPFCutoff 3.20 Hz depth +34.0%
//   mod: slot 1 envelope -> Frequency release 50.0 ms depth -20.0%
//   mod: slot 3 rampup -> ReverbMix 0.500 Hz depth +20.0% unipolar   (MOD-C)
// Selecting a slot is not logged (MOD-D: the settings lines say what played).
//
// No allocation except log lines (each guarded; a failed one is dropped).
// UI thread only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app/Buttons.h"
#include "app/ModView.h"
#include "mod/ModControlSource.h"
#include "mod/ModTypes.h"

namespace audreybrick
{

class ModPanel
{
  public:
    static constexpr double kRateTap          = 1.0 / 100.0;
    static constexpr double kRateTapFine      = 1.0 / 1000.0;
    static constexpr double kDepthTap         = 0.02;
    static constexpr double kDepthTapFine     = 0.002;
    static constexpr double kDepthRampScale   = 2.0; // depth travel -1..+1
    static constexpr double kTargetRepeatHz   = 8.0; // MOD-D: a held left / right on TARGET (as kInputCursorRepeatHz)
    static constexpr double kLogStableSeconds = 0.5;

    // The default slot: sine, target None, the 1 Hz rate, depth 0, bipolar.
    static ModSlotSettings defaultSlot() noexcept;

    explicit ModPanel(ModUiPort port = ModUiPort()) noexcept;

    void setPort(ModUiPort port) noexcept { port_ = port; }

    // A button event, after the ControlModel and the INPUT panel. `onModTab`:
    // the MOD tab is current (after the event).
    void onButton(const ButtonEvent& e, double t, bool onModTab) noexcept;
    // As if every button had been released (focus loss, controller removed):
    // the X state is dropped.
    void releaseAll() noexcept;
    // Every UI frame: ramps, the TARGET repeat, unsent settings, log lines.
    void tick(double t) noexcept;

    // The slots (presets, --mod). setSlot() sanitises, stores and sends at
    // once (again from tick() if refused); logged once stable.
    const ModSlotSettings& slot(std::size_t i) const noexcept;
    void                   setSlot(std::size_t i, const ModSlotSettings& s) noexcept;

    // Everything the screen shows, including the read-back (each value once).
    void fill(UiModView& v) const noexcept;
    // Writes the log line of every slot whose line is still waiting for its
    // 0.5 s of stability (the app calls it before shutting down).
    void flushLog() noexcept;
    // Log lines since the last call (appended to `out`; may throw std::bad_alloc).
    void takeLog(std::vector<std::string>& out);

    // --- State (tests, the self-test report) ---------------------------------------------
    std::size_t   currentSlot() const noexcept { return current_; }
    ModRow        row() const noexcept { return row_; }
    bool          choosing() const noexcept { return xArmed_; } // X held, pressed on MOD: left / right select the slot
    bool          unsent(std::size_t i) const noexcept { return i < kModSlots && dirty_[i]; }
    std::uint64_t sendsRefused() const noexcept { return refused_; }
    std::uint64_t sendsAccepted() const noexcept { return accepted_; }

  private:
    struct Press
    {
        bool   down       = false;
        bool   inert      = false;
        bool   repeat     = false; // left / right on TARGET: repeats step the target (tick)
        double nextRepeat = 0.0;   // TARGET: when the next repeat is due
        double time       = 0.0;
    };
    static std::size_t idx(Button b) noexcept { return static_cast<std::size_t>(b); }
    bool               down(Button b) const noexcept { return press_[idx(b)].down; }
    bool               yHeld() const noexcept { return down(Button::Y) && yArmed_; } // Y pressed on MOD

    void change(std::size_t i, const ModSlotSettings& s) noexcept;
    void trySend(std::size_t i) noexcept;
    void makeDpadInert() noexcept;
    void settleRamp() noexcept; // a DEPTH ramp ended: to the fine grid
    void onDpad(Button b, double t) noexcept;
    void tapValue(int dir) noexcept;
    void resetRow() noexcept;
    void selectSlot(int dir) noexcept;
    void stepTarget(long steps) noexcept;
    void ramp(double prev, double t) noexcept;
    void targetRepeat(double t) noexcept;
    void logSlot(std::size_t i) noexcept;
    void log(const char* line) noexcept;

    ModUiPort       port_;
    ModSlotSettings slots_[kModSlots];
    bool            dirty_[kModSlots]      = {};
    bool            logPending_[kModSlots] = {};
    double          changedAt_[kModSlots]  = {};
    Press           press_[kButtonCount];
    std::size_t     current_     = 0;
    ModRow          row_         = ModRow::Target;
    bool            xArmed_      = false;
    bool            yArmed_      = false; // Y held, pressed on the MOD tab (as xArmed_)
    double          now_         = 0.0; // the latest time seen (events and ticks)
    double          lastTick_    = 0.0;
    bool            ticked_      = false;
    bool            depthRamped_ = false; // a DEPTH ramp has moved since its press
    std::size_t     rampSlot_    = 0;
    std::uint64_t   refused_     = 0;
    std::uint64_t   accepted_    = 0;
    std::vector<std::string> log_;
};

} // namespace audreybrick
