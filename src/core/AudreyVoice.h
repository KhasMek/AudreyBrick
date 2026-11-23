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
    // MOD-A: the frame's knobMod offsets are applied after the parameter
    // smoother (applyKnob); the frame's delaySwitchMod is ORed with delaySwitch.
    // A source that writes 0 and false gives exactly the unmodulated output.
    //
    // Before init() has been called, writes silence and does not read controls.
    // Does not allocate, lock, log, do I/O or wait.
    void process(ControlSource& controls, const float* inMono,
                 float* outL, float* outR, std::size_t frames) noexcept;

    // MOD-A: the largest move, in octaves per control block, of a MODULATED loop
    // filter cutoff (FeedbackLPFCutoff, FeedbackHPFCutoff); see applyKnob.
    // Audio-rate modulation of the vendored biquads (transposed direct form II,
    // coefficients recomputed every block) without a limit can make them
    // unstable; the limit is chosen from scans (docs/PARAMETERS.md section 6).
    static constexpr float kCutoffSlewOctavesPerBlock = 0.035f;
    // Tests and scans only: another limit (0 or less: no limit). Not while
    // process() runs.
    void setCutoffSlewLimit(float octavesPerBlock) noexcept { cutoffSlew_ = octavesPerBlock; }
    // Tests: the cutoff last given to the engine (0 lowpass, 1 highpass; Hz) and
    // whether the limiter is engaged (the modulated path).
    float cutoffGiven(std::size_t f) const noexcept { return cutLast_[f < 2 ? f : 0]; }
    bool  cutoffLimiting(std::size_t f) const noexcept { return cutActive_[f < 2 ? f : 0]; }

  private:
    using Engine = infrasonic::FeedbackSynth::Engine;

    void startControlBlock(ControlSource& controls) noexcept;
    // The registry's handler for knob i: the engine setter, given the smoothed
    // value, or with a non-zero frame_.knobMod[i] the modulated value
    // (modulateEngineValue, ParamTable.h). MOD-A.
    //
    // The two loop filter cutoffs are slew-limited on the modulated path: the
    // cutoff given to the setter moves by at most cutoffSlew_ octaves per block
    // towards the modulated value. The limit starts from the last cutoff given
    // (the smoothed one, when modulation switches on), and the handler returns
    // to the unmodulated path (the smoothed value, bit for bit) only once the
    // offset is 0 again and the limited cutoff is within one step of the
    // smoothed one.
    void applyKnob(std::size_t i, float smoothed) noexcept;
    float slewCutoff(std::size_t f, Knob knob, float smoothed, float offset) noexcept;

    std::unique_ptr<Engine>       engine_;
    std::unique_ptr<KnobRegistry> params_;
    daisysp::Limiter              limiter_[2];
    ControlFrame                  frame_{};
    bool                          switchOn_ = false; // this block's switch: manual OR modulated
    // MOD-A, the cutoff slew limit, per filter (0 lowpass, 1 highpass).
    float                         cutoffSlew_ = kCutoffSlewOctavesPerBlock;
    float                         cutLast_[2] = {0.0f, 0.0f}; // the cutoff last given to the setter, Hz
    float                         cutLog_[2]  = {0.0f, 0.0f}; // its log2, while limiting
    bool                          cutActive_[2] = {false, false}; // limiting (off: the unmodulated path)
    float                         cutLogMin_[2] = {0.0f, 0.0f};   // log2 of the range ends (init())
    float                         cutLogMax_[2] = {0.0f, 0.0f};
    int                           blockPos_ = 0; // samples of the current block already processed
};

} // namespace audreybrick
