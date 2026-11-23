// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// registerKnobParams, updateKnobParams and AudreyVoice transcribe
// Controls::registerParams and Controls::Update (FeedbackSynthControls.cpp) and
// AudioCallback and main (FeedbackSynth_main.cpp) from upstream Audrey II,
// https://github.com/Synthux-Academy/Audrey-II, commit e5ab799, which carries
// this notice:
//
//   MIT License
//
//   Copyright (c) 2026 Synthux Academy
//   Copyright (c) 2022 Infrasonic Audio, LLC
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.

#include "core/AudreyVoice.h"

#include <cmath>
#include <functional>

#include "core/ParamTable.h"

namespace audreybrick
{

// Upstream registers each knob with
//   params_.Register(Parameter::X, initial, min, max,
//                    std::bind(&Engine::SetX, &engine, _1), smooth_time, mapping);
// The values come from ParamTable, which transcribes them.
void registerKnobParams(KnobRegistry& registry, const KnobHandlers& handlers)
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob       knob = static_cast<Knob>(i);
        const ParamSpec& s    = paramSpec(knob);
        registry.Register(knob, s.initial, s.min, s.max, handlers[i], s.smoothTime, s.curve);
    }
}

// Upstream Controls::Update calls UpdateNormalized for each parameter in enum
// order with (1 - adc); ControlFrame already holds that value. ReverbDecay gets
// ftension(n, -3) and EchoDelayTime gets n * (switch ? 0.5 : 1); preShape does
// both and leaves the others unchanged. MOD-A: the switch is the manual switch
// OR the modulated request (exactly the manual switch while the request is off).
void updateKnobParams(KnobRegistry& registry, const ControlFrame& frame) noexcept
{
    const bool sw = frame.delaySwitch || frame.delaySwitchMod;
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob knob = static_cast<Knob>(i);
        registry.UpdateNormalized(knob, preShape(knob, frame.knob[i], sw));
    }
}

namespace
{
using EngineSetter = void (infrasonic::FeedbackSynth::Engine::*)(float);

// The engine setter of each knob, in Knob order (upstream registerParams).
constexpr EngineSetter kSetters[kKnobCount] = {
    &infrasonic::FeedbackSynth::Engine::SetStringPitch,
    &infrasonic::FeedbackSynth::Engine::SetFeedbackGain,
    &infrasonic::FeedbackSynth::Engine::SetFeedbackDelay,
    &infrasonic::FeedbackSynth::Engine::SetFeedbackLPFCutoff,
    &infrasonic::FeedbackSynth::Engine::SetFeedbackHPFCutoff,
    &infrasonic::FeedbackSynth::Engine::SetReverbMix,
    &infrasonic::FeedbackSynth::Engine::SetReverbFeedback,
    &infrasonic::FeedbackSynth::Engine::SetEchoDelaySendAmount,
    &infrasonic::FeedbackSynth::Engine::SetEchoDelayTime,
    &infrasonic::FeedbackSynth::Engine::SetEchoDelayFeedback,
    &infrasonic::FeedbackSynth::Engine::SetOutputLevel,
};
} // namespace

void AudreyVoice::applyKnob(std::size_t i, float smoothed) noexcept
{
    // MOD-A: the modulation goes in after the original's smoother. With an
    // offset of exactly 0 the setter gets the smoothed value untouched, as
    // upstream's std::bind(&Engine::SetX, &engine, _1) handler did.
    const float offset = frame_.knobMod[i];
    const Knob  knob   = static_cast<Knob>(i);
    float       v      = smoothed;
    if(knob == Knob::FeedbackLPFCutoff || knob == Knob::FeedbackHPFCutoff)
        v = slewCutoff(knob == Knob::FeedbackLPFCutoff ? 0 : 1, knob, smoothed, offset);
    else if(offset != 0.0f)
        v = modulateEngineValue(knob, smoothed, offset, switchOn_);
    (engine_.get()->*kSetters[i])(v);
}

