// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Test-only reference harness: the Audrey II firmware's main() and AudioCallback
// (FeedbackSynth_main.cpp) and its Controls class (Source/FeedbackSynthControls.{h,cpp})
// at upstream commit e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3, transcribed directly
// from those files with only the libDaisy hardware removed. It drives the upstream
// Engine, ParameterRegistry and daisysp::Limiter.
//
// It exists so that the parity test can pin AudreyVoice (src/core) to an independent
// reading of the firmware. It therefore must not include or link anything from
// src/core or src/control, and it is not used outside the tests.
//
// What replaces the hardware:
//   - hw.adc.GetFloat(ch): the firmware always uses 1.0f - GetFloat(ch). The harness is
//     given that inverted value directly, per ADC channel, in AdcFrame::inverted[ch].
//   - del_sw_.Debounce() / Pressed(): AdcFrame::switchPressed (no debounce).
//   - IN_L[i]: a plain mono input pointer (nullptr = silence).
//   - The callback size is always kBlockSize (4), as hw.SetAudioBlockSize(4).
//
// Upstream notice for the transcribed logic:
//
// MIT License
//
// Copyright (c) 2026 Synthux Academy
// Copyright (c) 2022 Infrasonic Audio, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <daisysp.h>

#include "FeedbackSynthEngine.h"
#include "ParameterRegistry.h"

namespace audreyref
{

// FeedbackSynthControls.h: kNumAdcChannels.
constexpr std::size_t kNumAdcChannels = 11;

// The names of the firmware's Parameter enum, in enum order (= ADC channel order),
// transcribed from FeedbackSynthControls.h. The parity test checks the port's
// script knob names against this list.
extern const char* const kParameterNames[kNumAdcChannels];

// One callback's worth of hardware state.
struct AdcFrame
{
    float inverted[kNumAdcChannels]; // 1.0f - hw.adc.GetFloat(ch), per ADC channel
    bool  switchPressed;             // del_sw_.Pressed()
};

class ReferenceHarness
{
  public:
    // FeedbackSynth_main.cpp: kSampleRate (SAI_48KHZ) and kBlockSize.
    static constexpr float       kSampleRate = 48000.0f;
    static constexpr std::size_t kBlockSize  = 4;

    ReferenceHarness();
    ~ReferenceHarness();
    ReferenceHarness(const ReferenceHarness&)            = delete;
    ReferenceHarness& operator=(const ReferenceHarness&) = delete;

    // main(): engine.Init, controls.Init, limiter Init. Call once.
    void Init();

    // AudioCallback with size == kBlockSize. `in` may be nullptr (silence);
    // otherwise it points at kBlockSize samples. outL/outR receive kBlockSize samples.
    void AudioCallback(const AdcFrame& adc, const float* in, float* outL, float* outR);

  private:
    // The firmware's Controls class, minus the hardware.
    class Controls
    {
      public:
        void Init(infrasonic::FeedbackSynth::Engine& engine);
        void Update(const AdcFrame& adc);
        void Process() { params_.Process(); }

      private:
        // FeedbackSynthControls.h
        enum class Parameter : std::uint8_t
        {
            Frequency = 0,
            FeedbackGain,      // 1
            FeedbackBody,      // 2
            FeedbackLPFCutoff, // 3
            FeedbackHPFCutoff, // 4
            ReverbMix,         // 5
            ReverbDecay,       // 6
            EchoDelaySend,     // 7
            EchoDelayTime,     // 8
            EchoDelayFeedback, // 9
            OutputVolume       // 10
        };

        using Parameters = infrasonic::ParameterRegistry<Parameter>;

        Parameters params_;

        void registerParams(infrasonic::FeedbackSynth::Engine& engine);
    };

    // The firmware has these as statics; the Engine is large, so it lives on the heap.
    std::unique_ptr<infrasonic::FeedbackSynth::Engine> engine_;
    Controls                                           controls_;
    daisysp::Limiter                                   limiter_[2];
    bool                                               initialised_ = false;
};

} // namespace audreyref
