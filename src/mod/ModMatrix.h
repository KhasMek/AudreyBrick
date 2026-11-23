// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ModMatrix: the four modulation slots' DSP (MOD-A, docs/briefs/MOD.md). No
// threads, no SDL, no allocation; one owner (the audio thread in the app).
//
// Timing: tick() runs once per control block (12 kHz, kModControlRate); feed()
// runs once per input sample (48 kHz) and drives only the envelope followers.
// Per tick, for each slot:
//   1. value = the shape at the current phase (Sine, Triangle, Square: -1..+1,
//      phase 0 is the rising zero crossing of all three; RampUp, RampDown,
//      ExpUp, ExpDown (MOD-C): -1..+1, phase 0 the start of the ramp, see
//      ModTypes.h; SampleHold: the held random value -1..+1; Envelope: the
//      follower, 0..1). MOD-C, the polarity: an LFO value v becomes
//      v + p * ((v + 1) / 2 - v), p the slot's smoothed polarity 0 (Bipolar)
//      .. 1 (Unipolar). p follows its goal (the stored polarity, 0 or 1, also
//      under Envelope, whose value is used as it is without p) through the same one-pole as the weights (10 ms) and
//      snaps to it within kModSnapThreshold, so at p = 0 the value is v itself
//      (the bipolar path is unchanged) and at p = 1 exactly (v + 1) / 2. A
//      polarity change is therefore a 10 ms blend of the two mappings, like a
//      target change's cross-fade. A slot that is not live (no target, every
//      weight 0) takes the goal at once: nothing of it sounds.
//   2. The phase advances by rateHz / 12000 (kept in double, so a 0.01 Hz cycle
//      is exact to far below a sample over hours). On a wrap SampleHold takes a
//      new value from the slot's PRNG (xorshift32, seeded per slot by reset()),
//      whatever the shape, so switching to SampleHold shows the current step.
//      Free-running: nothing resets the phase except reset(); a rate change only
//      changes the increment, so it continues from the current phase.
//   3. Per target, a smoothed weight w[target] moves by a one-pole
//      (kModDepthSmoothingSeconds, 10 ms) towards depth for the slot's target
//      and towards 0 for every other target. A target change is therefore a
//      cross-fade: the old target's weight falls to 0 while the new one's rises
//      from 0, at the same time. A weight whose goal is 0 snaps to exactly 0
//      once it is below kModSnapThreshold (1e-6), so the unmodulated path is
//      taken again.
// Then per target the offset is the sum over slots of w[target] * value, over
// the weights that are not 0 (a target nobody modulates gets exactly 0.0f).
// The DelaySwitch request is on while its offset is above 0.
//
// Idle slots: a slot with target None whose weights are all 0 only advances its
// phase (and its SampleHold steps); its value is 0 (so slotOutput() reads 0)
// and no shape is computed. This keeps the cost of four idle slots small.
//
// Envelope follower (per sample, only for a slot whose shape is Envelope; a
// slot switched to Envelope starts from 0 and rises with the attack): x = |in|;
// e += a * (x - e) with the attack coefficient (2 ms) while x > e, else the
// slot's release coefficient (5 s to 5 ms from rateNorm); e below 1e-12 is set
// to 0. The slot's value is min(e, 1).

#pragma once

#include <cstddef>
#include <cstdint>

#include "mod/ModTypes.h"

namespace audreybrick
{

class ModMatrix
{
  public:
    static constexpr double kControlRate = 12000.0; // control blocks per second
    static constexpr double kSampleRate  = 48000.0;

    ModMatrix() noexcept { reset(); }

    // Every slot back to its default settings (target None, depth 0), phase 0,
    // PRNG reseeded, followers 0, every weight 0.
    void reset() noexcept;

    // New settings for slot i (sanitised; an index >= kModSlots is ignored).
    // Depth and target changes go through the smoothing; the phase is kept.
    void setSlot(std::size_t i, const ModSlotSettings& s) noexcept;
    const ModSlotSettings& slot(std::size_t i) const noexcept { return slots_[i < kModSlots ? i : 0].set; }

    // One input sample for the envelope followers.
    void feed(float x) noexcept;
    // True if any slot's shape is Envelope (feed() does nothing otherwise).
    bool hasEnvelope() const noexcept { return hasEnvelope_; }
    // True while nothing modulates: no slot has a target and every weight is 0
    // (every offset and value is then exactly 0, and tick() only advances the
    // phases).
    bool idle() const noexcept { return idle_; }

    // One control block.
    void tick() noexcept;

    // After tick(): the slot's value (before depth) and the summed offset of a
    // target, in normalised knob travel (InputGain: of the input gain knob;
    // DelaySwitch: the request is offset > 0).
    float slotValue(std::size_t i) const noexcept { return slots_[i < kModSlots ? i : 0].value; }
    float offset(ModTarget t) const noexcept
    {
        return static_cast<std::size_t>(t) < kModTargetCount ? offset_[static_cast<std::size_t>(t)] : 0.0f;
    }
    bool switchRequest() const noexcept { return offset_[static_cast<std::size_t>(ModTarget::DelaySwitch)] > 0.0f; }
    // The smoothed weight of slot i on target t (tests).
    float weight(std::size_t i, ModTarget t) const noexcept
    {
        return slots_[i < kModSlots ? i : 0].w[static_cast<std::size_t>(t) < kModTargetCount ? static_cast<std::size_t>(t) : 0];
    }
    // MOD-C: the smoothed polarity of slot i, 0 (bipolar) .. 1 (unipolar) (tests).
    float polarityBlend(std::size_t i) const noexcept { return slots_[i < kModSlots ? i : 0].uni; }
    // The follower level of slot i (tests).
    float envelope(std::size_t i) const noexcept { return slots_[i < kModSlots ? i : 0].env; }
    // The phase of slot i, 0..1 (tests).
    double phase(std::size_t i) const noexcept { return slots_[i < kModSlots ? i : 0].phase; }

  private:
    struct Slot
    {
        ModSlotSettings set;
        double          phase   = 0.0;
        double          inc     = 0.0;   // phase per control block
        float           relCoef = 0.0f;  // follower release, per sample
        std::uint32_t   rng     = 1u;
        float           held    = 0.0f;  // SampleHold value
        float           env     = 0.0f;  // follower level
        float           value   = 0.0f;  // the last tick's value (after the polarity)
        float           uni     = 0.0f;  // MOD-C: smoothed polarity, 0 bipolar .. 1 unipolar
        float           w[kModTargetCount] = {};
        std::uint32_t   wMask = 0; // bit t set while w[t] != 0
    };

    static float nextRandom(std::uint32_t& state) noexcept;
    static float polarityGoal(const ModSlotSettings& s) noexcept
    {
        // The stored polarity, whatever the shape (MOD-C review): an Envelope
        // slot does not apply the blend, but keeps it where the stored setting
        // says, so leaving ENVELOPE does not swing the target the wrong way.
        return s.polarity == ModPolarity::Unipolar ? 1.0f : 0.0f;
    }
    static void  setRate(Slot& s) noexcept;
    void         updateEnvelopeFlag() noexcept;

    Slot  slots_[kModSlots];
    float offset_[kModTargetCount] = {};
    float attackCoef_ = 0.0f;
    float depthCoef_  = 0.0f;
    bool  hasEnvelope_ = false;
    bool  idle_        = true;
};

} // namespace audreybrick
