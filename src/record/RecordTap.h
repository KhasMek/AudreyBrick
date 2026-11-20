// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// RecordTap: the audio-thread end of the recording (docs/briefs/REC.md). SDL-free.
// It owns the ring (record/RecordRing.h) and the "armed" flag.
//
//   offer(block)  audio thread, once per callback with the finished interleaved
//                 output block (AudioRender::render(), after the meters). If armed:
//                 the whole block goes into the ring, or, if it does not fit, none
//                 of it and the frames are counted as dropped. Otherwise nothing.
//                 No allocation, lock, log, I/O or wait.
//   arm()         writer thread, once the file is open: resets the counters, then
//                 sets the flag. The next block offered is the first recorded.
//   disarm()      any thread (the UI on Start + Y, the writer on its own stops).
//   waitIdle()    writer thread, after disarm(): returns once no offer() that saw
//                 the flag set is still writing, so after it the ring receives
//                 nothing more and can be drained to the end. It only waits for a
//                 block copy in progress (microseconds); it gives up after the
//                 timeout (false) so a stuck audio thread cannot hang the writer.
//
// Why waitIdle() is exact: offer() stores busy = true, then loads armed; disarm()
// stores armed = false, then (in waitIdle()) the writer loads busy; all four are
// sequentially consistent. So either the writer sees busy set (and waits until
// that offer() has finished, pushed block included), or that offer() sees armed
// false and pushes nothing. Recorded blocks are therefore always whole callback
// blocks, at the start and at the end.
//
// Counters (relaxed; the UI reads them for the elapsed time and the drop count):
// frames queued (pushed into the ring since arm()) and frames dropped (whole
// blocks that did not fit).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "record/RecordRing.h"

namespace audreybrick
{

class RecordTap
{
  public:
    // A ring of `ringFrames` stereo frames. Allocates.
    explicit RecordTap(std::size_t ringFrames) : ring_(ringFrames) {}
    RecordTap(const RecordTap&)            = delete;
    RecordTap& operator=(const RecordTap&) = delete;

    // --- Audio thread ------------------------------------------------------------------
    void offer(const float* interleaved, std::size_t frames) noexcept
    {
        busy_.store(true, std::memory_order_seq_cst);
        if(armed_.load(std::memory_order_seq_cst) && frames > 0)
        {
            if(ring_.pushBlock(interleaved, frames))
                queued_.fetch_add(frames, std::memory_order_relaxed);
            else
                dropped_.fetch_add(frames, std::memory_order_relaxed);
        }
        busy_.store(false, std::memory_order_seq_cst);
    }

    // --- Writer thread -------------------------------------------------------------------
    void arm() noexcept
    {
        queued_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
        armed_.store(true, std::memory_order_seq_cst);
    }
    bool waitIdle(double timeoutSeconds) noexcept;
    RecordRing& ring() noexcept { return ring_; }

    // --- Any thread ------------------------------------------------------------------------
    void disarm() noexcept { armed_.store(false, std::memory_order_seq_cst); }
    bool armed() const noexcept { return armed_.load(std::memory_order_relaxed); }
    std::uint64_t framesQueued() const noexcept { return queued_.load(std::memory_order_relaxed); }
    std::uint64_t framesDropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    std::size_t   capacity() const noexcept { return ring_.capacity(); }
    std::size_t   fill() const noexcept { return ring_.fill(); }

  private:
    RecordRing                 ring_;
    std::atomic<bool>          armed_{false};
    std::atomic<bool>          busy_{false};
    std::atomic<std::uint64_t> queued_{0};
    std::atomic<std::uint64_t> dropped_{0};

    static_assert(std::atomic<bool>::is_always_lock_free, "the armed flag must be lock-free");
};

} // namespace audreybrick
