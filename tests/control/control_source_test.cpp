// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ButtonControlSource on its own, driven by commands and held words directly:
//   - criterion 1: no input = ParamTable defaults, switch off; AudreyVoice for 10 s
//     bit-identical to an empty ScriptControlSource
//   - Step, clamping, Reset, SetAll, ToggleSwitchLatch, bounded draining
//   - the d-pad ramp: speeds, acceleration, fine, restart on direction or knob
//     change, stop on release
//   - the feedback ramp; feedback maximum; switch latch XOR held
//   - published values; determinism

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "control/ButtonControlSource.h"
#include "control/ScriptControlSource.h"
#include "control_test_common.h"
#include "core/AudreyVoice.h"

using namespace audreybrick;
using ctest::check;
using ctest::defaultOf;
using ctest::ki;
using ctest::near;
using ctest::within;

namespace
{
constexpr double kB = tuning::kBlocksPerSecond;

std::uint32_t axisWord(int axis, Knob k, int dir, bool fine = false)
{
    HeldState s;
    s.axisKnob[axis] = k;
    s.axisDir[axis]  = static_cast<std::int8_t>(dir);
    s.fine           = fine;
    return packHeld(s);
}

std::uint32_t fbWord(int dir, bool fine = false, bool max = false)
{
    HeldState s;
    s.feedbackDir = static_cast<std::int8_t>(dir);
    s.fine        = fine;
    s.feedbackMax = max;
    return packHeld(s);
}

// Reads n blocks and returns the knob value after each.
std::vector<double> track(ButtonControlSource& src, Knob k, int n)
{
    std::vector<double> v;
    ControlFrame        f{};
    for(int i = 0; i < n; ++i)
    {
        src.read(f);
        v.push_back(f.knob[ki(k)]);
    }
    return v;
}

void put(ButtonControlSource& src, const ControlCommand& c)
{
    if(!src.post(c))
        check(false, "post");
}
} // namespace

