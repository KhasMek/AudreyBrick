// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Meters shared between the audio thread (the only writer of new readings) and
// the UI thread (the reader), through atomics only.
//
//   MaxHold:  the maximum of the values offered since the last take(). The audio
//             thread calls offer(); the UI calls take(), which returns the maximum
//             and resets it to 0. Used for the per-channel output peaks and the CPU
//             maximum.
//   CpuMeter: callback load (time spent in the callback / the buffer's duration).
//             The audio thread calls record(); the UI reads smoothed() and
//             takeMax().
//
// offer() is a compare-and-swap max with a bounded number of attempts. It uses
// compare_exchange_strong: on the Brick's Cortex-A53 (load/store-exclusive, no
// LSE atomics) the weak form may fail spuriously even without a competitor. The
// only real competitor is the reader's take() (an exchange, at most once per UI
// frame), so the first or second attempt wins in practice.
//
// What can be lost, and why it does not matter:
//   - If all kMaxAttempts attempts fail, that callback's reading is dropped. This
//     needs take() to win eight races in a row within one callback, which the
//     60 Hz reader cannot do; and the next callback offers again.
//   - A reading offered just before a take() goes to that take(); one offered just
//     after goes to the next. A reading is never counted twice and the maximum is
//     never torn. For a display meter, the worst case is a peak shown one UI frame
//     late.
// The audio thread never spins unbounded and never waits.

#pragma once

#include <atomic>
#include <cmath>

namespace audreybrick
{

class MaxHold
{
  public:
    static constexpr int kMaxAttempts = 8;

    // Audio thread. Non-finite or negative values are ignored.
    void offer(float v) noexcept
    {
        if(!(v > 0.0f) || !std::isfinite(v))
            return;
        float cur = value_.load(std::memory_order_relaxed);
        for(int i = 0; i < kMaxAttempts && v > cur; ++i)
        {
            if(value_.compare_exchange_strong(cur, v, std::memory_order_relaxed, std::memory_order_relaxed))
                return;
        }
    }

    // Reader: the maximum since the last take(), and resets it to 0.
    float take() noexcept { return value_.exchange(0.0f, std::memory_order_relaxed); }

    // Reader: the maximum since the last take(), without resetting it.
    float peek() const noexcept { return value_.load(std::memory_order_relaxed); }

  private:
    std::atomic<float> value_{0.0f};
    static_assert(std::atomic<float>::is_always_lock_free, "meters must be lock-free");
};

class CpuMeter
{
  public:
    // Smoothing time constant of smoothed(), seconds of audio.
    static constexpr double kSmoothingSeconds = 0.5;

    // Audio thread only. `load` = callback time / buffer duration (1.0 = the whole
    // buffer's time was spent in the callback). `bufferSeconds` = the buffer's
    // duration, which sets the smoothing step.
    void record(double load, double bufferSeconds) noexcept
    {
        if(!std::isfinite(load) || load < 0.0)
            return;
        const double a = 1.0 - std::exp(-bufferSeconds / kSmoothingSeconds);
        smoothedLocal_ += a * (load - smoothedLocal_);
        smoothed_.store(static_cast<float>(smoothedLocal_), std::memory_order_relaxed);
        max_.offer(static_cast<float>(load));
    }

    // Any thread: the smoothed load.
    float smoothed() const noexcept { return smoothed_.load(std::memory_order_relaxed); }
    // Reader: the maximum load since the last takeMax(), and resets it.
    float takeMax() noexcept { return max_.take(); }

  private:
    double             smoothedLocal_ = 0.0; // audio thread only
    std::atomic<float> smoothed_{0.0f};
    MaxHold            max_;
};

} // namespace audreybrick
