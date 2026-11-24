// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ControlModel + ButtonControlSource together (criterion 2): button events and
// times in, frames out. Time advances one control block per read, and the model
// is ticked every block, so hold timing is exact to a block.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "control_test_common.h"

using namespace audreybrick;
using ctest::check;
using ctest::defaultOf;
using ctest::ki;
using ctest::near;
using ctest::Rig;
using ctest::within;

namespace
{
constexpr double kB = tuning::kBlocksPerSecond;

// Goes from LOOP to tab `t` with R1 taps.
void goTo(Rig& r, Tab t)
{
    while(r.model.currentTab() != t)
        r.tap(Button::R1);
}

bool othersUnchanged(const ControlFrame& before, const ControlFrame& after, Knob except)
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
        if(i != ki(except) && before.knob[i] != after.knob[i])
            return false;
    return before.delaySwitch == after.delaySwitch;
}

bool allUnchanged(const ControlFrame& a, const ControlFrame& b)
{
    return othersUnchanged(a, b, Knob::Count);
}
} // namespace

int main()
{
    // ---- Start-up -------------------------------------------------------------------
    {
        Rig r;
        r.run(1);
        bool ok = r.model.currentTab() == Tab::Loop && r.model.heldWord() == 0 && !r.frame.delaySwitch;
        for(std::size_t i = 0; i < kKnobCount; ++i)
            ok = ok && r.frame.knob[i] == defaultOf(static_cast<Knob>(i));
        check(ok, "start-up: tab LOOP, nothing held, every knob at its default, switch off");
    }

    // ---- Tap sizes -----------------------------------------------------------------
    {
        Rig r; // LOOP: left/right = Frequency, up/down = body
        r.run(1);
        const float f0 = r.v(Knob::Frequency);
        r.tap(Button::DpadRight);
        check(near(r.v(Knob::Frequency) - f0, 1.0 / 56, 1e-6), "Frequency tap = 1/56 (one semitone)");
        r.press(Button::Select);
        r.tap(Button::DpadLeft);
        r.release(Button::Select);
        check(near(r.v(Knob::Frequency) - f0, 1.0 / 56 - 1.0 / 560, 1e-6), "Frequency fine tap = 1/560 (10 cents)");
        r.tap(Button::DpadUp);
        check(near(r.v(Knob::FeedbackBody), 1.0 / 128, 1e-7), "ordinary tap = 1/128");
        r.press(Button::Select);
        r.tap(Button::DpadUp);
        r.release(Button::Select);
        check(near(r.v(Knob::FeedbackBody), 1.0 / 128 + 1.0 / 1024, 1e-7), "ordinary fine tap = 1/1024");
        r.tap(Button::R2);
        check(near(r.v(Knob::FeedbackGain), 0.5 / 72, 1e-7), "feedback tap (R2) = 0.5/72 (0.5 dB)");
        r.press(Button::Select);
        r.tap(Button::L2);
        r.release(Button::Select);
        check(near(r.v(Knob::FeedbackGain), 0.4 / 72, 1e-7), "feedback fine tap (L2) = 0.1/72 down");
        check(r.model.commandsSent() == 6 && r.model.commandsDropped() == 0 && r.src.commandsProcessed() == 6,
              "one command per tap, none dropped, all processed");
    }

    // ---- Clamping through the buttons ------------------------------------------------
    {
        Rig r;
        r.tap(Button::DpadDown); // body is at 0
        check(r.v(Knob::FeedbackBody) == 0.0f, "a tap below 0 clamps at 0");
        r.tap(Button::L2); // feedback is at 0
        check(r.v(Knob::FeedbackGain) == 0.0f, "a feedback tap below 0 clamps at 0");
        goTo(r, Tab::Out);
        for(int i = 0; i < 60; ++i)
            r.tap(Button::DpadUp);
        check(r.v(Knob::OutputVolume) == 1.0f, "taps above 1 clamp at 1");
    }

    // ---- Hold delay and ramp ---------------------------------------------------------
    {
        Rig r;
        goTo(r, Tab::Reverb); // left/right = dry/wet, starts at 0
        r.press(Button::DpadRight);
        const std::int64_t pressBlock = r.block;
        r.run(1);
        const float afterTap = r.v(Knob::ReverbMix);
        r.run(2999); // blocks press+1 .. press+2999: still under 0.25 s
        const bool stillTap = r.v(Knob::ReverbMix) == afterTap && r.model.heldWord() == 0;
        r.run(1); // block at exactly press + 3000: the hold begins
        std::printf("       hold began at block %lld after the press (%.4f s)\n",
                    static_cast<long long>(r.block - 1 - pressBlock),
                    static_cast<double>(r.block - 1 - pressBlock) / kB);
        check(near(afterTap, 1.0 / 128, 1e-7) && stillTap, "a press steps once and nothing more for 250 ms");
        check(r.model.heldWord() != 0 && r.v(Knob::ReverbMix) > afterTap,
              "the ramp starts 250 ms (3000 blocks) after the press");
        const float h0 = r.v(Knob::ReverbMix);
        check(within((h0 - afterTap) * kB, 0.1, 0.01), "the first ramp block moves at 0.1/s");
        r.run(5999);
        const float a = r.v(Knob::ReverbMix);
        r.run(1);
        const double mid = (r.v(Knob::ReverbMix) - a) * kB;
        r.run(6000);
        const float b0 = r.v(Knob::ReverbMix);
        r.run(1200);
        const double late = (r.v(Knob::ReverbMix) - b0) / 1200.0 * kB;
        std::printf("       measured through the model: start %.6f/s, 0.5 s %.6f/s, after 1 s %.6f/s\n",
                    (h0 - afterTap) * kB, mid, late);
        check(within(mid, 0.25, 0.01) && within(late, 0.4, 0.01), "ramp accelerates linearly to 0.4/s over 1 s");
        r.release(Button::DpadRight);
        r.run(1);
        const float stopped = r.v(Knob::ReverbMix);
        r.run(1200);
        check(r.v(Knob::ReverbMix) == stopped && r.model.heldWord() == 0, "release stops the ramp");

        // Fine ramp through Select.
        r.press(Button::Select);
        r.press(Button::DpadLeft);
        r.run(3000 + 12000 + 1200);
        const float c0 = r.v(Knob::ReverbMix);
        r.run(1200);
        const double fineLate = (c0 - r.v(Knob::ReverbMix)) / 1200.0 * kB;
        std::printf("       measured fine ramp through the model after 1 s: %.6f/s\n", fineLate);
        check(within(fineLate, 0.05, 0.01), "fine ramp (Select held) is one eighth: 0.05/s after 1 s");
        r.release(Button::DpadLeft);
        r.release(Button::Select);

        // A tap shorter than 250 ms is exactly one step.
        const float t0 = r.v(Knob::ReverbMix);
        r.press(Button::DpadRight);
        r.run(2900);
        r.release(Button::DpadRight);
        r.run(10);
        check(near(r.v(Knob::ReverbMix) - t0, 1.0 / 128, 1e-7), "a 242 ms press is one tap, no ramp");
    }

    // ---- Feedback ramp through L2/R2 ---------------------------------------------------
    {
        Rig r;
        r.press(Button::R2);
        r.run(3000); // up to the hold
        const float tapOnly = r.v(Knob::FeedbackGain);
        r.run(6000);
        const double rate = (r.v(Knob::FeedbackGain) - tapOnly) / 6000.0 * kB;
        std::printf("       measured feedback ramp through the model: %.6f/s = %.3f dB/s\n", rate, rate * 72.0);
        check(near(tapOnly, 0.5 / 72, 1e-7), "R2: one tap, then nothing until 250 ms");
        check(within(rate, 24.0 / 72.0, 0.001), "R2 held: feedback rises at 24/72 per second");
        r.release(Button::R2);
        r.run(1);
        const float top = r.v(Knob::FeedbackGain);
        r.press(Button::Select);
        r.press(Button::L2);
        r.run(3000 + 6000);
        const double fineRate = (top - 0.1 / 72 - r.v(Knob::FeedbackGain)) / 6000.0 * kB;
        check(within(fineRate, 4.0 / 72.0, 0.002), "Select + L2 held: feedback falls at 4/72 per second");
        r.release(Button::L2);
        r.release(Button::Select);
    }

    // ---- Both directions held ----------------------------------------------------------
    {
        Rig r;
        goTo(r, Tab::Reverb);
        r.press(Button::DpadRight);
        r.run(5000);
        const float before = r.v(Knob::ReverbMix);
        r.press(Button::DpadLeft);
        r.run(1);
        const float atBoth = r.v(Knob::ReverbMix);
        r.run(12000);
        check(r.v(Knob::ReverbMix) == atBoth && atBoth > before - 1e-6f && atBoth < before + 1e-3f,
              "d-pad: opposite directions held = no movement, and the second press sends no step");
        r.release(Button::DpadRight);
        r.run(2);
        check(r.v(Knob::ReverbMix) < atBoth, "releasing one leaves the other (held > 250 ms) ramping");
        r.release(Button::DpadLeft);

        r.press(Button::R2);
        r.run(5000);
        const float fb = r.v(Knob::FeedbackGain);
        r.press(Button::L2);
        r.run(1);
        const float fbBoth = r.v(Knob::FeedbackGain);
        r.run(12000);
        check(r.v(Knob::FeedbackGain) == fbBoth && fbBoth >= fb && fbBoth < fb + 1e-3f,
              "L2 + R2 held = no feedback movement, and the second press sends no step");
    }

    // ---- Feedback maximum ----------------------------------------------------------------
    {
        Rig r;
        for(int i = 0; i < 10; ++i)
            r.tap(Button::R2);
        const float stored = r.v(Knob::FeedbackGain);
        r.press(Button::A);
        r.run(1);
        check(r.v(Knob::FeedbackGain) == 1.0f && r.src.publishedStoredFeedback() == stored && r.model.feedbackMaxHeld(),
              "A held: frame feedback 1.0, stored value unchanged");
        r.run(6000);
        check(r.v(Knob::FeedbackGain) == 1.0f && r.src.publishedStoredFeedback() == stored, "still so after 0.5 s");
        r.release(Button::A);
        r.run(1);
        check(r.v(Knob::FeedbackGain) == stored && !r.src.publishedFeedbackMax(), "A released: stored value restored");
    }

    // ---- Switch: held, latched, XOR, Select + B ------------------------------------------
    {
        Rig r;
        r.press(Button::B);
        r.run(1);
        const bool held = r.frame.delaySwitch && r.model.switchHeld();
        r.release(Button::B);
        r.run(1);
        check(held && !r.frame.delaySwitch, "B held = switch on; released = off");

        r.press(Button::Select);
        r.press(Button::B);
        r.run(1);
        const bool latchedWhileDown = r.frame.delaySwitch && !r.model.switchHeld() && r.src.publishedSwitchLatch();
        r.run(6000);
        r.release(Button::B);
        r.release(Button::Select);
        r.run(1);
        check(latchedWhileDown && r.frame.delaySwitch,
              "Select + B latches the switch on; that press is not a momentary hold");
        r.press(Button::B);
        r.run(1);
        const bool xorOff = !r.frame.delaySwitch;
        r.release(Button::B);
        r.run(1);
        check(xorOff && r.frame.delaySwitch, "B held while latched = off (XOR); released = on again");
        r.press(Button::Select);
        r.tap(Button::B);
        r.release(Button::Select);
        r.run(1);
        check(!r.frame.delaySwitch && !r.src.publishedSwitchLatch(), "Select + B again unlatches");

        // B held first, then Select: still a momentary hold, no toggle.
        r.press(Button::B);
        r.press(Button::Select);
        r.run(1);
        check(r.frame.delaySwitch && !r.src.publishedSwitchLatch(), "B then Select: B stays a momentary hold");
        r.release(Button::Select);
        r.release(Button::B);
        r.run(1);
    }

    // ---- Every tab's bindings, including X ---------------------------------------------------
    {
        struct Expect
        {
            Tab  tab;
            Knob lr, ud, xlr, xud;
        };
        const Expect table[] = {
            {Tab::Loop, Knob::Frequency, Knob::FeedbackBody, kNoKnob, kNoKnob},
            {Tab::Filter, Knob::FeedbackLPFCutoff, Knob::FeedbackHPFCutoff, kNoKnob, kNoKnob},
            {Tab::Reverb, Knob::ReverbMix, Knob::ReverbDecay, kNoKnob, kNoKnob},
            {Tab::Delay, Knob::EchoDelayTime, Knob::EchoDelaySend, kNoKnob, Knob::EchoDelayFeedback},
            {Tab::Out, kNoKnob, Knob::OutputVolume, kNoKnob, kNoKnob},
            {Tab::Input, kNoKnob, kNoKnob, kNoKnob, kNoKnob}, // M2b-A: the input panel, no knobs
            {Tab::Mod, kNoKnob, kNoKnob, kNoKnob, kNoKnob},   // MOD-B: the mod panel, no knobs
        };
        const char* names[] = {"LOOP", "FILTER", "REVERB", "DELAY", "OUT", "INPUT", "MOD"};
        for(const Expect& e : table)
        {
            check(std::string(tabSpec(e.tab).name) == names[static_cast<int>(e.tab)],
                  std::string("tab name ") + names[static_cast<int>(e.tab)]);
            for(int x = 0; x < 2; ++x)
                for(Button b : {Button::DpadLeft, Button::DpadRight, Button::DpadDown, Button::DpadUp})
                {
                    Rig r;
                    goTo(r, e.tab);
                    // Start every knob mid-travel so both directions show.
                    float mid[kKnobCount];
                    for(float& m : mid)
                        m = 0.5f;
                    r.src.post(ControlCommand::setAll(mid, false));
                    r.run(2);
                    const bool lr     = b == Button::DpadLeft || b == Button::DpadRight;
                    const bool up     = b == Button::DpadRight || b == Button::DpadUp;
                    const Knob expect = x ? (lr ? e.xlr : e.xud) : (lr ? e.lr : e.ud);
                    if(x)
                        r.press(Button::X);
                    const ControlFrame before = r.frame;
                    r.press(b);
                    r.run(1);
                    const ControlFrame after = r.frame;
                    std::string what = std::string(names[static_cast<int>(e.tab)]) + (x ? " X+" : " ") +
                                       buttonName(b) + " -> " +
                                       (expect == kNoKnob ? std::string("nothing") : paramSpec(expect).id);
                    bool ok;
                    if(expect == kNoKnob)
                        ok = allUnchanged(before, after);
                    else
                    {
                        const double step = tuning::tapStep(expect, false) * (up ? 1 : -1);
                        ok = othersUnchanged(before, after, expect) &&
                             near(after.knob[ki(expect)] - before.knob[ki(expect)], step, 1e-6);
                    }
                    // The hold ramps the same knob and nothing else.
                    r.run(6000);
                    if(expect == kNoKnob)
                        ok = ok && allUnchanged(before, r.frame) && r.model.heldWord() == 0;
                    else
                        ok = ok && othersUnchanged(before, r.frame, expect) &&
                             (up ? r.frame.knob[ki(expect)] > after.knob[ki(expect)]
                                 : r.frame.knob[ki(expect)] < after.knob[ki(expect)]);
                    check(ok, what);
                }
        }
        bool noFb = true;
        for(std::size_t t = 0; t < kTabCount; ++t)
            for(int a = 0; a < 2; ++a)
                for(int x = 0; x < 2; ++x)
                    noFb = noFb && tabBinding(static_cast<Tab>(t), static_cast<Axis>(a), x) != Knob::FeedbackGain;
        check(noFb, "no tab binds FeedbackGain to the d-pad");
    }

    // ---- Tab change, wrapping, and tab change while a direction is held -------------------------
    {
        Rig r;
        // M2b-A: INPUT after OUT; MOD-B: MOD after INPUT; PRESET-B: eight
        // tabs, PRESET last.
        r.tap(Button::L1);
        check(r.model.currentTab() == Tab::Preset, "L1 from LOOP wraps to PRESET");
        r.tap(Button::L1);
        check(r.model.currentTab() == Tab::Mod, "L1 from PRESET: MOD");
        r.tap(Button::L1);
        check(r.model.currentTab() == Tab::Input, "L1 from MOD: INPUT");
        r.tap(Button::R1);
        r.tap(Button::R1);
        r.tap(Button::R1);
        check(r.model.currentTab() == Tab::Loop, "R1 from INPUT: MOD, PRESET, and from PRESET it wraps to LOOP");
        for(int i = 0; i < 8; ++i)
            r.tap(Button::R1);
        check(r.model.currentTab() == Tab::Loop && kTabCount == 8, "eight R1 presses come back to LOOP");

        r.press(Button::DpadRight); // Frequency
        r.run(6000);
        check(r.model.heldWord() != 0, "Frequency ramping");
        r.press(Button::R1); // -> FILTER while right is held
        r.run(1);
        const ControlFrame atChange = r.frame;
        r.run(12000);
        check(r.model.currentTab() == Tab::Filter && r.model.heldWord() == 0 && allUnchanged(atChange, r.frame),
              "tab change while a direction is held: the ramp stops, the new tab's knob does not move");
        r.release(Button::R1);
        r.release(Button::DpadRight);
        r.run(1);
        const float lp = r.v(Knob::FeedbackLPFCutoff);
        r.tap(Button::DpadLeft);
        check(near(lp - r.v(Knob::FeedbackLPFCutoff), 1.0 / 128, 1e-6), "a fresh press after release moves the new tab's knob");
    }

    // ---- X pressed or released while a direction is held -------------------------------------------
    {
        Rig r;
        goTo(r, Tab::Delay);
        r.press(Button::DpadUp); // mix
        r.run(6000);
        r.press(Button::X);
        r.run(1);
        const ControlFrame atX = r.frame;
        r.run(6000);
        check(allUnchanged(atX, r.frame) && r.model.heldWord() == 0,
              "X pressed during a hold: the held direction goes inert (no jump to the X binding)");
        r.release(Button::DpadUp);
        r.press(Button::DpadUp); // X+up = repeats
        r.run(6000);
        r.release(Button::X);
        r.run(1);
        const ControlFrame atRelease = r.frame;
        r.run(6000);
        check(allUnchanged(atRelease, r.frame), "X released during a hold: the held direction goes inert");
        r.release(Button::DpadUp);
    }

    // ---- Y + direction: reset, with and without X ----------------------------------------------------
    {
        Rig r;
        goTo(r, Tab::Delay);
        for(int i = 0; i < 5; ++i)
            r.tap(Button::DpadUp); // mix up
        r.press(Button::X);
        for(int i = 0; i < 5; ++i)
            r.tap(Button::DpadUp); // repeats up
        r.release(Button::X);
        r.run(1);
        const float mix = r.v(Knob::EchoDelaySend), rep = r.v(Knob::EchoDelayFeedback);

        r.press(Button::Y);
        r.press(Button::X);
        r.press(Button::DpadUp);
        r.run(12000); // held a whole second: no ramp
        check(r.v(Knob::EchoDelayFeedback) == defaultOf(Knob::EchoDelayFeedback) && r.v(Knob::EchoDelaySend) == mix,
              "Y + X + up resets repeats (the X binding) only, without a step or a ramp");
        r.release(Button::DpadUp);
        r.release(Button::X);
        r.press(Button::DpadDown);
        r.run(12000);
        check(r.v(Knob::EchoDelaySend) == defaultOf(Knob::EchoDelaySend),
              "Y + down resets mix (the plain binding) without a step or a ramp");
        r.release(Button::DpadDown);
        r.release(Button::Y);
        check(rep > 0.0f && mix > 0.0f, "(the knobs had been moved away from their defaults first)");

        // Y + an unbound direction does nothing.
        goTo(r, Tab::Out);
        r.run(1);
        const ControlFrame before = r.frame;
        r.press(Button::Y);
        r.tap(Button::DpadLeft);
        r.release(Button::Y);
        r.run(1);
        check(allUnchanged(before, r.frame), "Y + an unbound direction does nothing");
    }

    // ---- Start, repeated events ----------------------------------------------------------------------
    {
        Rig r;
        r.run(1);
        const ControlFrame before = r.frame;
        r.tap(Button::Start);
        check(allUnchanged(before, r.frame) && r.model.commandsSent() == 0 && r.model.heldWord() == 0,
              "Start does nothing in M2");
        r.press(Button::DpadUp);
        r.press(Button::DpadUp); // repeated down
        r.run(1);
        r.release(Button::DpadUp);
        r.release(Button::DpadUp); // repeated up
        r.run(1);
        check(r.model.commandsSent() == 1, "a repeated down sends nothing; a repeated up is ignored");
        check(!r.model.isDown(Button::DpadUp), "button state is up after the release");
    }

    // ---- Modifier state exposed for the UI -----------------------------------------------------------
    {
        Rig r;
        r.press(Button::Select);
        r.press(Button::X);
        r.press(Button::Y);
        r.press(Button::A);
        check(r.model.fine() && r.model.xHeld() && r.model.yHeld() && r.model.feedbackMaxHeld(),
              "fine, X, Y and maximum are reported while held");
        const HeldState s = unpackHeld(r.model.heldWord());
        check(s.fine && s.feedbackMax && !s.switchHeld, "the held word carries fine and maximum");
    }

    // ---- Y reset for knobs whose default is not 0 (from above and below) -----------------------
    {
        struct Case
        {
            Tab    tab;
            Button dir;
            Knob   knob;
        };
        const Case cases[] = {
            {Tab::Loop, Button::DpadRight, Knob::Frequency},
            {Tab::Filter, Button::DpadLeft, Knob::FeedbackLPFCutoff},
            {Tab::Filter, Button::DpadUp, Knob::FeedbackHPFCutoff},
            {Tab::Out, Button::DpadDown, Knob::OutputVolume},
        };
        for(const Case& c : cases)
            for(double offset : {+0.2, -0.2})
            {
                const double displaced = defaultOf(c.knob) + offset;
                if(displaced > 1.0)
                {
                    std::printf("       (%s: no value above its default %.4f to start from)\n", paramSpec(c.knob).id,
                                defaultOf(c.knob));
                    continue;
                }
                Rig r;
                goTo(r, c.tab);
                float vals[kKnobCount];
                for(std::size_t i = 0; i < kKnobCount; ++i)
                    vals[i] = defaultOf(static_cast<Knob>(i));
                vals[ki(c.knob)] = static_cast<float>(displaced);
                r.src.post(ControlCommand::setAll(vals, false));
                r.run(1);
                const float start = r.v(c.knob);
                r.press(Button::Y);
                r.press(c.dir);
                r.run(1);
                const float afterReset = r.v(c.knob);
                r.run(12000);
                check(start == static_cast<float>(displaced) && afterReset == defaultOf(c.knob) &&
                          r.v(c.knob) == defaultOf(c.knob),
                      std::string("Y + ") + buttonName(c.dir) + " resets " + paramSpec(c.knob).id + " from " +
                          (offset > 0 ? "above" : "below") + " its default to exactly the default, no ramp");
            }
    }

    // ---- Opposite directions: the first released, the second ramps after its own 250 ms -----------
    {
        struct Pair
        {
            Button first, second;
            Knob   knob;
        };
        const Pair pairs[] = {
            {Button::DpadRight, Button::DpadLeft, Knob::ReverbMix},
            {Button::R2, Button::L2, Knob::FeedbackGain},
        };
        for(const Pair& pr : pairs)
        {
            const std::string name = std::string(buttonName(pr.first)) + "/" + buttonName(pr.second);
            // Second direction down for at least 250 ms when the first is released: ramps at once.
            {
                Rig r;
                goTo(r, Tab::Reverb);
                r.press(pr.first);
                r.run(5000);
                r.press(pr.second);
                r.run(4000);
                const float before = r.v(pr.knob);
                r.release(pr.first);
                r.run(1);
                check(r.v(pr.knob) < before && within((before - r.v(pr.knob)) * kB,
                                                       pr.knob == Knob::FeedbackGain ? 24.0 / 72.0 : 0.1, 0.01),
                      name + ": second held 333 ms when the first is released ramps from the very next block");
            }
            // Second direction down for only 100 ms: no movement until 250 ms after its own press.
            {
                Rig r;
                goTo(r, Tab::Reverb);
                r.press(pr.first);
                r.run(5000);
                r.press(pr.second);
                const std::int64_t secondPress = r.block;
                r.run(1200);
                const float before = r.v(pr.knob);
                r.release(pr.first);
                r.run(3000 - 1200); // up to block secondPress + 2999
                const bool still = r.v(pr.knob) == before && r.model.heldWord() == 0;
                r.run(1);           // block secondPress + 3000
                check(still && r.v(pr.knob) < before && r.block - 1 - secondPress == 3000,
                      name + ": second held 100 ms when the first is released starts exactly 250 ms after its own press");
            }
        }
    }

    // ---- releaseAll ---------------------------------------------------------------------------------
    {
        Rig r;
        r.tap(Button::R1); // FILTER
        r.press(Button::Select);
        r.tap(Button::B); // latch on
        r.release(Button::Select);
        r.run(1);
        const bool latched = r.src.publishedSwitchLatch();
        r.press(Button::A);
        r.press(Button::B);
        r.press(Button::R2);
        r.press(Button::Select);
        r.press(Button::DpadUp);
        r.run(6000);
        const HeldState held = unpackHeld(r.model.heldWord());
        const bool allActive = held.axisDir[1] == 1 && held.feedbackDir == 1 && held.fine && held.feedbackMax &&
                               held.switchHeld;
        const std::uint64_t sent = r.model.commandsSent();
        r.model.releaseAll(r.now());
        r.run(1);
        const ControlFrame atRelease = r.frame;
        r.run(6000);
        check(latched && allActive, "releaseAll setup: latch on; A, B, R2, Select and up all active");
        check(r.model.heldWord() == 0 && r.model.commandsSent() == sent,
              "releaseAll: held word 0 and no new command");
        check(allUnchanged(atRelease, r.frame) && r.v(Knob::FeedbackGain) == r.src.publishedStoredFeedback() &&
                  r.frame.delaySwitch && r.src.publishedSwitchLatch() && r.model.currentTab() == Tab::Filter &&
                  !r.model.fine() && !r.model.feedbackMaxHeld() && !r.model.switchHeld(),
              "releaseAll: ramps stop, maximum and momentary switch released, modifiers clear; tab and latch kept");
        for(Button b : {Button::A, Button::B, Button::R2, Button::Select, Button::DpadUp})
            r.release(b);
        r.run(1);
        check(r.model.heldWord() == 0 && r.model.commandsSent() == sent && allUnchanged(atRelease, r.frame),
              "releaseAll: the later real ups are ignored");
        const float hp = r.v(Knob::FeedbackHPFCutoff);
        r.press(Button::DpadUp);
        r.run(1);
        check(r.model.commandsSent() == sent + 1 && near(r.v(Knob::FeedbackHPFCutoff) - hp, 1.0 / 128, 1e-6),
              "releaseAll: a later down steps normally (coarse, Select no longer held)");
        r.run(3000);
        r.press(Button::A);
        r.run(1);
        check(r.v(Knob::FeedbackHPFCutoff) > hp + 1.0 / 128 + 1e-6 && r.v(Knob::FeedbackGain) == 1.0f,
              "releaseAll: a later hold ramps and a later A gives maximum");
    }

    return ctest::finish();
}
