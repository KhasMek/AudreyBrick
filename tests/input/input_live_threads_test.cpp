// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M2b-B, with real threads (run under ThreadSanitizer by the TSan build):
//   1. The ring alone: a writer thread writes a counting sequence in blocks of
//      varying size while this thread reads it through the reader API; every
//      sample arrives once, in order, or is counted as dropped (none here: the
//      reader keeps up).
//   2. The stage with the simulated capture system (LiveSim's writer thread,
//      paced by the frames this thread has pulled): source changes, new sessions
//      and gain changes from this thread between pulls; the output stays finite
//      and within the step bound.
//   3. The INPUT tab's model (InputController) with LiveSim: choose the device
//      with the buttons, hear it, unplug it: DEVICE LOST; plug it again and
//      choose it.
//   4. M2b-B review (S2): three threads, the model on this one, pull() on
//      another, LiveSim's writer on a third. A quiet device (0.05) is played at
//      unity gain; a loud one (3.0) is opened now and then and never given any
//      gain; the source goes LIVE -> FILE -> LIVE (a reopen) at random moments.
//      Every open sets the gain to zero first, so nothing of the loud device,
//      and no held value of it, may ever reach the output: its ceiling is the
//      quiet device's level.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "app/Buttons.h"
#include "input/InputController.h"
#include "input/InputStage.h"
#include "input/LiveInput.h"
#include "input/LiveSim.h"
#include "input_test_common.h"

namespace
{
using namespace audreybrick;
using itest::check;

std::atomic<std::uint64_t> g_pulled{0};

std::uint64_t pulledFrames(const void*)
{
    return g_pulled.load(std::memory_order_relaxed);
}

InputFolderScan noFiles(const std::string&)
{
    InputFolderScan s;
    s.ok = true;
    return s;
}
} // namespace

