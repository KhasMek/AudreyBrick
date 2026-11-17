// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// AudreyVoice: the Audrey II firmware's audio callback without the hardware
// (docs/PLAN.md section 3). Contains logic transcribed from upstream
// FeedbackSynthControls.cpp and FeedbackSynth_main.cpp; see AudreyVoice.cpp for
// the upstream notice.

#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include <daisysp.h>

#include "FeedbackSynthEngine.h"
#include "ParameterRegistry.h"
#include "core/ControlSource.h"

namespace audreybrick
{

using KnobRegistry = infrasonic::ParameterRegistry<Knob>;
using KnobHandlers = std::array<KnobRegistry::Handler, kKnobCount>;

// Controls::registerParams: registers the eleven knobs with the ParamTable values,
// in enum order, each with its handler. `registry` must already be Init'ed with
// the control rate. Allocates (init only).
void registerKnobParams(KnobRegistry& registry, const KnobHandlers& handlers);

// Controls::Update: UpdateNormalized for the eleven knobs from one frame, in enum
// order, with the ReverbDecay tension and the delay switch scaling. Does not
// allocate.
void updateKnobParams(KnobRegistry& registry, const ControlFrame& frame) noexcept;

class AudreyVoice
{
  public:
    static constexpr int kSampleRate   = 48000;
    static constexpr int kControlBlock = 4; // samples, as the firmware

    AudreyVoice();
    ~AudreyVoice();
    AudreyVoice(const AudreyVoice&)            = delete;
    AudreyVoice& operator=(const AudreyVoice&) = delete;

    // Allocates; call before audio starts, never on the audio thread.
    // Calling init() again discards the engine, the parameter registry, the
    // limiters and the control-block position, and starts over exactly as a
    // newly constructed and initialised voice. It must not run concurrently
    // with process().
    void init();

    // Any frame count. Controls are read and applied every kControlBlock samples,
    // counted across calls: a call that ends inside a block finishes that block
    // at the start of the next call with the same control values, and the
    // limiters see the same sample sequence as in one long call.
    //
    // inMono may be nullptr (treated as silence). A non-finite input sample is
    // replaced by 0 before the engine. This is the one deliberate addition to the
    // firmware's behaviour: a NaN or inf entering the feedback loop would stay
    // there for good.
    //
    // Before init() has been called, writes silence and does not read controls.
    // Does not allocate, lock, log, do I/O or wait.
    void process(ControlSource& controls, const float* inMono,
                 float* outL, float* outR, std::size_t frames) noexcept;

  private:
    using Engine = infrasonic::FeedbackSynth::Engine;

    void startControlBlock(ControlSource& controls) noexcept;

    std::unique_ptr<Engine>       engine_;
    std::unique_ptr<KnobRegistry> params_;
    daisysp::Limiter              limiter_[2];
    ControlFrame                  frame_{};
    int                           blockPos_ = 0; // samples of the current block already processed
};

} // namespace audreybrick
