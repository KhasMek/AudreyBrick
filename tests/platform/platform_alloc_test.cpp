// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Criterion 5: the callback body, AudioRender::render(), performs no operator new,
// including the flush-to-zero check and set (the FZ bit is cleared between some
// calls so the set path runs while counting), the chunked path for calls
// larger than its capacity, and the CPU clock reads, with commands arriving from
// a ControlModel between calls. Same technique as tests/core/voice_alloc_test.cpp
// and tests/control/control_alloc_test.cpp: this binary replaces the global
// operator new/delete and counts calls while a flag is set.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "platform/AudioRender.h"
#include "platform/Denormals.h"
#include "platform_test_common.h"

namespace
{
std::atomic<long> g_whileCounting{0};
std::atomic<bool> g_counting{false};

void note()
{
    if(g_counting.load(std::memory_order_relaxed))
        g_whileCounting.fetch_add(1, std::memory_order_relaxed);
}

void* countedAlloc(std::size_t size)
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    note();
    return p;
}

void* countedAlignedAlloc(std::size_t size, std::size_t alignment)
{
    void* p = nullptr;
    if(alignment < sizeof(void*))
        alignment = sizeof(void*);
    if(posix_memalign(&p, alignment, size == 0 ? 1 : size) != 0)
        throw std::bad_alloc();
    note();
    return p;
}

std::uint64_t steadyTicks()
{
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

// A clock that advances one second per read: every callback "takes" a second, far
// longer than its buffer, so the overrun counter path runs on every call.
std::uint64_t slowTicks()
{
    static std::uint64_t t = 0;
    t += 1000000000u;
    return t;
}
} // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void* operator new(std::size_t size, std::align_val_t a) { return countedAlignedAlloc(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a) { return countedAlignedAlloc(size, static_cast<std::size_t>(a)); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p)
        note();
    return p;
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept { return operator new(size, t); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void  operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

int main()
{
    using namespace audreybrick;
    using ptest::check;

    {
        g_counting  = true;
        void* probe = ::operator new(16);
        g_counting  = false;
        const long seen = g_whileCounting.exchange(0);
        ::operator delete(probe);
        check(seen == 1, "the counting operator new sees an allocation made while counting");
    }

    const auto ev = ptest::loadRiseScript();
    using Period  = std::chrono::steady_clock::period;
    const double ticksPerSecond = static_cast<double>(Period::den) / static_cast<double>(Period::num);

    for(const std::size_t frames : {std::size_t{256}, std::size_t{5000}})
    {
        ptest::Rig  rig;
        AudioRender render(*rig.voice, *rig.src, AudioRender::FlushToZero::Enable, &steadyTicks, ticksPerSecond);
        render.prepare(256);
        std::vector<float> out(2 * frames);
        long               allocs = 0;
        const std::size_t  total  = 6 * AudreyVoice::kSampleRate; // 6 s: the whole script
        for(std::size_t pos = 0; pos < total; pos += frames)
        {
            rig.advanceTo(ev, static_cast<double>(pos) / AudreyVoice::kSampleRate); // not counted (UI side)
            if(pos / frames % 50 == 0)
                setFlushToZero(false); // the next render() sets it again
            g_counting = true;
            render.render(out.data(), frames);
            g_counting = false;
            allocs += g_whileCounting.exchange(0);
        }
        setFlushToZero(false); // the render ran on this thread
        std::printf("render(%zu frames): %llu callbacks, %llu commands processed, operator new calls = %ld\n", frames,
                    static_cast<unsigned long long>(render.callbacks()),
                    static_cast<unsigned long long>(rig.src->commandsProcessed()), allocs);
        check(rig.src->commandsProcessed() > 0, "commands arrived during the run");
        check(allocs == 0, "AudioRender::render performs no operator new (" + std::to_string(frames) + "-frame calls)");
        std::printf("  overruns with the real clock: %llu\n", static_cast<unsigned long long>(render.overruns()));
    }

    // The overrun path (a callback slower than its buffer): counted on every call,
    // still no operator new.
    {
        ptest::Rig  rig;
        AudioRender render(*rig.voice, *rig.src, AudioRender::FlushToZero::Enable, &slowTicks, 1e9);
        render.prepare(256);
        std::vector<float> out(2 * 256);
        long               allocs = 0;
        for(int i = 0; i < 200; ++i)
        {
            g_counting = true;
            render.render(out.data(), 256);
            g_counting = false;
            allocs += g_whileCounting.exchange(0);
        }
        setFlushToZero(false);
        check(render.overruns() == 200 && render.callbacks() == 200,
              "every slow callback counted as an overrun (" + std::to_string(render.overruns()) + " of 200)");
        check(allocs == 0, "the overrun path performs no operator new");
    }
    return ptest::finish();
}
