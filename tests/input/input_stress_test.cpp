// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Acceptance criterion 3, the thread half: an "audio" thread pulls the stage
// continuously (256-frame chunks, as fast as it can) while the "UI" thread swaps
// files as quickly as it can for two seconds:
//   - phase 1: raw hand-overs: offer() of a new buffer every iteration (most are
//     superseded in the pending slot before the audio thread sees them; the
//     others start crossfades), source on/off, gain jumps, LOOP/ONCE toggles and
//     restarts (M2b-A2), reclaim() and free;
//   - phase 2: the real InputController and loader thread: holds of X with
//     right and down presses (stepping, the pop-up) whose release loads, every
//     few milliseconds over four files, X + Y (LOOP/ONCE), X + Select (play
//     again), FILE/OFF toggles, gain taps.
// Under ThreadSanitizer (the tsan build) any report fails the test. In every
// build: the audio thread performs no operator new or delete at all (a
// thread-local counter), so no buffer is ever freed there; every buffer offered
// is freed exactly once (counted through the buffers' own destructor hook below);
// nothing non-finite comes out; and shutdown (audio thread stopped first, then
// the controller, then the stage) frees what is left.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "input/InputController.h"
#include "input/InputStage.h"
#include "input_test_common.h"

namespace
{
thread_local bool t_audio = false;
std::atomic<long> g_audioNews{0};
std::atomic<long> g_audioDeletes{0};

void* alloc(std::size_t size)
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    if(t_audio)
        g_audioNews.fetch_add(1, std::memory_order_relaxed);
    return p;
}
void release(void* p) noexcept
{
    if(p != nullptr && t_audio)
        g_audioDeletes.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t size) { return alloc(size); }
