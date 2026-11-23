// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: stability sweep, three times with different seeds. Five minutes of audio through AudreyVoice and the
// modulation decorator, with an input (noise and sine bursts), at extreme knob
// settings (a new patch every 30 s: everything at maximum, feedback and echo
// repeats at maximum with the lowpass closed, everything at minimum, feedback
// maximum with the shortest echo, and random knobs), while one slot gets new
// random settings (every shape, target, rate, depth and, MOD-C, polarity: the
// four ramps included) every 100 to 500 ms. MOD-C: the envelope followers read
// a separate pre-gain record (the input times 4, as a stage at a low gain would
// give), not the voice's input.
//   - Every output sample is finite, and |x| <= kOutputBound (1.75). Not 1.0:
//     the original's output stage (daisysp Limiter: pre-gain 0.7, a peak
//     follower with a 0.05-per-sample attack, then SoftLimit) lets a sudden
//     step through for a few samples. The follower bounds |pre * gain| by
//     1 / 0.05 = 20, so its input to SoftLimit is at most 0.7 * 20 = 14, and
//     SoftLimit(14) = 14 (27 + 196) / (27 + 1764) = 1.7434 is the largest
//     value it can ever give (it rises monotonically). Square and sample-and-
//     hold steps reach it briefly. The peak is printed.
//   - The modulators' own state never holds a denormal (follower levels and
//     weights are 0 or normal floats), and the 90th percentile of the 10 s
//     windows' times is at most twice the median (a slowdown that builds up
//     trips it; one preemption on a loaded machine does not). The real-time
//     factor is printed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "mod_test_common.h"

using namespace audreybrick;
using modtest::check;

namespace
{
struct Rng
{
    std::uint32_t x = 0x1234567u;
    std::uint32_t next()
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }
    float uniform() { return static_cast<float>(next() >> 8) / 16777216.0f; } // 0..1
};

void patch(modtest::FixedSource& s, int which, Rng& rng)
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
        s.knob[i] = which == 0 ? 1.0f : (which == 2 ? 0.0f : paramSpec(static_cast<Knob>(i)).defaultNorm);
    s.sw = false;
    switch(which)
    {
        case 1:
            s.knob[static_cast<int>(Knob::FeedbackGain)]      = 1.0f;
            s.knob[static_cast<int>(Knob::EchoDelayFeedback)] = 1.0f;
            s.knob[static_cast<int>(Knob::EchoDelaySend)]     = 1.0f;
            s.knob[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.0f;
            s.knob[static_cast<int>(Knob::OutputVolume)]      = 1.0f;
            break;
        case 3:
            s.knob[static_cast<int>(Knob::FeedbackGain)]      = 1.0f;
            s.knob[static_cast<int>(Knob::EchoDelayTime)]     = 0.0f;
            s.knob[static_cast<int>(Knob::EchoDelayFeedback)] = 1.0f;
            s.knob[static_cast<int>(Knob::EchoDelaySend)]     = 1.0f;
            s.knob[static_cast<int>(Knob::ReverbDecay)]       = 1.0f;
            s.knob[static_cast<int>(Knob::ReverbMix)]         = 1.0f;
            s.knob[static_cast<int>(Knob::OutputVolume)]      = 1.0f;
            s.sw                                              = true;
            break;
        case 4:
            for(std::size_t i = 0; i < kKnobCount; ++i)
                s.knob[i] = rng.uniform() < 0.5f ? (rng.uniform() < 0.5f ? 0.0f : 1.0f) : rng.uniform();
            s.sw = rng.uniform() < 0.5f;
            break;
        default: break;
    }
}

// The largest |output| the original's output stage can give (see the header).
constexpr float kOutputBound = 1.75f;

bool normalOrZero(float v)
{
    return v == 0.0f || std::fabs(v) >= 1.17549435e-38f;
}
} // namespace

