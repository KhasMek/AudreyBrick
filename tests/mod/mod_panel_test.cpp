// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-B (docs/briefs/MOD.md): the MOD tab's panel (app/ModPanel.h) and its
// formatting (app/ModFormat.h) and view helpers (app/ModView.h). No SDL.
//
//   - defaults; the cursor rows (no wrap); SHAPE steps (no wrap);
//   - RATE: tap 1/100, fine 1/1000, the ramp at the knobs' speeds (and fine),
//     clamps, RELEASE (envelope) with right = longer;
//   - DEPTH: the 2 % grid (0.2 % fine), exactly 0 from either side, an off-grid
//     value, clamps, the ramp at twice the knob speed passing through 0;
//   - MOD-D: the TARGET row: left/right step the target through the list,
//     wrapping both ways, at once; the held repeat at 8 per second after the
//     hold delay (wrapping too), both held (no backlog), Select changes
//     nothing; X held: left/right select the slot, wrapping both ways, one
//     per press, up/down nothing, the cursor row kept; no pop-up any more
//     (X + up/down changes nothing and logs nothing);
//   - MOD-C: the POLARITY row (right UNIPOLAR, left BIPOLAR, no wrap, one step
//     per press, nothing under the envelope, Y resets it, the whole-slot reset
//     too), the new shapes in the SHAPE order, the log's " unipolar";
//   - Y + left/right resets the cursor row (TARGET: None), Y + up/down nothing;
//     MOD-D: Y + X + left/right resets the whole selected slot (either held
//     first);
//   - inert rules: a tab change, X or Y pressed or released, a row change;
//     presses made on another tab; X pressed on another tab; MOD-D: a ramp
//     does not continue into slot selection nor the reverse, a held TARGET
//     repeat stops at X, a tab change with X held drops the X state; MOD-D
//     review: Y pressed on another tab (alone, with X, held across a tab
//     change) does not count; a stalled tick moves the TARGET repeat one step;
//     a DEPTH ramp ended by releaseAll() lands on the 0.2 % grid;
//   - resend after a full queue (the final value of a ramp reaches the engine);
//   - cursor memory across tab changes; the log line after 0.5 s stable;
//   - fill(): the read-back; the formatting of rate, release and depth; the
//     target list; the tag text, effective input peak and gain.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app/ModFormat.h"
#include "app/ModPanel.h"
#include "app/ModView.h"
#include "control/ControlTuning.h"
#include "mod/ModControlSource.h"
#include "mod_test_common.h"

namespace
{
using namespace audreybrick;
using modtest::check;

constexpr double kHold = tuning::kHoldDelaySeconds;
constexpr double kTargetHz = ModPanel::kTargetRepeatHz;

// The ramp law (as ModPanel and the knobs): distance after tau seconds.
double rampDist(double tau)
{
    if(tau <= 0.0)
        return 0.0;
    const double s = tuning::kRampStartRate, e = tuning::kRampEndRate, a = tuning::kRampAccelSeconds;
    if(tau <= a)
        return s * tau + (e - s) * tau * tau / (2.0 * a);
    return s * a + (e - s) * a / 2.0 + e * (tau - a);
}

bool near(double a, double b, double tol = 1e-6)
{
    return std::fabs(a - b) <= tol;
}

struct Rig
{
    modtest::FixedSource inner;
    ModControlSource     dec;
    ModPanel             panel;
    double               t     = 1.0;
    bool                 onMod = true;

    explicit Rig(std::size_t queue = ModControlSource::kQueueCapacity) : dec(inner, queue), panel(ModUiPort(dec)) {}

    void press(Button b) { panel.onButton({b, true}, t, onMod); }
    void release(Button b) { panel.onButton({b, false}, t, onMod); }
    void tap(Button b)
    {
        press(b);
        release(b);
    }
    void tick(double dt = 0.0)
    {
        t += dt;
        panel.tick(t);
    }
    // Frame by frame (60 per second) up to the absolute time `until`.
    void tickTo(double until)
    {
        while(t + 1.0 / 60.0 < until)
            tick(1.0 / 60.0);
        tick(until - t);
    }
    // One audio chunk of one control block: applies the queued settings and
    // publishes the read-back.
    void drain(int blocks = 1)
    {
        for(int i = 0; i < blocks; ++i)
        {
            dec.beginChunk(nullptr, 4, nullptr);
            ControlFrame f;
            dec.read(f);
            dec.endChunk();
        }
    }
    const ModSlotSettings& cur() const { return panel.slot(panel.currentSlot()); }
    void goRow(ModRow r)
    {
        while(panel.row() != r)
            tap(static_cast<int>(panel.row()) < static_cast<int>(r) ? Button::DpadDown : Button::DpadUp);
    }
};

bool same(const ModSlotSettings& a, const ModSlotSettings& b)
{
    return a.shape == b.shape && a.target == b.target && a.rateNorm == b.rateNorm && a.depth == b.depth &&
           a.polarity == b.polarity;
}

std::string fmtRate(float n, ModShape s, bool upper = true)
{
    char b[32];
    formatModRate(b, sizeof b, n, s, upper);
    return b;
}
std::string fmtHz(double hz)
{
    char b[32];
    formatModRateHz(b, sizeof b, hz);
    return b;
}
std::string fmtRel(double s, bool upper = true)
{
    char b[32];
    formatModRelease(b, sizeof b, s, upper);
    return b;
}
std::string fmtDepth(float d)
{
    char b[32];
    formatModDepth(b, sizeof b, d);
    return b;
}
} // namespace

