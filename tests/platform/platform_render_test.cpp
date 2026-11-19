// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Criterion 4: AudioRender::render(), the body of the SDL callback, called
// directly (no SDL device) with buffer sizes 64, 256, 1024 and one larger than
// its preallocated capacity. With flush-to-zero left alone, its interleaved
// output equals AudreyVoice::process driven directly with an identical control
// rig, bit for bit, over 5 s of tests/platform/rise.txt.
//
// M2b-A, acceptance criterion 1: the same comparison with an InputStage attached
// and its source OFF (a buffer handed over, a gain of +6 dB set, the gain moved
// during the run): bit-identical to the voice driven with nullptr, for every
// buffer size above. And with the source switched to FILE the output does change
// (the comparison would notice).
//
// Also: peaks and the CPU load with a fake clock, silence before prepare(), and
// (informational) whether enabling flush-to-zero changes any sample over 20 s of
// the same script. The flush-to-zero render runs on its own thread, because the
// FPCR is per thread; the comparison render runs on the main thread with FZ clear.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "input/InputStage.h"
#include "platform/AudioRender.h"
#include "platform/Denormals.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
constexpr std::size_t kRate = AudreyVoice::kSampleRate;

bool sameBits(float a, float b)
{
    std::uint32_t x, y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

// Renders `seconds` of the script through AudioRender in calls of `bufferFrames`,
// and the same through AudreyVoice::process directly, and compares.
void compareBufferSize(const std::vector<TimedButtonEvent>& ev, std::size_t bufferFrames, std::size_t prepareFrames,
                       double seconds)
{
    ptest::Rig  a, b, c;
    AudioRender render(*a.voice, *a.src, AudioRender::FlushToZero::LeaveAlone);
    render.prepare(prepareFrames);
    // M2b-A: with the input stage attached, source OFF, a file handed over.
    InputStage  stage;
    AudioRender withInput(*c.voice, *c.src, AudioRender::FlushToZero::LeaveAlone);
    withInput.attachInput(&stage);
    withInput.prepare(prepareFrames);
    {
        auto buf = std::make_unique<InputBuffer>();
        buf->samples.assign(48000, 0.5f);
        stage.offer(std::move(buf));
        stage.setGainNorm(0.7071f); // about +6 dB
        stage.setSource(InputSource::Off);
    }
    std::vector<float> interC(2 * bufferFrames);
    std::size_t        mismatchesC = 0;

    const std::size_t  total = static_cast<std::size_t>(seconds * kRate);
    std::vector<float> inter(2 * bufferFrames), l(bufferFrames), r(bufferFrames);
    std::size_t        mismatches = 0, nonFinite = 0;
    float              peak       = 0.0f;
    for(std::size_t pos = 0; pos < total;)
    {
        const std::size_t n = std::min(bufferFrames, total - pos);
        const double      t = static_cast<double>(pos) / kRate;
        a.advanceTo(ev, t);
        b.advanceTo(ev, t);
        c.advanceTo(ev, t);
        stage.setGainNorm(static_cast<float>(0.3 + 0.6 * t / seconds)); // moves while OFF
        render.render(inter.data(), n);
        withInput.render(interC.data(), n);
        b.voice->process(*b.src, nullptr, l.data(), r.data(), n);
        for(std::size_t i = 0; i < n; ++i)
        {
            if(!sameBits(inter[2 * i], l[i]) || !sameBits(inter[2 * i + 1], r[i]))
                ++mismatches;
            if(!sameBits(interC[2 * i], l[i]) || !sameBits(interC[2 * i + 1], r[i]))
                ++mismatchesC;
            if(!std::isfinite(inter[2 * i]) || !std::isfinite(inter[2 * i + 1]))
                ++nonFinite;
            peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
        }
        pos += n;
    }
    std::printf("buffer %zu frames (capacity %zu): %zu frames, %zu mismatching frames, peak %.4f, callbacks %llu\n",
                bufferFrames, render.capacity(), total, mismatches, static_cast<double>(peak),
                static_cast<unsigned long long>(render.callbacks()));
    check(mismatches == 0, "buffer " + std::to_string(bufferFrames) + ": bit-identical to AudreyVoice driven directly");
    std::printf("  with the input stage attached, source OFF: %zu mismatching frames; stage active %s\n", mismatchesC,
                stage.active() ? "yes" : "no");
    check(mismatchesC == 0 && !stage.active() && stage.peak().take() == 0.0f,
          "buffer " + std::to_string(bufferFrames) + ": with the input stage attached and its source OFF, bit-identical "
          "to AudreyVoice driven with nullptr (M2b-A criterion 1)");
    check(nonFinite == 0, "buffer " + std::to_string(bufferFrames) + ": output is finite");
    check(peak > 0.01f, "buffer " + std::to_string(bufferFrames) + ": the script makes sound (peak > 0.01)");
    check(render.framesRendered() == total, "framesRendered counts every frame");
    check(render.largestCallback() == std::min(bufferFrames, total), "largestCallback is the call size");
    check(render.ftzState() == AudioRender::FtzState::Off, "LeaveAlone: FZ reported off on this thread");
}

std::vector<float> renderScript(const std::vector<TimedButtonEvent>& ev, AudioRender::FlushToZero mode, double seconds,
                                AudioRender::FtzState& stateOut)
{
    ptest::Rig  rig;
    AudioRender render(*rig.voice, *rig.src, mode);
    render.prepare(256);
    const std::size_t  total = static_cast<std::size_t>(seconds * kRate);
    std::vector<float> out(2 * total);
    for(std::size_t pos = 0; pos < total; pos += 256)
    {
        const std::size_t n = std::min<std::size_t>(256, total - pos);
        rig.advanceTo(ev, static_cast<double>(pos) / kRate);
        render.render(out.data() + 2 * pos, n);
    }
    stateOut = render.ftzState();
    return out;
}

std::uint64_t g_fakeTicks = 0;
std::uint64_t fakeClock()
{
    g_fakeTicks += 1000; // 1 ms per read at 1 MHz
    return g_fakeTicks;
}
} // namespace

