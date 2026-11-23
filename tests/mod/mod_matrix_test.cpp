// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A part 2: the modulators (ModMatrix, ModTypes.h).
//   - The rate and release mappings at their ends and the inverse.
//   - The shapes at known phases; the phase accumulator over 20 minutes at
//     0.01 Hz; a rate change continues from the current phase.
//   - Sample-and-hold: reproducible after reset(), a new value at each cycle
//     start only, values in -1..1, different per slot.
//   - Depth smoothing (10 ms), the target cross-fade, the snap to exactly 0.
//   - The envelope follower: 2 ms attack, the release range, decay to exactly 0.
//   - Several slots on one target add; the DelaySwitch request; sanitising.
//   - MOD-C: the four ramps (values at known phases, exact end points, the
//     exponential's k, monotonic, a rate change keeps the phase); the polarity
//     (the mapping, its 10 ms blend on a change, the snap, the envelope
//     ignoring it, a slot without a target taking it at once); every shape's
//     and polarity's name.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>

#include "mod/ModMatrix.h"
#include "mod_test_common.h"

using namespace audreybrick;
using modtest::check;

namespace
{
ModSlotSettings slot(ModShape shape, ModTarget target, float rate, float depth)
{
    ModSlotSettings s;
    s.shape    = shape;
    s.target   = target;
    s.rateNorm = rate;
    s.depth    = depth;
    return s;
}

void rates()
{
    check(std::fabs(modRateHz(0.0f) - 0.01) < 1e-12 && std::fabs(modRateHz(1.0f) - 2000.0) < 1e-9,
          "rate: rateNorm 0 is 0.01 Hz, 1 is 2000 Hz");
    check(std::fabs(modRateHz(0.5f) - std::sqrt(0.01 * 2000.0)) < 1e-9, "rate: exponential (0.5 is the geometric mean)");
    check(std::fabs(modReleaseSeconds(0.0f) - 5.0) < 1e-12 && std::fabs(modReleaseSeconds(1.0f) - 0.005) < 1e-12,
          "release: rateNorm 0 is 5 s, 1 is 5 ms");
    bool ok = true;
    for(int j = 0; j <= 100; ++j)
    {
        const float r = static_cast<float>(j) / 100.0f;
        ok            = ok && std::fabs(modRateNormFromHz(modRateHz(r)) - r) < 1e-5f;
    }
    check(ok && modRateNormFromHz(0.0) == 0.0f && modRateNormFromHz(1e9) == 1.0f,
          "modRateNormFromHz inverts modRateHz and clamps");
    check(modRateHz(std::nanf("")) == modRateHz(0.0f), "rate: NaN counts as 0");
}

void shapes()
{
    // 1000 Hz: 12 blocks per cycle, so the phases are exact multiples of 1/12.
    const float r = modRateNormFromHz(1000.0);
    ModMatrix   m;
    m.setSlot(0, slot(ModShape::Sine, ModTarget::ReverbMix, r, 1.0f));
    m.setSlot(1, slot(ModShape::Triangle, ModTarget::ReverbMix, r, 1.0f));
    m.setSlot(2, slot(ModShape::Square, ModTarget::ReverbMix, r, 1.0f));
    bool   sine = true, tri = true, sq = true;
    double inc  = modRateHz(r) / 12000.0;
    for(int b = 0; b < 48; ++b)
    {
        const double p = std::fmod(b * inc, 1.0);
        m.tick();
        sine = sine && std::fabs(m.slotValue(0) - std::sin(2.0 * 3.14159265358979323846 * p)) < 1e-5;
        const double t = p < 0.25 ? 4 * p : (p < 0.75 ? 2 - 4 * p : 4 * p - 4);
        tri            = tri && std::fabs(m.slotValue(1) - t) < 1e-5;
        sq             = sq && m.slotValue(2) == (p < 0.5 ? 1.0f : -1.0f);
    }
    check(sine, "sine: sin(2 pi phase), starting at 0 rising");
    check(tri, "triangle: 0 at phase 0, +1 at 0.25, -1 at 0.75");
    check(sq, "square: +1 for the first half cycle, -1 for the second");

    // Bounds of every LFO shape over many rates (MOD-C: the ramps too, and
    // unipolar within 0..1).
    bool bounded = true;
    for(int pol = 0; pol < 2; ++pol)
        for(int first = 0; first < 8; first += 4)
        {
            ModMatrix m2;
            for(int s = 0; s < 4; ++s)
            {
                ModSlotSettings st = slot(static_cast<ModShape>(first + s), ModTarget::FeedbackGain, 0.9f, 1.0f);
                st.polarity        = pol == 0 ? ModPolarity::Bipolar : ModPolarity::Unipolar;
                m2.setSlot(static_cast<std::size_t>(s), st);
            }
            const float lo = pol == 0 ? -1.0f : 0.0f;
            for(int b = 0; b < 120000; ++b)
            {
                m2.tick();
                for(std::size_t s = 0; s < 4; ++s)
                    bounded = bounded && m2.slotValue(s) >= lo && m2.slotValue(s) <= 1.0f;
            }
        }
    check(bounded, "every LFO shape stays within -1..+1, unipolar within 0..1 (10 s at 271 Hz)");
}

void phaseAccuracy()
{
    // 0.01 Hz for 20 minutes of control blocks: the accumulated phase against
    // the exact count of cycles.
    ModMatrix m;
    m.setSlot(0, slot(ModShape::Sine, ModTarget::None, 0.0f, 0.0f));
    const std::int64_t blocks = 12000LL * 60 * 20;
    for(std::int64_t b = 0; b < blocks; ++b)
        m.tick();
    const double exact = std::fmod(static_cast<double>(blocks) * 0.01 / 12000.0, 1.0);
    const double err   = std::fabs(m.phase(0) - exact);
    check(err < 1e-9, "0.01 Hz over 20 minutes: phase error " + std::to_string(err) + " cycles (below 1e-9)");

    // A rate change continues from the current phase.
    ModMatrix a;
    a.setSlot(0, slot(ModShape::Sine, ModTarget::None, 0.6f, 0.0f));
    for(int b = 0; b < 777; ++b)
        a.tick();
    const double before = a.phase(0);
    a.setSlot(0, slot(ModShape::Triangle, ModTarget::None, 0.9f, 0.0f)); // rate and shape change
    check(a.phase(0) == before, "a rate or shape change keeps the phase (free-running)");
    a.tick();
    const double inc = modRateHz(0.9f) / 12000.0;
    check(std::fabs(a.phase(0) - std::fmod(before + inc, 1.0)) < 1e-12, "the next block advances by the new rate");
}

void sampleHold()
{
    const float r = modRateNormFromHz(100.0); // 120 blocks per cycle
    auto        run = [&](std::vector<float>& values, std::size_t slotIndex) {
        ModMatrix m;
        m.setSlot(slotIndex, slot(ModShape::SampleHold, ModTarget::ReverbMix, r, 1.0f));
        for(int b = 0; b < 12000; ++b)
        {
            m.tick();
            values.push_back(m.slotValue(slotIndex));
        }
    };
    std::vector<float> a, b, c;
    run(a, 0);
    run(b, 0);
    run(c, 1);
    check(a == b, "sample-and-hold: the same values after a fresh start (seeded per slot)");
    check(a != c, "sample-and-hold: another slot has its own sequence");
    std::size_t changes = 0;
    bool        inRange = true;
    for(std::size_t i = 1; i < a.size(); ++i)
    {
        changes += a[i] != a[i - 1] ? 1 : 0;
        inRange = inRange && a[i] >= -1.0f && a[i] < 1.0f;
    }
    // 100 Hz for 1 s: 100 cycle starts; a step can repeat a value only by chance.
    check(changes >= 98 && changes <= 100 && inRange,
          "sample-and-hold: a new value at each of the 100 cycle starts in 1 s (" + std::to_string(changes) +
              " changes), all in -1..1");
    std::set<float> distinct(a.begin(), a.end());
    check(distinct.size() >= 95, "sample-and-hold: the values differ (" + std::to_string(distinct.size()) + " distinct)");
}

void smoothing()
{
    // Square at 0.01 Hz: +1 for 50 s, so the offset is the weight.
    ModMatrix m;
    m.setSlot(0, slot(ModShape::Square, ModTarget::FeedbackLPFCutoff, 0.0f, 1.0f));
    m.tick();
    const float a     = static_cast<float>(1.0 - std::exp(-1.0 / 120.0));
    const float first = m.offset(ModTarget::FeedbackLPFCutoff);
    check(std::fabs(first - a) < 1e-7f, "depth smoothing: the first block moves by the one-pole coefficient (" +
                                            std::to_string(first) + ")");
    for(int b = 1; b < 120; ++b)
        m.tick();
    const float tenMs = m.offset(ModTarget::FeedbackLPFCutoff);
    check(std::fabs(tenMs - (1.0f - std::exp(-1.0f))) < 0.01f,
          "depth smoothing: 63 % after 10 ms (" + std::to_string(tenMs) + ")");

    // Target change: cross-fade.
    for(int b = 0; b < 2400; ++b)
        m.tick();
    m.setSlot(0, slot(ModShape::Square, ModTarget::FeedbackHPFCutoff, 0.0f, 1.0f));
    bool  crossfade = true;
    float prevOld   = m.offset(ModTarget::FeedbackLPFCutoff);
    float prevNew   = 0.0f;
    float maxStep   = 0.0f;
    int   snapAt    = -1;
    for(int b = 0; b < 12000; ++b)
    {
        m.tick();
        const float o = m.offset(ModTarget::FeedbackLPFCutoff);
        const float n = m.offset(ModTarget::FeedbackHPFCutoff);
        crossfade     = crossfade && o <= prevOld && n >= prevNew;
        maxStep       = std::max(maxStep, std::max(prevOld - o, n - prevNew));
        if(snapAt < 0 && o == 0.0f)
            snapAt = b;
        prevOld = o;
        prevNew = n;
    }
    check(crossfade, "target change: the old target falls and the new one rises, at the same time");
    check(maxStep <= a + 1e-6f, "target change: no step larger than the one-pole coefficient (" +
                                    std::to_string(maxStep) + ")");
    check(snapAt > 0 && m.weight(0, ModTarget::FeedbackLPFCutoff) == 0.0f,
          "target change: the old weight snaps to exactly 0 (after " + std::to_string(snapAt) + " blocks, " +
              std::to_string(snapAt / 12.0) + " ms)");

    // Depth to 0: back to exactly 0 everywhere.
    m.setSlot(0, slot(ModShape::Square, ModTarget::FeedbackHPFCutoff, 0.0f, 0.0f));
    for(int b = 0; b < 12000; ++b)
        m.tick();
    bool allZero = true;
    for(std::size_t t = 0; t < kModTargetCount; ++t)
        allZero = allZero && m.offset(static_cast<ModTarget>(t)) == 0.0f && m.weight(0, static_cast<ModTarget>(t)) == 0.0f;
    check(allZero, "depth 0: every weight and every offset is exactly 0 again");
}

void envelope()
{
    ModMatrix m;
    m.setSlot(0, slot(ModShape::Envelope, ModTarget::FeedbackLPFCutoff, 1.0f, 1.0f)); // release 5 ms
    m.setSlot(1, slot(ModShape::Envelope, ModTarget::None, 0.0f, 0.0f));               // release 5 s
    for(int i = 0; i < 96; ++i) // 2 ms of a constant 0.5
        m.feed(0.5f);
    check(std::fabs(m.envelope(0) - 0.5f * (1.0f - std::exp(-1.0f))) < 0.002f,
          "envelope attack: 63 % of the level after 2 ms (" + std::to_string(m.envelope(0)) + ")");
    for(int i = 0; i < 48000; ++i)
        m.feed(i % 2 ? 0.5f : -0.5f); // |x| is what counts
    check(std::fabs(m.envelope(0) - 0.5f) < 1e-4f, "envelope: follows |x| (a +-0.5 square settles at 0.5)");
    for(int i = 0; i < 240; ++i) // 5 ms of silence
        m.feed(0.0f);
    check(std::fabs(m.envelope(0) - 0.5f * std::exp(-1.0f)) < 0.002f,
          "envelope release at rate 1: 37 % after 5 ms (" + std::to_string(m.envelope(0)) + ")");
    check(std::fabs(m.envelope(1) - 0.5f * std::exp(-0.005f / 5.0f)) < 0.002f,
          "envelope release at rate 0: about 5 s time constant (" + std::to_string(m.envelope(1)) + ")");
    for(int i = 0; i < 48000; ++i)
        m.feed(0.0f);
    check(m.envelope(0) == 0.0f, "envelope: decays to exactly 0 (no denormals)");
    m.feed(4.0f);
    for(int i = 0; i < 4800; ++i)
        m.feed(4.0f);
    m.tick();
    check(m.slotValue(0) == 1.0f, "envelope value is clamped at 1");
    m.feed(std::numeric_limits<float>::infinity());
    m.feed(std::nanf(""));
    check(std::isfinite(m.envelope(0)), "envelope: inf and NaN input count as 0");
}

void sums()
{
    // Four squares at 0.01 Hz (value +1), depth 0.5 each, on one target: the
    // offset approaches 2 (the voice clamps).
    ModMatrix m;
    for(std::size_t s = 0; s < kModSlots; ++s)
        m.setSlot(s, slot(ModShape::Square, ModTarget::EchoDelayFeedback, 0.0f, 0.5f));
    for(int b = 0; b < 12000; ++b)
        m.tick();
    check(std::fabs(m.offset(ModTarget::EchoDelayFeedback) - 2.0f) < 1e-4f,
          "four slots on one target add (" + std::to_string(m.offset(ModTarget::EchoDelayFeedback)) + ")");
    check(m.offset(ModTarget::ReverbMix) == 0.0f && m.offset(ModTarget::None) == 0.0f,
          "other targets and None stay exactly 0");

    // DelaySwitch: square at 2 Hz, depth 1: on for the first half of each cycle
    // once the weight has risen.
    ModMatrix d;
    d.setSlot(0, slot(ModShape::Square, ModTarget::DelaySwitch, modRateNormFromHz(2.0), 1.0f));
    int on = 0, flips = 0;
    bool prev = false;
    for(int b = 0; b < 12000; ++b)
    {
        d.tick();
        on += d.switchRequest() ? 1 : 0;
        flips += d.switchRequest() != prev ? 1 : 0;
        prev = d.switchRequest();
    }
    check(on >= 5990 && on <= 6010 && flips == 4,
          "DelaySwitch: a 2 Hz square requests the switch half the time (" + std::to_string(on) + " of 12000 blocks, " +
              std::to_string(flips) + " changes)");

    // Negative depth inverts.
    ModMatrix n;
    n.setSlot(0, slot(ModShape::Square, ModTarget::OutputVolume, 0.0f, -1.0f));
    for(int b = 0; b < 12000; ++b)
        n.tick();
    check(n.offset(ModTarget::OutputVolume) < -0.999f, "negative depth inverts the shape");
}

void sanitise()
{
    ModSlotSettings bad;
    bad.shape    = static_cast<ModShape>(200);
    bad.target   = static_cast<ModTarget>(200);
    bad.rateNorm = std::nanf("");
    bad.depth    = std::nanf("");
    const ModSlotSettings s = sanitizeModSlot(bad);
    check(s.shape == ModShape::Sine && s.target == ModTarget::None && s.rateNorm == 0.0f && s.depth == 0.0f,
          "sanitize: unknown shape and target, NaN rate and depth become defaults");
    bad.rateNorm = 5.0f;
    bad.depth    = -5.0f;
    const ModSlotSettings t = sanitizeModSlot(bad);
    check(t.rateNorm == 1.0f && t.depth == -1.0f, "sanitize: rate and depth are clamped");
    bad.depth = 1e-30f;
    check(sanitizeModSlot(bad).depth == 0.0f, "sanitize: a denormal-sized depth is 0");

    ModShape  sh;
    ModTarget tg;
    check(modShapeFromName("SampleHold", sh) && sh == ModShape::SampleHold && modShapeFromName("env", sh) &&
              sh == ModShape::Envelope && !modShapeFromName("saw", sh),
          "shape names");
    check(modTargetFromName("feedbacklpfcutoff", tg) && tg == ModTarget::FeedbackLPFCutoff &&
              modTargetFromName("InputGain", tg) && tg == ModTarget::InputGain && modTargetFromName("switch", tg) &&
              tg == ModTarget::DelaySwitch && modTargetFromName("NONE", tg) && tg == ModTarget::None &&
              !modTargetFromName("pitch", tg),
          "target names");
    bool names = true;
    for(std::size_t t2 = 0; t2 < kModTargetCount; ++t2)
        names = names && modTargetFromName(modTargetName(static_cast<ModTarget>(t2)), tg) &&
                tg == static_cast<ModTarget>(t2);
    check(names, "every target's name parses back to it");
}
// MOD-C: the ramps.
void ramps()
{
    // The exponential: exact end points, k = 5, monotonic, mirror.
    const double e = std::exp(-5.0);
    bool         mono = true, mirror = true, formula = true;
    double       prev = 2.0;
    for(int j = 0; j <= 1000; ++j)
    {
        const double u = j / 1000.0;
        const double d = modExpDown(u);
        mono           = mono && d <= prev && d >= -1.0 && d <= 1.0;
        prev           = d;
        mirror         = mirror && modExpUp(u) == modExpDown(1.0 - u);
        if(j > 0 && j < 1000)
            formula = formula && std::fabs(d - (2.0 * (std::exp(-5.0 * u) - e) / (1.0 - e) - 1.0)) < 1e-12;
    }
    check(modExpDown(0.0) == 1.0 && modExpDown(1.0) == -1.0 && modExpUp(0.0) == -1.0 && modExpUp(1.0) == 1.0,
          "exp ramps: end points exactly +1 / -1 (down) and -1 / +1 (up)");
    check(mono && formula && mirror,
          "exp down: 2 (exp(-5u) - exp(-5)) / (1 - exp(-5)) - 1, falling monotonically; exp up its mirror in time");
    // Fast then slow: half the travel is covered within the first 14 % of the
    // cycle (0 is crossed at u = ln(2 / (1 + exp(-5))) / 5 = 0.137).
    check(modExpDown(0.14) < 0.0 && modExpDown(0.13) > 0.0 && modExpUp(0.86) < 0.0 && modExpUp(0.87) > 0.0 &&
              modExpDown(0.5) < -0.84 && modExpUp(0.5) == modExpDown(0.5),
          "exp down: falls fast then ever more slowly (0 crossed at u = 0.137, -0.848 at u = 0.5); exp up: "
          "slow start, fast finish");
    check(modExpDown(std::nan("")) == 1.0 && modExpDown(-1.0) == 1.0 && modExpDown(2.0) == -1.0,
          "exp ramps: NaN and out-of-range phases are clamped");

    // In the matrix: 1000 Hz, 12 blocks per cycle.
    const float r = modRateNormFromHz(1000.0);
    ModMatrix   m;
    m.setSlot(0, slot(ModShape::RampUp, ModTarget::ReverbMix, r, 1.0f));
    m.setSlot(1, slot(ModShape::RampDown, ModTarget::ReverbMix, r, 1.0f));
    m.setSlot(2, slot(ModShape::ExpUp, ModTarget::ReverbMix, r, 1.0f));
    m.setSlot(3, slot(ModShape::ExpDown, ModTarget::ReverbMix, r, 1.0f));
    const double inc = modRateHz(r) / 12000.0;
    bool         up = true, down = true, eup = true, edown = true, phaseOk = true;
    for(int b = 0; b < 48; ++b)
    {
        // The value is computed at the slot's phase before the tick. That phase
        // is compared with the exact one modulo 1 (a wrap that lands a rounding
        // error on the other side of 1 is still the same phase), and the shapes
        // are checked at the matrix's own phase.
        const double p     = m.phase(0);
        const double exact = std::fmod(b * inc, 1.0);
        double       dist  = std::fabs(p - exact);
        dist               = std::min(dist, 1.0 - dist);
        phaseOk            = phaseOk && dist < 1e-9 && m.phase(3) == p;
        m.tick();
        up    = up && std::fabs(m.slotValue(0) - (2.0 * p - 1.0)) < 1e-6;
        down  = down && std::fabs(m.slotValue(1) - (1.0 - 2.0 * p)) < 1e-6;
        eup   = eup && std::fabs(m.slotValue(2) - modExpUp(p)) < 1e-6;
        edown = edown && std::fabs(m.slotValue(3) - modExpDown(p)) < 1e-6;
    }
    check(up, "ramp up: -1 at phase 0 rising linearly (2u - 1), then back");
    check(down, "ramp down: +1 at phase 0 falling linearly (1 - 2u)");
    check(eup && edown, "exp up / exp down in the matrix at their phases");
    check(phaseOk, "the phases advance by rate / 12000 per block (modulo 1)");
    ModMatrix z;
    z.setSlot(0, slot(ModShape::RampUp, ModTarget::ReverbMix, r, 1.0f));
    z.setSlot(1, slot(ModShape::ExpDown, ModTarget::ReverbMix, r, 1.0f));
    z.tick();
    check(z.slotValue(0) == -1.0f && z.slotValue(1) == 1.0f, "phase 0: ramp up at exactly -1, exp down at exactly +1");

    // A rate change continues from the current phase (no jump in the ramp).
    ModMatrix a;
    a.setSlot(0, slot(ModShape::RampUp, ModTarget::ReverbMix, modRateNormFromHz(3.0), 1.0f));
    for(int b = 0; b < 1000; ++b)
        a.tick();
    const float v0 = a.slotValue(0);
    const double ph = a.phase(0);
    a.setSlot(0, slot(ModShape::RampUp, ModTarget::ReverbMix, modRateNormFromHz(7.0), 1.0f));
    a.tick();
    check(a.phase(0) != ph && std::fabs(a.slotValue(0) - static_cast<float>(2.0 * ph - 1.0)) < 1e-6 &&
              std::fabs(a.slotValue(0) - v0) < 0.01f,
          "ramp up: a rate change continues from the current phase (the next value follows on)");
}

// MOD-C: the polarity.
void polarity()
{
    const float     r = modRateNormFromHz(1000.0);
    ModSlotSettings s = slot(ModShape::Sine, ModTarget::ReverbMix, r, 1.0f);
    ModMatrix       bi, uni;
    bi.setSlot(0, s);
    s.polarity = ModPolarity::Unipolar;
    uni.setSlot(0, s); // no weight yet: the polarity applies at once
    bool map = true;
    for(int b = 0; b < 48; ++b)
    {
        bi.tick();
        uni.tick();
        map = map && uni.slotValue(0) == (bi.slotValue(0) + 1.0f) * 0.5f;
    }
    check(map && uni.polarityBlend(0) == 1.0f && bi.polarityBlend(0) == 0.0f,
          "unipolar: (v + 1) / 2 exactly; a slot set from idle takes its polarity at once");

    // A change on a live slot: a 10 ms blend (the weights' one-pole), then the
    // snap. A ramp down at 0.01 Hz after 50 s is at v = 0, so unipolar moves it
    // to 0.5: the value follows 0.5 * blend.
    ModMatrix       h;
    ModSlotSettings w = slot(ModShape::RampDown, ModTarget::FeedbackGain, 0.0f, 1.0f);
    h.setSlot(0, w);
    for(std::int64_t b = 0; b < 12000LL * 50; ++b)
        h.tick();
    const float hv0 = h.slotValue(0);
    w.polarity      = ModPolarity::Unipolar;
    h.setSlot(0, w);
    float hworst = 0.0f, hlast = hv0, hb10 = 0.0f;
    int   snapAt = -1;
    for(int b = 0; b < 2400; ++b)
    {
        h.tick();
        hworst = std::max(hworst, std::fabs(h.slotValue(0) - hlast));
        hlast  = h.slotValue(0);
        if(b == 119)
            hb10 = h.polarityBlend(0);
        if(snapAt < 0 && h.polarityBlend(0) == 1.0f)
            snapAt = b;
    }
    const float onePole = static_cast<float>(1.0 - std::exp(-1.0 / (0.010 * 12000.0)));
    check(std::fabs(hv0) < 1e-3f && std::fabs(hb10 - (1.0f - std::exp(-1.0f))) < 0.01f &&
              hworst <= 0.5f * onePole * 1.01f && snapAt > 0 && std::fabs(hlast - 0.5f) < 0.01f,
          "a polarity change on a live slot blends over 10 ms (63 % after 10 ms: " + std::to_string(hb10) +
              "; largest step " + std::to_string(hworst) + ", the one-pole's first step " +
              std::to_string(0.5f * onePole) + "), then snaps to exactly unipolar (block " + std::to_string(snapAt) + ")");

    // Back to bipolar snaps to exactly 0: the bipolar path is v itself again.
    w.polarity = ModPolarity::Bipolar;
    h.setSlot(0, w);
    for(int b = 0; b < 2400; ++b)
        h.tick();
    const double ph = h.phase(0);
    h.tick();
    check(h.polarityBlend(0) == 0.0f && h.slotValue(0) == static_cast<float>(1.0 - 2.0 * ph),
          "back to bipolar: the blend snaps to exactly 0 (the value is v itself)");

    // The envelope ignores the polarity: its value is the follower's, 0..1.
    ModMatrix       en;
    ModSlotSettings es = slot(ModShape::Envelope, ModTarget::FeedbackLPFCutoff, 1.0f, 1.0f);
    es.polarity        = ModPolarity::Unipolar;
    en.setSlot(0, es);
    ModMatrix eb;
    es.polarity = ModPolarity::Bipolar;
    eb.setSlot(0, es);
    bool same = true;
    for(int b = 0; b < 2000; ++b)
    {
        for(int k = 0; k < 4; ++k)
        {
            const float x = (b / 200) % 2 == 0 ? 0.4f : 0.0f;
            en.feed(x);
            eb.feed(x);
        }
        en.tick();
        eb.tick();
        same = same && en.slotValue(0) == eb.slotValue(0) && en.slotValue(0) == std::min(en.envelope(0), 1.0f);
    }
    check(same && en.polarityBlend(0) == 1.0f && eb.polarityBlend(0) == 0.0f,
          "envelope: the polarity setting changes nothing (its value is the follower's, 0..1); the blend still "
          "follows the stored setting");

    // MOD-C review (S1): leaving ENVELOPE with UNIPOLAR stored. The blend
    // already sits at the stored polarity, so the new LFO shape is unipolar
    // from its first block: with a negative depth the offset is never above 0,
    // with a positive depth never below 0.
    for(ModShape to : {ModShape::Square, ModShape::RampUp, ModShape::SampleHold})
        for(float depth : {-0.5f, 0.5f})
        {
            ModMatrix       x;
            ModSlotSettings xs = slot(ModShape::Envelope, ModTarget::FeedbackGain, 0.5f, depth);
            xs.polarity        = ModPolarity::Unipolar;
            x.setSlot(0, xs);
            bool ok = true;
            for(int b = 0; b < 2400; ++b) // 200 ms of envelope on a +-0.4 input
            {
                for(int k = 0; k < 4; ++k)
                    x.feed((b / 100) % 2 == 0 ? 0.4f : 0.0f);
                x.tick();
                const float o = x.offset(ModTarget::FeedbackGain);
                ok            = ok && (depth < 0.0f ? o <= 0.0f : o >= 0.0f);
            }
            xs.shape    = to;
            xs.rateNorm = modRateNormFromHz(37.0);
            x.setSlot(0, xs);
            for(int b = 0; b < 2400; ++b) // 200 ms after the change
            {
                x.tick();
                const float o = x.offset(ModTarget::FeedbackGain);
                ok            = ok && (depth < 0.0f ? o <= 0.0f : o >= 0.0f);
            }
            check(ok && x.polarityBlend(0) == 1.0f,
                  std::string("ENVELOPE -> ") + modShapeName(to) + ", UNIPOLAR, depth " + (depth < 0 ? "-0.5" : "+0.5") +
                      ": the offset stays on its side (never " + (depth < 0 ? "above" : "below") + " 0)");
        }

    // The offset uses the mapped value: unipolar with negative depth moves only below.
    ModMatrix       neg;
    ModSlotSettings ns = slot(ModShape::Sine, ModTarget::OutputVolume, modRateNormFromHz(50.0), -0.5f);
    ns.polarity        = ModPolarity::Unipolar;
    neg.setSlot(0, ns);
    bool below = true;
    for(int b = 0; b < 12000; ++b)
    {
        neg.tick();
        below = below && neg.offset(ModTarget::OutputVolume) <= 0.0f;
    }
    check(below, "unipolar with a negative depth: the offset is never above 0 (the target only moves below its knob)");

    // Names.
    ModPolarity p;
    check(modPolarityFromName("bipolar", p) && p == ModPolarity::Bipolar && modPolarityFromName("UNI", p) &&
              p == ModPolarity::Unipolar && modPolarityFromName("Unipolar", p) && !modPolarityFromName("both", p) &&
              std::string(modPolarityName(ModPolarity::Unipolar)) == "unipolar",
          "polarity names: bipolar / unipolar (bi / uni), case-insensitive");
    ModShape sh;
    bool     names = true;
    for(std::size_t i = 0; i < kModShapeCount; ++i)
        names = names && modShapeFromName(modShapeName(static_cast<ModShape>(i)), sh) && sh == static_cast<ModShape>(i);
    check(names && modShapeFromName("rampup", sh) && sh == ModShape::RampUp && modShapeFromName("EXPDOWN", sh) &&
              sh == ModShape::ExpDown,
          "every shape's name parses back to it (rampup, rampdown, expup, expdown)");
    check(static_cast<int>(ModShape::RampUp) == 3 && static_cast<int>(ModShape::SampleHold) == 7 &&
              static_cast<int>(ModShape::Envelope) == 8 && kModShapeCount == 9,
          "shape order: SINE, TRIANGLE, SQUARE, RAMP UP, RAMP DOWN, EXP UP, EXP DOWN, S&H, ENVELOPE");
    ModSlotSettings bad;
    bad.polarity = static_cast<ModPolarity>(7);
    check(sanitizeModSlot(bad).polarity == ModPolarity::Bipolar, "sanitize: an unknown polarity is bipolar");
}
} // namespace

int main()
{
    rates();
    shapes();
    phaseAccuracy();
    sampleHold();
    smoothing();
    envelope();
    sums();
    sanitise();
    ramps();
    polarity();
    return modtest::finish();
}