int main()
{
    static_assert(noexcept(std::declval<ModPanel&>().onButton(ButtonEvent{}, 0.0, true)), "onButton is noexcept");
    static_assert(noexcept(std::declval<ModPanel&>().tick(0.0)), "tick is noexcept");
    static_assert(noexcept(std::declval<const ModPanel&>().fill(std::declval<UiModView&>())), "fill is noexcept");

    const float k1Hz = modRateNormFromHz(1.0);

    // ---- Defaults ------------------------------------------------------------------------------
    {
        Rig  r;
        bool ok = true;
        for(std::size_t i = 0; i < kModSlots; ++i)
        {
            const ModSlotSettings& s = r.panel.slot(i);
            ok = ok && s.shape == ModShape::Sine && s.target == ModTarget::None && s.rateNorm == k1Hz && s.depth == 0.0f &&
                 s.polarity == ModPolarity::Bipolar;
        }
        check(ok && near(modRateHz(k1Hz), 1.0, 1e-5),
              "defaults: every slot sine, target None, rate 1 Hz, depth 0, bipolar");
        r.drain();
        check(r.panel.row() == ModRow::Target && r.panel.currentSlot() == 0 && r.dec.settingsApplied() == 0 &&
                  !r.panel.choosing(),
              "start: cursor on TARGET, slot 1, nothing sent (the decorator starts with every target None)");
    }

    // ---- Rows and SHAPE ----------------------------------------------------------------------------
    {
        Rig r;
        for(int i = 0; i < 7; ++i)
            r.tap(Button::DpadDown);
        check(r.panel.row() == ModRow::Polarity, "down: TARGET -> SHAPE -> RATE -> DEPTH -> POLARITY, no wrap");
        for(int i = 0; i < 6; ++i)
            r.tap(Button::DpadUp);
        check(r.panel.row() == ModRow::Target, "up: back to TARGET, no wrap");
        r.goRow(ModRow::Shape);
        const ModShape order[] = {ModShape::Triangle, ModShape::Square,     ModShape::RampUp,
                                  ModShape::RampDown, ModShape::ExpUp,      ModShape::ExpDown,
                                  ModShape::SampleHold, ModShape::Envelope, ModShape::Envelope};
        bool ok = true;
        for(ModShape s : order)
        {
            r.tap(Button::DpadRight);
            ok = ok && r.cur().shape == s;
        }
        check(ok, "SHAPE right: sine, triangle, square, ramp up, ramp down, exp up, exp down, sample-and-hold, "
                  "envelope, no wrap");
        for(int i = 0; i < 10; ++i)
            r.tap(Button::DpadLeft);
        check(r.cur().shape == ModShape::Sine, "SHAPE left: back to sine, no wrap");
        // Holding SHAPE does not repeat.
        r.press(Button::DpadRight);
        r.tick(0.0);
        r.tick(2.0);
        r.release(Button::DpadRight);
        check(r.cur().shape == ModShape::Triangle, "SHAPE held: one step only");
    }

    // ---- RATE: taps, fine, clamps, ramps, RELEASE ---------------------------------------------------
    {
        Rig r;
        r.goRow(ModRow::Rate);
        const float before = r.cur().rateNorm;
        r.tap(Button::DpadRight);
        const float tap1 = r.cur().rateNorm;
        check(near(tap1 - before, 0.01, 1e-6) && near(modRateHz(tap1) / modRateHz(before), std::pow(2.0e5, 0.01), 1e-4),
              "RATE tap: 1/100 of travel (x" + std::to_string(std::pow(2.0e5, 0.01)) + " in frequency)");
        r.press(Button::Select);
        r.tap(Button::DpadLeft);
        r.release(Button::Select);
        check(near(r.cur().rateNorm, tap1 - 0.001, 1e-6), "RATE fine tap (Select): 1/1000");
        ModSlotSettings s = r.cur();
        s.rateNorm        = 0.995f;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight);
        check(r.cur().rateNorm == 1.0f, "RATE clamps at 1 (2 kHz)");
        s.rateNorm = 0.004f;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadLeft);
        check(r.cur().rateNorm == 0.0f, "RATE clamps at 0 (0.01 Hz)");

        // Ramp: press, tick through the hold delay and 1 s beyond.
        s.rateNorm = 0.2f;
        r.panel.setSlot(0, s);
        r.tick(0.0);
        const double p0 = r.t;
        r.press(Button::DpadRight); // tap: +0.01
        r.tick(kHold - 0.01);
        const float atDelay = r.cur().rateNorm;
        for(int i = 0; i < 60; ++i)
            r.tick(1.0 / 60.0); // 1 s at 60 Hz
        const double tau = r.t - (p0 + kHold);
        r.release(Button::DpadRight);
        check(near(atDelay, 0.21, 1e-6) && near(r.cur().rateNorm, 0.21 + rampDist(tau), 1e-4),
              "RATE held: the tap, nothing until the hold delay, then the knob ramp (" +
                  std::to_string(r.cur().rateNorm) + " vs " + std::to_string(0.21 + rampDist(tau)) + ")");
        s.rateNorm = 0.2f;
        r.panel.setSlot(0, s);
        r.tick(0.0);
        const double p1 = r.t;
        r.press(Button::Select);
        r.press(Button::DpadLeft);
        for(int i = 0; i < 75; ++i)
            r.tick(1.0 / 60.0);
        const double tau1 = r.t - (p1 + kHold);
        r.release(Button::DpadLeft);
        r.release(Button::Select);
        check(near(r.cur().rateNorm, 0.2 - 0.001 - rampDist(tau1) * tuning::kFineRampScale, 1e-4),
              "RATE fine ramp (Select): the fine tap, then the ramp at 1/8 speed");

        // RELEASE (envelope): right makes the release longer.
        s.shape    = ModShape::Envelope;
        s.rateNorm = 0.5f;
        r.panel.setSlot(0, s);
        const double relBefore = modReleaseSeconds(r.cur().rateNorm);
        r.tap(Button::DpadRight);
        check(near(r.cur().rateNorm, 0.49, 1e-6) && modReleaseSeconds(r.cur().rateNorm) > relBefore,
              "RELEASE right: longer release (rateNorm down by 1/100)");
        UiModView v;
        r.panel.fill(v);
        check(v.row == ModRow::Rate && v.slot[0].shape == ModShape::Envelope, "fill: the RATE row with the envelope");
    }

    // ---- POLARITY (MOD-C) ---------------------------------------------------------------------------
    {
        Rig r;
        r.goRow(ModRow::Polarity);
        r.tap(Button::DpadRight);
        const bool uni = r.cur().polarity == ModPolarity::Unipolar;
        r.tap(Button::DpadRight);
        const bool stays = r.cur().polarity == ModPolarity::Unipolar;
        r.tap(Button::DpadLeft);
        const bool bi = r.cur().polarity == ModPolarity::Bipolar;
        r.tap(Button::DpadLeft);
        check(uni && stays && bi && r.cur().polarity == ModPolarity::Bipolar,
              "POLARITY: right UNIPOLAR, left BIPOLAR, no wrap");
        // Held: one step per press (no ramp, no toggling back and forth).
        r.press(Button::DpadRight);
        r.tick(0.0);
        r.tick(2.0);
        r.release(Button::DpadRight);
        check(r.cur().polarity == ModPolarity::Unipolar, "POLARITY held: one step only");
        // Sent to the decorator like every change.
        r.drain();
        check(r.dec.matrix().slot(0).polarity == ModPolarity::Unipolar, "POLARITY: reaches the decorator");
        // Y + left / right: back to BIPOLAR; nothing else changes.
        ModSlotSettings s = r.cur();
        s.depth           = 0.5f;
        s.target          = ModTarget::ReverbMix;
        r.panel.setSlot(0, s);
        r.press(Button::Y);
        r.tap(Button::DpadLeft);
        r.release(Button::Y);
        check(r.cur().polarity == ModPolarity::Bipolar && r.cur().depth == 0.5f && r.cur().target == ModTarget::ReverbMix,
              "Y + left on POLARITY: BIPOLAR, nothing else");
        // The whole-slot reset (MOD-D: Y + X + left / right) makes it bipolar too.
        s.polarity = ModPolarity::Unipolar;
        r.panel.setSlot(0, s);
        r.press(Button::Y);
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.release(Button::X);
        r.release(Button::Y);
        check(same(r.cur(), ModPanel::defaultSlot()) && r.panel.currentSlot() == 0,
              "Y + X + right: the whole slot, bipolar too; the slot stays selected");
        // Under the envelope: left / right do nothing (the stored setting stays).
        s          = ModPanel::defaultSlot();
        s.shape    = ModShape::Envelope;
        s.polarity = ModPolarity::Bipolar;
        r.panel.setSlot(0, s);
        r.goRow(ModRow::Polarity);
        const std::uint64_t sent = r.panel.sendsAccepted();
        r.tap(Button::DpadRight);
        r.tap(Button::DpadLeft);
        check(r.cur().polarity == ModPolarity::Bipolar && r.panel.sendsAccepted() == sent,
              "POLARITY under the envelope: left / right do nothing");
        s.polarity = ModPolarity::Unipolar;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadLeft);
        check(r.cur().polarity == ModPolarity::Unipolar && modEffectivePolarity(r.cur()) == ModPolarity::Unipolar,
              "POLARITY under the envelope: the stored unipolar is kept");
        // MOD-C review: Y + left / right does nothing there either.
        const std::uint64_t sent2 = r.panel.sendsAccepted();
        r.press(Button::Y);
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadRight);
        r.release(Button::Y);
        check(r.cur().polarity == ModPolarity::Unipolar && r.panel.sendsAccepted() == sent2,
              "Y + left / right on POLARITY under the envelope: nothing (the stored unipolar is kept)");
        // ... back to an LFO shape: the stored setting applies again.
        r.goRow(ModRow::Shape);
        r.tap(Button::DpadLeft);
        check(r.cur().shape == ModShape::SampleHold && r.cur().polarity == ModPolarity::Unipolar &&
                  modEffectivePolarity(r.cur()) == ModPolarity::Unipolar,
              "envelope -> S&H: the stored polarity (unipolar) applies");
        // A move onto POLARITY with left / right held: inert (no step).
        r.goRow(ModRow::Depth);
        r.press(Button::DpadLeft);
        r.tap(Button::DpadDown);
        r.tick(2.0);
        r.release(Button::DpadLeft);
        check(r.panel.row() == ModRow::Polarity && r.cur().polarity == ModPolarity::Unipolar,
              "a held left does not act on POLARITY after a row change");
    }

    // ---- DEPTH: grid, zero, clamps, ramp through zero ---------------------------------------------
    {
        Rig r;
        r.goRow(ModRow::Depth);
        r.tap(Button::DpadRight);
        check(r.cur().depth == 0.02f, "DEPTH tap: +2 %");
        r.tap(Button::DpadLeft);
        check(r.cur().depth == 0.0f && !std::signbit(r.cur().depth), "DEPTH back to exactly 0 (+0.0)");
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadRight);
        check(r.cur().depth == 0.0f, "DEPTH from -2 % back to exactly 0");
        r.press(Button::Select);
        r.tap(Button::DpadRight);
        r.release(Button::Select);
        check(r.cur().depth == 0.002f, "DEPTH fine tap: 0.2 %");
        ModSlotSettings s = r.cur();
        s.depth           = 0.013f;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadRight);
        check(r.cur().depth == 0.02f, "DEPTH off the grid (1.3 %), right: the next grid point, 2 %");
        s.depth = 0.013f;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadLeft);
        check(r.cur().depth == 0.0f, "DEPTH off the grid (1.3 %), left: exactly 0");
        s.depth = -0.013f;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadRight);
        check(r.cur().depth == 0.0f, "DEPTH off the grid (-1.3 %), right: exactly 0");
        for(int i = 0; i < 60; ++i)
            r.tap(Button::DpadRight);
        check(r.cur().depth == 1.0f, "DEPTH clamps at +100 %");
        for(int i = 0; i < 120; ++i)
            r.tap(Button::DpadLeft);
        check(r.cur().depth == -1.0f, "DEPTH clamps at -100 %");
        // 50 taps up from 0 land on +100 % exactly; grid points stay exact.
        s.depth = 0.0f;
        r.panel.setSlot(0, s);
        bool grid = true;
        for(int i = 1; i <= 50; ++i)
        {
            r.tap(Button::DpadRight);
            grid = grid && near(r.cur().depth, i * 0.02, 1e-6);
        }
        check(grid && r.cur().depth == 1.0f, "DEPTH: 50 taps from 0 are 2 % each and end at +100 %");

        // A ramp through 0 does not stop there; it runs at twice the knob speed.
        s.depth = -0.1f;
        r.panel.setSlot(0, s);
        r.tick(0.0);
        const double p0 = r.t;
        r.press(Button::DpadRight); // tap: -0.08
        for(int i = 0; i < 75; ++i)
            r.tick(1.0 / 60.0);
        const double tau = r.t - (p0 + kHold);
        r.release(Button::DpadRight);
        const double want = -0.08 + 2.0 * rampDist(tau);
        const double k    = r.cur().depth / ModPanel::kDepthTapFine;
        check(want > 0.1 && near(r.cur().depth, want, 0.0011) && near(k, std::round(k), 1e-3),
              "DEPTH held: the ramp (twice the knob speed) passes through 0 without stopping, and ends on the 0.2 % "
              "grid (" + std::to_string(r.cur().depth) + ")");

        // MOD-B review: wherever a ramp (coarse or fine) stops, "0.0%" means exactly 0.
        int misleading = 0;
        for(int fine = 0; fine < 2; ++fine)
            for(int stop = 1; stop < 200; ++stop)
            {
                Rig q;
                q.goRow(ModRow::Depth);
                ModSlotSettings z = q.cur();
                z.depth           = fine ? -0.04f : -0.3f;
                q.panel.setSlot(0, z);
                q.tick(0.0);
                if(fine)
                    q.press(Button::Select);
                q.press(Button::DpadRight);
                for(int f = 0; f < stop; ++f)
                    q.tick(1.0 / 60.0);
                q.release(Button::DpadRight);
                if(fine)
                    q.release(Button::Select);
                misleading += fmtDepth(q.cur().depth) == "0.0%" && q.cur().depth != 0.0f;
            }
        check(misleading == 0, "DEPTH ramps stopped at 398 different moments: never \"0.0%\" with a non-zero depth");
    }

    // ---- MOD-D: the TARGET row ---------------------------------------------------------------------
    {
        Rig r;
        check(r.panel.row() == ModRow::Target, "TARGET is the first row");
        r.tap(Button::DpadRight);
        r.drain();
        check(r.cur().target == ModTarget::FeedbackGain && r.dec.matrix().slot(0).target == ModTarget::FeedbackGain,
              "TARGET right: None -> FEEDBACK (the list's first knob), sent at once");
        r.tap(Button::DpadRight);
        check(r.cur().target == ModTarget::Frequency, "TARGET right: FEEDBACK -> FREQUENCY");
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadLeft);
        check(r.cur().target == ModTarget::None, "TARGET left: back to None");
        r.tap(Button::DpadLeft);
        check(r.cur().target == ModTarget::DelaySwitch, "TARGET left on None wraps to DELAY SWITCH");
        r.tap(Button::DpadRight);
        check(r.cur().target == ModTarget::None, "TARGET right on DELAY SWITCH wraps to None");
        // Fourteen taps go round once.
        bool round = true;
        for(std::size_t i = 1; i <= 14; ++i)
        {
            r.tap(Button::DpadRight);
            round = round && r.cur().target == modTargetAt(i % 14);
        }
        check(round && r.cur().target == ModTarget::None, "TARGET: fourteen taps right visit every target and wrap");
        // Select (fine) means nothing here: one step as without it.
        r.press(Button::Select);
        r.tap(Button::DpadRight);
        r.release(Button::Select);
        check(r.cur().target == ModTarget::FeedbackGain, "TARGET with Select: an ordinary step");
        // The other properties do not change.
        ModSlotSettings s = ModPanel::defaultSlot();
        s.shape           = ModShape::Square;
        s.depth           = 0.4f;
        s.target          = ModTarget::ReverbMix;
        r.panel.setSlot(0, s);
        r.tap(Button::DpadRight);
        check(r.cur().target == ModTarget::ReverbDecay && r.cur().shape == ModShape::Square && r.cur().depth == 0.4f,
              "TARGET right: only the target changes");

        // Held: the first step at the press, then 8 per second from the hold delay.
        r.panel.setSlot(0, ModPanel::defaultSlot());
        r.tick(0.0);
        const double p0 = r.t;
        r.press(Button::DpadRight); // +1: FEEDBACK
        r.tickTo(p0 + kHold - 0.001);
        const std::size_t atDelay = modTargetListIndex(r.cur().target);
        r.tickTo(p0 + kHold + 0.5); // repeats due: floor(0.5 * 8) + 1 = 5
        const std::size_t at05 = modTargetListIndex(r.cur().target);
        check(atDelay == 1 && at05 == 6,
              "TARGET held right: one step at the press, nothing until the hold delay, then 8 per second (" +
                  std::to_string(at05) + ")");
        r.tickTo(p0 + kHold + 1.625); // 14 repeats: 15 steps: wrapped round to position 1
        check(modTargetListIndex(r.cur().target) == 1, "TARGET held: the repeat wraps (15 steps from None: FEEDBACK)");
        // Both held: no movement, no backlog.
        r.press(Button::DpadLeft);
        const ModTarget both = r.cur().target;
        r.tick(2.0);
        check(r.cur().target == both, "TARGET: both directions held, nothing moves");
        r.release(Button::DpadLeft);
        r.tick(0.01);
        check(r.cur().target == both, "TARGET: releasing one of both: no backlog of repeats");
        r.release(Button::DpadRight);
        // Held left repeats backwards and wraps.
        r.panel.setSlot(0, ModPanel::defaultSlot());
        r.tick(0.0);
        const double p1 = r.t;
        r.press(Button::DpadLeft); // None -> DELAY SWITCH
        r.tickTo(p1 + kHold + 0.25); // 3 repeats: DELAY SWITCH -> INPUT GAIN -> VOLUME -> REPEATS
        r.release(Button::DpadLeft);
        check(r.cur().target == ModTarget::EchoDelayFeedback, "TARGET held left: repeats backwards from the wrap");
        const ModTarget held = r.cur().target;
        r.tick(1.0);
        check(r.cur().target == held, "TARGET: released, the repeat stops");
        // MOD-D review: after a long UI stall the held repeat moves one step, not
        // the backlog, and the next repeat is due one period after that tick.
        r.panel.setSlot(0, ModPanel::defaultSlot());
        r.tick(0.0);
        const double p2 = r.t;
        r.press(Button::DpadRight);  // None -> FEEDBACK (1)
        r.tickTo(p2 + kHold + 0.1);  // the first repeat (2)
        const std::size_t beforeStall = modTargetListIndex(r.cur().target);
        r.tick(1.0); // a 1 s stall: 8 repeats due, one step (3)
        const std::size_t afterStall = modTargetListIndex(r.cur().target);
        r.tick(1.0 / kTargetHz - 0.01); // not yet due
        const std::size_t early = modTargetListIndex(r.cur().target);
        r.tick(0.02); // due one period after the stalled tick (4)
        const std::size_t next = modTargetListIndex(r.cur().target);
        r.tickTo(r.t + 0.5); // then 8 per second again: 4 more (8)
        const std::size_t later = modTargetListIndex(r.cur().target);
        r.release(Button::DpadRight);
        check(beforeStall == 2 && afterStall == 3 && early == 3 && next == 4 && later == 8,
              "TARGET held: a stalled tick moves one step, the backlog is dropped, the next repeat one period later (" +
                  std::to_string(beforeStall) + " " + std::to_string(afterStall) + " " + std::to_string(early) + " " +
                  std::to_string(next) + " " + std::to_string(later) + ")");
        // The list's order.
        const ModTarget want[] = {ModTarget::None,          ModTarget::FeedbackGain,      ModTarget::Frequency,
                                  ModTarget::FeedbackBody,  ModTarget::FeedbackLPFCutoff, ModTarget::FeedbackHPFCutoff,
                                  ModTarget::ReverbMix,     ModTarget::ReverbDecay,       ModTarget::EchoDelayTime,
                                  ModTarget::EchoDelaySend, ModTarget::EchoDelayFeedback, ModTarget::OutputVolume,
                                  ModTarget::InputGain,     ModTarget::DelaySwitch};
        bool order = kModTargetListSize == 14;
        for(std::size_t i = 0; i < 14; ++i)
            order = order && modTargetAt(i) == want[i] && modTargetListIndex(want[i]) == i;
        check(order && modTargetAt(99) == ModTarget::None,
              "target list: None, FEEDBACK, LOOP's, FILTER's, REVERB's, DELAY's, OUT's, INPUT GAIN, DELAY SWITCH");
    }

    // ---- MOD-D: X selects the slot -----------------------------------------------------------------------
    {
        Rig r;
        r.press(Button::X);
        check(r.panel.choosing(), "X pressed on MOD: choosing (the cards lit)");
        r.tap(Button::DpadRight);
        check(r.panel.currentSlot() == 1, "X + right: slot 2");
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight);
        check(r.panel.currentSlot() == 3, "X + right: slot 4");
        r.tap(Button::DpadRight);
        check(r.panel.currentSlot() == 0, "X + right on slot 4 wraps to slot 1");
        r.tap(Button::DpadLeft);
        check(r.panel.currentSlot() == 3, "X + left on slot 1 wraps to slot 4");
        // Held: one step per press (no repeat).
        r.tick(0.0);
        r.press(Button::DpadLeft);
        r.tick(kHold + 2.0);
        r.release(Button::DpadLeft);
        check(r.panel.currentSlot() == 2, "X + left held: one step only");
        // Up / down do nothing (no row change, no pop-up, no log).
        const std::uint64_t sent = r.panel.sendsAccepted();
        r.tap(Button::DpadDown);
        r.tap(Button::DpadUp);
        r.tick(0.0);
        r.tick(2.0);
        std::vector<std::string> log;
        r.panel.takeLog(log);
        check(r.panel.row() == ModRow::Target && r.panel.currentSlot() == 2 && r.panel.sendsAccepted() == sent &&
                  log.empty(),
              "X + up / down: nothing (no row change, nothing sent, nothing logged: the pop-up is gone)");
        // Select with X: still one slot per press.
        r.press(Button::Select);
        r.tap(Button::DpadRight);
        r.release(Button::Select);
        check(r.panel.currentSlot() == 3, "X + Select + right: an ordinary slot step");
        // Both directions down: the second press does nothing.
        r.press(Button::DpadRight); // slot 1
        r.press(Button::DpadLeft);
        r.release(Button::DpadLeft);
        r.release(Button::DpadRight);
        check(r.panel.currentSlot() == 0, "X + right with left pressed too: one step");
        r.release(Button::X);
        check(!r.panel.choosing() && r.panel.slot(0).target == ModTarget::None && r.panel.sendsAccepted() == sent,
              "releasing X: rows again; slot selection changes no settings");
        // Selecting changes which slot the rows edit; the cursor row is kept.
        r.goRow(ModRow::Depth);
        r.tap(Button::DpadRight); // slot 1: +2 %
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.release(Button::X);
        check(r.panel.currentSlot() == 1 && r.panel.row() == ModRow::Depth,
              "X + right on DEPTH: the next slot's DEPTH (the cursor row kept)");
        r.tap(Button::DpadLeft); // slot 2: -2 %
        check(r.panel.slot(0).depth == 0.02f && r.panel.slot(1).depth == -0.02f,
              "the rows edit the selected slot (slot 1 +2 %, slot 2 -2 %)");
        r.goRow(ModRow::Target);
        r.press(Button::X);
        r.tap(Button::DpadLeft);
        r.tap(Button::DpadLeft);
        r.release(Button::X);
        r.tap(Button::DpadRight);
        check(r.panel.currentSlot() == 3 && r.panel.row() == ModRow::Target &&
                  r.panel.slot(3).target == ModTarget::FeedbackGain && r.panel.slot(1).target == ModTarget::None,
              "X + left twice on TARGET: slot 4's TARGET; right steps slot 4's target only");
        UiModView v;
        r.press(Button::X);
        r.panel.fill(v);
        const bool held = v.choosing && v.current == 3 && v.row == ModRow::Target;
        r.release(Button::X);
        r.panel.fill(v);
        check(held && !v.choosing, "fill: choosing while X is held, the selected slot and the row");
    }

    // ---- Y: resets ------------------------------------------------------------------------------------
    {
        Rig             r;
        ModSlotSettings s;
        s.shape    = ModShape::Square;
        s.target   = ModTarget::ReverbMix;
        s.rateNorm = 0.9f;
        s.depth    = 0.5f;
        r.panel.setSlot(0, s);
        r.goRow(ModRow::Shape);
        r.press(Button::Y);
        r.tap(Button::DpadUp); // Y + up: nothing (the row stays)
        check(r.panel.row() == ModRow::Shape && same(r.cur(), sanitizeModSlot(s)), "Y + up/down: nothing");
        r.tap(Button::DpadRight);
        check(r.cur().shape == ModShape::Sine && r.cur().depth == 0.5f, "Y + right on SHAPE: sine, nothing else");
        r.release(Button::Y);
        r.goRow(ModRow::Rate);
        r.press(Button::Y);
        r.press(Button::DpadLeft);
        r.tick(2.0); // a reset press never ramps
        r.release(Button::DpadLeft);
        check(r.cur().rateNorm == k1Hz, "Y + left on RATE: 1 Hz, and holding it does not ramp");
        r.release(Button::Y);
        r.goRow(ModRow::Depth);
        r.press(Button::Y);
        r.tap(Button::DpadRight);
        r.release(Button::Y);
        check(r.cur().depth == 0.0f && r.cur().target == ModTarget::ReverbMix, "Y + right on DEPTH: 0, target kept");
        // TARGET: Y + left / right: None, the rest kept.
        s.depth = -0.3f;
        r.panel.setSlot(0, s);
        r.goRow(ModRow::Target);
        r.press(Button::Y);
        r.press(Button::DpadLeft);
        r.tick(0.0);
        r.tick(kHold + 1.0); // a reset press never repeats
        r.release(Button::DpadLeft);
        r.release(Button::Y);
        check(r.cur().target == ModTarget::None && r.cur().depth == -0.3f && r.cur().shape == ModShape::Square &&
                  r.panel.currentSlot() == 0,
              "Y + left on TARGET: None, the rest kept, held: no repeat");
        r.panel.setSlot(0, s);
        r.press(Button::Y);
        r.tap(Button::DpadRight);
        r.release(Button::Y);
        check(r.cur().target == ModTarget::None, "Y + right on TARGET: None");
        // Y + X + left / right: the whole selected slot (either held first), any row.
        ModSlotSettings u = s;
        u.polarity        = ModPolarity::Unipolar;
        r.panel.setSlot(0, u);
        r.goRow(ModRow::Rate);
        r.press(Button::Y);
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.release(Button::X);
        r.release(Button::Y);
        check(same(r.cur(), ModPanel::defaultSlot()) && r.panel.currentSlot() == 0 && r.panel.row() == ModRow::Rate,
              "Y then X + right: the whole slot back to its default (all five properties); slot and row kept");
        // X first, on slot 3; the other slots untouched.
        r.panel.setSlot(0, u);
        r.panel.setSlot(2, u);
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight); // slot 3
        r.press(Button::Y);
        r.tap(Button::DpadLeft);
        check(r.panel.currentSlot() == 2 && same(r.panel.slot(2), ModPanel::defaultSlot()) &&
                  same(r.panel.slot(0), sanitizeModSlot(u)),
              "X then Y + left: slot 3 reset (no slot step), slot 1 untouched");
        r.release(Button::Y);
        r.tap(Button::DpadLeft);
        r.release(Button::X);
        check(r.panel.currentSlot() == 1, "... and X + left after Y is released selects again (slot 2)");
        // Y + X + up / down: nothing.
        r.panel.setSlot(1, u);
        r.press(Button::X);
        r.press(Button::Y);
        r.tap(Button::DpadUp);
        r.tap(Button::DpadDown);
        r.release(Button::Y);
        r.release(Button::X);
        check(same(r.panel.slot(1), sanitizeModSlot(u)) && r.panel.row() == ModRow::Rate,
              "Y + X + up / down: nothing");
    }

    // ---- Inert rules ----------------------------------------------------------------------------------
    {
        Rig r;
        r.goRow(ModRow::Rate);
        ModSlotSettings s = r.cur();
        s.rateNorm        = 0.3f;
        r.panel.setSlot(0, s);
        r.tick(0.0);
        r.press(Button::DpadRight);
        r.tick(kHold + 0.2);
        const float ramping = r.cur().rateNorm;
        r.onMod = false;
        r.tap(Button::R1); // tab change while held
        r.tick(1.0);
        check(ramping > 0.31f && r.cur().rateNorm == ramping, "a tab change stops the ramp");
        r.release(Button::DpadRight);
        r.onMod = true;
        r.tap(Button::L1);

        r.press(Button::DpadRight);
        r.tick(kHold + 0.2);
        r.press(Button::X);
        const float atX = r.cur().rateNorm;
        r.tick(1.0);
        check(r.cur().rateNorm == atX, "pressing X stops the ramp");
        r.release(Button::X);
        r.tick(1.0);
        r.release(Button::DpadRight);
        check(r.cur().rateNorm == atX, "... and releasing X does not restart it");

        r.press(Button::DpadRight);
        r.tick(kHold + 0.2);
        r.tap(Button::DpadDown); // RATE -> DEPTH while right is held
        const ModSlotSettings atRow = r.cur();
        r.tick(1.0);
        r.release(Button::DpadRight);
        check(r.panel.row() == ModRow::Depth && same(r.cur(), atRow), "a row change stops the ramp (no carry-over)");

        // MOD-B review: Y pressed (and released) during a ramp stops it.
        r.goRow(ModRow::Depth);
        r.press(Button::DpadRight);
        r.tick(kHold + 0.2);
        r.press(Button::Y);
        const float atY = r.cur().depth;
        r.tick(1.0);
        r.release(Button::Y);
        r.tick(1.0);
        r.release(Button::DpadRight);
        check(atY > 0.05f && r.cur().depth == atY, "pressing Y stops a DEPTH ramp; releasing it does not restart it");
        const ModSlotSettings atRow2 = r.cur();
        r.goRow(ModRow::Rate);
        r.press(Button::Y);
        r.press(Button::DpadLeft); // reset (Y held): inert
        r.release(Button::Y);
        r.tick(2.0);
        r.release(Button::DpadLeft);
        check(r.cur().rateNorm == k1Hz && r.cur().depth == atRow2.depth,
              "Y + left on RATE, Y released while left is held: no ramp afterwards");
        r.goRow(ModRow::Depth);
        const ModSlotSettings base = r.cur();

        // A press made on another tab never acts here.
        r.onMod = false;
        r.press(Button::DpadRight);
        r.onMod = true;
        r.tap(Button::R1);
        r.tick(2.0);
        r.release(Button::DpadRight);
        check(same(r.cur(), base), "a direction pressed on another tab does nothing on MOD");

        // X pressed on another tab does not count.
        r.onMod = false;
        r.press(Button::X);
        r.onMod = true;
        r.tap(Button::R1);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadDown);
        check(same(r.cur(), base) && r.panel.currentSlot() == 0 && r.panel.row() == ModRow::Depth &&
                  !r.panel.choosing(),
              "X pressed on another tab: X + a direction does nothing on MOD (no slot step, no value)");
        r.release(Button::X);
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.release(Button::X);
        check(r.panel.currentSlot() == 1, "X pressed again on MOD: X + right selects slot 2");

        // MOD-D review: Y pressed on another tab does not count either.
        {
            Rig             y;
            ModSlotSettings s0 = ModPanel::defaultSlot();
            s0.target          = ModTarget::ReverbMix;
            s0.depth           = 0.4f;
            y.panel.setSlot(0, s0);
            y.panel.setSlot(1, s0);
            const ModSlotSettings b0 = y.panel.slot(0);
            // Y alone: Y + right on TARGET neither resets nor steps.
            y.onMod = false;
            y.press(Button::Y);
            y.onMod = true;
            y.tap(Button::DpadRight);
            y.tap(Button::DpadLeft);
            y.tick(kHold + 1.0);
            const bool yAloneTarget = same(y.cur(), b0);
            y.tap(Button::DpadDown); // Y + up / down: nothing, as with Y pressed on MOD
            check(yAloneTarget && y.panel.row() == ModRow::Target && same(y.cur(), b0),
                  "Y pressed on another tab: Y + a direction on MOD does nothing (no reset, no step)");
            y.release(Button::Y);
            y.goRow(ModRow::Depth);
            y.press(Button::Y);
            y.tap(Button::DpadLeft);
            y.release(Button::Y);
            check(y.cur().depth == 0.0f && y.cur().target == ModTarget::ReverbMix,
                  "Y pressed again on MOD: Y + left resets the row (DEPTH 0)");
            y.panel.setSlot(0, s0);
            // Y and X both pressed on another tab: nothing, neither slot reset nor selection.
            y.onMod = false;
            y.press(Button::Y);
            y.press(Button::X);
            y.onMod = true;
            y.tap(Button::DpadRight);
            y.tap(Button::DpadLeft);
            check(same(y.panel.slot(0), b0) && same(y.panel.slot(1), b0) && y.panel.currentSlot() == 0 &&
                      !y.panel.choosing(),
                  "Y + X pressed on another tab: left/right on MOD do nothing (no slot reset, no selection)");
            y.release(Button::X);
            // Y from another tab, X pressed on MOD: still nothing (no reset).
            y.press(Button::X);
            y.tap(Button::DpadRight);
            check(same(y.panel.slot(0), b0) && y.panel.currentSlot() == 0,
                  "Y from another tab with X pressed on MOD: no slot reset, no selection");
            y.release(Button::X);
            y.release(Button::Y);
            // Y pressed on MOD, held across a tab change and back: no longer a reset modifier.
            y.press(Button::Y);
            y.onMod = false;
            y.tap(Button::R1);
            y.onMod = true;
            y.tap(Button::L1);
            y.tap(Button::DpadLeft);
            check(same(y.cur(), b0), "Y held across L1 / R1: Y + left on MOD does nothing");
            y.release(Button::Y);
            // Y + X both pressed on MOD still reset the slot.
            y.press(Button::Y);
            y.press(Button::X);
            y.tap(Button::DpadLeft);
            y.release(Button::X);
            y.release(Button::Y);
            check(same(y.cur(), ModPanel::defaultSlot()) && y.panel.currentSlot() == 0,
                  "Y + X pressed on MOD: the whole slot reset as before");
        }

        // MOD-D: a ramp does not continue into slot selection, nor the reverse.
        r.goRow(ModRow::Depth);
        ModSlotSettings d = r.cur();
        d.depth           = 0.0f;
        r.panel.setSlot(1, d);
        r.panel.setSlot(2, d);
        r.tick(0.0);
        r.press(Button::DpadRight);
        r.tick(kHold + 0.3);
        r.press(Button::X); // stops the ramp; the held right does not select
        const float atX2 = r.cur().depth;
        r.tick(1.0);
        r.release(Button::X);
        r.tick(1.0);
        r.release(Button::DpadRight);
        const double kk = atX2 / ModPanel::kDepthTapFine;
        check(r.panel.currentSlot() == 1 && atX2 > 0.05f && r.cur().depth == atX2 && near(kk, std::round(kk), 1e-3) &&
                  r.panel.slot(2).depth == 0.0f,
              "a DEPTH ramp stops at X (on the 0.2 % grid), the held right selects no slot, nothing ramps after X");
        r.press(Button::X);
        r.press(Button::DpadRight); // slot 3
        r.release(Button::X);       // right still held: inert
        r.tick(0.0);
        r.tick(kHold + 1.0);
        r.release(Button::DpadRight);
        check(r.panel.currentSlot() == 2 && r.panel.slot(2).depth == 0.0f,
              "X + right, X released while right is held: no DEPTH ramp on the new slot");
        // A held TARGET repeat stops at X.
        r.goRow(ModRow::Target);
        r.panel.setSlot(2, ModPanel::defaultSlot());
        r.tick(0.0);
        r.press(Button::DpadRight);
        r.tickTo(r.t + kHold + 0.2);
        r.press(Button::X);
        const ModTarget atXt = r.cur().target;
        const std::size_t slotAtX = r.panel.currentSlot();
        r.tick(1.0);
        r.release(Button::X);
        r.tick(1.0);
        r.release(Button::DpadRight);
        check(modTargetListIndex(atXt) == 3 && r.cur().target == atXt && slotAtX == 2 && r.panel.currentSlot() == 2,
              "a held TARGET repeat stops at X and does not resume after it");
        // Y pressed during a TARGET repeat stops it too.
        r.press(Button::DpadLeft);
        r.tickTo(r.t + kHold + 0.2);
        r.press(Button::Y);
        const ModTarget atYt = r.cur().target;
        r.tick(1.0);
        r.release(Button::Y);
        r.tick(1.0);
        r.release(Button::DpadLeft);
        check(r.cur().target == atYt && atYt != atXt, "pressing Y stops a TARGET repeat; releasing it does not restart it");
        // A row change during a TARGET repeat stops it.
        r.press(Button::DpadRight);
        r.tick(kHold + 0.2);
        r.tap(Button::DpadDown);
        const ModSlotSettings atRowT = r.cur();
        r.tick(1.0);
        r.release(Button::DpadRight);
        check(r.panel.row() == ModRow::Shape && same(r.cur(), atRowT), "a row change stops a TARGET repeat");

        // L1 / R1 while X is held: the tab changes, the X state is dropped.
        r.press(Button::X);
        const bool chose = r.panel.choosing();
        r.onMod = false;
        r.tap(Button::R1);
        const bool dropped = !r.panel.choosing();
        r.onMod = true;
        r.tap(Button::L1); // back on MOD, X still held
        r.tap(Button::DpadRight);
        check(chose && dropped && !r.panel.choosing() && r.panel.currentSlot() == 2 && r.panel.row() == ModRow::Shape &&
                  r.cur().shape == ModShape::Sine,
              "L1 / R1 with X held: the X state is dropped; back on MOD with X still held nothing acts");
        r.release(Button::X);
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.release(Button::X);
        check(r.panel.currentSlot() == 3, "... X pressed again on MOD selects again");
        r.press(Button::X);
        r.panel.releaseAll();
        check(!r.panel.choosing(), "releaseAll: the X state is dropped");
    }

    // ---- MOD-D review: a DEPTH ramp ended by releaseAll() ------------------------------------------------
    {
        Rig r;
        r.goRow(ModRow::Depth);
        r.tick(0.0);
        const double p0 = r.t;
        r.press(Button::DpadRight);
        r.tickTo(p0 + kHold + 0.37); // ramping, off the fine grid in general
        const float ramped = r.cur().depth;
        r.panel.releaseAll(); // focus loss, controller removed
        const float  d  = r.cur().depth;
        const double k  = d / ModPanel::kDepthTapFine;
        r.tickTo(r.t + 1.0);
        r.drain();
        check(ramped > 0.05f && near(k, std::round(k), 1e-3) && std::fabs(d - ramped) <= ModPanel::kDepthTapFine &&
                  r.cur().depth == d && r.dec.matrix().slot(0).depth == d,
              "a DEPTH ramp ended by releaseAll(): on the 0.2 % grid, sent, and it stops (" + fmtDepth(ramped) + " -> " +
                  fmtDepth(d) + ")");
        // The right press is gone: releasing it later does nothing, a new press steps once.
        r.release(Button::DpadRight);
        r.tick(0.0);
        check(r.cur().depth == d, "after releaseAll(): a late release changes nothing");
    }

    // ---- Resend after a full queue ----------------------------------------------------------------------
    {
        Rig r(2); // the decorator's queue holds two commands
        r.goRow(ModRow::Depth);
        for(int i = 0; i < 10; ++i)
            r.tap(Button::DpadRight); // ten sends, the queue full after two
        check(r.panel.sendsRefused() > 0 && r.panel.unsent(0), "a full queue refuses; the slot stays unsent");
        r.drain();
        r.tick(0.01); // sends the latest settings again
        r.drain();
        check(!r.panel.unsent(0) && r.dec.matrix().slot(0).depth == 0.2f, "tick() resends: the engine gets +20 %");
        // A ramp with a tiny queue: its final value reaches the engine.
        r.goRow(ModRow::Rate);
        r.press(Button::DpadLeft);
        for(int i = 0; i < 90; ++i)
            r.tick(1.0 / 60.0); // no drain: the queue fills
        r.release(Button::DpadLeft);
        const float last = r.cur().rateNorm;
        for(int i = 0; i < 4; ++i)
        {
            r.drain();
            r.tick(1.0 / 60.0);
        }
        r.drain();
        check(!r.panel.unsent(0) && r.dec.matrix().slot(0).rateNorm == last && last < k1Hz - 0.2f,
              "the final value of a ramp is never lost (" + std::to_string(last) + ")");
    }

    // ---- Cursor memory, log lines ------------------------------------------------------------------------
    {
        Rig r;
        r.press(Button::X);
        r.tap(Button::DpadRight);
        r.tap(Button::DpadRight); // slot 3
        r.release(Button::X);
        r.goRow(ModRow::Depth);
        r.onMod = false;
        r.tap(Button::R1);
        r.tap(Button::DpadUp);
        r.onMod = true;
        r.tap(Button::L1);
        check(r.panel.currentSlot() == 2 && r.panel.row() == ModRow::Depth, "cursor row and slot kept across tabs");

        Rig             q;
        ModSlotSettings s;
        s.shape    = ModShape::Triangle;
        s.target   = ModTarget::FeedbackLPFCutoff;
        s.rateNorm = modRateNormFromHz(3.2);
        s.depth    = 0.34f;
        q.tick(0.0);
        q.panel.setSlot(1, s);
        std::vector<std::string> log;
        q.tick(0.49);
        q.panel.takeLog(log);
        const bool early = log.empty();
        q.tick(0.02);
        q.panel.takeLog(log);
        check(early && log.size() == 1 && log[0] == "mod: slot 2 triangle -> FeedbackLPFCutoff 3.20 Hz depth +34.0%",
              "log: one line once stable for 0.5 s: \"" + (log.empty() ? std::string() : log[0]) + "\"");
        // Ramping keeps postponing it; one line after the end.
        log.clear();
        q.press(Button::X);
        q.tap(Button::DpadRight); // slot 2
        q.release(Button::X);
        q.goRow(ModRow::Depth);
        q.press(Button::DpadLeft);
        for(int i = 0; i < 120; ++i)
            q.tick(1.0 / 60.0);
        q.release(Button::DpadLeft);
        q.panel.takeLog(log);
        const bool none = log.empty();
        q.tick(0.6);
        q.panel.takeLog(log);
        check(none && log.size() == 1 && log[0].find("mod: slot 2 triangle -> FeedbackLPFCutoff 3.20 Hz depth ") == 0,
              "log: nothing while ramping, one line after");
        s.shape    = ModShape::Envelope;
        s.target   = ModTarget::Frequency;
        s.rateNorm = 0.5f;
        s.depth    = -0.2f;
        q.panel.setSlot(0, s);
        q.tick(0.6);
        log.clear();
        q.panel.takeLog(log);
        s.depth = 0.5f;
        q.panel.setSlot(3, s);
        q.panel.flushLog(); // quitting before 0.5 s
        q.panel.flushLog(); // once only
        q.tick(1.0);
        q.panel.takeLog(log);
        check(log.size() == 2 && log[1] == "mod: slot 4 envelope -> Frequency release 158 ms depth +50.0%",
              "flushLog(): a slot's line still waiting is written at once, and only once");
        log.resize(1);
        check(log.size() == 1 && log[0] == "mod: slot 1 envelope -> Frequency release 158 ms depth -20.0%",
              "log: the envelope's release: \"" + (log.empty() ? std::string() : log[0]) + "\"");

        // MOD-C: a unipolar LFO ends in " unipolar"; an envelope never does.
        Rig u;
        s          = ModPanel::defaultSlot();
        s.shape    = ModShape::RampUp;
        s.target   = ModTarget::ReverbMix;
        s.rateNorm = modRateNormFromHz(0.5);
        s.depth    = 0.2f;
        s.polarity = ModPolarity::Unipolar;
        u.panel.setSlot(2, s);
        s.shape = ModShape::Envelope;
        u.panel.setSlot(3, s);
        u.tick(1.0);
        log.clear();
        u.panel.takeLog(log);
        check(log.size() == 2 && log[0] == "mod: slot 3 rampup -> ReverbMix 0.500 Hz depth +20.0% unipolar" &&
                  log[1].find("mod: slot 4 envelope -> ReverbMix release ") == 0 &&
                  log[1].find("unipolar") == std::string::npos,
              "log: a unipolar LFO ends in \" unipolar\" (" + (log.empty() ? std::string() : log[0]) + ")");
    }

    // ---- fill(): the read-back ------------------------------------------------------------------------------
    {
        Rig             r;
        ModSlotSettings s;
        s.shape    = ModShape::Square;
        s.target   = ModTarget::FeedbackLPFCutoff;
        s.rateNorm = 0.2f;
        s.depth    = 0.5f;
        r.panel.setSlot(2, s);
        r.drain(2000); // 2000 blocks: the depth smoothing settles
        UiModView v;
        r.panel.fill(v);
        const float off = v.offset[static_cast<std::size_t>(ModTarget::FeedbackLPFCutoff)];
        check(std::fabs(off) > 0.45f && std::fabs(off) <= 0.5f && std::fabs(v.output[2]) == 1.0f &&
                  v.offset[static_cast<std::size_t>(ModTarget::Frequency)] == 0.0f && same(v.slot[2], s),
              "fill: the slot, its output and the target's offset (" + std::to_string(off) + ")");
        check(modTargetActive(v, ModTarget::FeedbackLPFCutoff) && !modTargetActive(v, ModTarget::Frequency) &&
                  !modTargetActive(v, ModTarget::None),
              "view: active only where a slot with depth targets it");
        char tag[8];
        modTagText(tag, sizeof tag, v, ModTarget::FeedbackLPFCutoff);
        v.slot[0] = s;
        v.slot[3] = s;
        char tag3[8];
        modTagText(tag3, sizeof tag3, v, ModTarget::FeedbackLPFCutoff);
        v.slot[3].depth = 0.0f;
        char tag2[8];
        modTagText(tag2, sizeof tag2, v, ModTarget::FeedbackLPFCutoff);
        char none[8];
        modTagText(none, sizeof none, v, ModTarget::Frequency);
        check(std::string(tag) == "M3" && std::string(tag3) == "M134" && std::string(tag2) == "M13" && none[0] == '\0',
              "tag text: M3, M134, M13 (a slot with depth 0 is left out), none");
        check(near(modMovingNorm(v, ModTarget::FeedbackLPFCutoff, 0.9f), std::min(1.0, 0.9 + off), 1e-6) &&
                  modMovingNorm(v, ModTarget::Frequency, 0.4f) == 0.4f,
              "moving value: clamp(knob + offset)");

        UiModView g;
        g.inputGainRatio = 0.5f;
        check(modEffectiveInputPeak(g, 0.8f) == 0.8f, "effective input peak: unchanged without a slot on the gain");
        g.slot[1].target = ModTarget::InputGain;
        g.slot[1].depth  = 0.5f;
        check(modEffectiveInputPeak(g, 0.8f) == 0.4f, "effective input peak: times the ratio with a slot on the gain");
        g.offset[static_cast<std::size_t>(ModTarget::InputGain)] = 0.25f;
        check(modEffectiveInputNorm(g, 0.25f) == 0.5f && modEffectiveInputNorm(g, 0.9f) == 1.0f &&
                  modEffectiveInputNorm(g, 0.0f) == 0.0f,
              "INPUT gain marker: clamp(knob + offset) (knob 0.25, offset +0.25: 0.5, whatever the ratio); closed stays 0");
    }

    // ---- Formatting -------------------------------------------------------------------------------------------
    {
        struct HzCase
        {
            double      hz;
            const char* want;
        };
        const HzCase hz[] = {{0.01, "0.010 HZ"}, {0.25, "0.250 HZ"}, {0.5, "0.500 HZ"},  {0.9996, "1.00 HZ"},
                             {1.0, "1.00 HZ"},   {3.2, "3.20 HZ"},     {9.996, "10.0 HZ"}, {44.0, "44.0 HZ"},
                             {99.96, "100 HZ"},  {440.0, "440 HZ"},    {999.6, "1.00 KHZ"}, {2000.0, "2.00 KHZ"}};
        int bad = 0;
        for(const HzCase& c : hz)
            if(fmtHz(c.hz) != c.want)
            {
                ++bad;
                std::printf("  %g Hz: \"%s\", want \"%s\"\n", c.hz, fmtHz(c.hz).c_str(), c.want);
            }
        check(bad == 0, "rate: three significant digits, tiers after rounding (0.010 HZ .. 2.00 KHZ)");
        check(fmtRate(0.0f, ModShape::Sine) == "0.010 HZ" && fmtRate(1.0f, ModShape::Square) == "2.00 KHZ" &&
                  fmtRate(k1Hz, ModShape::Triangle) == "1.00 HZ" && fmtRate(1.0f, ModShape::Sine, false) == "2.00 kHz" &&
                  fmtRate(k1Hz, ModShape::SampleHold, false) == "1.00 Hz",
              "rate by rateNorm: 0 -> 0.010 HZ, 1 Hz position -> 1.00 HZ, 1 -> 2.00 KHZ; log case Hz / kHz");
        struct RelCase
        {
            double      s;
            const char* want;
        };
        const RelCase rel[] = {{5.0, "5.00 S"},   {1.0, "1.00 S"},    {0.9996, "1.00 S"}, {0.5, "500 MS"},
                               {0.0999, "99.9 MS"}, {0.05, "50.0 MS"}, {0.00999, "9.99 MS"}, {0.005, "5.00 MS"}};
        bad = 0;
        for(const RelCase& c : rel)
            if(fmtRel(c.s) != c.want)
            {
                ++bad;
                std::printf("  %g s: \"%s\", want \"%s\"\n", c.s, fmtRel(c.s).c_str(), c.want);
            }
        check(bad == 0 && fmtRate(0.0f, ModShape::Envelope) == "5.00 S" && fmtRate(1.0f, ModShape::Envelope) == "5.00 MS" &&
                  fmtRel(0.05, false) == "50.0 ms" && fmtRel(2.0, false) == "2.00 s",
              "release: 5.00 S .. 5.00 MS, three significant digits; log case s / ms");
        check(fmtDepth(0.34f) == "+34.0%" && fmtDepth(-1.0f) == "-100.0%" && fmtDepth(1.0f) == "+100.0%" &&
                  fmtDepth(0.0f) == "0.0%" && fmtDepth(-0.0f) == "0.0%" && fmtDepth(-0.0004f) == "0.0%" &&
                  fmtDepth(0.002f) == "+0.2%" && fmtDepth(NAN) == "--",
              "depth: signed, one decimal, 0.0% for zero, -- for NaN");
        check(fmtHz(INFINITY) == "--" && fmtRel(NAN) == "--", "not finite: --");
        char small[4];
        formatModRateHz(small, sizeof small, 440.0);
        check(std::string(small) == "440", "a short buffer is cut and terminated");
        check(std::string(modShapeDisplayName(ModShape::SampleHold)) == "S&H" &&
                  std::string(modTargetDisplayName(ModTarget::DelaySwitch)) == "DELAY SWITCH" &&
                  std::string(modTargetDisplayName(ModTarget::InputGain)) == "INPUT GAIN",
              "names: S&H, INPUT GAIN, DELAY SWITCH");
        check(std::string(modShapeDisplayName(ModShape::RampUp)) == "RAMP UP" &&
                  std::string(modShapeDisplayName(ModShape::RampDown)) == "RAMP DOWN" &&
                  std::string(modShapeDisplayName(ModShape::ExpUp)) == "EXP UP" &&
                  std::string(modShapeDisplayName(ModShape::ExpDown)) == "EXP DOWN" &&
                  std::string(modPolarityDisplayName(ModPolarity::Bipolar)) == "BIPOLAR" &&
                  std::string(modPolarityDisplayName(ModPolarity::Unipolar)) == "UNIPOLAR" &&
                  std::string(modPolarityShortName(ModPolarity::Bipolar)) == "+/-" &&
                  std::string(modPolarityShortName(ModPolarity::Unipolar)) == "+",
              "MOD-C names: RAMP UP, RAMP DOWN, EXP UP, EXP DOWN; BIPOLAR / UNIPOLAR, +/- / +");
        bool fit = true;
        for(std::size_t i = 0; i < kModShapeCount; ++i)
            fit = fit && std::string(modShapeDisplayName(static_cast<ModShape>(i))).size() <= 9;
        check(fit, "every shape's name fits the card (9 characters at 16 px from x+40)");
    }

    return modtest::finish();
}