float AudreyVoice::slewCutoff(std::size_t f, Knob knob, float smoothed, float offset) noexcept
{
    const bool modulated = offset != 0.0f && offset == offset;
    if(!modulated && !cutActive_[f])
    {
        cutLast_[f] = smoothed; // the unmodulated path: the smoothed value as it is
        return smoothed;
    }
    if(!(cutoffSlew_ > 0.0f)) // no limit (scans)
    {
        cutActive_[f] = false;
        cutLast_[f]   = modulated ? modulateEngineValue(knob, smoothed, offset, switchOn_) : smoothed;
        return cutLast_[f];
    }
    if(!cutActive_[f])
    {
        cutActive_[f] = true;
        cutLog_[f]    = std::log2(cutLast_[f] > 0.0f ? cutLast_[f] : smoothed); // from the cutoff last given
    }
    // The target in log2(Hz): for a LOG knob an offset adds offset * log2(max/min)
    // (the same as modulateEngineValue, without leaving the log domain), then
    // the range clamp.
    const ParamSpec& s    = paramSpec(knob);
    const float      lmin = cutLogMin_[f];
    const float      lmax = cutLogMax_[f];
    float            tl   = std::log2(smoothed);
    if(modulated)
    {
        const float o = offset < -2.0f ? -2.0f : (offset > 2.0f ? 2.0f : offset);
        tl += o * (lmax - lmin);
        tl = tl < lmin ? lmin : (tl > lmax ? lmax : tl);
    }
    const float d = tl - cutLog_[f];
    if(!modulated && std::fabs(d) <= cutoffSlew_)
    {
        cutActive_[f] = false; // converged: back to the unmodulated path
        cutLast_[f]   = smoothed;
        return smoothed;
    }
    const float step = d > cutoffSlew_ ? cutoffSlew_ : (d < -cutoffSlew_ ? -cutoffSlew_ : d);
    cutLog_[f] += step;
    float v = std::exp2(cutLog_[f]);
    v       = v < s.min ? s.min : (v > s.max ? s.max : v);
    cutLast_[f] = v;
    return v;
}

AudreyVoice::AudreyVoice()  = default;
AudreyVoice::~AudreyVoice() = default;

void AudreyVoice::init()
{
    // Release the old state first, so a re-init does not hold two engines at once.
    params_.reset();
    engine_.reset();

    // main(): engine.Init(hw.AudioSampleRate()); controls.Init(hw, engine);
    // limiter Init. Controls::Init: params_.Init(AudioSampleRate() / AudioBlockSize()).
    auto engine = std::make_unique<Engine>();
    engine->Init(static_cast<float>(kSampleRate));

    auto params = std::make_unique<KnobRegistry>();
    params->Init(static_cast<float>(kSampleRate) / static_cast<float>(kControlBlock));

    // Upstream binds each setter directly (std::bind(&Engine::SetX, &engine, _1)).
    // MOD-A: each handler goes through applyKnob, which calls the same setter with
    // the same value unless the knob has a modulation offset.
    KnobHandlers handlers;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        handlers[i] = [this, i](const float v) { applyKnob(i, v); };
    registerKnobParams(*params, handlers);

    for(auto& lim : limiter_)
        lim.Init();

    engine_   = std::move(engine);
    params_   = std::move(params);
    frame_    = ControlFrame{};
    switchOn_ = false;
    for(std::size_t f = 0; f < 2; ++f)
    {
        cutLast_[f]   = 0.0f;
        cutLog_[f]    = 0.0f;
        cutActive_[f] = false;
        const ParamSpec& s = paramSpec(f == 0 ? Knob::FeedbackLPFCutoff : Knob::FeedbackHPFCutoff);
        cutLogMin_[f]      = std::log2(s.min);
        cutLogMax_[f]      = std::log2(s.max);
    }
    blockPos_ = 0;
}

void AudreyVoice::startControlBlock(ControlSource& controls) noexcept
{
    // AudioCallback: controls.Update(hw); controls.Process();
    controls.read(frame_);
    switchOn_ = frame_.delaySwitch || frame_.delaySwitchMod;
    updateKnobParams(*params_, frame_);
    params_->Process();
}

void AudreyVoice::process(ControlSource& controls, const float* inMono,
                          float* outL, float* outR, std::size_t frames) noexcept
{
    if(!engine_)
    {
        for(std::size_t i = 0; i < frames; ++i)
        {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        return;
    }

    std::size_t i = 0;
    while(i < frames)
    {
        if(blockPos_ == 0)
            startControlBlock(controls);

        const std::size_t left  = static_cast<std::size_t>(kControlBlock - blockPos_);
        const std::size_t count = (frames - i < left) ? frames - i : left;

        // AudioCallback: engine.Process(IN_L[i], OUT_L[i], OUT_R[i]) for each sample.
        for(std::size_t k = i; k < i + count; ++k)
        {
            float in = (inMono != nullptr) ? inMono[k] : 0.0f;
            if(!std::isfinite(in))
                in = 0.0f;
            engine_->Process(in, outL[k], outR[k]);
        }

        // AudioCallback: limiter[c].ProcessBlock(OUT_c, size, 0.7f) after the block.
        // Limiter::ProcessBlock is a per-sample loop over its own state, so running
        // it on the pieces of a split block gives the same result as one call of 4.
        limiter_[0].ProcessBlock(outL + i, count, 0.7f);
        limiter_[1].ProcessBlock(outR + i, count, 0.7f);

        blockPos_ = (blockPos_ + static_cast<int>(count)) % kControlBlock;
        i += count;
    }
}

} // namespace audreybrick
