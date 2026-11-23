// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "mod/ModMatrix.h"

#include <cmath>

namespace audreybrick
{
namespace
{
constexpr double kTwoPi        = 6.283185307179586476925286766559;
constexpr float  kEnvFloor     = 1.0e-12f;
// Per-slot PRNG seeds: fixed, non-zero, different per slot.
constexpr std::uint32_t kSeeds[kModSlots] = {0x9E3779B9u, 0x7F4A7C15u, 0x2545F491u, 0x6C8E9CF5u};

float onePoleCoef(double seconds, double rate) noexcept
{
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * rate)));
}
} // namespace

float ModMatrix::nextRandom(std::uint32_t& x) noexcept
{
    // xorshift32 (Marsaglia), then the top 24 bits as a uniform value in -1..+1.
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<float>(x >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

void ModMatrix::setRate(Slot& s) noexcept
{
    s.inc     = modRateHz(s.set.rateNorm) / kControlRate;
    s.relCoef = onePoleCoef(modReleaseSeconds(s.set.rateNorm), kSampleRate);
}

void ModMatrix::reset() noexcept
{
    attackCoef_ = onePoleCoef(kModAttackSeconds, kSampleRate);
    depthCoef_  = onePoleCoef(kModDepthSmoothingSeconds, kControlRate);
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        Slot& s = slots_[i];
        s       = Slot{};
        s.rng   = kSeeds[i];
        s.held  = nextRandom(s.rng); // phase 0 is a cycle start
        setRate(s);
    }
    for(float& o : offset_)
        o = 0.0f;
    idle_ = true;
    updateEnvelopeFlag();
}

void ModMatrix::setSlot(std::size_t i, const ModSlotSettings& in) noexcept
{
    if(i >= kModSlots)
        return;
    Slot&                 s   = slots_[i];
    const ModSlotSettings set = sanitizeModSlot(in);
    const bool            rateChanged = set.rateNorm != s.set.rateNorm;
    const bool            wasLive     = s.set.target != ModTarget::None || s.wMask != 0;
    s.set                             = set;
    if(rateChanged)
        setRate(s);
    // MOD-C: a slot that was not live (no target, no weight left) takes its
    // polarity at once (its weights start from 0, so nothing of it sounds
    // yet); a live one blends to it in tick().
    if(!wasLive)
        s.uni = polarityGoal(set);
    if(set.shape != ModShape::Envelope)
        s.env = 0.0f; // a slot switched to Envelope starts from silence (2 ms attack)
    if(set.target != ModTarget::None)
        idle_ = false;
    updateEnvelopeFlag();
}

void ModMatrix::updateEnvelopeFlag() noexcept
{
    hasEnvelope_ = false;
    for(const Slot& s : slots_)
        hasEnvelope_ = hasEnvelope_ || s.set.shape == ModShape::Envelope;
}

void ModMatrix::feed(float x) noexcept
{
    // A non-finite sample counts as 0, as AudreyVoice replaces it by 0.
    const float a = std::isfinite(x) ? std::fabs(x) : 0.0f;
    for(Slot& s : slots_)
    {
        if(s.set.shape != ModShape::Envelope)
            continue; // only an Envelope slot follows the input (its level is 0 otherwise)
        const float coef = a > s.env ? attackCoef_ : s.relCoef;
        s.env += coef * (a - s.env);
        if(!(s.env >= kEnvFloor)) // flush the decay tail (no denormals)
            s.env = 0.0f;
    }
}

void ModMatrix::tick() noexcept
{
    if(idle_)
    {
        // Nothing modulates: only the free-running phases (and the SampleHold
        // steps) advance; every value and offset is already 0.
        for(Slot& s : slots_)
        {
            s.phase += s.inc;
            if(s.phase >= 1.0)
            {
                s.phase -= std::floor(s.phase);
                s.held = nextRandom(s.rng);
            }
        }
        return;
    }

    for(float& o : offset_)
        o = 0.0f;

    for(Slot& s : slots_)
    {
        // 1. The value at the current phase, for a live slot (a target, or a
        //    weight still fading out); an idle slot's value is 0 and costs nothing.
        const bool live = s.set.target != ModTarget::None || s.wMask != 0;
        const double p = s.phase;
        float        v = 0.0f;
        switch(live ? s.set.shape : ModShape::Count)
        {
            case ModShape::Sine: v = std::sin(static_cast<float>(kTwoPi * p)); break; // float sin: cheaper, 1e-7 accurate
            case ModShape::Triangle:
                v = static_cast<float>(p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0));
                break;
            case ModShape::Square: v = p < 0.5 ? 1.0f : -1.0f; break;
            case ModShape::RampUp: v = static_cast<float>(2.0 * p - 1.0); break;
            case ModShape::RampDown: v = static_cast<float>(1.0 - 2.0 * p); break;
            case ModShape::ExpUp: v = static_cast<float>(modExpUp(p)); break;
            case ModShape::ExpDown: v = static_cast<float>(modExpDown(p)); break;
            case ModShape::SampleHold: v = s.held; break;
            case ModShape::Envelope: v = s.env < 1.0f ? s.env : 1.0f; break;
            case ModShape::Count: break;
        }
        // MOD-C: the polarity, blended through the 10 ms one-pole (p = 0: v
        // itself; p = 1: (v + 1) / 2 exactly). The Envelope's value is used as
        // it is (p is not applied to it, but still follows the stored polarity).
        if(live)
        {
            const float goal = polarityGoal(s.set);
            if(s.uni != goal)
            {
                // Snapped within kModSnapThreshold, or once a step no longer
                // changes the float (near 1 that happens a few 1e-6 short).
                const float n = s.uni + depthCoef_ * (goal - s.uni);
                s.uni         = (std::fabs(goal - n) < kModSnapThreshold || n == s.uni) ? goal : n;
            }
            if(s.set.shape != ModShape::Envelope && s.uni != 0.0f)
            {
                const float u = (v + 1.0f) * 0.5f;
                v             = s.uni == 1.0f ? u : v + s.uni * (u - v);
            }
        }
        else
        {
            s.uni = polarityGoal(s.set); // nothing of it sounds
        }
        s.value = v;

        // 2. Advance the phase; a wrap is a new SampleHold step.
        s.phase += s.inc;
        if(s.phase >= 1.0)
        {
            s.phase -= std::floor(s.phase);
            s.held = nextRandom(s.rng);
        }

        // 3. The weights, and this slot's share of each target's offset.
        if(!live)
            continue;
        // Only the targets whose weight is not 0, plus the slot's own target
        // when its depth is not 0 (every other weight is 0 and stays 0), in
        // ascending target order.
        const std::size_t own = static_cast<std::size_t>(s.set.target);
        std::uint32_t     todo = s.wMask;
        if(own != 0 && s.set.depth != 0.0f)
            todo |= 1u << own;
        todo &= ~1u; // 0 is None: never contributes
        while(todo != 0)
        {
            const std::size_t t = static_cast<std::size_t>(__builtin_ctz(todo));
            todo &= todo - 1u;
            const float goal = own == t ? s.set.depth : 0.0f;
            float&      w    = s.w[t];
            w += depthCoef_ * (goal - w);
            if(goal == 0.0f && std::fabs(w) < kModSnapThreshold)
                w = 0.0f;
            if(w != 0.0f)
            {
                offset_[t] += w * v;
                s.wMask |= 1u << t;
            }
            else
            {
                s.wMask &= ~(1u << t);
            }
        }
    }

    // Idle again once no slot has a target and every weight has snapped to 0.
    bool idle = true;
    for(const Slot& s : slots_)
    {
        idle = idle && s.set.target == ModTarget::None && s.wMask == 0;
    }
    if(idle)
    {
        for(Slot& s : slots_)
            s.value = 0.0f;
        for(float& o : offset_)
            o = 0.0f;
    }
    idle_ = idle;
}

} // namespace audreybrick
