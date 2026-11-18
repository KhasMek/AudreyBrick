// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Flush-to-zero control for the calling thread (docs/PLATFORM_NOTES.md section 6,
// rule 5; docs/briefs/M2.md, "Design shared by all three tasks").
//
// On aarch64 (macOS and Linux, compiler macro __aarch64__) this sets or clears the
// FZ bit (bit 24) of the FPCR. With FZ set, denormal results of single and double
// precision arithmetic are flushed to zero, and denormal inputs are treated as
// zero. The FPCR is per thread, so the audio thread must call setFlushToZero(true)
// itself; AudioRender checks and sets it at the start of every callback. A thread
// created while its creator has FZ set may start with FZ set too: on Linux
// aarch64 a new thread started with its creator's FZ bit (observed in the Docker
// container, gcc 14 / glibc 2.41); on macOS it started clear.
//
// On every other architecture these functions are a documented no-op:
// kFlushToZeroSupported is false, setFlushToZero() does nothing and returns false,
// and flushToZeroEnabled() returns false. (The target and the development machine
// are both aarch64; an x86 MXCSR version is not needed.)
//
// Real-time safe: two system-register instructions, no call, no allocation.

#pragma once

#include <cstdint>

namespace audreybrick
{

#if defined(__aarch64__)
constexpr bool kFlushToZeroSupported = true;

namespace detail
{
constexpr std::uint64_t kFpcrFzBit = std::uint64_t{1} << 24;

inline std::uint64_t readFpcr() noexcept
{
    std::uint64_t v;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
    return v;
}

inline void writeFpcr(std::uint64_t v) noexcept
{
    __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
}
} // namespace detail

// Sets (on = true) or clears the FZ bit for the calling thread. Returns true.
inline bool setFlushToZero(bool on) noexcept
{
    const std::uint64_t v = detail::readFpcr();
    detail::writeFpcr(on ? (v | detail::kFpcrFzBit) : (v & ~detail::kFpcrFzBit));
    return true;
}

// Whether the FZ bit is set for the calling thread.
inline bool flushToZeroEnabled() noexcept
{
    return (detail::readFpcr() & detail::kFpcrFzBit) != 0;
}
#else
// Not aarch64: flush-to-zero is not controlled here (no-op by design).
constexpr bool kFlushToZeroSupported = false;

inline bool setFlushToZero(bool) noexcept
{
    return false;
}

inline bool flushToZeroEnabled() noexcept
{
    return false;
}
#endif

} // namespace audreybrick
