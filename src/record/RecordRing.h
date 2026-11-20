// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// RecordRing: the recording's lock-free single-producer, single-consumer ring of
// interleaved stereo float frames (docs/briefs/REC.md). SDL-free.
//
//   - The capacity (in frames) is fixed at construction, which is the only
//     allocation. The app sizes it for about 4 seconds at the output rate.
//   - pushBlock() (the producer: the audio thread) writes a whole block or
//     nothing: if the free space is smaller than the block, it returns false and
//     writes nothing. A memory copy (in at most two pieces, at the wrap) and one
//     release store of the write index.
//   - read() (the consumer: the writer thread) copies out up to the requested
//     number of frames and publishes the new read index; discard() drops
//     everything that is in the ring.
//   - Indices are 64-bit frame counters that only grow (2^64 frames at 48 kHz
//     is millions of years); the slot is index % capacity.
//
// Ordering: the producer writes the slots, then stores the write index with
// release; the consumer loads it with acquire before reading the slots and
// stores its read index with release after reading; the producer loads that
// with acquire before reusing slots. Both functions are wait-free; no lock, no
// allocation, no system call.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace audreybrick
{

class RecordRing
{
  public:
    static constexpr std::size_t kChannels = 2;

    // Holds up to `capacityFrames` frames (at least 1). Allocates.
    explicit RecordRing(std::size_t capacityFrames)
    : cap_(capacityFrames < 1 ? 1 : capacityFrames), buf_(new float[cap_ * kChannels]())
    {
    }

    RecordRing(const RecordRing&)            = delete;
    RecordRing& operator=(const RecordRing&) = delete;

    std::size_t capacity() const noexcept { return cap_; }

    // Frames in the ring now (approximate from any thread other than the two ends).
    std::size_t fill() const noexcept
    {
        const std::uint64_t w = write_.load(std::memory_order_acquire);
        const std::uint64_t r = read_.load(std::memory_order_acquire);
        return w >= r ? static_cast<std::size_t>(w - r) : 0;
    }

    // Producer only. Writes all `frames` frames of `interleaved` (2 * frames
    // floats) or nothing. Returns false (nothing written) if they do not fit.
    bool pushBlock(const float* interleaved, std::size_t frames) noexcept
    {
        const std::uint64_t w = write_.load(std::memory_order_relaxed);
        const std::uint64_t r = read_.load(std::memory_order_acquire);
        const std::size_t   used = static_cast<std::size_t>(w - r);
        if(frames > cap_ - used)
            return false;
        const std::size_t at    = static_cast<std::size_t>(w % cap_);
        const std::size_t first = std::min(frames, cap_ - at);
        std::memcpy(buf_.get() + at * kChannels, interleaved, first * kChannels * sizeof(float));
        if(frames > first)
            std::memcpy(buf_.get(), interleaved + first * kChannels, (frames - first) * kChannels * sizeof(float));
        write_.store(w + frames, std::memory_order_release);
        return true;
    }

    // Consumer only. Copies up to `maxFrames` frames into `dst` (2 * maxFrames
    // floats) and returns how many.
    std::size_t read(float* dst, std::size_t maxFrames) noexcept
    {
        const std::uint64_t r = read_.load(std::memory_order_relaxed);
        const std::uint64_t w = write_.load(std::memory_order_acquire);
        const std::size_t   n = std::min(static_cast<std::size_t>(w - r), maxFrames);
        const std::size_t   at    = static_cast<std::size_t>(r % cap_);
        const std::size_t   first = std::min(n, cap_ - at);
        std::memcpy(dst, buf_.get() + at * kChannels, first * kChannels * sizeof(float));
        if(n > first)
            std::memcpy(dst + first * kChannels, buf_.get(), (n - first) * kChannels * sizeof(float));
        read_.store(r + n, std::memory_order_release);
        return n;
    }

    // Consumer only. Drops everything in the ring now; returns how many frames.
    std::size_t discard() noexcept
    {
        const std::uint64_t r = read_.load(std::memory_order_relaxed);
        const std::uint64_t w = write_.load(std::memory_order_acquire);
        read_.store(w, std::memory_order_release);
        return static_cast<std::size_t>(w - r);
    }

  private:
    static constexpr std::size_t kCacheLine = 64;

    const std::size_t        cap_;
    std::unique_ptr<float[]> buf_;
    alignas(kCacheLine) std::atomic<std::uint64_t> write_{0}; // frames written (producer)
    alignas(kCacheLine) std::atomic<std::uint64_t> read_{0};  // frames read (consumer)

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the ring needs lock-free 64-bit atomics");
};

} // namespace audreybrick
