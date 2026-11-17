// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ScriptControlSource: parsing, defaults, jumps, linear ramps, the switch, block
// quantisation, rewind and determinism, and rejection of malformed lines.

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "control/ScriptControlSource.h"
#include "core/ParamTable.h"
#include "test_common.h"

using namespace audreybrick;
using coretest::check;

namespace
{
constexpr std::size_t kFb  = static_cast<std::size_t>(Knob::FeedbackGain);
constexpr std::size_t kVol = static_cast<std::size_t>(Knob::OutputVolume);

bool rejects(const std::string& text, const char* what)
{
    ScriptControlSource s;
    std::string         err;
    const bool          ok = s.loadString(text, err);
    std::printf("       %s -> %s\n", what, ok ? "accepted" : err.c_str());
    return !ok;
}
} // namespace

int main()
{
    // Defaults before any event.
    {
        ScriptControlSource s = coretest::loadScript("# nothing\n\n");
        ControlFrame        f{};
        s.read(f);
        bool ok = !f.delaySwitch;
        for(std::size_t i = 0; i < kKnobCount; ++i)
            ok = ok && f.knob[i] == paramSpec(static_cast<Knob>(i)).defaultNorm;
        check(ok, "empty script: every knob at its default position, switch off");
    }

    // Jump, ramp, switch, quantisation.
    {
        // Block = 1/12000 s. 0.1 s = block 1200 exactly. 1.00005 s = block 12000.6 -> 12001.
        ScriptControlSource s = coretest::loadScript(
            "0     FeedbackGain 0.5        # jump at block 0\n"
            "0.1   FeedbackGain 1.0  0.01  # ramp over 120 blocks from block 1200\n"
            "0.2   switch 1\n"
            "1.00005 OutputVolume 0.25\n"
            "0.15  feedbackgain 0.0  0.01  # later in time than the 0.1 ramp but listed after\n");
        check(s.events().size() == 5, "five events parsed");
        check(s.events()[1].startBlock == 1200 && s.events()[1].rampBlocks == 120,
              "0.1 s is block 1200 and a 0.01 s ramp is 120 blocks");
        check(s.events()[2].timeSeconds == 0.15, "events are sorted by time");
        check(s.events()[4].startBlock == 12001, "1.00005 s rounds up to block 12001");

        ControlFrame f{};
        float        at[13000][2];
        bool         sw[13000];
        for(int b = 0; b < 13000; ++b)
        {
            s.read(f);
            at[b][0] = f.knob[kFb];
            at[b][1] = f.knob[kVol];
            sw[b]    = f.delaySwitch;
        }
        check(at[0][0] == 0.5f && at[1199][0] == 0.5f, "jump applies at block 0 and holds");
        check(at[1200][0] == 0.5f && at[1260][0] == 0.75f && at[1320][0] == 1.0f && at[1400][0] == 1.0f,
              "ramp is linear: 0.5 at its start, 0.75 halfway, 1.0 at the end and after");
        check(at[1800][0] == 1.0f && at[1860][0] == 0.5f && at[1920][0] == 0.0f, "second ramp goes 1 -> 0");
        check(!sw[2399] && sw[2400], "switch turns on at block 2400 (0.2 s)");
        check(at[12000][1] == paramSpec(Knob::OutputVolume).defaultNorm && at[12001][1] == 0.25f,
              "an event between blocks takes effect at the next block");
        check(s.blockIndex() == 13000, "one block per read()");

        // Rewind reproduces the same sequence.
        s.rewind();
        bool same = true;
        for(int b = 0; b < 13000; ++b)
        {
            s.read(f);
            same = same && f.knob[kFb] == at[b][0] && f.knob[kVol] == at[b][1] && f.delaySwitch == sw[b];
        }
        check(same, "rewind reproduces the identical sequence (deterministic)");
    }

    // Block quantisation late in a script: "first block that starts at or after the
    // time" must hold at any time (absolute snap tolerance).
    {
        ScriptControlSource s = coretest::loadScript("41.00004 ReverbMix 0.1\n"
                                                     "100.00004 ReverbMix 0.2\n"
                                                     "600.00004 ReverbMix 0.3\n"
                                                     "600.0 ReverbMix 0.4\n"
                                                     "3600.0 ReverbMix 0.5\n"
                                                     "100.1 ReverbMix 0.6\n");
        const auto& e = s.events();
        const struct
        {
            double       t;
            std::int64_t want;
        } cases[] = {{41.00004, 492001}, {100.00004, 1200001}, {100.1, 1201200},
                     {600.0, 7200000},   {600.00004, 7200001}, {3600.0, 43200000}};
        for(const auto& c : cases)
        {
            std::int64_t got = -1;
            for(const auto& ev : e)
                if(ev.timeSeconds == c.t)
                    got = ev.startBlock;
            char msg[160];
            std::snprintf(msg, sizeof msg, "%.5f s -> block %lld (want %lld)", c.t, static_cast<long long>(got),
                          static_cast<long long>(c.want));
            check(got == c.want, msg);
        }
    }

    // Number format.
    {
        ScriptControlSource s = coretest::loadScript("+0 FeedbackGain 2.5e-1 1E-2\n"
                                                     "1e0 ReverbMix 1\n");
        const auto& e = s.events();
        check(e.size() == 2 && e[0].value == 0.25f && e[0].rampBlocks == 120 && e[1].startBlock == 12000,
              "accepts +0, 2.5e-1, 1E-2, 1e0 and integers");
    }
    check(rejects("0 FeedbackGain .5\n", "\".5\""), "\".5\" is rejected");
    check(rejects("0 FeedbackGain 5.\n", "\"5.\""), "\"5.\" is rejected");
    check(rejects("0 FeedbackGain 0,5\n", "\"0,5\""), "\"0,5\" is rejected");
    check(rejects("0x1 FeedbackGain 0.5\n", "hex time"), "hex is rejected");
    check(rejects("0 FeedbackGain inf\n", "inf"), "inf is rejected");
    check(rejects("1e999 FeedbackGain 0.5\n", "1e999"), "overflowing number is rejected");

    // Locale independence: parse after switching LC_NUMERIC to a comma-decimal locale.
    {
        const char* candidates[] = {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "nl_NL.UTF-8", "de_DE.ISO8859-1",
                                    "fr_FR", "nl_NL"};
        const char* used = nullptr;
        for(const char* c : candidates)
        {
            if(std::setlocale(LC_NUMERIC, c) != nullptr && std::localeconv()->decimal_point[0] == ',')
            {
                used = c;
                break;
            }
        }
        if(used == nullptr)
        {
            std::setlocale(LC_NUMERIC, "C");
            std::printf("[SKIP] no comma-decimal locale could be set; locale test not run\n");
        }
        else
        {
            // Show that the locale is really in effect: strtod stops at the '.'.
            const double strtodSays = std::strtod("0.25", nullptr);
            ScriptControlSource s;
            std::string         err;
            const bool ok = s.loadString("0.5 FeedbackGain 0.25 0.125\n", err);
            const bool rejectsComma = !s.loadString("0,5 FeedbackGain 0,25\n", err);
            ScriptControlSource s2 = coretest::loadScript("0.5 FeedbackGain 0.25 0.125\n");
            std::setlocale(LC_NUMERIC, "C");
            std::printf("       LC_NUMERIC=%s: strtod(\"0.25\") = %g\n", used, strtodSays);
            check(ok && s2.events().size() == 1 && s2.events()[0].timeSeconds == 0.5
                      && s2.events()[0].value == 0.25f && s2.events()[0].rampSeconds == 0.125
                      && s2.events()[0].startBlock == 6000 && s2.events()[0].rampBlocks == 1500,
                  std::string("script parses correctly under LC_NUMERIC=") + used);
            check(rejectsComma, std::string("comma decimals are still rejected under LC_NUMERIC=") + used);
        }
    }

    // A new event during a ramp starts from the current value.
    {
        ScriptControlSource s = coretest::loadScript("0 ReverbMix 0\n"
                                                     "0 ReverbMix 1 1.0\n"     // 12000 blocks
                                                     "0.5 ReverbMix 0 0.5\n"); // from 0.5 at block 6000
        ControlFrame        f{};
        float               v[12001];
        for(int b = 0; b <= 12000; ++b)
        {
            s.read(f);
            v[b] = f.knob[static_cast<std::size_t>(Knob::ReverbMix)];
        }
        check(v[6000] == 0.5f && v[9000] == 0.25f && v[12000] == 0.0f,
              "an interrupting ramp continues from the knob's value at that block");
    }

    // Malformed scripts.
    std::printf("rejections:\n");
    check(rejects("0 Nope 0.5\n", "unknown knob"), "unknown knob name is rejected");
    check(rejects("0 FeedbackGain 1.5\n", "value > 1"), "knob value above 1 is rejected");
    check(rejects("0 FeedbackGain -0.1\n", "value < 0"), "knob value below 0 is rejected");
    check(rejects("-1 FeedbackGain 0.5\n", "negative time"), "negative time is rejected");
    check(rejects("0 FeedbackGain 0.5 -1\n", "negative ramp"), "negative ramp is rejected");
    check(rejects("0 switch 0.5\n", "switch 0.5"), "switch value other than 0/1 is rejected");
    check(rejects("0 switch 1 0.1\n", "switch ramp"), "switch with a ramp is rejected");
    check(rejects("0 FeedbackGain\n", "two fields"), "too few fields is rejected");
    check(rejects("0 FeedbackGain 0.5 1 2\n", "five fields"), "too many fields is rejected");
    check(rejects("0 FeedbackGain 0.5x\n", "trailing junk"), "trailing junk in a number is rejected");
    check(rejects("0 FeedbackGain nan\n", "nan"), "non-finite value is rejected");

    // A failed load leaves the previous script in place.
    {
        ScriptControlSource s = coretest::loadScript("0 FeedbackGain 1\n");
        std::string         err;
        const bool          loaded = s.loadString("0 Nope 1\n", err);
        ControlFrame        f{};
        s.read(f);
        check(!loaded && s.events().size() == 1 && f.knob[kFb] == 1.0f, "a failed load keeps the previous script");
    }

    // File loading.
    {
        ScriptControlSource s;
        std::string         err;
        check(!s.loadFile("/nonexistent/definitely/not/here.txt", err) && !err.empty(),
              "missing file is reported");
    }

    return coretest::finish();
}