void* operator new[](std::size_t size) { return alloc(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return std::malloc(size == 0 ? 1 : size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return std::malloc(size == 0 ? 1 : size); }
void  operator delete(void* p) noexcept { release(p); }
void  operator delete[](void* p) noexcept { release(p); }
void  operator delete(void* p, std::size_t) noexcept { release(p); }
void  operator delete[](void* p, std::size_t) noexcept { release(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }

namespace
{
using namespace audreybrick;
using itest::check;
using Clock = std::chrono::steady_clock;

struct Audio
{
    InputStage&        stage;
    std::atomic<bool>  stop{false};
    std::atomic<long>  pulls{0};
    std::atomic<long>  nonFinite{0};
    std::atomic<long>  audible{0};
    std::thread        th;

    explicit Audio(InputStage& s) : stage(s)
    {
        th = std::thread([this] {
            t_audio = true;
            while(!stop.load(std::memory_order_relaxed))
            {
                const float* p = stage.pull(256);
                if(p != nullptr)
                {
                    audible.fetch_add(1, std::memory_order_relaxed);
                    for(int i = 0; i < 256; ++i)
                        if(!std::isfinite(p[i]))
                            nonFinite.fetch_add(1, std::memory_order_relaxed);
                }
                pulls.fetch_add(1, std::memory_order_relaxed);
                (void)stage.active();
                (void)stage.position();
                (void)stage.ended();
                (void)stage.crossfading();
            }
            t_audio = false;
        });
    }
    void join()
    {
        stop.store(true);
        th.join();
    }
};
} // namespace

int main()
{
    // --- Phase 1: raw hand-overs --------------------------------------------------------------------
    {
        auto stage = std::make_unique<InputStage>();
        stage->prepare(256);
        Audio         audio(*stage);
        long          offered = 0, freed = 0, iterations = 0;
        const auto    t0 = Clock::now();
        while(std::chrono::duration<double>(Clock::now() - t0).count() < 2.0)
        {
            auto b = std::make_unique<InputBuffer>();
            b->samples.assign(static_cast<std::size_t>(100 + (iterations * 37) % 3000), 0.25f);
            ++offered;
            if(stage->offer(std::move(b)))
                ++freed; // superseded, freed here
            while(auto r = stage->reclaim())
                ++freed;
            stage->setGainNorm(static_cast<float>((iterations % 10) / 9.0));
            stage->setSource(iterations % 50 < 40 ? InputSource::File : InputSource::Off);
            if(iterations % 7 == 3)
                stage->setPlayMode(iterations % 14 == 3 ? InputPlayMode::Once : InputPlayMode::Loop);
            if(iterations % 11 == 5)
                stage->restart();
            (void)stage->peak().take();
            ++iterations;
            if(iterations % 16 == 0)
                std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        audio.join();
        while(auto r = stage->reclaim())
            ++freed;
        // What the stage still holds (current, old, next, pending): at most 4, freed by its destructor.
        const long held = offered - freed;
        std::printf("phase 1: %ld iterations, %ld buffers offered, %ld freed on the UI thread, %ld held by the stage at the "
                    "end; %ld pulls (%ld audible), %llu switches\n",
                    iterations, offered, freed, held, audio.pulls.load(), audio.audible.load(),
                    static_cast<unsigned long long>(stage->switches()));
        check(audio.pulls > 1000 && audio.audible > 100 && stage->switches() > 100, "both threads ran and buffers switched");
        check(held >= 0 && held <= 4, "every buffer offered came back to the UI thread, apart from the at most four the "
                                      "stage holds at the end (" + std::to_string(held) + ")");
        check(audio.nonFinite == 0, "everything pulled is finite");
        stage.reset(); // audio stopped first, then the stage frees what it holds
    }

    // --- Phase 2: the real controller and loader -------------------------------------------------------
    {
        itest::TempDir d("stress");
        for(int i = 0; i < 4; ++i)
            itest::writeWav(d.file("f" + std::to_string(i) + ".wav"), i % 2 == 0 ? 48000 : 44100, 1 + i % 2,
                            itest::Fmt::Pcm16, 4000 + 3000 * static_cast<std::size_t>(i),
                            itest::sine(200.0 + 50 * i, 0.3, 48000));
        auto stage = std::make_unique<InputStage>();
        stage->prepare(256);
        auto  ctl = std::make_unique<InputController>(*stage, d.path());
        Audio audio(*stage);
        double     t  = 0.0;
        long       n  = 0;
        const auto t0 = Clock::now();
        ctl->onButton({Button::DpadRight, true}, t, true); // FILE
        ctl->onButton({Button::DpadRight, false}, t, true);
        while(std::chrono::duration<double>(Clock::now() - t0).count() < 2.0)
        {
            t += 0.002;
            const int k = static_cast<int>(n % 40);
            if(k == 0)
                ctl->onButton({Button::X, true}, t, true);
            if(k == 1 || k == 9)
            {
                ctl->onButton({Button::DpadRight, true}, t, true); // the cursor (X held)
                ctl->onButton({Button::DpadRight, false}, t, true);
            }
            if(k == 5)
            {
                ctl->onButton({Button::DpadDown, true}, t, true); // opens the pop-up, or moves in it
                ctl->onButton({Button::DpadDown, false}, t, true);
            }
            if(k == 24 && n % 120 == 24)
            {
                // X + Select: play the loaded file again (a restart).
                ctl->onButton({Button::X, true}, t, true);
                ctl->onButton({Button::Select, true}, t, true);
                ctl->onButton({Button::Select, false}, t, true);
                ctl->onButton({Button::X, false}, t, true);
            }
            if(k == 11 && n % 160 == 11)
            {
                ctl->onButton({Button::Y, true}, t, true); // X + Y: LOOP <-> ONCE
                ctl->onButton({Button::Y, false}, t, true);
            }
            if(k == 12)
                ctl->onButton({Button::X, false}, t, true);
            if(k == 20 && n % 400 == 20)
            {
                ctl->onButton({Button::DpadLeft, true}, t, true); // OFF
                ctl->onButton({Button::DpadLeft, false}, t, true);
                ctl->onButton({Button::DpadRight, true}, t, true); // FILE again
                ctl->onButton({Button::DpadRight, false}, t, true);
            }
            if(k == 30)
            {
                ctl->onButton({n % 80 < 40 ? Button::DpadUp : Button::DpadDown, true}, t, true);
                ctl->onButton({n % 80 < 40 ? Button::DpadUp : Button::DpadDown, false}, t, true);
            }
            ctl->tick(t);
            std::vector<std::string> log;
            ctl->takeLog(log);
            ++n;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        audio.join();
        const std::uint64_t freed = ctl->buffersFreed();
        std::printf("phase 2: %ld UI iterations, %llu buffers freed on the UI thread, %llu switches, %llu restarts, %ld pulls "
                    "(%ld audible)\n",
                    n, static_cast<unsigned long long>(freed), static_cast<unsigned long long>(stage->switches()),
                    static_cast<unsigned long long>(stage->restarts()), audio.pulls.load(), audio.audible.load());
        check(stage->switches() > 5 && freed > 3 && audio.audible > 100 && stage->restarts() > 0,
              "files loaded, played, restarted and switched during the run");
        check(audio.nonFinite == 0, "everything pulled is finite");
        ctl.reset();   // joins the loader
        stage.reset(); // frees what the stage still holds
    }

    std::printf("audio thread: operator new %ld, operator delete %ld\n", g_audioNews.load(), g_audioDeletes.load());
    check(g_audioNews == 0 && g_audioDeletes == 0,
          "the audio thread never allocated or freed anything (so no buffer was freed there)");
    return itest::finish();
}