int main()
{
    // --- 1. The ring alone ------------------------------------------------------------------------------
    {
        LiveInput                lv;
        constexpr std::uint64_t  kTotal = 3000000;
        std::atomic<bool>        done{false};
        std::thread              writer([&] {
            std::vector<float> blk(1024);
            std::uint64_t      next = 0;
            std::uint32_t      seed = 12345;
            while(next < kTotal)
            {
                seed               = seed * 1664525u + 1013904223u;
                const std::size_t n = std::min<std::uint64_t>(1 + (seed >> 22), kTotal - next); // 1..1024
                for(std::size_t i = 0; i < n; ++i)
                    blk[i] = static_cast<float>((next + i) % 4096) / 1024.0f; // exact in float, within +/-8
                const std::size_t w = lv.write(blk.data(), n);
                next += w;
                if(w < n)
                    std::this_thread::yield(); // full: try the rest again (a test writer, not a callback)
            }
            done.store(true, std::memory_order_release);
        });
        std::uint64_t expect = 0;
        std::uint64_t bad    = 0;
        while(expect < kTotal)
        {
            const std::uint64_t w = lv.writeIndex();
            std::uint64_t       r = lv.readIndex();
            for(; r < w; ++r, ++expect)
                bad += lv.at(r) != static_cast<float>(expect % 4096) / 1024.0f ? 1 : 0;
            lv.setReadIndex(r);
            if(r == w)
                std::this_thread::yield();
        }
        writer.join();
        // The writer above retries what did not fit, so frames "dropped" by
        // write() were written later: the sequence check is what counts.
        check(bad == 0 && expect == kTotal && done.load(),
              "ring with a writer thread: 3,000,000 samples arrive once and in order (" + std::to_string(bad) + " wrong)");
    }

    // --- 2. The stage with LiveSim's writer thread -----------------------------------------------------------
    {
        LiveInput  lv;
        InputStage st;
        st.attachLive(&lv);
        st.prepare(4096, 256);
        st.setGainNorm(kInputGainUnityNorm);
        SimCaptureDevice dev;
        dev.name      = "sim";
        dev.channels  = 1;
        dev.toneHz    = 330.0;
        dev.amplitude = 0.4;
        dev.rateHz    = 48240.0; // 0.5 % fast
        g_pulled.store(0);
        LiveSim        sim(lv, {dev}, &pulledFrames, nullptr, 256);
        LiveOpenResult res;
        const bool     opened = sim.open("sim", res);
        st.setSource(InputSource::Live);
        auto file = std::make_unique<InputBuffer>();
        file->samples.resize(24000);
        for(std::size_t i = 0; i < file->samples.size(); ++i)
            file->samples[i] = static_cast<float>(0.4 * std::sin(2.0 * itest::kPi * 220.0 * static_cast<double>(i) / 48000.0));
        st.offer(std::move(file));
        const double bound  = 1.01 * 0.4 * (2.0 * itest::kPi * 330.0 / 48000.0 + 1.0 / 480.0 + 2.0 / 257.0) + 1e-6;
        double       worst  = 0.0;
        float        prev   = 0.0f;
        bool         finite = true;
        std::size_t  sounding = 0;
        const auto   t0     = std::chrono::steady_clock::now();
        for(int i = 0; i < 6000; ++i)
        {
            if(i % 700 == 350)
                st.setSource(InputSource::File);
            if(i % 700 == 500)
                st.setSource(InputSource::Live);
            if(i % 1500 == 1200)
                lv.newSession(); // as when a device is reopened
            if(i % 2000 == 1900)
                st.setSource(InputSource::Off);
            if(i % 2000 == 1950)
                st.setSource(InputSource::Live);
            const float* p = st.pull(256);
            for(int k = 0; k < 256; ++k)
            {
                const float v = p != nullptr ? p[k] : 0.0f;
                finite        = finite && std::isfinite(v);
                worst         = std::max(worst, static_cast<double>(std::fabs(v - prev)));
                prev          = v;
            }
            sounding += p != nullptr ? 1 : 0;
            g_pulled.fetch_add(256, std::memory_order_relaxed);
            // About real time / 4, so the writer thread (1 ms naps) keeps up mostly.
            if(i % 4 == 0)
                std::this_thread::sleep_for(std::chrono::microseconds(1300));
            while(auto b = st.reclaim())
            {
            }
        }
        sim.close();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("stage + LiveSim thread: %.2f s, sim wrote %llu frames, underruns %llu, overfills %llu, sounding "
                    "chunks %zu of 6000, largest step %.5f (bound %.5f)\n",
                    secs, static_cast<unsigned long long>(lv.framesCaptured()),
                    static_cast<unsigned long long>(lv.underruns()), static_cast<unsigned long long>(lv.overfills()),
                    sounding, worst, bound);
        check(opened && finite && worst <= bound && sounding > 3000 && lv.framesCaptured() > 1000000,
              "stage with a capture thread, source changes and new sessions: finite, no step above the bound, mostly sounding");
    }

    // --- 3. InputController with LiveSim: choose, hear, unplug, re-plug ----------------------------------------
    {
        LiveInput  lv;
        InputStage st;
        st.attachLive(&lv);
        st.prepare(4096, 256);
        SimCaptureDevice mic;
        mic.name      = "mic";
        mic.channels  = 1;
        mic.amplitude = 0.3;
        SimCaptureDevice usb;
        usb.name      = "usb";
        usb.channels  = 2;
        usb.amplitude = 0.2;
        g_pulled.store(0);
        LiveSim         sim(lv, {mic, usb}, &pulledFrames, nullptr, 256);
        InputController ctl(st, "", &noFiles);
        ctl.setLive(&lv, &sim);
        double t     = 0.0;
        auto   press = [&](Button b, bool down) {
            ctl.onButton(ButtonEvent{b, down}, t, true);
        };
        auto run = [&](int chunks, float& peak) {
            for(int i = 0; i < chunks; ++i)
            {
                const float* p = st.pull(256);
                for(int k = 0; p != nullptr && k < 256; ++k)
                    peak = std::max(peak, std::fabs(p[k]));
                g_pulled.fetch_add(256, std::memory_order_relaxed);
                t += 256.0 / 48000.0;
                ctl.tick(t);
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        };
        press(Button::DpadRight, true);
        press(Button::DpadRight, false);
        press(Button::DpadRight, true);
        press(Button::DpadRight, false); // LIVE
        // X, right, right (SYSTEM DEFAULT -> mic -> usb), release: opens usb.
        press(Button::X, true);
        press(Button::DpadRight, true);
        press(Button::DpadRight, false);
        press(Button::DpadRight, true);
        press(Button::DpadRight, false);
        press(Button::X, false);
        float peak0 = 0.0f;
        run(100, peak0);
        // Y + up: unity gain (the gain was set to zero by the open).
        press(Button::Y, true);
        press(Button::DpadUp, true);
        press(Button::DpadUp, false);
        press(Button::Y, false);
        float peak1 = 0.0f;
        run(200, peak1);
        check(ctl.deviceOpen() && ctl.deviceOpenName() == "usb" && peak0 == 0.0f && peak1 > 0.35f && peak1 < 0.45f,
              "LIVE, X + right + right, release: usb opens at gain zero (silent: " + std::to_string(peak0) +
                  "); Y + up: its L + R mix sounds (" + std::to_string(peak1) + ")");
        sim.setPresent("usb", false); // unplugged while playing
        float peak2 = 0.0f;
        run(60, peak2);
        UiInputView v;
        ctl.fill(v);
        check(!ctl.deviceOpen() && ctl.liveStatus() == LiveStatus::Lost && v.liveStatus == LiveStatus::Lost &&
                  v.deviceCount == 2 && std::string(v.liveFailName) == "usb",
              "unplugged: DEVICE LOST, closed, the list shrinks to SYSTEM DEFAULT and mic");
        sim.setPresent("usb", true);
        run(2, peak2);
        press(Button::X, true);
        press(Button::DpadUp, true); // opens the list (nothing open: counts as a choice)
        press(Button::DpadUp, false);
        press(Button::DpadUp, true); // up: towards the first entry... cursor moves
        press(Button::DpadUp, false);
        ctl.fill(v);
        const int cursor = v.cursor;
        press(Button::X, false);
        ctl.fill(v);
        check(ctl.devices().size() == 3 && ctl.deviceOpen() && v.deviceOpen && ctl.gainNorm() == 0.0f &&
                  ctl.deviceOpenIndex() == cursor,
              "plugged in again: listed; chosen from the pop-up it opens, the gain zero again");
        ctl.stop();
        sim.close();
    }

    // --- 4. Three threads: no leak of a device never given gain --------------------------------------------
    {
        LiveInput        lv;
        InputStage       st;
        st.attachLive(&lv);
        st.prepare(4096, 256);
        SimCaptureDevice q;
        q.name      = "quiet";
        q.channels  = 1;
        q.amplitude = 0.05;
        q.toneHz    = 100.0;
        SimCaptureDevice l = q;
        l.name             = "loud";
        l.amplitude        = 3.0;
        g_pulled.store(0);
        LiveSim           sim(lv, {q, l}, &pulledFrames, nullptr, 256);
        InputController   ctl(st, "", &noFiles);
        ctl.setLive(&lv, &sim);
        std::atomic<bool> stop{false};
        float             maxOut = 0.0f;
        std::uint64_t     sounding = 0;
        std::thread       out([&] {
            while(!stop.load(std::memory_order_acquire))
            {
                const float* p = st.pull(256);
                if(p != nullptr)
                {
                    ++sounding;
                    for(int k = 0; k < 256; ++k)
                        maxOut = std::max(maxOut, std::fabs(p[k]));
                }
                g_pulled.fetch_add(256, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
        double t     = 0.0;
        auto   press = [&](Button b) {
            ctl.onButton(ButtonEvent{b, true}, t, true);
            ctl.onButton(ButtonEvent{b, false}, t, true);
        };
        auto choose = [&](Button dir) {
            ctl.onButton(ButtonEvent{Button::X, true}, t, true);
            press(dir);
            ctl.onButton(ButtonEvent{Button::X, false}, t, true);
        };
        auto unity = [&] {
            ctl.onButton(ButtonEvent{Button::Y, true}, t, true);
            press(Button::DpadUp);
            ctl.onButton(ButtonEvent{Button::Y, false}, t, true);
        };
        auto wait = [&](int ms) {
            for(int i = 0; i < ms; ++i)
            {
                t += 0.001;
                ctl.tick(t);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        unsigned seed = 1;
        auto     rnd  = [&](int n) {
            seed = seed * 1103515245u + 12345u;
            return static_cast<int>((seed >> 16) % static_cast<unsigned>(n));
        };
        press(Button::DpadRight);
        press(Button::DpadRight); // LIVE
        choose(Button::DpadRight); // SYSTEM DEFAULT -> quiet
        int opensLoud = 0;
        for(int i = 0; i < 150; ++i)
        {
            unity(); // on the quiet device
            wait(rnd(30));
            switch(rnd(3))
            {
                case 0:
                    choose(Button::DpadRight); // loud (gain to zero first)
                    ++opensLoud;
                    wait(rnd(40));
                    choose(Button::DpadLeft); // back to quiet (gain zero again)
                    break;
                case 1:
                    press(Button::DpadLeft); // FILE: closes
                    wait(rnd(5));
                    press(Button::DpadRight); // LIVE: quiet reopens at gain zero
                    break;
                default: wait(rnd(10)); break;
            }
            wait(rnd(20));
        }
        stop.store(true, std::memory_order_release);
        out.join();
        ctl.stop();
        sim.close();
        std::printf("three threads: %llu opens (%d of the loud device), %llu sounding pulls, largest output %.4f "
                    "(quiet 0.05, loud 3.0)\n",
                    static_cast<unsigned long long>(ctl.deviceOpens()), opensLoud,
                    static_cast<unsigned long long>(sounding), static_cast<double>(maxOut));
        check(opensLoud > 20 && sounding > 100 && maxOut <= 0.0501f && maxOut > 0.01f,
              "three threads, random opens and FILE / LIVE switches: the loud device (never given gain) never reaches the "
              "output; the ceiling is the quiet device's level");
    }

    return itest::finish();
}
