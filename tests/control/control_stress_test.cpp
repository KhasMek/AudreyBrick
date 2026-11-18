// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Criterion 3: one thread (the UI side) posts commands and held words as fast as
// it can for 2 seconds, while another (the audio side) calls read() without
// pause and a third reads the published values. Run under ThreadSanitizer in the
// tsan build. Checks: processed + dropped == sent, every value within 0..1.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "control/ButtonControlSource.h"
#include "control_test_common.h"

using namespace audreybrick;
using ctest::check;

int main()
{
    ButtonControlSource src(64); // small, so the queue is often full
    std::atomic<bool>   stop{false};
    std::atomic<bool>   audioStop{false};
    std::uint64_t       sent = 0, dropped = 0, heldWrites = 0;
    std::uint64_t       reads = 0, badFrames = 0;
    std::uint64_t       uiReads = 0, badPublished = 0;

    std::thread audio([&] {
        ControlFrame f{};
        while(!audioStop.load(std::memory_order_acquire))
        {
            src.read(f);
            ++reads;
            for(float v : f.knob)
                if(!(v >= 0.0f && v <= 1.0f))
                    ++badFrames;
        }
    });

    std::thread viewer([&] {
        while(!stop.load(std::memory_order_acquire))
        {
            for(std::size_t i = 0; i < kKnobCount; ++i)
            {
                const float v = src.publishedKnob(static_cast<Knob>(i));
                if(!(v >= 0.0f && v <= 1.0f))
                    ++badPublished;
            }
            const float s = src.publishedStoredFeedback();
            if(!(s >= 0.0f && s <= 1.0f))
                ++badPublished;
            (void)src.publishedSwitch();
            (void)src.publishedSwitchLatch();
            (void)src.publishedFeedbackMax();
            (void)src.commandsProcessed();
            ++uiReads;
        }
    });

    // The producer runs on this thread.
    std::uint32_t rng      = 12345u;
    auto          next     = [&] { return rng = rng * 1664525u + 1013904223u; };
    const auto    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(std::chrono::steady_clock::now() < deadline)
    {
        for(int burst = 0; burst < 64; ++burst)
        {
            const std::uint32_t r = next();
            ControlCommand      c;
            switch(r % 4)
            {
                case 0: c = ControlCommand::step(static_cast<Knob>((r >> 8) % kKnobCount), ((r >> 16) % 201 - 100) / 50.0); break;
                case 1: c = ControlCommand::reset(static_cast<Knob>((r >> 8) % kKnobCount)); break;
                case 2: c = ControlCommand::toggleSwitchLatch(); break;
                default:
                {
                    float v[kKnobCount];
                    for(std::size_t i = 0; i < kKnobCount; ++i)
                        v[i] = static_cast<float>((next() % 3001)) / 1000.0f - 1.0f; // -1 .. 2, clamped by the source
                    c = ControlCommand::setAll(v, r & 1u);
                }
            }
            ++sent;
            if(!src.post(c))
                ++dropped;
            if(burst % 8 == 0)
            {
                HeldState s;
                s.axisKnob[0] = static_cast<Knob>(next() % (kKnobCount + 1));
                s.axisKnob[1] = static_cast<Knob>(next() % (kKnobCount + 1));
                s.axisDir[0]  = static_cast<std::int8_t>(next() % 3) - 1;
                s.axisDir[1]  = static_cast<std::int8_t>(next() % 3) - 1;
                s.feedbackDir = static_cast<std::int8_t>(next() % 3) - 1;
                const std::uint32_t bits = next();
                s.fine        = bits & 1u;
                s.feedbackMax = bits & 2u;
                s.switchHeld  = bits & 4u;
                src.setHeld(packHeld(s));
                ++heldWrites;
            }
        }
    }
    stop.store(true, std::memory_order_release);
    viewer.join();

    // Let the audio thread drain the queue, then stop it.
    const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(src.commandsProcessed() + dropped < sent && std::chrono::steady_clock::now() < drainDeadline)
        std::this_thread::yield();
    audioStop.store(true, std::memory_order_release);
    audio.join();

    const std::uint64_t processed = src.commandsProcessed();
    std::printf("       sent %llu, processed %llu, dropped %llu, held words %llu, read() calls %llu, viewer passes %llu\n",
                static_cast<unsigned long long>(sent), static_cast<unsigned long long>(processed),
                static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(heldWrites),
                static_cast<unsigned long long>(reads), static_cast<unsigned long long>(uiReads));
    check(sent > 0 && reads > 0 && uiReads > 0, "all three threads ran");
    check(processed + dropped == sent, "commands processed + commands dropped == commands sent");
    check(badFrames == 0, "every frame value read on the audio thread was within 0..1");
    check(badPublished == 0, "every published value read on the viewer thread was within 0..1");

    src.setHeld(0);
    ControlFrame f{};
    src.read(f);
    bool ok = true;
    for(std::size_t i = 0; i < kKnobCount; ++i)
        ok = ok && f.knob[i] >= 0.0f && f.knob[i] <= 1.0f && src.publishedKnob(static_cast<Knob>(i)) == f.knob[i];
    check(ok, "final values are within 0..1 and match the published values");
    return ctest::finish();
}
