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
        // M2b-A: the input folder and the input-peak expectation.
        {
            AppOptions in;
            check(parse({"--input-dir", "/userdata/roms/ports/audreybrick/data/input"}, in, err) &&
                      in.inputDir == "/userdata/roms/ports/audreybrick/data/input",
                  "--input-dir <dir>");
            AppOptions none;
            check(parse({}, none, err) && none.inputDir.empty(), "no --input-dir: no folder");
            check(fails({"--input-dir"}) && fails({"--input-dir", ""}), "--input-dir needs a non-empty value");
            AppOptions st;
            check(parse({"--selftest", "x.txt", "--seconds", "1", "--expect-input-peak-min", "0.4"}, st, err) &&
                      st.hasInputPeakMin && st.inputPeakMin == 0.4,
                  "--expect-input-peak-min with --selftest");
            check(fails({"--expect-input-peak-min", "0.4"}) && fails({"--selftest", "x", "--seconds", "1",
                                                                    "--expect-input-peak-min", "x"}),
                  "--expect-input-peak-min only with --selftest, and only a number");
            check(usageText("audreybrick").find("--input-dir <dir>") != std::string::npos,
                  "the usage text lists --input-dir");
            // M2b-B: the live input's expectations.
            AppOptions lv;
            check(parse({"--selftest", "x.txt", "--seconds", "1", "--expect-live-underruns-max", "3",
                         "--expect-live-overfills-min", "2", "--expect-live-repeats-min", "4", "--expect-live-status", "lost"},
                        lv, err) &&
                      lv.hasLiveUnderrunsMax && lv.liveUnderrunsMax == 3 && lv.hasLiveOverfillsMin &&
                      lv.liveOverfillsMin == 2 && lv.hasLiveRepeatsMin && lv.liveRepeatsMin == 4 && lv.expectLiveStatus == "lost",
                  "--expect-live-underruns-max, --expect-live-overfills-min, --expect-live-status with --selftest");
            check(fails({"--expect-live-status", "open"}) && fails({"--expect-live-underruns-max", "1"}) &&
                      fails({"--selftest", "x", "--seconds", "1", "--expect-live-status", "maybe"}) &&
                      fails({"--selftest", "x", "--seconds", "1", "--expect-live-overfills-min", "-1"}),
                  "the live expectations only with --selftest, and only valid values");
            AppOptions lt;
            check(parse({"--live-target-buffers", "8"}, lt, err) && lt.liveTargetBuffers == 8 &&
                      parse({}, none, err) && none.liveTargetBuffers == 2,
                  "--live-target-buffers 8; default 2");
            check(fails({"--live-target-buffers", "0"}) && fails({"--live-target-buffers", "33"}) &&
                      fails({"--live-target-buffers", "x"}) && fails({"--live-target-buffers"}),
                  "--live-target-buffers: only 1 to 32");
            check(usageText("audreybrick").find("--expect-live-status") != std::string::npos &&
                      usageText("audreybrick").find("--live-target-buffers <n>") != std::string::npos,
                  "the usage text lists the live expectations");
        }
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
    // M3-B: --swap-ab, --swap-xy, --version, --restore-on-exit <file> <content> <expected>
    // (repeatable, validated).
    {
        AppOptions  o;
        std::string err;
        check(parse({}, o, err) && !o.swapAB && !o.swapXY && !o.version && o.restoreOnExit.empty(),
              "defaults: no swaps, no --version, nothing to restore");
        check(parse({"--fullscreen", "--swap-ab"}, o, err) && o.swapAB && !o.swapXY, "--swap-ab");
        check(parse({"--swap-xy"}, o, err) && o.swapXY && !o.swapAB, "--swap-xy");
        check(parse({"--swap-xy", "--swap-ab"}, o, err) && o.swapXY && o.swapAB, "--swap-ab and --swap-xy together");
        check(parse({"--version"}, o, err) && o.version, "--version");
        check(parse({"--restore-on-exit", "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor", "schedutil",
                     "performance", "--restore-on-exit", "/x/policy4/scaling_governor", "ondemand", "performance",
                     "--swap-ab"},
                    o, err) &&
                  o.restoreOnExit.size() == 2 &&
                  o.restoreOnExit[0].file == "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor" &&
                  o.restoreOnExit[0].content == "schedutil" && o.restoreOnExit[0].expected == "performance" &&
                  o.restoreOnExit[1].file == "/x/policy4/scaling_governor" && o.restoreOnExit[1].content == "ondemand" &&
                  o.swapAB,
              "--restore-on-exit takes three values, is repeatable, in order, and options after it still parse");
        check(fails({"--restore-on-exit"}) && fails({"--restore-on-exit", "/a"}) && fails({"--restore-on-exit", "/a", "x"}),
              "--restore-on-exit needs <file> <content> <expected>");
        check(fails({"--restore-on-exit", "/a", "schedutil", "--swap-ab"}) &&
                  fails({"--restore-on-exit", "/a", "--swap-ab", "performance"}),
              "a following option is never taken as content or expected (usage error)");
        check(fails({"--restore-on-exit", "relative/file", "a", "b"}) && fails({"--restore-on-exit", "", "a", "b"}),
              "the file must be an absolute path");
        check(fails({"--restore-on-exit", "/a", "", "b"}) && fails({"--restore-on-exit", "/a", "a", ""}) &&
                  fails({"--restore-on-exit", "/a", "Performance", "b"}) &&
                  fails({"--restore-on-exit", "/a", "a b", "c"}) && fails({"--restore-on-exit", "/a", "a;reboot", "c"}) &&
                  fails({"--restore-on-exit", "/a", std::string(33, 'x').c_str(), "c"}),
              "content and expected must be [a-z0-9_]{1,32}");
    }
    // M3-C: the help names Menu + Start as the pad's quit; no hold-to-quit is left.
    check(keyboardHelpText().find("on a pad: Menu + Start") != std::string::npos &&
              usageText("audreybrick").find("held 1 s") == std::string::npos &&
              usageText("audreybrick").find("Select + Start") == std::string::npos,
          "help: quit on a pad is Menu + Start; no Select + Start hold");
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
