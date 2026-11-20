// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// AudioRender: the body of the audio callback, without SDL (docs/briefs/M2.md,
// M2-B). AudioHost's SDL callback forwards to render(); tests call render()
// directly, without an audio device.
//
// What render() does, and nothing else:
//   1. At the start of every call: reads the calling thread's FZ bit and, if
//      asked to (FlushToZero::Enable) and it is clear, sets it; then publishes
//      whether FZ is set. Checking every call covers a callback thread that SDL
//      recreates (for example after a device change); the cost is one register
//      read per callback.
//   2. Reads the clock (if one was given).
//   3. In chunks of at most capacity() frames: InputStage::pull() for the chunk
//      if a stage is attached (M2b-A), then AudreyVoice::process with the control
//      source and what pull() returned as the input (nullptr without a stage, or
//      while the stage has nothing to give: the voice then runs exactly as with
//      no input) into the two preallocated channel buffers, then interleaves them
//      into the output and takes the per-channel peaks.
//   3b. REC: if a recording tap is attached, offers it the finished
//      interleaved block (`out`, exactly what goes to the device): copied into
//      its ring only while it is armed (RecordTap::offer: a memory copy and
//      a few atomics). It reads the output and never changes it.
//   4. Reads the clock again and records the load (time / buffer duration); a
//      callback that took longer than its buffer lasts (load above 1, a likely
//      underrun) also increments the overrun counter (one relaxed atomic add).
//   5. Publishes peaks, load and counters through atomics (the load also goes
//      into CpuMeter's histogram: one relaxed counter store, M3-B).
// No allocation, lock, I/O, logging, exception or wait.
//
// The input stage (M2b-A): attachInput() before prepare(); prepare() also
// prepares the stage for the same capacity (and, M2b-B, tells it the device's
// buffer size, `frames`, which sets the live input's target fill). The stage must outlive every
// render() call.
//
// Threads: prepare() and the constructor on the main thread before audio starts;
// render() on the audio thread only (one thread at a time); the meter getters from
// any thread.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/AudreyVoice.h"
#include "core/ControlSource.h"
#include "input/InputStage.h"
#include "record/RecordTap.h"
#include "platform/Meters.h"

namespace audreybrick
{

class AudioRender
{
  public:
    enum class FlushToZero
    {
        Enable,     // make sure FZ is set on the calling thread at every callback (the app)
        LeaveAlone, // do not touch the FPCR (tests that need bit-exact comparisons)
    };

    // State of flush-to-zero on the audio thread, as of the last callback.
    enum class FtzState : int
    {
        NotYetKnown = 0, // render() has not run yet
        On          = 1, // the FZ bit was set during the last callback
        Off         = 2, // the FZ bit was clear (LeaveAlone on a clear thread, or not supported)
    };

    // A monotonic clock: ticks, and ticks per second. SDL_GetPerformanceCounter /
    // SDL_GetPerformanceFrequency in the app. nullptr = no CPU measurement.
    using ClockFn = std::uint64_t (*)();

    // The minimum capacity prepare() allocates, in frames.
    static constexpr std::size_t kMinCapacity = 4096;

    AudioRender(AudreyVoice& voice, ControlSource& controls, FlushToZero ftz, ClockFn clock = nullptr,
                double clockTicksPerSecond = 0.0) noexcept;

    AudioRender(const AudioRender&)            = delete;
    AudioRender& operator=(const AudioRender&) = delete;

    // Allocates the two channel buffers for max(frames, kMinCapacity) frames. Call
    // before audio starts, never while render() may run.
    void prepare(std::size_t frames);
    std::size_t capacity() const noexcept { return capacity_; }

    // Feeds the voice from `stage` (nullptr: no input, as before M2b). Call before
    // prepare(), never while render() may run.
    void attachInput(InputStage* stage) noexcept { input_ = stage; }
    InputStage* input() const noexcept { return input_; }

    // REC: offers every finished output block to `tap` (nullptr: none). Call
    // before audio starts, never while render() may run. The tap must outlive
    // every render() call.
    void attachRecorder(RecordTap* tap) noexcept { record_ = tap; }

    // Audio thread. Writes `frames` interleaved stereo float frames (2 * frames
    // floats). Before prepare(), writes silence.
    void render(float* interleaved, std::size_t frames) noexcept;

    // --- Any thread ----------------------------------------------------------
    MaxHold&       peakL() noexcept { return peak_[0]; }
    MaxHold&       peakR() noexcept { return peak_[1]; }
    CpuMeter&      cpu() noexcept { return cpu_; }
    FtzState       ftzState() const noexcept { return static_cast<FtzState>(ftz_.load(std::memory_order_relaxed)); }
    std::uint64_t  framesRendered() const noexcept { return frames_.load(std::memory_order_relaxed); }
    std::uint64_t  callbacks() const noexcept { return callbacks_.load(std::memory_order_relaxed); }
    // The largest frame count one render() call has been given.
    std::uint64_t  largestCallback() const noexcept { return largest_.load(std::memory_order_relaxed); }
    // Callbacks whose wall time exceeded their buffer's duration (likely underruns).
    // Counted only with a clock.
    std::uint64_t  overruns() const noexcept { return overruns_.load(std::memory_order_relaxed); }

  private:
    AudreyVoice&   voice_;
    ControlSource& controls_;
    FlushToZero    ftzMode_;
    ClockFn        clock_;
    double         ticksPerSecond_;
    InputStage*    input_ = nullptr;
    RecordTap*     record_ = nullptr;

    std::unique_ptr<float[]> left_;
    std::unique_ptr<float[]> right_;
    std::size_t              capacity_ = 0;

    MaxHold                    peak_[2];
    CpuMeter                   cpu_;
    std::atomic<int>           ftz_{static_cast<int>(FtzState::NotYetKnown)};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> largest_{0};
    std::atomic<std::uint64_t> overruns_{0};

    static_assert(std::atomic<int>::is_always_lock_free, "the FTZ state must be lock-free");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "counters must be lock-free");
};

} // namespace audreybrick