int main()
{
    const auto ev = ptest::loadRiseScript();

    // --- Criterion 4: bit-exact against the voice driven directly ------------
    compareBufferSize(ev, 64, 256, 5.0);
    compareBufferSize(ev, 256, 256, 5.0);
    compareBufferSize(ev, 1024, 256, 5.0);
    // Capacity is max(256, 4096) = 4096; 5000-frame calls are processed in chunks
    // of 4096 + 904.
    compareBufferSize(ev, AudioRender::kMinCapacity + 904, 256, 5.0);

    // --- M2b-A: the comparison notices an input: FILE changes the output ---------
    {
        ptest::Rig  x, y;
        InputStage  stage;
        AudioRender withInput(*x.voice, *x.src, AudioRender::FlushToZero::LeaveAlone);
        withInput.attachInput(&stage);
        withInput.prepare(256);
        auto buf = std::make_unique<InputBuffer>();
        buf->samples.resize(48000);
        for(std::size_t i = 0; i < buf->samples.size(); ++i)
            buf->samples[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
        stage.offer(std::move(buf));
        stage.setSource(InputSource::File);
        std::vector<float> o(2 * 256), l(256), r(256);
        std::size_t        differ = 0;
        float              peak   = 0.0f;
        for(int k = 0; k < 400; ++k) // about 2 s, no button pressed: feedback at its minimum
        {
            withInput.render(o.data(), 256);
            y.voice->process(*y.src, nullptr, l.data(), r.data(), 256);
            for(int i = 0; i < 256; ++i)
            {
                differ += !sameBits(o[2 * i], l[i]) ? 1 : 0;
                peak = std::max(peak, std::fabs(o[2 * i]));
            }
        }
        std::printf("FILE at unity, feedback at its minimum: %zu of %d samples differ from no input; output peak %.4f\n",
                    differ, 400 * 256, static_cast<double>(peak));
        check(differ > 50000 && peak > 0.05f,
              "with the source FILE the output differs and the input is audible at minimum feedback (peak > 0.05)");
    }

    // --- Before prepare(): silence --------------------------------------------
    {
        ptest::Rig         rig;
        AudioRender        render(*rig.voice, *rig.src, AudioRender::FlushToZero::LeaveAlone);
        std::vector<float> out(512, 1.0f);
        render.render(out.data(), 256);
        check(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }),
              "render() before prepare() writes silence");
        check(render.capacity() == 0, "capacity is 0 before prepare()");
    }

    // --- Peaks and the CPU load with a fake clock --------------------------------
    {
        ptest::Rig  rig;
        AudioRender render(*rig.voice, *rig.src, AudioRender::FlushToZero::LeaveAlone, &fakeClock, 1.0e6);
        render.prepare(256);
        check(render.capacity() == AudioRender::kMinCapacity, "prepare(256) allocates the 4096-frame minimum");
        std::vector<float> out(2 * 256);
        float              maxL = 0.0f, maxR = 0.0f, takenL = 0.0f, takenR = 0.0f;
        for(int i = 0; i < 1000; ++i) // 5.3 s
        {
            rig.advanceTo(ev, i * 256.0 / kRate);
            render.render(out.data(), 256);
            for(int k = 0; k < 256; ++k)
            {
                maxL = std::max(maxL, std::fabs(out[2 * k]));
                maxR = std::max(maxR, std::fabs(out[2 * k + 1]));
            }
            if(i % 100 == 99) // a UI read now and then
            {
                takenL = std::max(takenL, render.peakL().take());
                takenR = std::max(takenR, render.peakR().take());
            }
        }
        check(takenL == maxL && takenR == maxR, "peak meters hold the largest |sample| per channel");
        check(render.peakL().take() == 0.0f, "take() resets the peak");
        // Each callback reads the clock twice: 1 ms apart. Buffer = 256/48000 s.
        const double expected = 0.001 / (256.0 / kRate);
        const float  cmax     = render.cpu().takeMax();
        const float  csm      = render.cpu().smoothed();
        std::printf("fake clock: expected load %.6f, max %.6f, smoothed %.6f\n", expected, static_cast<double>(cmax),
                    static_cast<double>(csm));
        check(std::fabs(cmax - expected) < 1e-5, "CPU max = callback time / buffer duration");
        check(std::fabs(csm - expected) < 1e-3, "CPU smoothed converges to the steady load");
        check(render.cpu().takeMax() == 0.0f, "takeMax() resets the maximum");
    }

    // --- Flush-to-zero is checked at every callback ---------------------------------
    // A callback thread that SDL replaces, or a thread whose FZ bit was cleared,
    // gets FZ set again at its next callback.
    if(kFlushToZeroSupported)
    {
        ptest::Rig         rig;
        AudioRender        render(*rig.voice, *rig.src, AudioRender::FlushToZero::Enable);
        std::vector<float> out(2 * 256);
        render.prepare(256);
        bool firstThread = false, clearedThenRendered = false, secondThread = false, secondBefore = true;
        std::thread t1([&] {
            setFlushToZero(false);
            render.render(out.data(), 256);
            firstThread = flushToZeroEnabled() && render.ftzState() == AudioRender::FtzState::On;
            setFlushToZero(false); // as if something on this thread cleared it
            render.render(out.data(), 256);
            clearedThenRendered = flushToZeroEnabled();
            setFlushToZero(false);
        });
        t1.join();
        std::thread t2([&] { // a new callback thread, as after a device change
            setFlushToZero(false); // Linux threads inherit the creator's FPCR; start clear
            secondBefore = flushToZeroEnabled();
            render.render(out.data(), 256);
            secondThread = flushToZeroEnabled() && render.ftzState() == AudioRender::FtzState::On;
            setFlushToZero(false);
        });
        t2.join();
        check(firstThread, "Enable: FZ set on the first callback thread");
        check(clearedThenRendered, "Enable: FZ set again at the next callback after it was cleared");
        check(!secondBefore && secondThread, "Enable: FZ set on a new callback thread at its first callback");
    }

    // --- Informational: does flush-to-zero change any sample? ---------------------
    {
        const double          seconds = 20.0;
        AudioRender::FtzState ftzState = AudioRender::FtzState::NotYetKnown, plainState = ftzState;
        std::vector<float>    withFtz;
        bool                  threadFzBefore = true, threadFzAfter = false;
        std::thread           th([&] {
            threadFzBefore = flushToZeroEnabled();
            withFtz        = renderScript(ev, AudioRender::FlushToZero::Enable, seconds, ftzState);
            threadFzAfter  = flushToZeroEnabled();
            setFlushToZero(false);
        });
        th.join();
        const std::vector<float> plain = renderScript(ev, AudioRender::FlushToZero::LeaveAlone, seconds, plainState);

        check(!threadFzBefore, "a new thread starts with FZ clear");
        if(kFlushToZeroSupported)
        {
            check(ftzState == AudioRender::FtzState::On && threadFzAfter, "Enable: FZ set on the rendering thread");
            check(plainState == AudioRender::FtzState::Off && !flushToZeroEnabled(),
                  "the main thread's FZ bit is untouched (the FPCR is per thread)");
        }
        else
        {
            check(ftzState == AudioRender::FtzState::Off, "no FZ support here: reported off");
        }
        std::size_t differ = 0, firstDiff = plain.size();
        double      maxAbs = 0.0;
        for(std::size_t i = 0; i < plain.size(); ++i)
        {
            if(!sameBits(plain[i], withFtz[i]))
            {
                ++differ;
                firstDiff = std::min(firstDiff, i);
                maxAbs    = std::max(maxAbs, std::fabs(static_cast<double>(plain[i]) - withFtz[i]));
            }
        }
        std::printf("(informational) flush-to-zero over %.0f s of rise.txt: %zu of %zu samples differ", seconds,
                    differ, plain.size());
        if(differ > 0)
            std::printf(", first at frame %zu (%.4f s), max |difference| %.3g", firstDiff / 2,
                        static_cast<double>(firstDiff / 2) / kRate, maxAbs);
        std::printf("\n");
        check(std::all_of(withFtz.begin(), withFtz.end(), [](float v) { return std::isfinite(v); }),
              "flush-to-zero render is finite");
    }

    return ptest::finish();
}