int main()
{
    // ---- Criterion 1 ------------------------------------------------------------
    {
        ButtonControlSource src;
        ControlFrame        f{};
        bool                ok = true;
        for(int b = 0; b < 12000; ++b)
        {
            src.read(f);
            ok = ok && !f.delaySwitch;
            for(std::size_t i = 0; i < kKnobCount; ++i)
                ok = ok && f.knob[i] == defaultOf(static_cast<Knob>(i));
        }
        check(ok, "criterion 1: 12000 blocks with no input = ParamTable defaults, switch off");

        constexpr std::size_t kFrames = 10 * AudreyVoice::kSampleRate;
        std::vector<float>    al(kFrames), ar(kFrames), bl(kFrames), br(kFrames);
        auto                  va = std::make_unique<AudreyVoice>();
        auto                  vb = std::make_unique<AudreyVoice>();
        va->init();
        vb->init();
        ButtonControlSource buttons;
        ScriptControlSource script;
        std::string         err;
        script.loadString("", err);
        for(std::size_t pos = 0; pos < kFrames; pos += 256)
        {
            const std::size_t n = std::min<std::size_t>(256, kFrames - pos);
            va->process(buttons, nullptr, al.data() + pos, ar.data() + pos, n);
            vb->process(script, nullptr, bl.data() + pos, br.data() + pos, n);
        }
        const bool same = std::memcmp(al.data(), bl.data(), kFrames * sizeof(float)) == 0 &&
                          std::memcmp(ar.data(), br.data(), kFrames * sizeof(float)) == 0;
        check(same, "criterion 1: AudreyVoice 10 s with ButtonControlSource == with an empty script, bit for bit");
    }

    // ---- Step, clamping, Reset, SetAll ----------------------------------------
    {
        ButtonControlSource src;
        ControlFrame        f{};
        put(src, ControlCommand::step(Knob::ReverbMix, 0.25));
        src.read(f);
        check(f.knob[ki(Knob::ReverbMix)] == 0.25f, "Step adds its delta");
        put(src, ControlCommand::step(Knob::ReverbMix, -0.5));
        src.read(f);
        check(f.knob[ki(Knob::ReverbMix)] == 0.0f, "Step clamps at 0");
        put(src, ControlCommand::step(Knob::OutputVolume, 5.0));
        src.read(f);
        check(f.knob[ki(Knob::OutputVolume)] == 1.0f, "Step clamps at 1");
        put(src, ControlCommand::reset(Knob::OutputVolume));
        src.read(f);
        check(f.knob[ki(Knob::OutputVolume)] == defaultOf(Knob::OutputVolume), "Reset returns to the default position");
        put(src, ControlCommand::step(Knob::ReverbMix, std::nan("")));
        src.read(f);
        check(f.knob[ki(Knob::ReverbMix)] == 0.0f, "a NaN delta leaves a finite value (0)");
        put(src, ControlCommand::step(Knob::Count, 0.5));
        put(src, ControlCommand::reset(Knob::Count));
        src.read(f);
        check(src.commandsProcessed() == 7, "commands naming no knob are consumed and ignored");

        float vals[kKnobCount];
        for(std::size_t i = 0; i < kKnobCount; ++i)
            vals[i] = 0.1f * static_cast<float>(i) - 0.05f; // -0.05 .. 0.95
        vals[ki(Knob::OutputVolume)] = 1.5f;
        put(src, ControlCommand::setAll(vals, true));
        src.read(f);
        bool ok = f.knob[0] == 0.0f && f.knob[ki(Knob::OutputVolume)] == 1.0f && f.delaySwitch;
        for(std::size_t i = 1; i + 1 < kKnobCount; ++i)
            ok = ok && f.knob[i] == vals[i];
        check(ok, "SetAll sets all eleven values (clamped) and the latch");
        check(src.publishedSwitchLatch() && src.publishedStoredFeedback() == vals[ki(Knob::FeedbackGain)],
              "SetAll's latch and feedback are published");
    }

    // ---- Bounded draining ----------------------------------------------------------
    {
        ButtonControlSource src;
        for(int i = 0; i < 20; ++i)
            put(src, ControlCommand::step(Knob::ReverbMix, 0.01));
        ControlFrame f{};
        src.read(f);
        check(src.commandsProcessed() == static_cast<std::uint64_t>(tuning::kMaxCommandsPerBlock),
              "one read() applies at most kMaxCommandsPerBlock commands");
        src.read(f);
        src.read(f);
        check(src.commandsProcessed() == 20 && near(f.knob[ki(Knob::ReverbMix)], 0.2, 1e-6),
              "the rest follow in the next blocks, in order");
    }

    // ---- Queue full: post returns false -------------------------------------------
    {
        ButtonControlSource src(4);
        int                 accepted = 0;
        for(int i = 0; i < 10; ++i)
            accepted += src.post(ControlCommand::toggleSwitchLatch()) ? 1 : 0;
        check(accepted == 4, "post returns false once the queue (capacity 4) is full");
    }

    // ---- D-pad ramp speeds and acceleration --------------------------------------
    {
        ButtonControlSource src;
        put(src, ControlCommand::step(Knob::ReverbMix, -1.0)); // start at 0
        ControlFrame f{};
        src.read(f);
        src.setHeld(axisWord(0, Knob::ReverbMix, +1));
        const auto v = track(src, Knob::ReverbMix, 36000); // 3 s
        // v[i] = value after hold block i.
        const double r0   = v[0] * kB;                      // speed in the first block
        const double rMid = (v[6000] - v[5999]) * kB;       // at 0.5 s
        const double rEnd = (v[13000] - v[12000]) / 1000.0 * kB; // after 1 s
        const double d1   = v[11999];                        // displacement over the first second
        std::printf("       measured coarse ramp: start %.6f/s, at 0.5 s %.6f/s, after 1 s %.6f/s, first second %.6f\n",
                    r0, rMid, rEnd, d1);
        check(within(r0, 0.1, 0.01), "ramp starts at 0.1 of travel per second (within 1%)");
        check(within(rMid, 0.25, 0.01), "ramp is 0.25/s halfway through the acceleration (linear)");
        check(within(rEnd, 0.4, 0.01), "ramp reaches 0.4/s after 1 s and stays there");
        check(within(d1, 0.25, 0.01), "first second covers 0.25 of travel (mean of 0.1 and 0.4)");
        check(v[34499] < 1.0 && v[34500] == 1.0, "ramp reaches 1 after 2.875 s (0.25 + 1.875 * 0.4) and clamps there");
    }
    {
        ButtonControlSource src;
        ControlFrame        f{};
        put(src, ControlCommand::step(Knob::ReverbMix, 0.5 - defaultOf(Knob::ReverbMix)));
        src.read(f);
        src.setHeld(axisWord(0, Knob::ReverbMix, -1, true));
        const auto   v    = track(src, Knob::ReverbMix, 24000);
        const double r0   = (v[0] - 0.5) * kB;
        const double rEnd = (v[13000] - v[12000]) / 1000.0 * kB;
        std::printf("       measured fine ramp: start %.7f/s, after 1 s %.7f/s\n", -r0, -rEnd);
        check(within(-r0, 0.1 / 8, 0.01) && within(-rEnd, 0.4 / 8, 0.01),
              "fine ramp is one eighth of both speeds, downwards");

        // Release stops it.
        src.setHeld(0);
        const auto w = track(src, Knob::ReverbMix, 100);
        check(w.front() == w.back() && w.front() == static_cast<float>(v.back()), "release stops the ramp");
    }
    // Restart on direction change and on knob change; fine toggle does not restart.
    {
        ButtonControlSource src;
        ControlFrame        f{};
        put(src, ControlCommand::step(Knob::ReverbMix, 0.5));
        put(src, ControlCommand::step(Knob::ReverbDecay, 0.5));
        src.read(f);
        src.setHeld(axisWord(0, Knob::ReverbMix, +1));
        track(src, Knob::ReverbMix, 12000);
        src.setHeld(axisWord(0, Knob::ReverbMix, -1));
        auto v = track(src, Knob::ReverbMix, 2);
        check(within((v[0] - v[1]) * kB, 0.1, 0.01), "reversing direction restarts the acceleration");

        src.setHeld(axisWord(0, Knob::ReverbMix, -1));
        track(src, Knob::ReverbMix, 12000);
        src.setHeld(axisWord(0, Knob::ReverbDecay, -1));
        v = track(src, Knob::ReverbDecay, 2);
        check(within((v[0] - v[1]) * kB, 0.1, 0.01), "a different knob on the axis restarts the acceleration");

        track(src, Knob::ReverbDecay, 6000);
        src.setHeld(axisWord(0, Knob::ReverbDecay, -1, true));
        v = track(src, Knob::ReverbDecay, 2);
        check(within((v[0] - v[1]) * kB, (0.1 + 0.3 * 6002.0 / 12000.0) / 8, 0.01),
              "turning fine on mid-hold scales the speed without restarting the acceleration");

        // Both axes at once, independently.
        ButtonControlSource two;
        HeldState           s;
        s.axisKnob[0] = Knob::ReverbMix;
        s.axisDir[0]  = +1;
        s.axisKnob[1] = Knob::OutputVolume;
        s.axisDir[1]  = -1;
        two.setHeld(packHeld(s));
        for(int i = 0; i < 1200; ++i)
            two.read(f);
        check(f.knob[ki(Knob::ReverbMix)] > 0.0f &&
                  f.knob[ki(Knob::OutputVolume)] < defaultOf(Knob::OutputVolume) &&
                  near(f.knob[ki(Knob::ReverbMix)], defaultOf(Knob::OutputVolume) - f.knob[ki(Knob::OutputVolume)], 1e-6),
              "two axes ramp at the same time, each at its own direction");

        // An axis naming FeedbackGain is ignored.
        ButtonControlSource fb;
        fb.setHeld(axisWord(1, Knob::FeedbackGain, +1));
        for(int i = 0; i < 1200; ++i)
            fb.read(f);
        check(f.knob[ki(Knob::FeedbackGain)] == defaultOf(Knob::FeedbackGain), "the d-pad ramp never moves FeedbackGain");
    }

    // ---- Feedback ramp, maximum -----------------------------------------------------
    {
        ButtonControlSource src;
        src.setHeld(fbWord(+1));
        const auto   v  = track(src, Knob::FeedbackGain, 6000);
        const double r  = (v[5999] - v[0]) / 5999.0 * kB;
        const double r0 = v[0] * kB;
        std::printf("       measured feedback ramp: %.6f/s (%.3f dB/s), first block %.6f/s\n", r, r * 72.0, r0);
        check(within(r, 24.0 / 72.0, 0.001) && within(r0, 24.0 / 72.0, 0.001),
              "feedback ramp is a constant 24/72 per second (24 dB/s) from the first block");
        src.setHeld(fbWord(-1, true));
        const auto   w  = track(src, Knob::FeedbackGain, 6000);
        const double rf = (w[0] - w[5999]) / 5999.0 * kB;
        std::printf("       measured fine feedback ramp: %.6f/s (%.3f dB/s)\n", rf, rf * 72.0);
        check(within(rf, 4.0 / 72.0, 0.001), "fine feedback ramp is 4/72 per second, downwards");

        const float stored = static_cast<float>(w.back());
        src.setHeld(fbWord(0, false, true));
        ControlFrame f{};
        src.read(f);
        check(f.knob[ki(Knob::FeedbackGain)] == 1.0f, "feedback maximum: frame FeedbackGain is 1.0");
        check(src.publishedStoredFeedback() == stored && src.publishedFeedbackMax() &&
                  src.publishedKnob(Knob::FeedbackGain) == 1.0f,
              "feedback maximum: stored value unchanged and published, maximum flagged");
        src.setHeld(fbWord(+1, false, true));
        for(int i = 0; i < 120; ++i)
            src.read(f);
        check(f.knob[ki(Knob::FeedbackGain)] == 1.0f && src.publishedStoredFeedback() > stored,
              "the feedback ramp moves the stored value while maximum is held");
        const float stored2 = src.publishedStoredFeedback();
        src.setHeld(0);
        src.read(f);
        check(f.knob[ki(Knob::FeedbackGain)] == stored2 && !src.publishedFeedbackMax(),
              "releasing maximum restores the stored value");
    }

    // ---- Switch: latch XOR held ------------------------------------------------------
    {
        ButtonControlSource src;
        ControlFrame        f{};
        HeldState           s;
        s.switchHeld = true;
        src.setHeld(packHeld(s));
        src.read(f);
        const bool heldOnly = f.delaySwitch;
        src.setHeld(0);
        put(src, ControlCommand::toggleSwitchLatch());
        src.read(f);
        const bool latchedOnly = f.delaySwitch && src.publishedSwitchLatch();
        src.setHeld(packHeld(s));
        src.read(f);
        const bool both = f.delaySwitch;
        check(heldOnly && latchedOnly && !both && src.publishedSwitchLatch() && !src.publishedSwitch(),
              "switch: held = on, latched = on, held while latched = off (XOR)");
        put(src, ControlCommand::toggleSwitchLatch());
        src.setHeld(0);
        src.read(f);
        check(!f.delaySwitch && !src.publishedSwitchLatch(), "toggling the latch again turns it off");
    }

    // ---- Determinism -----------------------------------------------------------------------
    {
        auto runOnce = [](std::vector<float>& out) {
            ButtonControlSource src;
            ControlFrame        f{};
            for(int b = 0; b < 30000; ++b)
            {
                if(b % 997 == 0)
                    src.post(ControlCommand::step(static_cast<Knob>(b % kKnobCount), (b % 3 - 1) * 0.03));
                if(b % 4001 == 0)
                    src.post(ControlCommand::toggleSwitchLatch());
                if(b % 1500 == 0)
                {
                    HeldState s;
                    s.axisKnob[b / 1500 % 2] = static_cast<Knob>((b / 1500) % kKnobCount);
                    s.axisDir[b / 1500 % 2]  = static_cast<std::int8_t>((b / 1500) % 3 - 1);
                    s.feedbackDir            = static_cast<std::int8_t>((b / 3000) % 3 - 1);
                    s.fine                   = (b / 4500) % 2;
                    s.feedbackMax            = (b / 7000) % 2;
                    s.switchHeld             = (b / 5000) % 2;
                    src.setHeld(packHeld(s));
                }
                src.read(f);
                out.insert(out.end(), f.knob, f.knob + kKnobCount);
                out.push_back(f.delaySwitch ? 1.0f : 0.0f);
            }
        };
        std::vector<float> a, b;
        runOnce(a);
        runOnce(b);
        check(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0,
              "the same commands and held words at the same blocks give the same frames");
    }

    return ctest::finish();
}
