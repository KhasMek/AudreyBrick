// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A part 1: post-smoothing modulation in AudreyVoice and ParamTable.
//   - engineToKnob inverts knobToEngine for every knob (both switch states).
//   - modulateEngineValue: offset 0 (and -0, NaN) returns the value bit for bit;
//     map(unmap(v) + o) equals a direct map(n + o) within float tolerance across
//     the range; clamping at both ends; never NaN or inf, for any input.
//   - The voice: offsets of exactly 0 give the same output, bit for bit, as a
//     plain source; a non-zero offset changes the output; volume modulated by
//     -1 gives exact silence; delaySwitchMod is the same as delaySwitch.
//   - The input gain law's inverse.

#include <cmath>
#include <limits>
#include <string>

#include "input/InputTypes.h"
#include "mod_test_common.h"

using namespace audreybrick;
using modtest::check;

namespace
{
const char* name(Knob k)
{
    return paramSpec(k).id;
}

// Tolerance for comparing two engine values of knob k: relative for the
// exponential (LOG) curves, otherwise a small part of the range.
bool close(Knob k, float a, float b)
{
    const ParamSpec& s = paramSpec(k);
    if(s.curve == daisysp::Mapping::LOG)
        return std::fabs(a - b) <= 2e-5f * std::fabs(b) + 1e-6f;
    return std::fabs(a - b) <= 2e-5f * (s.max - s.min) + 1e-7f;
}

void inverse()
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob k = static_cast<Knob>(i);
        for(int sw = 0; sw < 2; ++sw)
        {
            double worst = 0.0;
            bool   ok    = true;
            for(int j = 0; j <= 1000; ++j)
            {
                const float  n    = static_cast<float>(j) / 1000.0f;
                const float  v    = knobToEngine(k, n, sw != 0);
                const double back = engineToKnob(k, v, sw != 0);
                const float  v2   = knobToEngine(k, static_cast<float>(back), sw != 0);
                ok                = ok && std::isfinite(back) && close(k, v2, v);
                worst             = std::max(worst, std::fabs(static_cast<double>(v2) - v));
            }
            check(ok, std::string(name(k)) + (sw ? " (switch on)" : "") +
                          ": knobToEngine(engineToKnob(v)) == v over 1001 positions (largest difference " +
                          std::to_string(worst) + ")");
        }
    }
    // Direct values of the inverse at a few known points.
    check(std::fabs(engineToKnob(Knob::Frequency, 40.0f, false) - 24.0 / 56.0) < 1e-6,
          "Frequency: note 40 is knob 24/56");
    check(std::fabs(engineToKnob(Knob::FeedbackHPFCutoff, 250.0f, false) - std::log(25.0) / std::log(400.0)) < 1e-6,
          "FeedbackHPFCutoff: 250 Hz is log(25)/log(400)");
    check(std::fabs(engineToKnob(Knob::OutputVolume, 0.5f, false) - std::sqrt(0.5)) < 1e-6,
          "OutputVolume: 0.5 is sqrt(0.5)");
    check(std::fabs(engineToKnob(Knob::ReverbDecay, knobToEngine(Knob::ReverbDecay, 0.25f, false), false) - 0.25) <
              1e-5,
          "ReverbDecay: the ftension pre-shaping is inverted (knob 0.25)");
    check(std::fabs(engineToKnob(Knob::EchoDelayTime, 5.0f, true) - 2.0) < 1e-6,
          "EchoDelayTime: 5 s with the switch on is knob 2 (the smoother still carries the switch-off value)");
}

void modulation()
{
    const float offsets[] = {-1.0f, -0.5f, -0.1f, -0.01f, -1e-4f, 1e-4f, 0.01f, 0.1f, 0.33f, 0.5f, 1.0f};
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob k = static_cast<Knob>(i);
        for(int sw = 0; sw < 2; ++sw)
        {
            bool        zero = true, match = true, clampLo = true, clampHi = true, finite = true;
            double      worst = 0.0;
            std::string where;
            for(int j = 0; j <= 400; ++j)
            {
                const float n = static_cast<float>(j) / 400.0f;
                const float v = knobToEngine(k, n, sw != 0);
                zero          = zero && modtest::sameBits(modulateEngineValue(k, v, 0.0f, sw != 0), v) &&
                       modtest::sameBits(modulateEngineValue(k, v, -0.0f, sw != 0), v) &&
                       modtest::sameBits(modulateEngineValue(k, v, std::nanf(""), sw != 0), v);
                for(float o : offsets)
                {
                    const float  got  = modulateEngineValue(k, v, o, sw != 0);
                    const float  m    = std::min(1.0f, std::max(0.0f, n + o));
                    const float  want = knobToEngine(k, m, sw != 0);
                    if(!close(k, got, want))
                    {
                        match = false;
                        where = "n=" + std::to_string(n) + " o=" + std::to_string(o) + " got " + std::to_string(got) +
                                " want " + std::to_string(want);
                    }
                    worst = std::max(worst, std::fabs(static_cast<double>(got) - want));
                }
                clampLo = clampLo && close(k, modulateEngineValue(k, v, -2.0f, sw != 0), knobToEngine(k, 0.0f, sw != 0));
                clampHi = clampHi && close(k, modulateEngineValue(k, v, 2.0f, sw != 0), knobToEngine(k, 1.0f, sw != 0));
            }
            // Any input: values outside the range, infinities, NaN.
            const float vals[] = {-1e30f, -1.0f, 0.0f, 1e-30f, 0.5f, 1e30f, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(), std::nanf("")};
            const float offs[] = {-1e30f, -1.0f, 1e-30f, 0.5f, 1e30f, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity()};
            for(float v : vals)
                for(float o : offs)
                    finite = finite && std::isfinite(modulateEngineValue(k, v, o, sw != 0));
            const std::string tag = std::string(name(k)) + (sw ? " (switch on)" : "");
            check(zero, tag + ": offset 0, -0 and NaN return the smoothed value bit for bit");
            check(match, tag + ": map(unmap(v) + o) == map(clamp(n + o)) over 401 positions x 11 offsets (largest "
                              "difference " + std::to_string(worst) + ")" + (where.empty() ? "" : "; " + where));
            check(clampLo && clampHi, tag + ": offsets of -2 and +2 clamp to the ends of the range");
            check(finite, tag + ": no NaN or inf for out-of-range, infinite or NaN values and any offset");
        }
    }
    // EchoDelayTime with the switch just turned on: the smoother carries 5 s
    // (knob 2 in switch-on units). A small negative offset moves a little, a
    // positive one stays at 5 s (no jump down to 1.29 s).
    const float carried = 5.0f;
    check(modulateEngineValue(Knob::EchoDelayTime, carried, 0.01f, true) == carried &&
              modulateEngineValue(Knob::EchoDelayTime, carried, -0.01f, true) < carried &&
              modulateEngineValue(Knob::EchoDelayTime, carried, -0.01f, true) > 4.9f,
          "EchoDelayTime, switch just on: an offset near 0 gives a value near the carried one (no jump)");
}

