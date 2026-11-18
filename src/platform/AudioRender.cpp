// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "platform/AudioRender.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "platform/Denormals.h"

namespace audreybrick
{

AudioRender::AudioRender(AudreyVoice& voice, ControlSource& controls, FlushToZero ftz, ClockFn clock,
                         double clockTicksPerSecond) noexcept
: voice_(voice), controls_(controls), ftzMode_(ftz), clock_(clock), ticksPerSecond_(clockTicksPerSecond)
{
}

void AudioRender::prepare(std::size_t frames)
{
    capacity_ = std::max(frames, kMinCapacity);
    left_     = std::make_unique<float[]>(capacity_);
    right_    = std::make_unique<float[]>(capacity_);
}

void AudioRender::render(float* out, std::size_t frames) noexcept
{
    // Every callback: one FPCR read; set FZ if this thread does not have it (for
    // example a callback thread SDL recreated after a device change).
    bool fz = flushToZeroEnabled();
    if(ftzMode_ == FlushToZero::Enable && !fz && kFlushToZeroSupported)
    {
        setFlushToZero(true);
        fz = flushToZeroEnabled();
    }
    ftz_.store(static_cast<int>(fz ? FtzState::On : FtzState::Off), std::memory_order_relaxed);

    const bool          timed = clock_ != nullptr && ticksPerSecond_ > 0.0;
    const std::uint64_t t0    = timed ? clock_() : 0;

    if(capacity_ == 0)
    {
        std::fill(out, out + 2 * frames, 0.0f);
    }
    else
    {
        float       pl = 0.0f, pr = 0.0f;
        std::size_t done = 0;
        while(done < frames)
        {
            const std::size_t n = std::min(frames - done, capacity_);
            voice_.process(controls_, nullptr, left_.get(), right_.get(), n);
            float* o = out + 2 * done;
            for(std::size_t i = 0; i < n; ++i)
            {
                const float l = left_[i];
                const float r = right_[i];
                o[2 * i]      = l;
                o[2 * i + 1]  = r;
                pl            = std::max(pl, std::fabs(l));
                pr            = std::max(pr, std::fabs(r));
            }
            done += n;
        }
        peak_[0].offer(pl);
        peak_[1].offer(pr);
    }

    if(timed && frames > 0)
    {
        const std::uint64_t t1             = clock_();
        const double        seconds        = static_cast<double>(t1 - t0) / ticksPerSecond_;
        const double        bufferDuration = static_cast<double>(frames) / AudreyVoice::kSampleRate;
        cpu_.record(seconds / bufferDuration, bufferDuration);
    }

    frames_.fetch_add(frames, std::memory_order_relaxed);
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    if(frames > largest_.load(std::memory_order_relaxed))
        largest_.store(frames, std::memory_order_relaxed); // audio thread is the only writer
}

} // namespace audreybrick
