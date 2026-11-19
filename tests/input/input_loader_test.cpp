// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputLoader, the one background thread:
//   - not started until the first request; poll() returns nothing until the load
//     is done, then the result once;
//   - a newer request supersedes a queued one and cancels the running one: only
//     the latest request's result comes back;
//   - a failure comes back with its reason;
//   - stop() while a 5-minute file (with rate conversion) is loading returns
//     within half a second, and stop() is idempotent; request() after stop()
//     does nothing;
//   - poll() and request() never wait for a load (timed while one runs).

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "input/InputLoader.h"
#include "input/InputTypes.h"
#include "input_test_common.h"

using namespace audreybrick;
using itest::check;
using Clock = std::chrono::steady_clock;

namespace
{
double since(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

bool waitFor(InputLoader& l, InputLoadResult& r, double seconds)
{
    const auto t0 = Clock::now();
    while(since(t0) < seconds)
    {
        if(l.poll(r))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
} // namespace

int main()
{
    itest::TempDir d("loader");
    itest::writeWav(d.file("short.wav"), 48000, 1, itest::Fmt::Pcm16, 4800, itest::sine(440, 0.5, 48000));
    itest::writeWav(d.file("other.wav"), 44100, 2, itest::Fmt::Pcm16, 44100, itest::sine(440, 0.25, 44100));
    // 6 minutes at 44.1 kHz stereo: converted (not cut since the 10-minute cap of M2b-A2), a long load.
    itest::writeWav(d.file("long.wav"), 44100, 2, itest::Fmt::Pcm16, 44100 * 360, itest::sine(440, 0.25, 44100));
    itest::writeBytes(d.file("bad.wav"), {'n', 'o', 'p', 'e'});

    {
        InputLoader     l;
        InputLoadResult r;
        check(!l.started() && !l.poll(r) && !l.busy(), "no thread and nothing to poll before the first request");
        const auto id = l.request(d.path(), "short.wav");
        check(l.started() && id == 1, "the first request starts the thread (id 1)");
        const bool got = waitFor(l, r, 5.0);
        check(got && r.ok && r.id == 1 && r.name == "short.wav" && r.file.buffer && r.file.buffer->samples.size() == 4800,
              "the result arrives through poll(): short.wav, 4800 frames");
        check(!l.poll(r) && !l.busy(), "once only; not busy afterwards");

        l.request(d.path(), "bad.wav");
        InputLoadResult f;
        check(waitFor(l, f, 5.0) && !f.ok && f.reason == "NOT A WAV FILE" && !f.file.buffer,
              "a failure comes back with its reason");
    }

    // Supersede: the long load is cancelled, the queued one dropped, only the last arrives.
    {
        InputLoader l;
        l.request(d.path(), "long.wav");
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // let it start
        const auto t0  = Clock::now();
        l.request(d.path(), "other.wav");
        const auto id3 = l.request(d.path(), "short.wav");
        InputLoadResult r1;
        const bool      p = l.poll(r1);
        const double    t = since(t0);
        InputLoadResult r;
        const bool      got = waitFor(l, r, 5.0);
        check(!p && t < 0.005, "request() and poll() while a long load runs return at once (" + std::to_string(t * 1000) + " ms)");
        check(got && r.ok && r.id == id3 && r.name == "short.wav", "only the latest request's result arrives");
        InputLoadResult extra;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(!l.poll(extra), "the superseded loads never arrive");
    }

    // cancel() (M2b-A2 review, S1): the long load stops early, nothing arrives,
    // and the next request works.
    {
        InputLoader l;
        l.request(d.path(), "long.wav");
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // let it start
        const bool wasBusy = l.busy();
        const auto t0      = Clock::now();
        l.cancel();
        bool idle = false;
        while(since(t0) < 2.0 && !(idle = !l.busy()))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const double    t = since(t0);
        InputLoadResult r;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const bool none = !l.poll(r);
        std::printf("cancel() during a long load: idle after %.1f ms\n", 1000 * t);
        check(wasBusy && idle && t < 0.1 && none,
              "cancel() during a long load: it stops within 100 ms (the whole load takes about 0.5 s) and no result arrives");
        l.request(d.path(), "short.wav");
        check(waitFor(l, r, 5.0) && r.ok && r.name == "short.wav", "a request after cancel() loads normally");
        l.request(d.path(), "short.wav");
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // finished, not polled
        l.cancel();
        check(!l.poll(r), "cancel() also drops a finished result not yet polled");
    }

    // stop() during a long load.
    {
        InputLoader l;
        l.request(d.path(), "long.wav");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const bool   busy = l.busy();
        const auto   t0   = Clock::now();
        l.stop();
        const double t = since(t0);
        std::printf("stop() during a long load took %.1f ms\n", 1000 * t);
        check(busy && t < 0.5, "stop() while a long file loads joins within half a second");
        l.stop();
        check(l.request(d.path(), "short.wav") == 0, "stop() twice is fine; request() after stop() does nothing");
    }

    // The long load itself, timed (for the report).
    {
        InputLoader     l;
        const auto      t0 = Clock::now();
        l.request(d.path(), "long.wav");
        InputLoadResult r;
        const bool      got = waitFor(l, r, 120.0);
        std::printf("a 6-minute 44.1 kHz stereo file: loaded in %.2f s (loader says %.2f s), %zu frames, cut %s\n",
                    since(t0), got ? r.file.info.loadSeconds : 0.0, got && r.ok ? r.file.info.frames : 0,
                    got && r.ok && r.file.info.cut ? "yes" : "no");
        check(got && r.ok && !r.file.info.cut && r.file.info.frames == static_cast<std::size_t>(360 * 48000),
              "a 6-minute file loads whole (under the 10-minute cap since M2b-A2), converted to 48 kHz");
    }

    // Destruction while loading (the destructor stops and joins).
    {
        const auto t0 = Clock::now();
        {
            InputLoader l;
            l.request(d.path(), "long.wav");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(since(t0) < 0.6, "destroying the loader during a load returns promptly");
    }
    return itest::finish();
}
