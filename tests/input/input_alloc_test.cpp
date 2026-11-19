// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Acceptance criterion 3, the allocation half: the audio path (AudioRender::render
// with the input stage attached, the voice and the stage) performs no operator new
// and no operator delete
//   - with a file playing (256- and 5000-frame callbacks, the second split into
//     chunks by the render),
//   - through hand-overs of new buffers (and an empty one) while it plays,
//   - while the gain ramps and jumps, and while the source is switched on and off;
//   - M2b-A2: through crossfades between files, ONCE files ending, restart()s and
//     LOOP/ONCE changes (each is seen to happen during the counted calls).
// The buffers it is done with come back through reclaim() and are freed on this
// (the "UI") side, outside the counted calls. Same technique as
// tests/platform/platform_alloc_test.cpp, but the counter is thread-local, so only
// the calls on the counting thread are seen.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "control/ButtonControlSource.h"
#include "core/AudreyVoice.h"
#include "input/InputFiles.h"
#include "input/InputStage.h"
#include "input_test_common.h"
#include "platform/AudioRender.h"

namespace
{
thread_local bool t_counting = false;
std::atomic<long> g_news{0};
std::atomic<long> g_deletes{0};

void* countedAlloc(std::size_t size)
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    if(t_counting)
        g_news.fetch_add(1, std::memory_order_relaxed);
    return p;
}
void countedFree(void* p) noexcept
{
    if(p != nullptr && t_counting)
        g_deletes.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p != nullptr && t_counting)
        g_news.fetch_add(1, std::memory_order_relaxed);
    return p;
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept { return operator new(size, t); }
void  operator delete(void* p) noexcept { countedFree(p); }
void  operator delete[](void* p) noexcept { countedFree(p); }
void  operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void  operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }

int main()
{
    using namespace audreybrick;
    using itest::check;

    {
        t_counting = true;
        void* probe = ::operator new(16);
        ::operator delete(probe);
        t_counting = false;
        check(g_news.exchange(0) == 1 && g_deletes.exchange(0) == 1, "the counters see a new and a delete while counting");
    }

    auto makeBuffer = [](std::size_t n, double hz) {
        auto b = std::make_unique<InputBuffer>();
        b->samples.resize(n);
        for(std::size_t i = 0; i < n; ++i)
            b->samples[i] = static_cast<float>(0.5 * std::sin(2.0 * itest::kPi * hz * static_cast<double>(i) / 48000.0));
        return b;
    };

    for(const std::size_t frames : {std::size_t{256}, std::size_t{5000}})
    {
        auto       voice = std::make_unique<AudreyVoice>();
        voice->init();
        auto       src   = std::make_unique<ButtonControlSource>();
        InputStage stage;
        AudioRender render(*voice, *src, AudioRender::FlushToZero::LeaveAlone);
        render.attachInput(&stage);
        render.prepare(256);
        std::vector<float> out(2 * frames);
        long               news = 0, deletes = 0, reclaimed = 0, superseded = 0;
        long               crossfading = 0, endedSeen = 0, xfSwitches = 0;
        std::uint64_t      lastSwitches = 0;
        bool               wasActive    = false;
        float              peak = 0.0f;
        stage.offer(makeBuffer(30000, 220));
        stage.setSource(InputSource::File);
        const std::size_t calls    = 12 * 48000 / frames; // 12 s
        std::size_t       lastSlot = 0;
        for(std::size_t i = 0; i < calls; ++i)
        {
            // The UI side, not counted: hand-overs, gain, source, reclaim. Events
            // every 0.1 s of audio (slots), whatever the callback size.
            const std::size_t slot = i * frames / 4800;
            if(slot != lastSlot)
            {
                lastSlot = slot;
                if(slot % 5 == 1)
                    superseded += stage.offer(makeBuffer(9000 + 100 * (slot % 7), 200.0 + slot)) ? 1 : 0;
                if(slot % 23 == 12)
                    superseded += stage.offer(std::make_unique<InputBuffer>()) ? 1 : 0; // "nothing"
                if(slot % 7 == 3)
                    stage.setGainNorm(1.0f); // a jump
                if(slot % 11 == 4)
                    stage.setSource(InputSource::Off);
                if(slot % 11 == 6)
                    stage.setSource(InputSource::File);
                if(slot % 13 == 2)
                    stage.setPlayMode(InputPlayMode::Once); // 9000-sample files end within 0.2 s
                if(slot % 13 == 8)
                    stage.setPlayMode(InputPlayMode::Loop);
                if(slot % 9 == 7)
                    stage.restart();
            }
            else
            {
                stage.setGainNorm(static_cast<float>(0.5 + 0.5 * std::sin(0.05 * static_cast<double>(i)))); // a ramp
            }
            while(auto b = stage.reclaim())
                ++reclaimed;

            t_counting = true;
            render.render(out.data(), frames);
            t_counting = false;
            news += g_news.exchange(0);
            deletes += g_deletes.exchange(0);
            peak = std::max(peak, stage.peak().take());
            crossfading += stage.crossfading() ? 1 : 0;
            // A switch while sound went on: a crossfade (for 5000-frame calls it
            // starts and ends inside one call).
            const std::uint64_t sw = stage.switches();
            xfSwitches += sw != lastSwitches && wasActive && stage.active() ? 1 : 0;
            lastSwitches = sw;
            wasActive    = stage.active();
            endedSeen += stage.ended() ? 1 : 0;
        }
        while(auto b = stage.reclaim())
            ++reclaimed;
        std::printf("render(%zu frames) with the input: %zu callbacks, %llu switches, %ld buffers reclaimed and freed "
                    "here, %ld superseded; input peak %.3f; operator new %ld, operator delete %ld\n",
                    frames, calls, static_cast<unsigned long long>(stage.switches()), reclaimed, superseded,
                    static_cast<double>(peak), news, deletes);
        std::printf("  calls ending inside a crossfade %ld, switches while sounding (crossfades) %ld, calls ending with the "
                    "file ended (ONCE) %ld, restarts %llu\n",
                    crossfading, xfSwitches, endedSeen, static_cast<unsigned long long>(stage.restarts()));
        check(stage.switches() > 10 && reclaimed > 10 && peak > 0.1f, "files played and were switched during the run");
        check(xfSwitches > 3 && (frames >= kInputFileCrossfadeFrames || crossfading > 0) && endedSeen > 0 &&
                  stage.restarts() > 3,
              "crossfades, ONCE endings and restarts happened inside the counted calls");
        check(news == 0 && deletes == 0, "render with a file playing, hand-overs, gain ramps and source changes: no "
                                         "operator new or delete (" + std::to_string(frames) + "-frame calls)");
    }
    // --- A float file of absurd values (3e38, -3e38): clamped at load, so the meter
    //     and the output stay finite even at +12 dB of gain -------------------------------------------
    {
        itest::TempDir d("huge");
        itest::writeWav(d.file("huge.wav"), 48000, 2, itest::Fmt::Float32, 4800,
                        [](std::size_t i, int) { return i % 2 == 0 ? 3.0e38 : -3.0e38; });
        LoadedInput f;
        std::string reason, detail;
        const bool  ok = loadInputFile(d.path(), "huge.wav", nullptr, f, reason, detail);
        auto        voice = std::make_unique<AudreyVoice>();
        voice->init();
        auto        src = std::make_unique<ButtonControlSource>();
        InputStage  stage;
        AudioRender render(*voice, *src, AudioRender::FlushToZero::Enable);
        render.attachInput(&stage);
        render.prepare(256);
        if(ok)
            stage.offer(std::move(f.buffer));
        stage.setGainNorm(1.0f); // x4
        stage.setSource(InputSource::File);
        std::vector<float> out(512);
        bool               finite = true;
        float              inPeak = 0.0f, outPeak = 0.0f;
        for(int i = 0; i < 400; ++i)
        {
            render.render(out.data(), 256);
            for(float v : out)
            {
                finite  = finite && std::isfinite(v);
                outPeak = std::max(outPeak, std::fabs(v));
            }
            const float p = stage.peak().take();
            finite        = finite && std::isfinite(p);
            inPeak        = std::max(inPeak, p);
        }
        std::printf("huge float file: input peak %.3f (8 x 4 = 32 at most), output peak %.4f\n", static_cast<double>(inPeak),
                    static_cast<double>(outPeak));
        check(ok && finite && inPeak <= 32.0f && inPeak > 1.0f && render.peakL().take() < 2.0f,
              "a float file holding +/-3e38: the input meter reads a finite value (<= 32 after +12 dB) and the output "
              "stays finite");
    }

    // --- Informational: the cost of the input stage per 256-frame callback ---------------------------
    // The stage's pull() alone, and AudioRender::render with and without a file
    // playing (the difference is the stage plus the engine's extra work, which is
    // none: the engine always adds its input sample). Steady clock, Release build
    // numbers only mean something; nothing is checked.
    {
        using Clock = std::chrono::steady_clock;
        constexpr int kCalls = 6000; // 32 s of audio
        InputStage    stage;
        stage.prepare(4096);
        stage.offer(makeBuffer(48000 * 10, 220));
        stage.setSource(InputSource::File);
        stage.setGainNorm(0.6f);
        for(int i = 0; i < 100; ++i)
            stage.pull(256);
        auto   t0  = Clock::now();
        double sum = 0.0;
        for(int i = 0; i < kCalls; ++i)
        {
            const float* p = stage.pull(256);
            sum += p != nullptr ? p[i % 256] : 0.0;
            if(i % 500 == 250)
                stage.setGainNorm(i % 1000 == 250 ? 0.8f : 0.6f); // keep the smoother busy
        }
        const double pullNs = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / kCalls;

        // M2b-A2: pull() during a crossfade (two buffers read per sample): 18 calls of
        // 256 (4608 samples) right after each hand-over are all inside the 4800-sample
        // crossfade; only those are timed.
        double xfNs   = 0.0;
        long   xfCall = 0;
        for(int round = 0; round < 300; ++round)
        {
            stage.offer(makeBuffer(48000, 200.0 + round));
            stage.pull(1); // picks it up: the crossfade starts
            const auto x0 = Clock::now();
            for(int i = 0; i < 18; ++i)
            {
                const float* p = stage.pull(256);
                sum += p != nullptr ? p[i] : 0.0;
            }
            xfNs += std::chrono::duration<double, std::nano>(Clock::now() - x0).count();
            xfCall += stage.crossfading() ? 18 : 0;
            for(int i = 0; i < 2; ++i)
                stage.pull(256);
            while(auto b = stage.reclaim())
            {
            }
        }
        xfNs /= 300 * 18;

        auto timeRender = [&](bool withFile) {
            auto voice = std::make_unique<AudreyVoice>();
            voice->init();
            auto        src = std::make_unique<ButtonControlSource>();
            InputStage  st;
            AudioRender render(*voice, *src, AudioRender::FlushToZero::Enable);
            if(withFile)
                render.attachInput(&st);
            render.prepare(256);
            st.offer(makeBuffer(48000 * 10, 220));
            st.setSource(InputSource::File);
            std::vector<float> out(512);
            for(int i = 0; i < 200; ++i)
                render.render(out.data(), 256);
            const auto t = Clock::now();
            for(int i = 0; i < kCalls; ++i)
                render.render(out.data(), 256);
            return std::chrono::duration<double, std::nano>(Clock::now() - t).count() / kCalls;
        };
        const double off = timeRender(false);
        const double on  = timeRender(true);
        const double buf = 256.0 / 48000.0 * 1e9;
        std::printf("cost per 256-frame callback (%.0f us of audio), this machine: InputStage::pull %.0f ns (%.3f %% of the "
                    "buffer), during a crossfade %.0f ns (%.3f %%; %ld of %d timed calls inside it); render without input "
                    "%.0f ns (%.2f %%), with a file playing %.0f ns (%.2f %%) [%g]\n",
                    buf / 1000.0, pullNs, 100.0 * pullNs / buf, xfNs, 100.0 * xfNs / buf, xfCall, 300 * 18, off,
                    100.0 * off / buf, on, 100.0 * on / buf, sum * 0.0);
    }
    return itest::finish();
}
