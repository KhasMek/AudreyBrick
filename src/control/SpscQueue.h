// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// SpscQueue: a bounded single-producer, single-consumer ring buffer.
//
//   - Capacity is fixed at construction, which is the only allocation.
//   - push() is called by exactly one thread (the producer), pop() by exactly one
//     thread (the consumer). Both are wait-free: a fixed number of steps, no locks,
//     no retries. push() returns false when the queue is full; pop() returns false
//     when it is empty.
//   - The element type must be trivially copyable.
//
// Ordering: the producer writes the slot, then publishes the new tail with
// release; the consumer reads the tail with acquire before reading the slot, and
// publishes the new head with release so the producer (acquire) does not reuse a
// slot that is still being read.

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace audreybrick
{

template <typename T>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue elements must be trivially copyable");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "SpscQueue needs lock-free size_t atomics");

  public:
    // Holds up to `capacity` elements (at least 1). Allocates.
    explicit SpscQueue(std::size_t capacity)
    : slots_(capacity < 1 ? 2 : capacity + 1), buf_(new T[slots_]())
    {
    }

    SpscQueue(const SpscQueue&)            = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    std::size_t capacity() const noexcept { return slots_ - 1; }

    // Producer thread only.
    bool push(const T& v) noexcept
    {
        const std::size_t t    = tail_.load(std::memory_order_relaxed);
        const std::size_t next = advance(t);
        if(next == head_.load(std::memory_order_acquire))
            return false;
        buf_[t] = v;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer thread only.
    bool pop(T& out) noexcept
    {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if(h == tail_.load(std::memory_order_acquire))
            return false;
        out = buf_[h];
        head_.store(advance(h), std::memory_order_release);
        return true;
    }

  private:
    std::size_t advance(std::size_t i) const noexcept { return i + 1 == slots_ ? 0 : i + 1; }

    static constexpr std::size_t kCacheLine = 64;

    const std::size_t    slots_; // capacity + 1: one slot stays empty to tell full from empty
    std::unique_ptr<T[]> buf_;
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; // next slot to read (consumer)
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; // next slot to write (producer)
};

} // namespace audreybrick
