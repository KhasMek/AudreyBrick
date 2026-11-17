// M1-A lifecycle test: create, Init, process and destroy the Engine 20 times in
// one process. Must not crash, and must not leak.
//
// Run under AddressSanitizer for memory errors (build/asan). LeakSanitizer is
// not available with Apple clang on macOS, so leaks are checked here directly:
// this binary replaces the global operator new/delete and counts live
// allocations. After the 20 cycles, the live count must equal the count before.
// This also proves that the shim's SDRAM::allocate pairs correctly with the
// engine's std::unique_ptr default deleter.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#include "engine_setup.h"

namespace
{
std::atomic<long> g_live{0};
std::atomic<long> g_total{0};

void* countedAlloc(std::size_t size)
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    g_live.fetch_add(1, std::memory_order_relaxed);
    g_total.fetch_add(1, std::memory_order_relaxed);
    return p;
}

void* countedAlignedAlloc(std::size_t size, std::size_t alignment)
{
    void* p = nullptr;
    if(alignment < sizeof(void*))
        alignment = sizeof(void*);
    if(posix_memalign(&p, alignment, size == 0 ? 1 : size) != 0)
        throw std::bad_alloc();
    g_live.fetch_add(1, std::memory_order_relaxed);
    g_total.fetch_add(1, std::memory_order_relaxed);
    return p;
}

void countedFree(void* p) noexcept
{
    if(p == nullptr)
        return;
    g_live.fetch_sub(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void* operator new(std::size_t size, std::align_val_t a) { return countedAlignedAlloc(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a) { return countedAlignedAlloc(size, static_cast<std::size_t>(a)); }
void  operator delete(void* p) noexcept { countedFree(p); }
void  operator delete[](void* p) noexcept { countedFree(p); }
void  operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void  operator delete(void* p, std::align_val_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::align_val_t) noexcept { countedFree(p); }
void  operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }

int main()
{
    constexpr int kCycles        = 20;
    constexpr int kFramesPerCycle = 4800; // 0.1 s

    const long liveBefore  = g_live.load();
    const long totalBefore = g_total.load();
    long       nonFinite   = 0;

    for(int cycle = 0; cycle < kCycles; ++cycle)
    {
        auto engine = std::make_unique<infrasonic::FeedbackSynth::Engine>();
        engine->Init(smoke::kSampleRate);
        smoke::setAllParameters(*engine, 6.0f);
        for(int i = 0; i < kFramesPerCycle; ++i)
        {
            float outL = 0.0f;
            float outR = 0.0f;
            engine->Process(0.0f, outL, outR);
            if(!std::isfinite(outL) || !std::isfinite(outR))
                ++nonFinite;
        }
    }

    const long liveAfter  = g_live.load();
    const long allocCount = g_total.load() - totalBefore;
    std::printf("cycles=%d allocations=%ld live before=%ld live after=%ld non-finite=%ld\n",
                kCycles, allocCount, liveBefore, liveAfter, nonFinite);

    int failures = 0;
    // Engine object + two echo delays + reverb per cycle.
    const bool allocOk = allocCount >= 4L * kCycles;
    std::printf("[%s] the counting allocator saw the engine allocations\n", allocOk ? "PASS" : "FAIL");
    failures += allocOk ? 0 : 1;
    const bool leakOk = liveAfter == liveBefore;
    std::printf("[%s] no allocation left live after %d create/destroy cycles\n", leakOk ? "PASS" : "FAIL", kCycles);
    failures += leakOk ? 0 : 1;
    const bool finiteOk = nonFinite == 0;
    std::printf("[%s] every output sample is finite\n", finiteOk ? "PASS" : "FAIL");
    failures += finiteOk ? 0 : 1;
    return failures == 0 ? 0 : 1;
}
