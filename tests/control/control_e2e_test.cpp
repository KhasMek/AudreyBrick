// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Criterion 5: a button script (feedback rise with R2, a tab change, a held d-pad
// ramp, a momentary maximum, a switch hold) parsed by ButtonScript, fed through
// ControlModel and ButtonControlSource into AudreyVoice for 20 s, the way the app
// will: a 60 Hz UI frame delivers the events due by then (stamped with the frame
// time) and ticks the model, then the audio renders one frame's worth (800
// samples). Checks finite output, peak below 1.0, and the same output twice.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "app/ButtonScript.h"
#include "app/ControlModel.h"
#include "control_test_common.h"
#include "core/AudreyVoice.h"

using namespace audreybrick;
using ctest::check;

namespace
{
const char* kScript = "# feedback rise: R2 held 2.8 s (tap + 2.55 s at 24 dB/s)\n"
                      "0.50  R2        down\n"
                      "3.30  R2        up\n"
                      "# tab change: LOOP -> FILTER\n"
                      "4.00  R1        down\n"
                      "4.05  R1        up\n"
                      "# held d-pad ramp: lowpass down for 1.5 s\n"
                      "5.00  DpadLeft  down\n"
                      "6.50  DpadLeft  up\n"
                      "# to DELAY (FILTER -> REVERB -> DELAY), raise the delay mix\n"
                      "7.00  R1        down\n"
                      "7.05  R1        up\n"
                      "7.20  R1        down\n"
                      "7.25  R1        up\n"
                      "8.00  DpadUp    down\n"
                      "9.20  DpadUp    up\n"
                      "# momentary feedback maximum\n"
                      "10.00 A         down\n"
                      "11.00 A         up\n"
                      "# switch held (doppler warp)\n"
                      "12.50 B         down\n"
                      "14.00 B         up\n";

struct Result
{
    std::vector<float> l, r;
    float              fb = 0, lp = 0, mix = 0;
    Tab                tab = Tab::Loop;
};

Result run(const std::vector<TimedButtonEvent>& events)
{
    constexpr int    kSeconds      = 20;
    constexpr int    kFrameSamples = AudreyVoice::kSampleRate / 60; // 800
    constexpr int    kFrames       = kSeconds * 60;
    auto             voice         = std::make_unique<AudreyVoice>();
    auto             src           = std::make_unique<ButtonControlSource>();
    ControlModel     model(*src);
    Result           res;
    std::size_t      next = 0;
    voice->init();
    res.l.resize(static_cast<std::size_t>(kFrames) * kFrameSamples);
    res.r.resize(res.l.size());
    for(int frame = 0; frame < kFrames; ++frame)
    {
        const double now = frame / 60.0;
        while(next < events.size() && events[next].timeSeconds <= now)
            model.onButton(events[next++].event, now);
        model.tick(now);
        const std::size_t pos = static_cast<std::size_t>(frame) * kFrameSamples;
        voice->process(*src, nullptr, res.l.data() + pos, res.r.data() + pos, kFrameSamples);
    }
    res.fb  = src->publishedStoredFeedback();
    res.lp  = src->publishedKnob(Knob::FeedbackLPFCutoff);
    res.mix = src->publishedKnob(Knob::EchoDelaySend);
    res.tab = model.currentTab();
    return res;
}
} // namespace

int main()
{
    std::vector<TimedButtonEvent> events;
    std::string                   err;
    if(!parseButtonScript(kScript, events, err))
    {
        std::printf("[FAIL] script: %s\n", err.c_str());
        return 1;
    }
    check(events.size() == 16, "the button script parses (16 events)");

    const Result a = run(events);
    const Result b = run(events);

    long  nonFinite = 0;
    float peak      = 0.0f;
    double sum      = 0.0;
    for(const auto* ch : {&a.l, &a.r})
        for(float v : *ch)
        {
            if(!std::isfinite(v))
                ++nonFinite;
            else
            {
                peak = std::max(peak, std::fabs(v));
                sum += v;
            }
        }
    // RMS of the last 5 s, to show the drone is sounding.
    double      sq = 0.0;
    std::size_t n0 = a.l.size() - 5 * AudreyVoice::kSampleRate;
    for(std::size_t i = n0; i < a.l.size(); ++i)
        sq += a.l[i] * a.l[i];
    const double rms = std::sqrt(sq / static_cast<double>(a.l.size() - n0));

    std::printf("       20 s: peak %.4f, mean %.2e, RMS of last 5 s (L) %.4f\n", peak, sum / (2.0 * a.l.size()), rms);
    std::printf("       final: feedback %.4f (%.2f dB), lowpass knob %.4f, delay mix %.4f, tab %s\n", a.fb,
                -60.0 + 72.0 * a.fb, a.lp, a.mix, tabSpec(a.tab).name);

    check(nonFinite == 0, "output is finite");
    check(peak < 1.0f, "peak below 1.0");
    check(rms > 0.001, "the instrument is sounding at the end (RMS of the last 5 s > 0.001)");
    check(a.l.size() == b.l.size() && std::memcmp(a.l.data(), b.l.data(), a.l.size() * sizeof(float)) == 0 &&
              std::memcmp(a.r.data(), b.r.data(), a.r.size() * sizeof(float)) == 0,
          "two runs give the same output, bit for bit");
    // Expected stored feedback: R2 down at the frame >= 0.5 s, up at the frame >= 3.3 s;
    // tap 0.5/72 + (hold - 0.25 s) * 24/72, with the hold quantised to 60 Hz frames.
    check(a.fb > 0.85f && a.fb < 0.87f, "feedback ended where the R2 hold put it (about 0.86)");
    check(a.lp < 1.0f && a.mix > 0.0f && a.tab == Tab::Delay, "the lowpass and delay mix moved; the tab is DELAY");
    return ctest::finish();
}