using modtest::FixedSource;

std::vector<float> render(FixedSource& src, std::size_t frames)
{
    AudreyVoice voice;
    voice.init();
    std::vector<float> l(frames), r(frames), both(2 * frames);
    for(std::size_t pos = 0; pos < frames; pos += 256)
    {
        const std::size_t n = std::min<std::size_t>(256, frames - pos);
        voice.process(src, nullptr, l.data() + pos, r.data() + pos, n);
    }
    for(std::size_t i = 0; i < frames; ++i)
    {
        both[2 * i]     = l[i];
        both[2 * i + 1] = r[i];
    }
    return both;
}

bool identical(const std::vector<float>& a, const std::vector<float>& b)
{
    if(a.size() != b.size())
        return false;
    for(std::size_t i = 0; i < a.size(); ++i)
        if(!modtest::sameBits(a[i], b[i]))
            return false;
    return true;
}

void voice()
{
    const std::size_t frames = 48000 * 3;
    // A singing patch: feedback high, lowpass mid.
    auto patch = [](FixedSource& s) {
        s.knob[static_cast<int>(Knob::FeedbackGain)]      = 0.92f;
        s.knob[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.6f;
        s.knob[static_cast<int>(Knob::ReverbMix)]         = 0.3f;
        s.knob[static_cast<int>(Knob::EchoDelaySend)]     = 0.4f;
    };
    FixedSource a, b;
    patch(a);
    patch(b);
    for(float& m : b.mod)
        m = -0.0f; // -0 is "not modulated" too
    const auto ra = render(a, frames);
    const auto rb = render(b, frames);
    float      peak = 0.0f;
    for(float x : ra)
        peak = std::max(peak, std::fabs(x));
    check(identical(ra, rb) && peak > 0.01f,
          "voice: offsets of -0 on every knob give the unmodulated output bit for bit (peak " + std::to_string(peak) +
              ")");

    FixedSource c;
    patch(c);
    c.mod[static_cast<int>(Knob::FeedbackLPFCutoff)] = 0.1f;
    check(!identical(ra, render(c, frames)), "voice: a lowpass offset of 0.1 changes the output");

    // Volume at its default (0.707) moved by -1: clamped to knob 0, level 0.
    FixedSource d;
    patch(d);
    d.mod[static_cast<int>(Knob::OutputVolume)] = -1.0f;
    const auto rd     = render(d, frames);
    float      tail   = 0.0f;
    for(std::size_t i = 2 * 48000; i < rd.size(); ++i)
        tail = std::max(tail, std::fabs(rd[i]));
    check(tail == 0.0f, "voice: OutputVolume offset -1 gives exact silence (engine level 0) once applied");

    FixedSource e, f;
    patch(e);
    patch(f);
    e.knob[static_cast<int>(Knob::EchoDelayTime)] = 0.8f;
    f.knob[static_cast<int>(Knob::EchoDelayTime)] = 0.8f;
    e.sw    = true;
    f.swMod = true;
    check(identical(render(e, frames), render(f, frames)), "voice: delaySwitchMod acts exactly as delaySwitch");
    FixedSource g;
    patch(g);
    g.knob[static_cast<int>(Knob::EchoDelayTime)] = 0.8f;
    g.sw    = true;
    g.swMod = true;
    check(identical(render(e, frames), render(g, frames)), "voice: switch on OR modulated on is still on");
}

void inputLaw()
{
    bool ok = true;
    for(int j = 0; j <= 1000; ++j)
    {
        const float n = static_cast<float>(j) / 1000.0f;
        ok            = ok && std::fabs(inputNormFromGain(inputGainFromNorm(n)) - n) < 1e-6f;
    }
    check(ok && inputNormFromGain(0.0f) == 0.0f && inputNormFromGain(-1.0f) == 0.0f &&
              inputNormFromGain(100.0f) == 1.0f && inputNormFromGain(std::nanf("")) == 0.0f,
          "inputNormFromGain inverts inputGainFromNorm and clamps (0, negative, NaN -> 0; above 4 -> 1)");
}
} // namespace

int main()
{
    inverse();
    modulation();
    voice();
    inputLaw();
    return modtest::finish();
}
