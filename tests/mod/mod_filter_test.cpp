// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: the loop-filter cutoff slew limit (AudreyVoice::applyKnob).
//   - Regression: the exact cases that drove the vendored biquads to NaN
//     without a limit (the MOD-A report and mod_stability_test's first
//     failure) stay finite for 20 s each with the limit. Without the limit
//     (setCutoffSlewLimit(0)) at least all but two of them still go non-finite
//     within 60 s (a count, since the exact moment depends on the libm), so this
//     test would notice if the limit stopped being applied; one case fails at 0.087
//     octaves per block (the original smoother's fastest move) and passes at
//     the chosen limit.
//   - The limit per block: on the modulated path the cutoff given to the
//     engine never moves more than the limit (plus float rounding) per block,
//     including when modulation switches on (from the smoothed value) and off
//     (it converges first, then returns to the smoothed value, bit for bit).
//   - Slow modulation is not reduced: a 0.5 Hz sine reaches its full depth.

#include <cmath>
#include <string>
#include <vector>

#include "mod_test_common.h"

using namespace audreybrick;
using modtest::check;

namespace
{
struct Failing
{
    const char* name;
    ModTarget   target;
    ModShape    shape;
    double      hz;
    float       depth, knob, fb;
    bool        echo;
    float       guard = 0.0f; // a limit at which the case still fails (0: none)
};

// From the MOD-A report (FeedbackGain 0.5) and mod_stability_test (patch 1:
// feedback, echo send and repeats and volume at maximum, lowpass knob 0).
const Failing kCases[] = {
    {"highpass sine 200 Hz depth 0.3 knob 0.25", ModTarget::FeedbackHPFCutoff, ModShape::Sine, 200, 0.3f, 0.25f, 0.5f, false},
    {"highpass sine 1000 Hz depth 0.3 knob 0.5", ModTarget::FeedbackHPFCutoff, ModShape::Sine, 1000, 0.3f, 0.5f, 0.5f, false},
    {"highpass sine 200 Hz depth 0.6 knob 0", ModTarget::FeedbackHPFCutoff, ModShape::Sine, 200, 0.6f, 0.0f, 0.5f, false},
    // Stable without a limit, but fails at 0.087 octaves per block (the original
    // smoother's fastest move): the case that set the limit below that.
    {"highpass sine 100 Hz depth 0.6 knob 0", ModTarget::FeedbackHPFCutoff, ModShape::Sine, 100, 0.6f, 0.0f, 0.5f, false,
     0.087f},
    {"lowpass sine 1500 Hz depth 0.3 knob 0.25", ModTarget::FeedbackLPFCutoff, ModShape::Sine, 1500, 0.3f, 0.25f, 0.5f, false},
    {"lowpass sine 1000 Hz depth 0.6 knob 0", ModTarget::FeedbackLPFCutoff, ModShape::Sine, 1000, 0.6f, 0.0f, 0.5f, false},
    {"lowpass triangle 2000 Hz depth 0.93 knob 0, patch 1", ModTarget::FeedbackLPFCutoff, ModShape::Triangle, 2000,
     0.93f, 0.0f, 1.0f, true},
};

void setup(modtest::FixedSource& src, const Failing& c)
{
    src.knob[static_cast<int>(Knob::FeedbackGain)]                     = c.fb;
    src.knob[static_cast<int>(modTargetKnob(c.target))]                = c.knob;
    if(c.echo)
    {
        src.knob[static_cast<int>(Knob::EchoDelaySend)]     = 1.0f;
        src.knob[static_cast<int>(Knob::EchoDelayFeedback)] = 1.0f;
        src.knob[static_cast<int>(Knob::OutputVolume)]      = 1.0f;
    }
}

// Seconds until the first non-finite sample, or -1 if none in `seconds`.
double firstNonFinite(const Failing& c, float limit, double seconds)
{
    modtest::FixedSource src;
    setup(src, c);
    AudreyVoice voice;
    voice.init();
    voice.setCutoffSlewLimit(limit);
    ModControlSource mod(src);
    mod.prepare(256);
    ModSlotSettings s;
    s.shape    = c.shape;
    s.target   = c.target;
    s.rateNorm = modRateNormFromHz(c.hz);
    s.depth    = c.depth;
    mod.setSlotNow(0, s);
    const auto        in    = modtest::testInput(48000 * 2);
    const std::size_t total = static_cast<std::size_t>(seconds * 48000.0);
    float             l[256], r[256];
    for(std::size_t pos = 0; pos < total; pos += 256)
    {
        const float* ip = mod.beginChunk(in.data() + pos % (in.size() - 256), 256, nullptr, 1.0f);
        voice.process(mod, ip, l, r, 256);
        mod.endChunk();
        for(int i = 0; i < 256; ++i)
            if(!std::isfinite(l[i]) || !std::isfinite(r[i]))
                return static_cast<double>(pos + i) / 48000.0;
    }
    return -1.0;
}

void regression()
{
    int guarded = 0;
    for(const Failing& c : kCases)
    {
        const double with = firstNonFinite(c, AudreyVoice::kCutoffSlewOctavesPerBlock, 20.0);
        check(with < 0.0, std::string(c.name) + ": finite for 20 s with the limit");
        // Informational, per case: the exact trajectory (and so the moment a
        // filter blows up) depends on the libm, so only the count is asserted.
        const double without = firstNonFinite(c, c.guard, 60.0);
        std::printf("  %s, %s: %s\n", c.name,
                    c.guard > 0.0f ? ("limit " + std::to_string(c.guard)).c_str() : "no limit",
                    without >= 0.0 ? ("NaN at " + std::to_string(without) + " s").c_str() : "finite for 60 s");
        guarded += without >= 0.0 ? 1 : 0;
    }
    const int n = static_cast<int>(sizeof kCases / sizeof kCases[0]);
    check(guarded >= n - 2, "without the limit (or at 0.087 for the 100 Hz case) at least " + std::to_string(n - 2) +
                                " of the " + std::to_string(n) + " cases still go non-finite within 60 s (" +
                                std::to_string(guarded) + "), so these cases guard the limit");
}

// Steps the voice one control block at a time and records the cutoff given.
struct Stepper
{
    modtest::FixedSource src;
    AudreyVoice          voice;
    float                l[4], r[4];
    Stepper() { voice.init(); }
    float step()
    {
        voice.process(src, nullptr, l, r, 4);
        return voice.cutoffGiven(0);
    }
};

void perBlock()
{
    const float L    = AudreyVoice::kCutoffSlewOctavesPerBlock;
    const float tol  = 1e-4f; // log2 rounding
    Stepper     s;
    s.src.knob[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.5f;
    for(int b = 0; b < 12000; ++b) // let the smoother settle at knob 0.5
        s.step();
    const float settled = s.voice.cutoffGiven(0);

    // Switch on: a big jump of the offset (+0.8 = 6 octaves).
    float prev  = settled;
    bool  ok    = true;
    float worst = 0.0f;
    for(int b = 0; b < 600; ++b)
    {
        s.src.mod[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.8f;
        const float c = s.step();
        const float d = std::fabs(std::log2(c / prev));
        worst         = std::max(worst, d);
        ok            = ok && d <= L + tol && s.voice.cutoffLimiting(0);
        prev          = c;
    }
    check(ok, "switch on: the cutoff moves at most the limit per block (largest " + std::to_string(worst) +
                  " octaves, limit " + std::to_string(L) + ")");
    check(std::fabs(prev - knobToEngine(Knob::FeedbackLPFCutoff, 1.0f, false)) < 1.0f,
          "switch on: it arrives at the modulated value (knob 0.5 + 0.8 clamped to 1: 18 kHz)");

    // Switch off: offset back to exactly 0. It must not jump back; it converges
    // in steps of at most the limit, then returns to the smoothed value exactly.
    ok           = true;
    worst        = 0.0f;
    int  snapped = -1;
    for(int b = 0; b < 600; ++b)
    {
        s.src.mod[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.0f;
        const float c = s.step();
        const float d = std::fabs(std::log2(c / prev));
        worst         = std::max(worst, d);
        ok            = ok && d <= L + tol;
        if(snapped < 0 && !s.voice.cutoffLimiting(0))
            snapped = b;
        prev = c;
    }
    check(ok, "switch off: no jump back; steps of at most the limit (largest " + std::to_string(worst) + ")");
    check(snapped > 0 && modtest::sameBits(prev, settled),
          "switch off: back on the unmodulated path, bit for bit the smoothed value, after " +
              std::to_string(snapped) + " blocks");
}

void slowIsUnchanged()
{
    // A 0.5 Hz sine of depth 0.5 on the lowpass at knob 0.5 swings 3.7 octaves
    // either way; its fastest move is far below the limit, so it is not reduced.
    Stepper s;
    s.src.knob[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.5f;
    for(int b = 0; b < 12000; ++b)
        s.step();
    float lo = 1e9f, hi = 0.0f;
    for(int b = 0; b < 3 * 12000; ++b)
    {
        const double ph = 0.5 * b / 12000.0;
        s.src.mod[static_cast<int>(Knob::FeedbackLPFCutoff)] =
            static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * ph));
        const float c = s.step();
        lo            = std::min(lo, c);
        hi            = std::max(hi, c);
    }
    const float wantLo = knobToEngine(Knob::FeedbackLPFCutoff, 0.0f, false);
    const float wantHi = knobToEngine(Knob::FeedbackLPFCutoff, 1.0f, false);
    check(std::fabs(lo / wantLo - 1.0f) < 0.01f && std::fabs(hi / wantHi - 1.0f) < 0.01f,
          "slow (0.5 Hz) modulation reaches its full depth (" + std::to_string(lo) + " to " + std::to_string(hi) +
              " Hz)");
}
} // namespace

int main()
{
    regression();
    perBlock();
    slowIsUnchanged();
    return modtest::finish();
}
