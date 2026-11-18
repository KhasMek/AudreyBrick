// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ui::Renderer::draw() performs no operator new after the first frame: 1000
// frames of a changing snapshot (tabs, X, maximum, values, peaks, CPU, dropped
// commands, controller) drawn with the software renderer onto a surface. Same
// technique as tests/platform/platform_alloc_test.cpp: this binary replaces the
// global operator new/delete and counts calls while a flag is set.
//
// Not counted: SDL's own allocations, which go through SDL_malloc/malloc (its
// render command queue, for example). They are SDL's business; this test checks
// only that our drawing code does not allocate.

#include <SDL.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "ui/Renderer.h"
#include "ui_test_common.h"

namespace
{
std::atomic<long> g_newCalls{0};
std::atomic<bool> g_counting{false};

void* countedAlloc(std::size_t size)
{
    if(g_counting.load(std::memory_order_relaxed))
        g_newCalls.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    return p;
}
} // namespace

void* operator new(std::size_t size)
{
    return countedAlloc(size);
}
void* operator new[](std::size_t size)
{
    return countedAlloc(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch(...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch(...)
    {
        return nullptr;
    }
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

int main(int, char**)
{
    using namespace audreybrick;
    using uitest::check;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ui::kScreenWidth, ui::kScreenHeight, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* sr = surf != nullptr ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    check(sr != nullptr, std::string("software renderer created") + (sr ? "" : std::string(": ") + SDL_GetError()));
    if(sr == nullptr)
        return uitest::finish();

    std::vector<uitest::NamedSnapshot> snaps = uitest::allSnapshots();
    auto                               ui    = std::make_unique<ui::Renderer>();

    // First frame: creates the atlas (allowed to allocate).
    ui->draw(sr, snaps[0].snap);
    SDL_RenderFlush(sr);
    check(ui->hasAtlas(), "atlas created on the first frame");

    g_newCalls = 0;
    g_counting = true;
    double t   = 1.0;
    for(int i = 0; i < 1000; ++i)
    {
        UiSnapshot& s = snaps[static_cast<std::size_t>(i) % snaps.size()].snap;
        t += 1.0 / 60.0;
        s.timeSeconds         = t;
        s.peakL               = static_cast<float>(i % 101) / 100.0f;
        s.peakR               = static_cast<float>((i * 7) % 103) / 100.0f;
        s.cpuLoad             = static_cast<float>(i % 37) / 37.0f;
        s.commandsDropped     = static_cast<std::uint64_t>(i / 3);
        s.tab                 = static_cast<Tab>(i % static_cast<int>(kTabCount));
        s.xHeld               = (i / 7) % 2 == 1;
        s.feedbackMax         = (i / 11) % 2 == 1;
        s.controllerConnected = (i / 13) % 2 == 1;
        s.knob[0].engine      = 16.0f + static_cast<float>(i % 560) / 10.0f;
        ui->draw(sr, s);
        SDL_RenderFlush(sr);
    }
    g_counting = false;
    check(g_newCalls.load() == 0,
          "draw(): no operator new over 1000 frames after the first (" + std::to_string(g_newCalls.load()) + ")");

    ui->release();
    SDL_DestroyRenderer(sr);
    SDL_FreeSurface(surf);
    return uitest::finish();
}
