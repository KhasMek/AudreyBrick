// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// MOD-B (docs/briefs/MOD.md): the MOD tab's panel on a UI thread against the
// modulation decorator on an audio thread, for ThreadSanitizer. The UI thread
// presses buttons (every MOD gesture: cursor, target steps and repeats, shape,
// rate and depth taps and ramps, X + left/right slot selection, Y and Y + X
// resets (MOD-D)), ticks the panel and fills a
// view from the read-back once per "frame"; the audio thread runs the
// decorator through chunks of 64 frames with an input (so the envelope and the
// InputGain path run) and reads every block. Afterwards the panel's settings
// are what the engine has (nothing lost), and every read-back value seen was
// finite.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "app/ModPanel.h"
#include "app/ModView.h"
#include "mod/ModControlSource.h"
#include "mod_test_common.h"

namespace
{
using namespace audreybrick;
using modtest::check;

bool same(const ModSlotSettings& a, const ModSlotSettings& b)
{
    return a.shape == b.shape && a.target == b.target && a.rateNorm == b.rateNorm && a.depth == b.depth &&
           a.polarity == b.polarity;
}
} // namespace

int main()
{
    modtest::FixedSource inner;
    ModControlSource     dec(inner, 8); // a small queue: refusals and resends happen
    constexpr std::size_t kChunk = 64;
    dec.prepare(kChunk);
    ModPanel panel{ModUiPort(dec)};
    // The panel starts at its defaults (1 Hz) without sending them; the
    // decorator's slots start at ModSlotSettings{} (rate 0.5). Start the engine
    // where the panel is, so a slot the random presses never touch compares
    // equal at the end (on a slow machine some slot may never be touched).
    for(std::size_t i = 0; i < kModSlots; ++i)
        dec.setSlotNow(i, panel.slot(i));

    std::atomic<bool>          stop{false};
    std::atomic<std::uint64_t> chunks{0};
    std::thread                audio([&] {
        std::vector<float> in(kChunk);
        std::uint64_t      n = 0;
        while(!stop.load(std::memory_order_acquire))
        {
            for(std::size_t i = 0; i < kChunk; ++i)
                in[i] = 0.3f * std::sin(0.01f * static_cast<float>(n * kChunk + i));
            const float* p = dec.beginChunk(in.data(), kChunk, nullptr, 0.5f);
            (void)p;
            for(std::size_t b = 0; b < kChunk / 4; ++b)
            {
                ControlFrame f;
                dec.read(f);
            }
            dec.endChunk();
            ++n;
            chunks.store(n, std::memory_order_release);
            // About a real callback's pace (a 64-frame chunk is 1.3 ms), slow
            // enough for the UI to fill the queue now and then.
            std::this_thread::sleep_for(std::chrono::microseconds(n % 64 == 0 ? 20000 : 200));
        }
    });

    std::mt19937  rng(1234);
    const Button  pool[] = {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight,
                            Button::X,      Button::Y,        Button::Select};
    bool          isDown[kButtonCount] = {};
    double        t                    = 0.0;
    int           nonFinite            = 0;
    std::uint64_t frames               = 0;
    const auto    until                = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while(std::chrono::steady_clock::now() < until || chunks.load(std::memory_order_acquire) < 200)
    {
        t += 1.0 / 60.0;
        for(int k = 0; k < 3; ++k)
        {
            const Button b = pool[rng() % (sizeof pool / sizeof pool[0])];
            const auto   i = static_cast<std::size_t>(b);
            isDown[i]      = !isDown[i];
            panel.onButton({b, isDown[i]}, t, true);
        }
        panel.tick(t);
        UiModView v;
        panel.fill(v);
        for(std::size_t i = 0; i < kModSlots; ++i)
            nonFinite += std::isfinite(v.output[i]) ? 0 : 1;
        for(std::size_t i = 0; i < kModTargetCount; ++i)
            nonFinite += std::isfinite(v.offset[i]) ? 0 : 1;
        nonFinite += std::isfinite(v.inputGainRatio) ? 0 : 1;
        ++frames;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    panel.releaseAll();
    // Let the audio side apply what is queued; the panel resends what was refused.
    for(int k = 0; k < 50; ++k)
    {
        const std::uint64_t c = chunks.load(std::memory_order_acquire);
        while(chunks.load(std::memory_order_acquire) < c + 2)
            std::this_thread::yield();
        t += 1.0 / 60.0;
        panel.tick(t);
    }
    const std::uint64_t c = chunks.load(std::memory_order_acquire);
    while(chunks.load(std::memory_order_acquire) < c + 4)
        std::this_thread::yield();
    stop.store(true, std::memory_order_release);
    audio.join();

    bool allSame = true, anyUnsent = false;
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        allSame   = allSame && same(panel.slot(i), dec.matrix().slot(i));
        anyUnsent = anyUnsent || panel.unsent(i);
    }
    std::printf("UI frames %llu, audio chunks %llu, sends accepted %llu, refused %llu, applied %llu\n",
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(chunks.load()),
                static_cast<unsigned long long>(panel.sendsAccepted()),
                static_cast<unsigned long long>(panel.sendsRefused()),
                static_cast<unsigned long long>(dec.settingsApplied()));
    check(nonFinite == 0, "every read-back value the UI saw was finite");
    check(!anyUnsent && allSame, "afterwards the engine has every slot exactly as the panel holds it");
    check(panel.sendsAccepted() > 50 && dec.settingsApplied() == panel.sendsAccepted(),
          "settings crossed threads; every accepted send was applied once");
    return modtest::finish();
}