void sweep(std::uint32_t seed)
{
    constexpr std::size_t kChunk   = 256;
    constexpr double      kSeconds = 300.0;
    const std::size_t     total    = static_cast<std::size_t>(kSeconds * 48000.0);
    const auto            in       = modtest::testInput(48000 * 2); // looped

    modtest::FixedSource src;
    AudreyVoice          voice;
    voice.init();
    ModControlSource mod(src);
    mod.prepare(kChunk);
    Rng rng;
    rng.x = seed;

    // All four slots start somewhere.
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        ModSlotSettings s;
        s.shape    = static_cast<ModShape>(i % kModShapeCount);
        s.target   = static_cast<ModTarget>(1 + (3 * i) % (kModTargetCount - 1));
        s.rateNorm = rng.uniform();
        s.depth    = 2.0f * rng.uniform() - 1.0f;
        s.polarity = (i % 2) != 0 ? ModPolarity::Unipolar : ModPolarity::Bipolar; // MOD-C
        mod.setSlotNow(i, s);
    }

    std::vector<float> l(kChunk), r(kChunk), chunkIn(kChunk), chunkPre(kChunk);
    std::size_t        shapesSeen[kModShapeCount] = {}, unipolarSeen = 0;
    float              peak = 0.0f;
    std::size_t        nonFinite = 0, changes = 0;
    bool               stateOk   = true;
    std::size_t        nextChange = 0, nextPatch = 0;
    int                patchNo    = 0;
    std::vector<double> windows;
    const std::size_t   windowFrames = 48000 * 10;
    auto                windowStart  = std::chrono::steady_clock::now();
    const auto          t0           = windowStart;

    for(std::size_t pos = 0; pos < total; pos += kChunk)
    {
        if(pos >= nextPatch)
        {
            patch(src, patchNo % 5, rng);
            ++patchNo;
            nextPatch += 48000 * 30;
        }
        if(pos >= nextChange)
        {
            ModSlotSettings s;
            s.shape    = static_cast<ModShape>(rng.next() % kModShapeCount);
            s.target   = static_cast<ModTarget>(rng.next() % kModTargetCount);
            s.rateNorm = rng.uniform() < 0.2f ? 1.0f : rng.uniform();
            s.depth    = rng.uniform() < 0.2f ? (rng.uniform() < 0.5f ? -1.0f : 1.0f) : 2.0f * rng.uniform() - 1.0f;
            s.polarity = rng.uniform() < 0.5f ? ModPolarity::Unipolar : ModPolarity::Bipolar; // MOD-C
            ++shapesSeen[static_cast<std::size_t>(s.shape)];
            unipolarSeen += s.polarity == ModPolarity::Unipolar;
            mod.setModSlot(rng.next() % kModSlots, s);
            ++changes;
            nextChange += static_cast<std::size_t>(48000.0 * (0.1 + 0.4 * rng.uniform()));
        }
        for(std::size_t i = 0; i < kChunk; ++i)
        {
            chunkIn[i]  = in[(pos + i) % in.size()];
            chunkPre[i] = 4.0f * chunkIn[i];
        }
        const float* ip = mod.beginChunk(chunkIn.data(), kChunk, nullptr, 1.0f, chunkPre.data());
        voice.process(mod, ip, l.data(), r.data(), kChunk);
        mod.endChunk();
        for(std::size_t i = 0; i < kChunk; ++i)
        {
            if(!std::isfinite(l[i]) || !std::isfinite(r[i]))
            {
                if(nonFinite == 0)
                {
                    std::printf("first non-finite sample at %.4f s (patch %d)\n", (pos + i) / 48000.0, (patchNo - 1) % 5);
                    for(std::size_t q = 0; q < kModSlots; ++q)
                    {
                        const ModSlotSettings& st = mod.matrix().slot(q);
                        std::printf("  slot %zu: %s -> %s rate %.3f depth %.3f value %f\n", q + 1, modShapeName(st.shape),
                                    modTargetName(st.target), st.rateNorm, st.depth, mod.matrix().slotValue(q));
                    }
                    for(std::size_t t = 0; t < kModTargetCount; ++t)
                        std::printf("  offset %s %f\n", modTargetName(static_cast<ModTarget>(t)), mod.matrix().offset(static_cast<ModTarget>(t)));
                    for(std::size_t k = 0; k < kKnobCount; ++k)
                        std::printf("  knob %s %f\n", paramSpec(static_cast<Knob>(k)).id, src.knob[k]);
                }
                ++nonFinite;
            }
            else
                peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
        }
        const ModMatrix& m = mod.matrix();
        for(std::size_t s = 0; s < kModSlots; ++s)
        {
            stateOk = stateOk && normalOrZero(m.envelope(s)) && std::isfinite(m.slotValue(s)) &&
                      normalOrZero(m.polarityBlend(s));
            for(std::size_t t = 0; t < kModTargetCount; ++t)
                stateOk = stateOk && normalOrZero(m.weight(s, static_cast<ModTarget>(t)));
        }
        if((pos + kChunk) % windowFrames < kChunk)
        {
            const auto now = std::chrono::steady_clock::now();
            windows.push_back(std::chrono::duration<double>(now - windowStart).count());
            windowStart = now;
        }
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    const std::string tag = "seed " + std::to_string(seed) + ": ";
    check(nonFinite == 0, tag + "5 minutes, " + std::to_string(changes) + " random slot changes, " +
                              std::to_string(patchNo) + " extreme patches: every sample finite");
    check(peak <= kOutputBound, tag + "output within the output stage's bound " + std::to_string(kOutputBound) +
                                    " (peak " + std::to_string(peak) + ")");
    std::printf("seed %u: output peak %.4f\n", seed, static_cast<double>(peak));
    check(stateOk, tag + "the modulators' state never holds a denormal");
    bool        everyShape = true;
    std::string seen;
    for(std::size_t k = 0; k < kModShapeCount; ++k)
    {
        everyShape = everyShape && shapesSeen[k] > 0;
        seen += std::string(k == 0 ? "" : ", ") + modShapeName(static_cast<ModShape>(k)) + " " + std::to_string(shapesSeen[k]);
    }
    check(everyShape && unipolarSeen > 0 && unipolarSeen < changes,
          tag + "every shape and both polarities were used (" + seen + "; unipolar " + std::to_string(unipolarSeen) +
              " of " + std::to_string(changes) + ")");
    std::vector<double> sorted = windows;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
    const double p90    = sorted.empty() ? 0.0 : sorted[(sorted.size() * 9) / 10];
    check(!sorted.empty() && p90 <= 2.0 * median,
          tag + "the 90th percentile of the 10 s windows is within 2x the median (median " + std::to_string(median) +
              " s, 90th percentile " + std::to_string(p90) + " s, slowest " + std::to_string(sorted.back()) + " s)");
    std::printf("seed %u: rendered %.0f s of audio in %.2f s (%.1fx real time)\n", seed, kSeconds, elapsed,
                elapsed > 0.0 ? kSeconds / elapsed : 0.0);
}

int main()
{
    // Three seeds: three different sequences of slot settings and random patches.
    for(std::uint32_t seed : {0x1234567u, 0xBEEFu, 0xC0FFEEu})
        sweep(seed);
    return modtest::finish();
}
