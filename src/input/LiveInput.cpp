// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/LiveInput.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace audreybrick
{

LiveInput::LiveInput() : ring_(std::make_unique<float[]>(kCapacity)) {}

std::size_t LiveInput::write(const float* mono, std::size_t n) noexcept
{
    const std::uint64_t w    = write_.load(std::memory_order_relaxed); // the writer's own index
    const std::uint64_t r    = read_.load(std::memory_order_acquire);
    const std::uint64_t used = w - r;
    const std::size_t   room = used < kCapacity ? kCapacity - static_cast<std::size_t>(used) : 0;
    const std::size_t   k    = n < room ? n : room;
    float               peak = 0.0f;
    for(std::size_t i = 0; i < n; ++i)
    {
        const float v = clean(mono[i]);
        if(i < k)
            ring_[static_cast<std::size_t>(w + i) & (kCapacity - 1)] = v;
        const float a = std::fabs(v);
        peak          = a > peak ? a : peak;
    }
    write_.store(w + k, std::memory_order_release);
    prePeak_.offer(peak);
    bump(callbacks_);
    captured_.store(captured_.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
    if(k < n)
        dropped_.store(dropped_.load(std::memory_order_relaxed) + (n - k), std::memory_order_relaxed);
    return k;
}

std::size_t LiveInput::writeInterleaved(const float* in, std::size_t frames, int channels) noexcept
{
    if(in == nullptr || channels < 1)
    {
        bump(callbacks_);
        return 0;
    }
    const std::uint64_t w    = write_.load(std::memory_order_relaxed);
    const std::uint64_t r    = read_.load(std::memory_order_acquire);
    const std::uint64_t used = w - r;
    const std::size_t   room = used < kCapacity ? kCapacity - static_cast<std::size_t>(used) : 0;
    const std::size_t   k    = frames < room ? frames : room;
    const auto          ch   = static_cast<std::size_t>(channels);
    float               peak = 0.0f;
    for(std::size_t i = 0; i < frames; ++i)
    {
        const float v = mixFrame(in + i * ch, channels);
        if(i < k)
            ring_[static_cast<std::size_t>(w + i) & (kCapacity - 1)] = v;
        const float a = std::fabs(v);
        peak          = a > peak ? a : peak;
    }
    write_.store(w + k, std::memory_order_release);
    prePeak_.offer(peak);
    bump(callbacks_);
    captured_.store(captured_.load(std::memory_order_relaxed) + frames, std::memory_order_relaxed);
    if(k < frames)
        dropped_.store(dropped_.load(std::memory_order_relaxed) + (frames - k), std::memory_order_relaxed);
    return k;
}

void LiveInput::publishFill(std::size_t fill) noexcept
{
    const std::uint32_t req = rangeReset_.load(std::memory_order_relaxed);
    if(req != rangeSeen_ || !rangeValid_)
    {
        rangeSeen_  = req;
        rangeValid_ = true;
        minLocal_   = fill;
        maxLocal_   = fill;
    }
    else
    {
        minLocal_ = fill < minLocal_ ? fill : minLocal_;
        maxLocal_ = fill > maxLocal_ ? fill : maxLocal_;
    }
    fill_.store(fill, std::memory_order_relaxed);
    minFill_.store(minLocal_, std::memory_order_relaxed);
    maxFill_.store(maxLocal_, std::memory_order_relaxed);
}

void LiveInput::takeFillRange(std::size_t& minFill, std::size_t& maxFill) noexcept
{
    minFill = minFill_.load(std::memory_order_relaxed);
    maxFill = maxFill_.load(std::memory_order_relaxed);
    rangeReset_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace audreybrick
