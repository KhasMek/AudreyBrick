// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Transcription of FeedbackSynth_main.cpp and Source/FeedbackSynthControls.cpp at
// upstream commit e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3. The comments name the
// upstream lines each block follows. See ReferenceHarness.h for what replaces the
// hardware and for the upstream MIT notice (Copyright (c) 2026 Synthux Academy;
// Copyright (c) 2022 Infrasonic Audio, LLC), which applies to this file too.

#include "ReferenceHarness.h"

#include <functional>

using namespace infrasonic;
using namespace infrasonic::FeedbackSynth;

namespace audreyref
{

const char* const kParameterNames[kNumAdcChannels] = {
    "Frequency",         // 0
    "FeedbackGain",      // 1
    "FeedbackBody",      // 2
    "FeedbackLPFCutoff", // 3
    "FeedbackHPFCutoff", // 4
    "ReverbMix",         // 5
    "ReverbDecay",       // 6
    "EchoDelaySend",     // 7
    "EchoDelayTime",     // 8
    "EchoDelayFeedback", // 9
    "OutputVolume",      // 10
};

ReferenceHarness::ReferenceHarness() : engine_(new Engine()) {}

ReferenceHarness::~ReferenceHarness() = default;

// FeedbackSynth_main.cpp, main():
//     engine.Init(hw.AudioSampleRate());
//     controls.Init(hw, engine);
//     for(auto& lim : limiter) lim.Init();
void ReferenceHarness::Init()
{
    engine_->Init(kSampleRate);
    controls_.Init(*engine_);

    for(auto& lim : limiter_)
    {
        lim.Init();
    }
    initialised_ = true;
}

// FeedbackSynth_main.cpp, AudioCallback:
//     controls.Update(hw);
//     controls.Process();
//     for (size_t i=0; i<size; i++) engine.Process(IN_L[i], OUT_L[i], OUT_R[i]);
//     limiter[0].ProcessBlock(OUT_L, size, 0.7f);
//     limiter[1].ProcessBlock(OUT_R, size, 0.7f);
void ReferenceHarness::AudioCallback(const AdcFrame& adc, const float* in, float* outL, float* outR)
{
    if(!initialised_)
    {
        for(std::size_t i = 0; i < kBlockSize; i++)
            outL[i] = outR[i] = 0.0f;
        return;
    }

    const std::size_t size = kBlockSize;

    controls_.Update(adc);
    controls_.Process();
    for(std::size_t i = 0; i < size; i++)
    {
        engine_->Process(in ? in[i] : 0.0f, outL[i], outR[i]);
    }
    limiter_[0].ProcessBlock(outL, size, 0.7f);
    limiter_[1].ProcessBlock(outR, size, 0.7f);
}

// FeedbackSynthControls.cpp, Controls::Init:
//     params_.Init(hw.AudioSampleRate() / hw.AudioBlockSize());
//     (del_sw_.Init and initADCs: hardware, dropped)
//     registerParams(engine);
void ReferenceHarness::Controls::Init(Engine& engine)
{
    params_.Init(48000.0f / 4);
    registerParams(engine);
}

// FeedbackSynthControls.cpp, Controls::Update. `adc.inverted[ch]` is
// `1.0f - hw.adc.GetFloat(ch)`.
void ReferenceHarness::Controls::Update(const AdcFrame& adc)
{
    params_.UpdateNormalized(Parameter::Frequency,          adc.inverted[0]);
    params_.UpdateNormalized(Parameter::FeedbackGain,       adc.inverted[1]);
    params_.UpdateNormalized(Parameter::FeedbackBody,       adc.inverted[2]);
    params_.UpdateNormalized(Parameter::FeedbackLPFCutoff,  adc.inverted[3]);
    params_.UpdateNormalized(Parameter::FeedbackHPFCutoff,  adc.inverted[4]);
    params_.UpdateNormalized(Parameter::ReverbMix,          adc.inverted[5]);
    // Special mapping for reverb feedback/decay (anti-exponential tension curve)
    params_.UpdateNormalized(Parameter::ReverbDecay,        ftension(adc.inverted[6], -3.0f));
    params_.UpdateNormalized(Parameter::EchoDelaySend,      adc.inverted[7]);
    // Delay switch doubles or halves delay time instantly for doppler warp
    // (del_sw_.Debounce(): hardware, dropped)
    float delay_norm = adc.inverted[8];
    float delay_scale = adc.switchPressed ? 0.5f : 1.0f;
    params_.UpdateNormalized(Parameter::EchoDelayTime, delay_norm * delay_scale);
    params_.UpdateNormalized(Parameter::EchoDelayFeedback,  adc.inverted[9]);
    params_.UpdateNormalized(Parameter::OutputVolume,       adc.inverted[10]);
}

// FeedbackSynthControls.cpp, Controls::registerParams.
void ReferenceHarness::Controls::registerParams(Engine& engine)
{
    using namespace std::placeholders;

    // String freq/pitch as note number
    params_.Register(Parameter::Frequency, 40.0f, 16.0f, 72.0f,
        std::bind(&Engine::SetStringPitch, &engine, _1), 0.2f);

    // Feedback Gain in dbFS
    params_.Register(Parameter::FeedbackGain, -60.0f, -60.0f, 12.0f,
        std::bind(&Engine::SetFeedbackGain, &engine, _1));

    // Feedback body/delay in seconds
    params_.Register(Parameter::FeedbackBody, 0.001f, 0.001f, 0.1f,
        std::bind(&Engine::SetFeedbackDelay, &engine, _1), 1.0f, daisysp::Mapping::EXP);

    // Feedback filter cutoffs in hz
    params_.Register(Parameter::FeedbackLPFCutoff, 18000.0f, 100.0f, 18000.0f,
        std::bind(&Engine::SetFeedbackLPFCutoff, &engine, _1), 0.05f, daisysp::Mapping::LOG);
    params_.Register(Parameter::FeedbackHPFCutoff, 250.0f, 10.0f, 4000.0f,
        std::bind(&Engine::SetFeedbackHPFCutoff, &engine, _1), 0.05f, daisysp::Mapping::LOG);

    // Reverb Mix
    params_.Register(Parameter::ReverbMix, 0.0f, 0.0f, 1.0f,
        std::bind(&Engine::SetReverbMix, &engine, _1));

    // Reverb Feedback (input is mapped to anti-exponential on ADC read)
    params_.Register(Parameter::ReverbDecay, 0.2f, 0.2f, 1.0f,
        std::bind(&Engine::SetReverbFeedback, &engine, _1));

    // Echo Delay send
    params_.Register(Parameter::EchoDelaySend, 0.0f, 0.0f, 1.0f,
        std::bind(&Engine::SetEchoDelaySendAmount, &engine, _1), 0.05f, daisysp::Mapping::EXP);

    // Echo Delay time in s
    params_.Register(Parameter::EchoDelayTime, 0.5f, 0.05f, 5.0f,
        std::bind(&Engine::SetEchoDelayTime, &engine, _1), 0.1f, daisysp::Mapping::EXP);

    // Echo Delay feedback
    params_.Register(Parameter::EchoDelayFeedback, 0.0f, 0.0f, 1.5f,
        std::bind(&Engine::SetEchoDelayFeedback, &engine, _1));

    // Output level
    params_.Register(Parameter::OutputVolume, 0.5f, 0.0f, 1.0f,
        std::bind(&Engine::SetOutputLevel, &engine, _1), 0.05f, daisysp::Mapping::EXP);
}

} // namespace audreyref
