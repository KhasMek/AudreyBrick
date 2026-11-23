// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A cost measurement (not a ctest; run it from a Release build):
//
//   build/host/tests/mod/mod_bench [seconds-per-run] [runs]
//
// Renders through AudioRender in 256-frame callbacks (the device's buffer) with
// a singing patch, in four configurations, interleaved, `runs` times each
// (default 7 runs of 30 s of audio), and prints the median time per callback
// and the increase over "decorator, 0 slots":
//   plain      AudioRender without the decorator (the app before MOD-A)
//   0 slots    the decorator attached, every slot off (the app after MOD-A)
//   1 slot     an audio-rate (1 kHz) sine on the frequency
//   4 slots    audio-rate sines (1 kHz, 1.3 kHz, 700 Hz, 1.7 kHz) on the
//              frequency, the lowpass, the echo time and the feedback
// The callback load on the Brick is about a third of a core at its default CPU
// speed (docs/PLATFORM_NOTES.md section 12), so the brief asks for anything
// above 10 % extra to be reported as a concern.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "mod_test_common.h"
#include "platform/AudioRender.h"

using namespace audreybrick;

namespace
{
ModSlotSettings sine(ModTarget t, double hz)
{
    ModSlotSettings s;
    s.shape    = ModShape::Sine;
    s.target   = t;
    s.rateNorm = modRateNormFromHz(hz);
    s.depth    = 0.2f;
    return s;
}

double run(int config, double seconds)
{
    modtest::FixedSource src;
    src.knob[static_cast<int>(Knob::FeedbackGain)]      = 0.93f;
    src.knob[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.6f;
    src.knob[static_cast<int>(Knob::ReverbMix)]         = 0.4f;
    src.knob[static_cast<int>(Knob::EchoDelaySend)]     = 0.5f;
    src.knob[static_cast<int>(Knob::EchoDelayFeedback)] = 0.6f;
    AudreyVoice voice;
    voice.init();
    ModControlSource mod(src);
    AudioRender      render(voice, src, AudioRender::FlushToZero::Enable);
    if(config > 0)
        render.attachModulation(&mod);
    render.prepare(256);
    if(config >= 2)
        mod.setSlotNow(0, sine(ModTarget::Frequency, 1000.0));
    if(config >= 3)
    {
        mod.setSlotNow(1, sine(ModTarget::FeedbackLPFCutoff, 1300.0));
        mod.setSlotNow(2, sine(ModTarget::EchoDelayTime, 700.0));
        mod.setSlotNow(3, sine(ModTarget::FeedbackGain, 1700.0));
    }
    std::vector<float> out(512);
    const std::size_t  callbacks = static_cast<std::size_t>(seconds * 48000.0 / 256.0);
    // One second to settle (and to fill the caches), not timed.
    for(int c = 0; c < 188; ++c)
        render.render(out.data(), 256);
    const auto t0 = std::chrono::steady_clock::now();
    for(std::size_t c = 0; c < callbacks; ++c)
        render.render(out.data(), 256);
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return 1e6 * t / static_cast<double>(callbacks); // microseconds per callback
}
} // namespace

int main(int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 30.0;
    const int    runs    = argc > 2 ? std::atoi(argv[2]) : 7;
    const char*  names[] = {"plain (no decorator)", "decorator, 0 slots", "1 slot (1 kHz sine on frequency)",
                            "4 slots (audio-rate sines: frequency, lowpass, echo time, feedback)"};
    std::vector<double> t[4];
    for(int r = 0; r < runs; ++r)
        for(int c = 0; c < 4; ++c)
            t[c].push_back(run(c, seconds));
    double med[4];
    for(int c = 0; c < 4; ++c)
    {
        std::sort(t[c].begin(), t[c].end());
        med[c] = t[c][t[c].size() / 2];
    }
    const double buffer = 1e6 * 256.0 / 48000.0;
    std::printf("mod_bench: %d runs of %.0f s each, 256-frame callbacks (%.1f us of audio each), median per callback:\n",
                runs, seconds, buffer);
    for(int c = 0; c < 4; ++c)
        std::printf("  %-70s %7.2f us  (%5.2f %% of the buffer)  %+6.2f %% vs 0 slots  %+6.2f %% vs plain\n", names[c],
                    med[c], 100.0 * med[c] / buffer, 100.0 * (med[c] / med[1] - 1.0), 100.0 * (med[c] / med[0] - 1.0));
    return 0;
}
