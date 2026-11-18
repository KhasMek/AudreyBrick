// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// App pieces that run without a window: command-line parsing (every option,
// unknown options and bad values are errors) and fillControlSnapshot (the
// snapshot reflects the model and the source's published values).

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/UiSnapshot.h"
#include "core/ParamTable.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
bool parse(std::initializer_list<const char*> args, AppOptions& o, std::string& err)
{
    std::vector<const char*> v{"audreybrick"};
    v.insert(v.end(), args.begin(), args.end());
    return parseOptions(static_cast<int>(v.size()), v.data(), o, err);
}

bool fails(std::initializer_list<const char*> args)
{
    AppOptions  o;
    std::string err;
    const bool  ok = parse(args, o, err);
    if(!ok)
        std::printf("    error: %s\n", err.c_str());
    return !ok && !err.empty();
}
} // namespace

int main()
{
    // --- Options -----------------------------------------------------------------
    {
        AppOptions  o;
        std::string err;
        check(parse({}, o, err) && o.bufferFrames == 256 && o.audioDriver.empty() && !o.listAudio && !o.help &&
                  !o.selftest(),
              "no options: defaults (256 frames, SDL's driver, normal mode)");
        check(parse({"--buffer", "512", "--audio-driver", "dummy"}, o, err) && o.bufferFrames == 512 &&
                  o.audioDriver == "dummy",
              "--buffer and --audio-driver");
        check(parse({"--list-audio"}, o, err) && o.listAudio, "--list-audio");
        check(parse({"--help"}, o, err) && o.help, "--help");
        check(parse({"--selftest", "x.txt", "--seconds", "2.5"}, o, err) && o.selftest() &&
                  o.selftestScript == "x.txt" && o.selftestSeconds == 2.5,
              "--selftest with --seconds");
        check(usageText("audreybrick").find("--selftest") != std::string::npos, "usage text lists the options");
        check(usageText("audreybrick").find("the A key is L2, not the A button") != std::string::npos &&
                  keyboardHelpText().find("Space          A button") != std::string::npos,
              "help states that the A key is L2 and Space is the A button");
        check(parse({"--list-audio", "--audio-driver", "dummy"}, o, err) && o.listAudio && o.audioDriver == "dummy",
              "--list-audio with --audio-driver (lists that driver only)");
        check(parse({"--selftest", "x.txt", "--seconds", "5", "--expect-knob", "FeedbackGain", "0.9", "0.95",
                     "--expect-knob", "outputvolume", "0.3", "0.4", "--expect-peak-min", "0.01",
                     "--expect-peak-max", "1.0"},
                    o, err) &&
                  o.expectKnobs.size() == 2 && o.expectKnobs[0].knob == Knob::FeedbackGain &&
                  o.expectKnobs[0].min == 0.9 && o.expectKnobs[0].max == 0.95 &&
                  o.expectKnobs[1].knob == Knob::OutputVolume && o.hasPeakMin && o.peakMin == 0.01 &&
                  o.hasPeakMax && o.peakMax == 1.0,
              "--expect-knob (repeatable, case-insensitive name) and --expect-peak-min/max");
    }
    {
        AppOptions  o;
        std::string err;
        check(parse({"--fullscreen", "--log", "data/a b.log"}, o, err) && o.fullscreen && o.logPath == "data/a b.log" &&
                  !o.noVideo,
              "--fullscreen and --log (M3-A)");
        check(parse({"--selftest", "x.txt", "--seconds", "1", "--no-video"}, o, err) && o.noVideo && o.selftest(),
              "--no-video with --selftest");
        check(parse({}, o, err) && !o.fullscreen && o.logPath.empty() && !o.noVideo, "defaults: windowed, no log, video");
        check(usageText("audreybrick").find("--no-video") != std::string::npos &&
                  usageText("audreybrick").find("--fullscreen") != std::string::npos,
              "usage lists --fullscreen, --log and --no-video");
    }
    check(fails({"--no-video"}), "--no-video without --selftest is an error");
    {
        AppOptions  o;
        std::string err;
        check(parse({"--fullscreen", "--exit-with-parent"}, o, err) && o.exitWithParent && o.fullscreen,
              "--exit-with-parent (what the launcher passes)");
        check(parse({}, o, err) && !o.exitWithParent && o.removeOnExit.empty(), "--exit-with-parent is off by default");
        check(parse({"--remove-on-exit", "/var/run/battery-saver/audreybrick.pause"}, o, err) &&
                  o.removeOnExit == "/var/run/battery-saver/audreybrick.pause",
              "--remove-on-exit <file>");
        check(fails({"--remove-on-exit"}) && fails({"--remove-on-exit", ""}), "--remove-on-exit needs a file name");
    }
    check(fails({"--log"}), "--log without a value");
    check(fails({"--log", ""}), "--log with an empty name");
    check(fails({"--bogus"}), "unknown option is an error");
    check(fails({"-x"}), "unknown short option is an error");
    check(fails({"extra"}), "a stray argument is an error");
    check(fails({"--buffer"}), "--buffer without a value");
    check(fails({"--buffer", "abc"}), "--buffer abc");
    check(fails({"--buffer", "8"}), "--buffer below the minimum");
    check(fails({"--buffer", "65536"}), "--buffer above the maximum");
    check(fails({"--buffer", "256x"}), "--buffer with trailing junk");
    check(fails({"--audio-driver"}), "--audio-driver without a value");
    check(fails({"--selftest", "x.txt"}), "--selftest without --seconds");
    check(fails({"--seconds", "3"}), "--seconds without --selftest");
    check(fails({"--selftest", "x.txt", "--seconds", "0"}), "--seconds 0");
    check(fails({"--selftest", "x.txt", "--seconds", "-1"}), "--seconds negative");
    check(fails({"--selftest", "x.txt", "--seconds", "nan"}), "--seconds nan");
    check(fails({"--expect-knob", "FeedbackGain", "0", "1"}), "--expect-knob without --selftest");
    check(fails({"--expect-peak-min", "0.1"}), "--expect-peak-min without --selftest");
    check(fails({"--selftest", "x.txt", "--seconds", "1", "--expect-knob", "Nope", "0", "1"}), "--expect-knob unknown knob");
    check(fails({"--selftest", "x.txt", "--seconds", "1", "--expect-knob", "FeedbackGain", "0.5"}),
          "--expect-knob with too few values");
    check(fails({"--selftest", "x.txt", "--seconds", "1", "--expect-knob", "FeedbackGain", "0.6", "0.5"}),
          "--expect-knob with min > max");
    check(fails({"--selftest", "x.txt", "--seconds", "1", "--expect-peak-max", "abc"}), "--expect-peak-max abc");

    // --- fillControlSnapshot ---------------------------------------------------------
    {
        ButtonControlSource src;
        ControlModel        model(src);
        ControlFrame        f{};
        UiSnapshot          s;
        src.read(f);
        fillControlSnapshot(s, model, src);
        bool defaults = true;
        for(std::size_t k = 0; k < kKnobCount; ++k)
        {
            const Knob       knob = static_cast<Knob>(k);
            const ParamSpec& p    = paramSpec(knob);
            defaults = defaults && s.knob[k].norm == p.defaultNorm &&
                       s.knob[k].engine == knobToEngine(knob, p.defaultNorm, false) &&
                       std::string(s.knob[k].unit) == p.unit;
        }
        check(defaults, "start-up: every knob at its default, engine value and unit from ParamTable");
        check(s.tab == Tab::Loop && !s.fine && !s.xHeld && !s.yHeld && !s.switchOn && !s.switchLatch &&
                  !s.feedbackMax && s.commandsDropped == 0,
              "start-up: LOOP, no modifiers, switch off, no maximum");
        check(std::fabs(s.storedFeedbackEngine - (-60.0f)) < 1e-4f, "stored feedback starts at -60 dB");

        double t = 0.0;
        model.onButton({Button::R1, true}, t);   // LOOP -> FILTER
        model.onButton({Button::R1, false}, t);
        model.onButton({Button::Select, true}, t);
        model.onButton({Button::X, true}, t);
        model.onButton({Button::Y, true}, t);
        model.onButton({Button::A, true}, t);    // feedback maximum
        model.onButton({Button::B, true}, t);    // Select + B: toggles the latch, not held
        model.onButton({Button::R2, true}, t);   // one fine feedback tap
        model.tick(t);
        for(int i = 0; i < 4; ++i)
            src.read(f);
        fillControlSnapshot(s, model, src);
        check(s.tab == Tab::Filter, "tab follows the model");
        check(s.fine && s.xHeld && s.yHeld, "modifiers Select, X, Y shown");
        check(s.buttonDown[static_cast<std::size_t>(Button::A)] && s.buttonDown[static_cast<std::size_t>(Button::R2)] &&
                  !s.buttonDown[static_cast<std::size_t>(Button::L1)],
              "buttonDown mirrors the model");
        check(s.feedbackMax && s.knob[static_cast<std::size_t>(Knob::FeedbackGain)].norm == 1.0f,
              "feedback maximum: the frame value reads 1.0");
        check(std::fabs(s.storedFeedback - static_cast<float>(tuning::kFbTapStepFine)) < 1e-6f,
              "stored feedback holds the fine tap, not the maximum");
        check(s.switchLatch && s.switchOn && !s.switchHeld, "Select + B: latched, on, not held");
        // The delay switch halves the delay time knob before mapping.
        const float n = s.knob[static_cast<std::size_t>(Knob::EchoDelayTime)].norm;
        check(s.knob[static_cast<std::size_t>(Knob::EchoDelayTime)].engine == knobToEngine(Knob::EchoDelayTime, n, true),
              "delay time engine value includes the switch");
        check(std::string(s.knob[static_cast<std::size_t>(Knob::FeedbackLPFCutoff)].unit) == "Hz", "units are set");
    }
    return ptest::finish();
}
