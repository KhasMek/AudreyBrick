// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Denormals.h: on aarch64, setFlushToZero sets and clears the FPCR FZ bit for the
// calling thread only (a thread that is already running is unaffected),
// flushToZeroEnabled reports it, and a denormal result is flushed while it is set.
// Elsewhere, the documented no-op.

#include <atomic>
#include <cstdio>
#include <thread>

#include "platform/Denormals.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::atomic<bool> st_go{false};
std::atomic<bool> st_done{false};
std::atomic<bool> st_other{true};
} // namespace

int main()
{
    std::printf("kFlushToZeroSupported = %s\n", kFlushToZeroSupported ? "true" : "false");
    if(!kFlushToZeroSupported)
    {
        check(!setFlushToZero(true), "setFlushToZero returns false (no-op)");
        check(!flushToZeroEnabled(), "flushToZeroEnabled is false (no-op)");
        return ptest::finish();
    }

    check(!flushToZeroEnabled(), "FZ is clear at start");

    // Started before FZ is set; reads its own FZ bit when told to.
    std::thread existing([] {
        while(!st_go.load())
            std::this_thread::yield();
        st_other = flushToZeroEnabled();
        st_done  = true;
    });

    volatile float tiny = 1.0e-38f; // normal; half of it is denormal
    volatile float half = 0.5f;
    {
        const float r = tiny * half;
        check(r != 0.0f, "FZ clear: a denormal result is kept");
    }

    check(setFlushToZero(true), "setFlushToZero(true) returns true");
    check(flushToZeroEnabled(), "FZ set after setFlushToZero(true)");
    {
        const float r = tiny * half;
        check(r == 0.0f, "FZ set: a denormal result is flushed to zero");
    }

    // A thread that already runs is unaffected: the FPCR is per thread. (A thread
    // created after the change may inherit it: observed on Linux aarch64, not on
    // macOS. Informational below.)
    st_go = true;
    while(!st_done.load())
        std::this_thread::yield();
    existing.join();
    check(!st_other, "an already running thread's FZ is unaffected (the FPCR is per thread)");

    bool        inherited = false;
    std::thread fresh([&] { inherited = flushToZeroEnabled(); });
    fresh.join();
    std::printf("(informational) a thread created while FZ is set starts with FZ %s\n",
                inherited ? "set (inherited)" : "clear (not inherited)");

    check(setFlushToZero(false), "setFlushToZero(false) returns true");
    check(!flushToZeroEnabled(), "FZ clear after setFlushToZero(false)");
    {
        const float r = tiny * half;
        check(r != 0.0f, "FZ clear again: denormal kept");
    }
    return ptest::finish();
}
