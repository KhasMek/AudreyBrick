// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-A: the audio path with modulation active performs no operator new
// (replaced and counted here, as in tests/record/record_render_test.cpp).
//   1. AudioRender with an input stage playing a file and the decorator with
//      all four slots active (an audio-rate sine on the lowpass, an envelope on
//      the frequency, a square on the delay switch, a sample-and-hold on the
//      input gain), settings changed from the "UI" between callbacks (queued
//      before the counted render() calls; the pushes are the UI thread's).
//      MOD-C: the envelope reads the stage's pre-gain record (AudioRender passes
//      it); the changes cycle through every shape (the ramps too) and both
//      polarities.
//   2. The render tool's path: a script with mod lines, the decorator as the
//      script's sink, at 1, 64 and 1000 frames per call.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "input/InputStage.h"
#include "mod_test_common.h"
#include "platform/AudioRender.h"

using namespace audreybrick;
using modtest::check;

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
ModSlotSettings slot(ModShape shape, ModTarget target, float rate, float depth)
{
    ModSlotSettings s;
    s.shape    = shape;
    s.target   = target;
    s.rateNorm = rate;
    s.depth    = depth;
    return s;
}

void renderPath()
{
    ScriptControlSource script = modtest::loadScript("0 FeedbackGain 0.9\n0 ReverbMix 0.3\n");
    AudreyVoice         voice;
    voice.init();
    InputStage       stage;
    ModControlSource mod(script);
    AudioRender      render(voice, script, AudioRender::FlushToZero::LeaveAlone);
    render.attachInput(&stage);
    render.attachModulation(&mod);
    render.prepare(256);
    auto buf     = std::make_unique<InputBuffer>();
    buf->samples = modtest::testInput(48000 * 2);
    stage.setGainNorm(0.5f);
    stage.offer(std::move(buf));
    stage.setSource(InputSource::File);
    mod.setModSlot(0, slot(ModShape::Sine, ModTarget::FeedbackLPFCutoff, modRateNormFromHz(300.0), 0.3f));
    mod.setModSlot(1, slot(ModShape::Envelope, ModTarget::Frequency, 0.5f, 0.2f));
    mod.setModSlot(2, slot(ModShape::Square, ModTarget::DelaySwitch, modRateNormFromHz(2.0), 1.0f));
    mod.setModSlot(3, slot(ModShape::SampleHold, ModTarget::InputGain, modRateNormFromHz(8.0), 0.4f));

    std::vector<float> out(2 * 256);
    g_news.store(0);
    for(int c = 0; c < 48000 * 4 / 256; ++c)
    {
        if(c % 100 == 50) // the "UI" changes a slot now and then (MOD-C: every shape, both polarities)
        {
            ModSlotSettings st = slot(static_cast<ModShape>(c % kModShapeCount), static_cast<ModTarget>(c % 14), 0.7f, 0.5f);
            st.polarity        = (c / 100) % 2 != 0 ? ModPolarity::Unipolar : ModPolarity::Bipolar;
            mod.setModSlot(static_cast<std::size_t>(c / 100) % kModSlots, st);
        }
        g_counting.store(true);
        render.render(out.data(), 256);
        g_counting.store(false);
        while(auto r = stage.reclaim())
        {
        }
    }
    check(g_news.load() == 0 && mod.settingsApplied() > 4,
          "AudioRender with a playing input and four active slots: no operator new (" + std::to_string(g_news.load()) +
              ")");
}

void toolPath()
{
    const std::string text = "0 FeedbackGain 0.9\n0 mod1 shape triangle\n0 mod1 target EchoDelayTime\n"
                             "0 mod1 hz 1000\n0 mod1 depth 0.4\n0.5 mod2 shape envelope\n"
                             "0.5 mod2 target OutputVolume\n0.5 mod2 depth -0.5\n1 mod1 target InputGain\n";
    const auto in = modtest::testInput(48000 * 2);
    for(std::size_t chunk : {1u, 64u, 1000u})
    {
        ScriptControlSource script = modtest::loadScript(text);
        AudreyVoice         voice;
        voice.init();
        ModControlSource mod(script);
        mod.prepare(chunk);
        mod.attachScript(script);
        std::vector<float> l(chunk), r(chunk);
        g_news.store(0);
        for(std::size_t pos = 0; pos < in.size(); pos += chunk)
        {
            const std::size_t n = std::min(chunk, in.size() - pos);
            g_counting.store(true);
            const float* ip = mod.beginChunk(in.data() + pos, n, nullptr, 1.0f);
            voice.process(mod, ip, l.data(), r.data(), n);
            mod.endChunk();
            g_counting.store(false);
        }
        check(g_news.load() == 0, "script mod lines through the decorator at " + std::to_string(chunk) +
                                      " frames per call: no operator new (" + std::to_string(g_news.load()) + ")");
    }
}
} // namespace

int main()
{
    {
        g_counting.store(true);
        void* probe = ::operator new(16);
        g_counting.store(false);
        ::operator delete(probe);
        check(g_news.exchange(0) == 1, "the counting operator new sees an allocation made while counting");
    }
    renderPath();
    toolPath();
    return modtest::finish();
}
