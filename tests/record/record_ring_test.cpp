// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// REC: the ring and the tap (record/RecordRing.h, record/RecordTap.h).
//   - wrap-around: blocks that straddle the end come out in order and intact;
//   - a full ring refuses a whole block (nothing written, nothing partial), the
//     tap counts its frames as dropped and records nothing while disarmed;
//   - a block larger than the ring is never written;
//   - threads (the ThreadSanitizer build runs this): a producer offering blocks
//     of varying sizes with running sample numbers, a consumer draining at its
//     own pace: what comes out is the produced sequence minus whole dropped
//     blocks, queued + dropped = offered;
//   - arm / disarm / waitIdle under a running producer: after waitIdle() the
//     ring receives nothing more (the stop is exact at a block boundary).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "record/RecordRing.h"
#include "record/RecordTap.h"
#include "record_test_common.h"

using namespace audreybrick;
using rtest::check;

namespace
{
std::vector<float> block(std::size_t frames, float start)
{
    std::vector<float> b(2 * frames);
    for(std::size_t i = 0; i < frames; ++i)
    {
        b[2 * i]     = start + static_cast<float>(i);
        b[2 * i + 1] = -(start + static_cast<float>(i));
    }
    return b;
}

void singleThreaded()
{
    RecordRing ring(1000);
    check(ring.capacity() == 1000 && ring.fill() == 0, "ring: capacity 1000 frames, empty");
    std::vector<float> out(2 * 1000);
    float              next = 0.0f, expect = 0.0f;
    int                bad  = 0;
    // 300-frame blocks pushed, 250 read each round: the indices wrap many times.
    for(int round = 0; round < 40; ++round)
    {
        const std::vector<float> b = block(300, next);
        if(ring.pushBlock(b.data(), 300))
            next += 300.0f;
        const std::size_t n = ring.read(out.data(), 250);
        for(std::size_t i = 0; i < n; ++i, expect += 1.0f)
            bad += (out[2 * i] != expect || out[2 * i + 1] != -expect) ? 1 : 0;
    }
    std::size_t n;
    while((n = ring.read(out.data(), 1000)) > 0)
        for(std::size_t i = 0; i < n; ++i, expect += 1.0f)
            bad += (out[2 * i] != expect || out[2 * i + 1] != -expect) ? 1 : 0;
    check(bad == 0 && expect == next && next > 3000.0f,
          "ring: wrap-around keeps every frame in order (" + std::to_string(static_cast<int>(next)) + " frames)");

    RecordRing small(512);
    const std::vector<float> b300 = block(300, 0.0f);
    check(small.pushBlock(b300.data(), 300), "ring: a 300-frame block fits in 512");
    check(!small.pushBlock(b300.data(), 300) && small.fill() == 300,
          "ring: a second 300-frame block does not fit: refused whole, fill unchanged (no partial block)");
    const std::vector<float> b212 = block(212, 300.0f);
    check(small.pushBlock(b212.data(), 212) && small.fill() == 512, "ring: a block that fits exactly is taken");
    const std::vector<float> big = block(600, 0.0f);
    RecordRing               empty(512);
    check(!empty.pushBlock(big.data(), 600) && empty.fill() == 0, "ring: a block larger than the ring is never written");
    check(small.discard() == 512 && small.fill() == 0, "ring: discard() empties it");

    RecordTap tap(512);
    tap.offer(b300.data(), 300);
    check(tap.fill() == 0 && tap.framesQueued() == 0 && tap.framesDropped() == 0, "tap: disarmed, nothing recorded");
    tap.arm();
    tap.offer(b300.data(), 300);
    tap.offer(b300.data(), 300); // does not fit
    tap.offer(b212.data(), 212);
    check(tap.framesQueued() == 512 && tap.framesDropped() == 300 && tap.fill() == 512,
          "tap: armed; a block that does not fit is dropped whole and counted (queued 512, dropped 300)");
    tap.disarm();
    tap.offer(b212.data(), 212);
    check(tap.framesQueued() == 512 && tap.framesDropped() == 300, "tap: disarmed again: nothing more, nothing counted");
    tap.arm();
    check(tap.framesQueued() == 0 && tap.framesDropped() == 0, "tap: arm() resets the counters");
    check(tap.waitIdle(0.1), "tap: waitIdle() with no block in progress returns at once");
}

void threaded()
{
    // Producer: blocks of 64..512 frames with running numbers. Consumer: reads in
    // chunks of 1000 with naps, so the 4096-frame ring fills and drops blocks.
    RecordTap               tap(4096);
    std::atomic<bool>       done{false};
    std::atomic<std::uint64_t> offered{0};
    std::atomic<std::uint64_t> sequenceOk{1};
    tap.arm();
    std::thread producer([&] {
        std::vector<float> b(2 * 512);
        std::uint64_t      n = 0;
        for(int i = 0; i < 20000; ++i)
        {
            const std::size_t frames = 64 + static_cast<std::size_t>((i * 37) % 449);
            for(std::size_t f = 0; f < frames; ++f)
            {
                b[2 * f]     = static_cast<float>((n + f) % 1000000);
                b[2 * f + 1] = static_cast<float>(i % 1000);
            }
            const std::uint64_t q = tap.framesQueued();
            tap.offer(b.data(), frames);
            // Only this thread writes: queued moved by `frames` or not at all.
            if(tap.framesQueued() == q + frames)
                n += frames;
            offered.fetch_add(frames);
        }
        done.store(true);
    });
    std::vector<float> out(2 * 1000);
    std::uint64_t      got = 0;
    std::uint64_t      expectNext = 0;
    bool               ok = true;
    for(;;)
    {
        const bool        fin = done.load();
        const std::size_t n   = tap.ring().read(out.data(), 1000);
        for(std::size_t i = 0; i < n; ++i)
        {
            const auto v = static_cast<std::uint64_t>(out[2 * i]);
            if(v != expectNext % 1000000)
                ok = false;
            ++expectNext;
        }
        got += n;
        if(fin && n == 0)
            break;
        if(n < 1000)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    producer.join();
    sequenceOk.store(ok ? 1 : 0);
    std::printf("  threads: offered %llu, queued %llu, dropped %llu, read %llu\n",
                static_cast<unsigned long long>(offered.load()), static_cast<unsigned long long>(tap.framesQueued()),
                static_cast<unsigned long long>(tap.framesDropped()), static_cast<unsigned long long>(got));
    check(sequenceOk.load() == 1, "threads: the frames read are the accepted blocks, in order, none torn or repeated");
    check(tap.framesQueued() + tap.framesDropped() == offered.load() && got == tap.framesQueued(),
          "threads: queued + dropped = offered, and everything queued was read");
}

void stopUnderLoad()
{
    // A producer offering 128-frame blocks as fast as it can; the consumer arms,
    // lets it run, disarms, waits for idle, drains: nothing may arrive afterwards,
    // and what arrived is whole blocks.
    RecordTap         tap(1 << 16);
    std::atomic<bool> stop{false};
    std::thread       producer([&] {
        std::vector<float> b(2 * 128, 0.5f);
        while(!stop.load())
            tap.offer(b.data(), 128);
    });
    std::vector<float> out(2 * 4096);
    int                late = 0, partial = 0;
    for(int round = 0; round < 50; ++round)
    {
        tap.arm();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
        tap.disarm();
        const bool idle = tap.waitIdle(1.0);
        std::size_t drained = 0, n;
        while((n = tap.ring().read(out.data(), 4096)) > 0)
            drained += n;
        partial += (drained % 128 != 0 || !idle) ? 1 : 0;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        late += tap.ring().fill() != 0 ? 1 : 0;
        tap.ring().discard();
    }
    stop.store(true);
    producer.join();
    check(late == 0, "stop: after disarm() and waitIdle() nothing more reaches the ring (50 rounds)");
    check(partial == 0, "stop: every recorded stretch is whole 128-frame blocks");
}
} // namespace

int main()
{
    singleThreaded();
    threaded();
    stopUnderLoad();
    return rtest::finish();
}
