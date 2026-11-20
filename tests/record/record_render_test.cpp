// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// REC: recording does not change the output by one bit, and records exactly it.
// tests/platform/rise.txt (feedback rising) is rendered for 5 s through two
// AudioRenders driven by identical rigs, in 256-frame callbacks (and once in 64
// and 1000): one with no tap, one with a tap that is armed from 1.0 s to 4.0 s.
// The two outputs are compared bit for bit, and the ring's contents (drained by
// the test, playing the writer) are compared bit for bit with the output blocks
// rendered while armed: whole callback blocks, from the first armed block to the
// last. Also: the tap attached but never armed records nothing and changes
// nothing; and render() with the tap armed (including blocks dropped because the
// ring is full) performs no operator new (replaced and counted here).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <cstring>
#include <string>
#include <vector>

#include "platform/AudioRender.h"
#include "platform_test_common.h"
#include "record/RecordTap.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::atomic<long> g_news{0};
std::atomic<bool> g_counting{false};
void*             counted(std::size_t n)
{
    void* p = std::malloc(n == 0 ? 1 : n);
    if(p == nullptr)
        throw std::bad_alloc();
    if(g_counting.load(std::memory_order_relaxed))
        g_news.fetch_add(1, std::memory_order_relaxed);
    return p;
}
} // namespace

void* operator new(std::size_t n)
{
    return counted(n);
}
void* operator new[](std::size_t n)
{
    return counted(n);
}
// The whole replaceable set, so every new matches the free() in every delete
// (libstdc++'s stable_sort uses the nothrow form; ASan checks the pairing).
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try
    {
        return counted(n);
    }
    catch(...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept
{
    return operator new(n, t);
}
void* operator new(std::size_t n, std::align_val_t a)
{
    void*       p     = nullptr;
    std::size_t align = static_cast<std::size_t>(a) < sizeof(void*) ? sizeof(void*) : static_cast<std::size_t>(a);
    if(posix_memalign(&p, align, n == 0 ? 1 : n) != 0)
        throw std::bad_alloc();
    if(g_counting.load(std::memory_order_relaxed))
        g_news.fetch_add(1, std::memory_order_relaxed);
    return p;
}
void* operator new[](std::size_t n, std::align_val_t a)
{
    return operator new(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace
{
bool sameBits(float a, float b)
{
    std::uint32_t x, y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

void compare(std::size_t bufferFrames, bool arm)
{
    const auto ev = ptest::loadRiseScript();
    ptest::Rig a, b;
    AudioRender plain(*a.voice, *a.src, AudioRender::FlushToZero::LeaveAlone);
    AudioRender tapped(*b.voice, *b.src, AudioRender::FlushToZero::LeaveAlone);
    RecordTap   tap(48000 * 4);
    tapped.attachRecorder(&tap);
    plain.prepare(bufferFrames);
    tapped.prepare(bufferFrames);

    const std::size_t  total = 5 * 48000;
    std::vector<float> outA(2 * bufferFrames), outB(2 * bufferFrames);
    std::vector<float> expected; // the output blocks rendered while armed
    std::vector<float> got;      // what the ring delivered
    std::vector<float> chunk(2 * 4096);
    std::size_t        mismatches = 0;
    float              peak       = 0.0f;
    bool               armed      = false;
    for(std::size_t pos = 0; pos < total;)
    {
        const std::size_t n = std::min(bufferFrames, total - pos);
        const double      t = static_cast<double>(pos) / 48000.0;
        a.advanceTo(ev, t);
        b.advanceTo(ev, t);
        if(arm && !armed && t >= 1.0 && t < 4.0)
        {
            tap.arm();
            armed = true;
        }
        if(armed && t >= 4.0)
        {
            tap.disarm();
            armed = false;
        }
        plain.render(outA.data(), n);
        g_counting.store(true);
        tapped.render(outB.data(), n);
        g_counting.store(false);
        for(std::size_t i = 0; i < 2 * n; ++i)
        {
            mismatches += sameBits(outA[i], outB[i]) ? 0 : 1;
            peak = std::max(peak, std::fabs(outA[i]));
        }
        if(armed)
            expected.insert(expected.end(), outB.begin(), outB.begin() + static_cast<std::ptrdiff_t>(2 * n));
        std::size_t r;
        while((r = tap.ring().read(chunk.data(), 4096)) > 0)
            got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(2 * r));
        pos += n;
    }
    const std::string name = "buffer " + std::to_string(bufferFrames) + (arm ? ", armed 1 s to 4 s" : ", never armed");
    check(mismatches == 0 && peak > 0.01f, name + ": output bit-identical with and without the tap (peak " +
                                               std::to_string(peak) + ")");
    if(arm)
    {
        std::size_t diff = got.size() == expected.size() ? 0 : 1;
        for(std::size_t i = 0; i < std::min(got.size(), expected.size()); ++i)
            diff += sameBits(got[i], expected[i]) ? 0 : 1;
        const std::size_t frames = got.size() / 2;
        check(diff == 0 && frames > 0 && frames % std::min<std::size_t>(bufferFrames, 1u << 30) == 0 &&
                  tap.framesQueued() == frames && tap.framesDropped() == 0,
              name + ": the ring holds exactly the armed output blocks, whole blocks (" + std::to_string(frames) +
                  " frames)");
        const double secs = static_cast<double>(frames) / 48000.0;
        check(secs > 2.98 && secs < 3.03, name + ": about 3 s recorded (" + std::to_string(secs) + " s)");
    }
    else
    {
        check(got.empty() && tap.framesQueued() == 0, name + ": nothing recorded");
    }
}
} // namespace

void noAllocation()
{
    const auto ev = ptest::loadRiseScript();
    ptest::Rig rig;
    AudioRender render(*rig.voice, *rig.src, AudioRender::FlushToZero::LeaveAlone);
    RecordTap   tap(48000); // 1 s: fills after 1 s and then drops blocks
    render.attachRecorder(&tap);
    render.prepare(256);
    std::vector<float> out(2 * 256);
    tap.arm();
    g_news.store(0);
    for(std::size_t pos = 0; pos < 3 * 48000; pos += 256)
    {
        rig.advanceTo(ev, static_cast<double>(pos) / 48000.0);
        g_counting.store(true);
        render.render(out.data(), 256);
        g_counting.store(false);
    }
    check(g_news.load() == 0 && tap.framesDropped() > 0 && tap.framesQueued() > 0,
          "render() with the tap armed, filling and then dropping: no operator new (" + std::to_string(g_news.load()) +
              ")");
}

int main()
{
    {
        g_counting.store(true);
        void* probe = ::operator new(16);
        g_counting.store(false);
        ::operator delete(probe);
        check(g_news.exchange(0) == 1, "the counting operator new sees an allocation made while counting");
    }
    g_news.store(0);
    compare(256, true);
    compare(64, true);
    compare(1000, true);
    compare(256, false);
    check(g_news.load() == 0, "no operator new inside any tapped render() call above");
    noAllocation();
    return ptest::finish();
}
