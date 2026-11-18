// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Number formatting for the screen (ui/Format.h): note names and Hz across the
// Frequency range (including its ends, notes 16 and 72), Hz/kHz tiers, ms/s
// switch-over, dB with sign and no negative zero, percentages, CPU, values at the
// ParamTable range ends (unitless knobs show their position, 0..100 %),
// non-finite values and truncation.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "core/ControlSource.h"
#include "core/ParamTable.h"
#include "ui/Format.h"
#include "ui_test_common.h"

namespace
{
using namespace audreybrick;
using namespace audreybrick::ui;
using uitest::check;

template <typename F>
void expect(F fn, float v, const char* want, const char* what)
{
    char buf[64];
    std::memset(buf, 'Z', sizeof buf);
    const std::size_t n  = fn(buf, sizeof buf, v);
    const bool        ok = std::strcmp(buf, want) == 0 && n == std::strlen(want);
    check(ok, std::string(what) + ": " + std::to_string(v) + " -> \"" + buf + "\" (want \"" + want + "\")");
}

void expectKnob(Knob k, float norm, bool sw, const char* want)
{
    char        buf[64];
    UiKnobView  v;
    v.norm   = norm;
    v.engine = knobToEngine(k, norm, sw);
    v.unit   = paramSpec(k).unit;
    formatKnob(buf, sizeof buf, v);
    check(std::strcmp(buf, want) == 0, std::string(paramSpec(k).id) + " at n=" + std::to_string(norm) +
                                           (sw ? " (switch on)" : "") + " -> \"" + buf + "\" (want \"" + want + "\")");
}
} // namespace

