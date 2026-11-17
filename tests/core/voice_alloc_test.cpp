// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M1-B acceptance criterion 4: AudreyVoice::process() (including every
// ScriptControlSource::read, ParameterRegistry::UpdateNormalized and Process, the
// std::function handlers, the engine and the limiters) never calls operator new.
// This binary replaces the global operator new/delete and counts calls while a
// flag is set.
//
// Also: creating, initialising (twice), running and destroying an AudreyVoice 20
// times leaves no allocation live (LeakSanitizer is not available with Apple
// clang, so live allocations are counted directly, as in tests/smoke).

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "control/ScriptControlSource.h"
#include "core/AudreyVoice.h"

namespace
{
std::atomic<long> g_live{0};
std::atomic<long> g_whileCounting{0};
std::atomic<bool> g_counting{false};

void note()
{
    g_live.fetch_add(1, std::memory_order_relaxed);
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

void countedFree(void* p) noexcept
{
    if(p == nullptr)
        return;
    g_live.fetch_sub(1, std::memory_order_relaxed);
    std::free(p);
}

int g_failures = 0;
void check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok)
        ++g_failures;
}

// Every knob ramps or jumps, the switch toggles, all within the run.
const char* kScript = "0    FeedbackGain 0.9\n"
                      "0    Frequency 0.8 0.5\n"
                      "0    FeedbackBody 0.5 0.5\n"
                      "0    FeedbackLPFCutoff 0.3 0.5\n"
                      "0    FeedbackHPFCutoff 0.6 0.5\n"
                      "0    ReverbMix 0.5 0.5\n"
                      "0    ReverbDecay 0.9 0.5\n"
                      "0    EchoDelaySend 0.8 0.5\n"
                      "0    EchoDelayTime 0.6 0.5\n"
                      "0    EchoDelayFeedback 1.0 0.5\n"
                      "0    OutputVolume 1.0 0.5\n"
                      "0.3  switch 1\n"
                      "0.6  switch 0\n"
                      "0.7  FeedbackGain 1.0\n";
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
void  operator delete(void* p) noexcept { countedFree(p); }
void  operator delete[](void* p) noexcept { countedFree(p); }
void  operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void  operator delete(void* p, std::align_val_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::align_val_t) noexcept { countedFree(p); }
void  operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }

int main()
{
    using namespace audreybrick;

    // The counter works.
    {
        // A direct ::operator new call (a new-expression may be elided by the optimiser).
        g_counting  = true;
        void* probe = ::operator new(16);
        g_counting  = false;
        const long seen = g_whileCounting.exchange(0);
        ::operator delete(probe);
        check(seen == 1, "the counting operator new sees an allocation made while counting");

        // init() is expected to allocate (engine, delay lines, reverb, registry, handlers).
        AudreyVoice voice;
        g_counting = true;
        voice.init();
        g_counting           = false;
        const long initCount = g_whileCounting.exchange(0);
        std::printf("init(): operator new calls = %ld\n", initCount);
        check(initCount > 0, "the counter sees the allocations made by init()");
    }

    // process() never allocates.
    {
        constexpr std::size_t kFrames = AudreyVoice::kSampleRate; // 1 s
        std::vector<float>    in(kFrames, 0.01f), outL(kFrames), outR(kFrames);
        ScriptControlSource   script;
        std::string           err;
        if(!script.loadString(kScript, err))
        {
            std::printf("[FAIL] script: %s\n", err.c_str());
            return 1;
        }
        AudreyVoice voice;
        voice.init();

        const std::size_t chunks[] = {1, 3, 4, 7, 64, 257, 256, 2};
        std::size_t       pos = 0, c = 0, calls = 0;
        g_counting                = true;
        while(pos < kFrames)
        {
            std::size_t n = chunks[c++ % (sizeof chunks / sizeof chunks[0])];
            if(n > kFrames - pos)
                n = kFrames - pos;
            voice.process(script, (calls % 5 == 4) ? nullptr : in.data() + pos, outL.data() + pos,
                          outR.data() + pos, n);
            pos += n;
            ++calls;
        }
        g_counting       = false;
        const long count = g_whileCounting.exchange(0);
        std::printf("process(): %zu calls, %zu frames, %lld control blocks, operator new calls = %ld\n", calls,
                    kFrames, static_cast<long long>(script.blockIndex()), count);
        check(count == 0, "process() and every ControlSource::read perform no operator new");
    }

    // Lifecycle: no leak over 20 create / init / init / process / destroy cycles.
    {
        const long liveBefore = g_live.load();
        for(int cycle = 0; cycle < 20; ++cycle)
        {
            auto voice = std::make_unique<AudreyVoice>();
            voice->init();
            voice->init();
            ScriptControlSource script;
            std::string         err;
            script.loadString(kScript, err);
            std::vector<float> l(4801), r(4801);
            voice->process(script, nullptr, l.data(), r.data(), l.size());
        }
        const long liveAfter = g_live.load();
        std::printf("lifecycle: live allocations before=%ld after=%ld\n", liveBefore, liveAfter);
        check(liveAfter == liveBefore, "20 create/init/init/process/destroy cycles leave nothing live");
    }

    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
