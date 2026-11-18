// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The app's two threads without SDL: an "audio" thread calls AudioRender::render
// continuously (flush-to-zero enabled, CPU clock on) while the "UI" thread plays
// button presses into a ControlModel and reads every meter and published value,
// for 2 seconds. Run under ThreadSanitizer in the tsan build (ctest -R platform):
// any report fails the test. Checks that nothing crashes, values stay in range and
// the meters move.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "platform/AudioRender.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::uint64_t steadyTicks()
{
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}
} // namespace

int main()
{
    using Period                = std::chrono::steady_clock::period;
    const double ticksPerSecond = static_cast<double>(Period::den) / static_cast<double>(Period::num);

    ptest::Rig  rig;
    AudioRender render(*rig.voice, *rig.src, AudioRender::FlushToZero::Enable, &steadyTicks, ticksPerSecond);
    render.prepare(256);

    std::atomic<bool> stop{false};
    std::thread       audio([&] {
        std::vector<float> out(2 * 256);
        while(!stop.load(std::memory_order_relaxed))
            render.render(out.data(), 256);
    });

    const auto   start = std::chrono::steady_clock::now();
    const Button cycle[] = {Button::R2, Button::DpadRight, Button::R1, Button::DpadUp, Button::A,
                            Button::B,  Button::Select,    Button::X,  Button::L2,     Button::Y};
    float        maxPeak = 0.0f, maxCpu = 0.0f;
    bool         inRange = true;
    int          frames  = 0;
    for(;; ++frames)
    {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if(t > 2.0)
            break;
        const Button b = cycle[frames / 6 % 10];
        if(frames % 6 == 0)
            rig.model->onButton({b, true}, t);
        if(frames % 6 == 4)
            rig.model->onButton({b, false}, t);
        if(frames % 97 == 0)
            rig.model->releaseAll(t);
        rig.model->tick(t);
        maxPeak = std::max({maxPeak, render.peakL().take(), render.peakR().take()});
        maxCpu  = std::max(maxCpu, render.cpu().takeMax());
        for(std::size_t k = 0; k < kKnobCount; ++k)
        {
            const float v = rig.src->publishedKnob(static_cast<Knob>(k));
            inRange       = inRange && v >= 0.0f && v <= 1.0f;
        }
        (void)render.cpu().smoothed();
        (void)render.ftzState();
        (void)render.framesRendered();
        (void)rig.src->publishedSwitch();
        (void)rig.src->publishedStoredFeedback();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stop = true;
    audio.join();

    std::printf("UI frames %d, callbacks %llu, max peak %.4f, max CPU load %.4f, ftz %d\n", frames,
                static_cast<unsigned long long>(render.callbacks()), static_cast<double>(maxPeak),
                static_cast<double>(maxCpu), static_cast<int>(render.ftzState()));
    check(render.callbacks() > 100, "the audio thread ran");
    check(inRange, "published knob values stayed within 0..1");
    check(std::isfinite(maxPeak) && maxPeak <= 1.5f, "peaks finite");
    check(maxCpu > 0.0f && std::isfinite(maxCpu), "the CPU meter moved");
    check(render.ftzState() != AudioRender::FtzState::NotYetKnown, "the FTZ state was published");
    return ptest::finish();
}
