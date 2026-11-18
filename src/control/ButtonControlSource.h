// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ButtonControlSource: the audio-side half of the button control scheme
// (docs/briefs/M2.md, M2-A). The UI thread (ControlModel) sends commands and a
// held word; read() turns them into one ControlFrame per control block.
//
// Threads:
//   - UI thread (the only producer): post(), setHeld().
//   - Audio thread (the only consumer): read(). It owns the knob values.
//   - Any thread: the published*() getters, which read atomics written by read().
//
// What read() does once per control block (1/12000 s):
//   1. Applies at most tuning::kMaxCommandsPerBlock queued commands, in order.
//   2. Loads the held word once.
//   3. For each d-pad axis with a knob and a direction, moves that knob. The speed
//      starts at kRampStartRate when the axis first appears active (or its knob
//      or direction changes) and rises linearly to kRampEndRate over
//      kRampAccelSeconds, counted in control blocks. Fine scales the speed by
//      kFineRampScale without restarting the acceleration. FeedbackGain is never
//      moved by an axis (the d-pad never binds it; an axis naming it is ignored).
//   4. Moves the stored feedback at the constant feedback ramp rate.
//   5. Fills the frame: the stored values (clamped to 0..1), except FeedbackGain
//      is 1.0 while feedback maximum is held; delaySwitch = latch XOR held.
//   6. Publishes the values for the UI.
//
// Time is counted in control blocks only; nothing here reads a clock. The same
// commands and held words at the same block indices give the same frames.
//
// Values are kept in double so slow ramps (fine: about 1e-6 per block) are not
// distorted by float rounding; the frame receives them rounded to float.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "control/ControlCommands.h"
#include "control/ControlTuning.h"
#include "control/SpscQueue.h"
#include "core/ControlSource.h"

namespace audreybrick
{

class ButtonControlSource final : public ControlSource
{
  public:
    // Allocates the command queue. Every knob starts at its ParamTable default
    // position, the switch latch off, nothing held.
    explicit ButtonControlSource(std::size_t queueCapacity = tuning::kCommandQueueCapacity);

    ButtonControlSource(const ButtonControlSource&)            = delete;
    ButtonControlSource& operator=(const ButtonControlSource&) = delete;

    // --- UI thread (single producer) ---------------------------------------
    // Queues a command. Returns false, and the command is lost, if the queue is
    // full; the caller counts drops. Never waits.
    bool post(const ControlCommand& c) noexcept { return queue_.push(c); }
    // Replaces the held word (layout in ControlCommands.h).
    void setHeld(std::uint32_t word) noexcept { held_.store(word, std::memory_order_relaxed); }

    // --- Audio thread (single consumer) --------------------------------------
    // No allocation, lock, I/O, logging or waiting; bounded work.
    void read(ControlFrame& out) noexcept override;

    // --- Any thread: published state ------------------------------------------
    // Each getter returns a value written at the end of some read(). The values
    // are independent relaxed atomics: two getters called one after the other may
    // come from different (normally adjacent) blocks, so the UI must not assume
    // that, say, publishedKnob(FeedbackGain), publishedStoredFeedback() and
    // publishedFeedbackMax() describe the same block. Each single value is never
    // torn and is always a value read() really produced. For display this is
    // harmless. Before the first read() they hold the start-up state.
    //
    // The knob value as it went into the last frame (FeedbackGain reads 1.0 while
    // feedback maximum is active).
    float publishedKnob(Knob k) const noexcept;
    // The stored feedback value, which maximum does not change.
    //
    // Presets and the freeze slot (M5) must save this stored feedback, not
    // publishedKnob(FeedbackGain), which reads 1.0 while maximum is held. Note also
    // that SetAll does not stop ramps in progress: a d-pad or feedback ramp that is
    // held when a preset loads keeps moving from the loaded value.
    float         publishedStoredFeedback() const noexcept { return storedFeedback_.load(std::memory_order_relaxed); }
    bool          publishedFeedbackMax() const noexcept { return feedbackMax_.load(std::memory_order_relaxed); }
    bool          publishedSwitch() const noexcept { return switchOn_.load(std::memory_order_relaxed); }
    bool          publishedSwitchLatch() const noexcept { return switchLatch_.load(std::memory_order_relaxed); }
    std::uint64_t commandsProcessed() const noexcept { return processed_.load(std::memory_order_relaxed); }

  private:
    struct AxisRamp
    {
        Knob         knob   = Knob::Count;
        std::int8_t  dir    = 0;
        std::int64_t blocks = 0; // control blocks since the hold (re)started
    };

    void apply(const ControlCommand& c) noexcept;
    void publish(const ControlFrame& f) noexcept;

    SpscQueue<ControlCommand>  queue_;
    std::atomic<std::uint32_t> held_{0};

    // Audio thread state.
    double        value_[kKnobCount]; // stored knob positions; [FeedbackGain] is the stored feedback
    bool          latch_ = false;
    AxisRamp      axis_[2];
    std::uint64_t processedLocal_ = 0;

    // Published (written by the audio thread, read by anyone).
    std::atomic<float>         knob_[kKnobCount];
    std::atomic<float>         storedFeedback_{0.0f};
    std::atomic<bool>          feedbackMax_{false};
    std::atomic<bool>          switchOn_{false};
    std::atomic<bool>          switchLatch_{false};
    std::atomic<std::uint64_t> processed_{0};

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the held word must be lock-free");
    static_assert(std::atomic<float>::is_always_lock_free, "published values must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free, "published flags must be lock-free");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the command count must be lock-free");
};

} // namespace audreybrick