int main()
{
    // --- Notes -------------------------------------------------------------------------
    check(std::fabs(midiNoteToHz(69.0) - 440.0) < 1e-9, "note 69 is 440 Hz");
    check(std::fabs(midiNoteToHz(16.0) - 20.6017) < 1e-3, "note 16 is 20.60 Hz (range bottom)");
    check(std::fabs(midiNoteToHz(72.0) - 523.2511) < 1e-3, "note 72 is 523.25 Hz (range top)");
    expect(formatNoteName, 16.0f, "E0", "note name");
    expect(formatNoteName, 72.0f, "C5", "note name");
    expect(formatNoteName, 40.0f, "E2", "note name");
    expect(formatNoteName, 60.0f, "C4", "note name (MIDI 60 = C4)");
    expect(formatNoteName, 69.0f, "A4", "note name");
    expect(formatNoteName, 61.0f, "C#4", "note name (sharp)");
    expect(formatNoteName, 40.1f, "E2+10c", "note name, fine step up");
    expect(formatNoteName, 39.9f, "E2-10c", "note name, fine step down");
    expect(formatNoteName, 43.3f, "G2+30c", "note name");
    expect(formatNoteName, 40.004f, "E2", "note name within half a cent");
    expect(formatNoteName, 40.6f, "F2-40c", "note name rounds to the nearest note");
    // Half a semitone always reads as -50 cents of the upper note.
    expect(formatNoteName, 40.494f, "E2+49c", "just below a half-semitone");
    expect(formatNoteName, 40.4951f, "F2-50c", "cents rounding to +50 become -50 of the upper note");
    expect(formatNoteName, 40.5f, "F2-50c", "exactly half a semitone");
    expect(formatNoteName, 40.5049f, "F2-50c", "just above a half-semitone");
    expect(formatNoteName, 40.506f, "F2-49c", "above a half-semitone");
    expect(formatNoteName, 71.5f, "C5-50c", "half a semitone below the range top");
    expect(formatNoteName, 59.5f, "C4-50c", "half a semitone across an octave boundary");
    {
        // Accumulated float steps (ten 0.05 steps, 0.1 fine steps, 1/112 of the
        // range): the same pitch must not flip between +50c and -50c forms.
        float a = 40.0f;
        for(int i = 0; i < 10; ++i)
            a += 0.05f;
        float b = 40.0f;
        for(int i = 0; i < 5; ++i)
            b += 0.1f;
        float c = 40.0f;
        for(int i = 0; i < 28; ++i)
            c += 1.0f / 56.0f;
        expect(formatNoteName, a, "F2-50c", "40 + 10 x 0.05 (float)");
        expect(formatNoteName, b, "F2-50c", "40 + 5 x 0.1 (float)");
        expect(formatNoteName, c, "F2-50c", "40 + 28 x 1/56 (float)");
        expect(formatNoteName, std::nextafter(40.5f, 0.0f), "F2-50c", "the float just below 40.5");
        expect(formatNoteName, std::nextafter(40.5f, 100.0f), "F2-50c", "the float just above 40.5");
        // No input ever produces +50c.
        int plus50 = 0;
        for(int i = 0; i <= 56000; ++i)
        {
            char buf[32];
            formatNoteName(buf, sizeof buf, 16.0f + static_cast<float>(i) / 1000.0f);
            if(std::strstr(buf, "+50c") != nullptr)
                ++plus50;
        }
        check(plus50 == 0, "no note from 16 to 72 in 1/1000 steps reads +50c (" + std::to_string(plus50) + ")");
    }
    expect(formatNote, 16.0f, "E0 20.6 Hz", "note with Hz (range bottom)");
    expect(formatNote, 72.0f, "C5 523 Hz", "note with Hz (range top)");
    expect(formatNote, 40.0f, "E2 82.4 Hz", "note with Hz (default)");
    expect(formatNote, 40.1f, "E2+10c 82.9 Hz", "note with cents and Hz");
    expect(formatNote, 45.0f, "A2 110 Hz", "note with Hz");
    expect(formatNote, 57.0f, "A3 220 Hz", "note with Hz");
    expect(formatNote, std::numeric_limits<float>::quiet_NaN(), "--", "note NaN");

    // --- Hz -------------------------------------------------------------------------------
    expect(formatHz, 10.0f, "10.0 Hz", "Hz");
    expect(formatHz, 25.31f, "25.3 Hz", "Hz");
    expect(formatHz, 99.94f, "99.9 Hz", "Hz below the tier");
    expect(formatHz, 99.96f, "100 Hz", "Hz rounding into the next tier");
    expect(formatHz, 250.0f, "250 Hz", "Hz");
    expect(formatHz, 999.4f, "999 Hz", "Hz");
    expect(formatHz, 999.6f, "1.00 kHz", "Hz rounding into kHz");
    expect(formatHz, 4000.0f, "4.00 kHz", "kHz");
    expect(formatHz, 9999.0f, "10.0 kHz", "kHz rounding into one decimal");
    expect(formatHz, 18000.0f, "18.0 kHz", "kHz");
    expect(formatHz, -0.0f, "0.0 Hz", "Hz negative zero");
    expect(formatHz, std::numeric_limits<float>::infinity(), "--", "Hz inf");

    // --- Seconds ----------------------------------------------------------------------------
    expect(formatSeconds, 0.001f, "1.00 ms", "ms");
    expect(formatSeconds, 0.009996f, "10.0 ms", "ms rounding into one decimal");
    expect(formatSeconds, 0.0453f, "45.3 ms", "ms");
    expect(formatSeconds, 0.05f, "50.0 ms", "ms");
    expect(formatSeconds, 0.1f, "100 ms", "ms");
    expect(formatSeconds, 0.5f, "500 ms", "ms");
    expect(formatSeconds, 0.9994f, "999 ms", "ms just below one second");
    expect(formatSeconds, 0.9996f, "1.00 s", "ms rounding into seconds");
    expect(formatSeconds, 1.0f, "1.00 s", "s at the switch-over");
    expect(formatSeconds, 1.29f, "1.29 s", "s");
    expect(formatSeconds, 5.0f, "5.00 s", "s");
    expect(formatSeconds, -0.0f, "0.00 ms", "seconds negative zero");

    // --- dB ------------------------------------------------------------------------------------
    expect(formatDb, -60.0f, "-60.0 dB", "dB");
    expect(formatDb, -10.0f, "-10.0 dB", "dB");
    expect(formatDb, 12.0f, "+12.0 dB", "dB");
    expect(formatDb, 0.0f, "0.0 dB", "dB zero");
    expect(formatDb, -0.0f, "0.0 dB", "dB negative zero");
    expect(formatDb, -0.04f, "0.0 dB", "dB rounding to zero from below");
    expect(formatDb, 0.04f, "0.0 dB", "dB rounding to zero from above");
    expect(formatDb, 0.06f, "+0.1 dB", "dB");
    expect(formatDb, std::numeric_limits<float>::quiet_NaN(), "--", "dB NaN");

    // --- Percent and CPU -------------------------------------------------------------------------
    expect(formatPercent, 0.0f, "0 %", "percent");
    expect(formatPercent, -0.0f, "0 %", "percent negative zero");
    expect(formatPercent, -0.004f, "0 %", "percent rounding to zero from below");
    expect(formatPercent, 0.42f, "42 %", "percent");
    expect(formatPercent, 1.0f, "100 %", "percent");
    expect(formatPercent, 1.5f, "150 %", "percent");
    expect(formatCpu, 0.0f, "0.0 %", "cpu");
    expect(formatCpu, 0.034f, "3.4 %", "cpu");
    expect(formatCpu, 0.0996f, "10 %", "cpu rounding into whole numbers");
    expect(formatCpu, 0.85f, "85 %", "cpu");
    expect(formatCpu, 1.0f, "100 %", "cpu");

    // --- Every knob at its range ends, through ParamTable ------------------------------------------
    expectKnob(Knob::Frequency, 0.0f, false, "E0 20.6 Hz");
    expectKnob(Knob::Frequency, 1.0f, false, "C5 523 Hz");
    expectKnob(Knob::FeedbackGain, 0.0f, false, "-60.0 dB");
    expectKnob(Knob::FeedbackGain, 1.0f, false, "+12.0 dB");
    expectKnob(Knob::FeedbackGain, 60.0f / 72.0f, false, "0.0 dB");
    expectKnob(Knob::FeedbackBody, 0.0f, false, "1.00 ms");
    expectKnob(Knob::FeedbackBody, 1.0f, false, "100 ms");
    expectKnob(Knob::FeedbackLPFCutoff, 0.0f, false, "100 Hz");
    expectKnob(Knob::FeedbackLPFCutoff, 1.0f, false, "18.0 kHz");
    expectKnob(Knob::FeedbackHPFCutoff, 0.0f, false, "10.0 Hz");
    expectKnob(Knob::FeedbackHPFCutoff, 1.0f, false, "4.00 kHz");
    expectKnob(Knob::FeedbackHPFCutoff, paramSpec(Knob::FeedbackHPFCutoff).defaultNorm, false, "250 Hz");
    expectKnob(Knob::ReverbMix, 0.0f, false, "0 %");
    expectKnob(Knob::ReverbMix, 1.0f, false, "100 %");
    expectKnob(Knob::ReverbDecay, 0.0f, false, "0 %");
    expectKnob(Knob::ReverbDecay, 0.5f, false, "50 %"); // engine 0.85: the number is the knob position
    expectKnob(Knob::ReverbDecay, 1.0f, false, "100 %");
    expectKnob(Knob::EchoDelaySend, 0.0f, false, "0 %");
    expectKnob(Knob::EchoDelaySend, 1.0f, false, "100 %");
    expectKnob(Knob::EchoDelayTime, 0.0f, false, "50.0 ms");
    expectKnob(Knob::EchoDelayTime, 1.0f, false, "5.00 s");
    expectKnob(Knob::EchoDelayTime, 1.0f, true, "1.29 s");
    expectKnob(Knob::EchoDelayTime, paramSpec(Knob::EchoDelayTime).defaultNorm, false, "500 ms");
    expectKnob(Knob::EchoDelayFeedback, 0.0f, false, "0 %");
    expectKnob(Knob::EchoDelayFeedback, 1.0f, false, "100 %"); // engine 1.5
    expectKnob(Knob::EchoDelayFeedback, 0.5f, false, "50 %");
    expectKnob(Knob::OutputVolume, 0.0f, false, "0 %");
    expectKnob(Knob::OutputVolume, 1.0f, false, "100 %");
    expectKnob(Knob::OutputVolume, paramSpec(Knob::OutputVolume).defaultNorm, false, "71 %"); // engine 0.5
    expectKnob(Knob::EchoDelaySend, 0.5f, false, "50 %"); // engine 0.25
    expectKnob(Knob::ReverbMix, 0.5f, false, "50 %");
    expectKnob(Knob::OutputVolume, -0.0f, false, "0 %");

    // --- Truncation and odd units ---------------------------------------------------------------------
    {
        char       small[6];
        const auto n = formatNote(small, sizeof small, 40.1f);
        check(std::strlen(small) == 5 && n == 5 && std::strncmp(small, "E2+10", 5) == 0,
              std::string("truncation keeps a terminated prefix: \"") + small + "\"");
        char one[1] = {'x'};
        formatDb(one, sizeof one, -10.0f);
        check(one[0] == '\0', "a one-byte buffer receives an empty string");
        char unk[32];
        formatEngineValue(unk, sizeof unk, 2.5f, "V");
        check(std::strcmp(unk, "2.5 V") == 0, std::string("unknown unit: \"") + unk + "\"");
        formatEngineValue(unk, sizeof unk, 0.25f, nullptr);
        check(std::strcmp(unk, "25 %") == 0, std::string("null unit reads as empty: \"") + unk + "\"");
        UiKnobView v;
        v.norm   = 0.3f;
        v.engine = 0.09f;
        v.unit   = nullptr;
        formatKnob(unk, sizeof unk, v);
        check(std::strcmp(unk, "30 %") == 0, std::string("formatKnob with a null unit shows the position: \"") + unk + "\"");
    }
    return uitest::finish();
}
