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
// both and leaves the others unchanged.
void updateKnobParams(KnobRegistry& registry, const ControlFrame& frame) noexcept
{
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        const Knob knob = static_cast<Knob>(i);
        registry.UpdateNormalized(knob, preShape(knob, frame.knob[i], frame.delaySwitch));
    }
}

AudreyVoice::AudreyVoice()  = default;
AudreyVoice::~AudreyVoice() = default;

void AudreyVoice::init()
{
    using namespace std::placeholders;

    // Release the old state first, so a re-init does not hold two engines at once.
    params_.reset();
    engine_.reset();

    // main(): engine.Init(hw.AudioSampleRate()); controls.Init(hw, engine);
    // limiter Init. Controls::Init: params_.Init(AudioSampleRate() / AudioBlockSize()).
    auto engine = std::make_unique<Engine>();
    engine->Init(static_cast<float>(kSampleRate));

    auto params = std::make_unique<KnobRegistry>();
    params->Init(static_cast<float>(kSampleRate) / static_cast<float>(kControlBlock));

    Engine* e = engine.get();
    const KnobHandlers handlers = {
        std::bind(&Engine::SetStringPitch, e, _1),
        std::bind(&Engine::SetFeedbackGain, e, _1),
        std::bind(&Engine::SetFeedbackDelay, e, _1),
        std::bind(&Engine::SetFeedbackLPFCutoff, e, _1),
        std::bind(&Engine::SetFeedbackHPFCutoff, e, _1),
        std::bind(&Engine::SetReverbMix, e, _1),
        std::bind(&Engine::SetReverbFeedback, e, _1),
        std::bind(&Engine::SetEchoDelaySendAmount, e, _1),
        std::bind(&Engine::SetEchoDelayTime, e, _1),
        std::bind(&Engine::SetEchoDelayFeedback, e, _1),
        std::bind(&Engine::SetOutputLevel, e, _1),
    };
    registerKnobParams(*params, handlers);

    for(auto& lim : limiter_)
        lim.Init();

    engine_   = std::move(engine);
    params_   = std::move(params);
    frame_    = ControlFrame{};
    blockPos_ = 0;
}

void AudreyVoice::startControlBlock(ControlSource& controls) noexcept
{
    // AudioCallback: controls.Update(hw); controls.Process();
    controls.read(frame_);
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
