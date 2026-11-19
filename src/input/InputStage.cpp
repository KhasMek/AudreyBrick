// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/InputStage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace audreybrick
{
namespace
{
// Below this distance the smoothed gain snaps to its target, so it reaches it
// exactly (and never creeps through subnormal steps). It also snaps when a step
// would be lost to float rounding (near 4.0 that happens at a distance of about
// 1e-4, a final step far below the stated bound).
constexpr float kGainSnap = 1.0e-6f;

constexpr std::size_t kXf = static_cast<std::size_t>(kInputFileCrossfadeFrames);
} // namespace

InputStage::InputStage()
: gainCoef_(static_cast<float>(1.0 - std::exp(-1.0 / (kInputGainSmoothingSeconds * kInputSampleRate))))
{
    gain_ = gainTarget_.load(std::memory_order_relaxed);
}

InputStage::~InputStage()
{
    // Only after the audio thread has stopped calling pull().
    delete cur_.buf;
    delete old_.buf;
    delete next_;
    delete pending_.exchange(nullptr, std::memory_order_acq_rel);
    InputBuffer* b = nullptr;
    while(retired_.pop(b))
        delete b;
}

void InputStage::prepare(std::size_t frames)
{
    capacity_ = frames > 0 ? frames : 1;
    out_      = std::make_unique<float[]>(capacity_);
    if(!xfTable_)
    {
        xfTable_ = std::make_unique<float[]>(kXf);
        constexpr double kHalfPi = 1.57079632679489661923;
        for(std::size_t k = 0; k < kXf; ++k)
            xfTable_[k] = static_cast<float>(std::sin(kHalfPi * static_cast<double>(k + 1) / static_cast<double>(kXf + 1)));
    }
}

void InputStage::setSource(InputSource s) noexcept
{
    const int v = static_cast<int>(s);
    if(source_.load(std::memory_order_relaxed) == v)
        return;
    source_.store(v, std::memory_order_release);
    sourceGen_.fetch_add(1, std::memory_order_release);
}

void InputStage::setGainNorm(float n) noexcept
{
    gainTarget_.store(inputGainFromNorm(n), std::memory_order_relaxed);
}

void InputStage::setPlayMode(InputPlayMode m) noexcept
{
    mode_.store(static_cast<int>(m), std::memory_order_relaxed);
}

void InputStage::restart() noexcept
{
    restartReq_.fetch_add(1, std::memory_order_relaxed);
}

std::unique_ptr<InputBuffer> InputStage::offer(std::unique_ptr<InputBuffer> buffer) noexcept
{
    if(!buffer)
        return nullptr;
    return std::unique_ptr<InputBuffer>(pending_.exchange(buffer.release(), std::memory_order_acq_rel));
}

std::unique_ptr<InputBuffer> InputStage::reclaim() noexcept
{
    InputBuffer* b = nullptr;
    if(retired_.pop(b))
        return std::unique_ptr<InputBuffer>(b);
    return nullptr;
}

float InputStage::Player::next(InputPlayMode mode) noexcept
{
    if(buf == nullptr || ended)
        return 0.0f;
    const std::vector<float>& s = buf->samples;
    const std::size_t         n = s.size();
    if(n == 0)
        return 0.0f;
    const std::size_t x    = std::min<std::size_t>(static_cast<std::size_t>(kInputLoopCrossfadeFrames), n / 2);
    const std::size_t tail = n - x;
    if(x > 0 && pos == tail)
        tailOnce = mode == InputPlayMode::Once; // the mode decides how this pass ends
    float v;
    if(x > 0 && pos >= tail)
    {
        // The tail: LOOP crossfades it with the head, ONCE fades it out.
        const std::size_t k = pos - tail;
        const float       w = static_cast<float>(k + 1) / static_cast<float>(x + 1);
        v                   = tailOnce ? s[pos] * (1.0f - w) : s[pos] * (1.0f - w) + s[k] * w;
    }
    else
    {
        v = s[pos];
    }
    if(++pos >= n)
    {
        const bool stop = x > 0 ? tailOnce : mode == InputPlayMode::Once;
        if(stop)
        {
            ended = true;
            pos   = n;
        }
        else
        {
            pos = x; // the head [0, x) was played inside the crossfade
        }
    }
    return v;
}

void InputStage::finishCrossfade() noexcept
{
    xf_ = kXf;
    if(old_.buf != nullptr && retired_.push(old_.buf))
        old_ = Player{};
    // else: the retire queue is full; the old buffer is held (not read) and
    // pushed by a later pull().
}

void InputStage::startFresh() noexcept
{
    // Only called while the output is silent: level 0, or nothing that sounds
    // (the current player ended or empty, no crossfade running). A crossfade
    // still under way is ended first, so it can never be left frozen.
    if(old_.buf != nullptr && xf_ < kXf)
        finishCrossfade();
    level_ = 0.0f;
    cur_.rewind();
    restartFade_ = false;
}

void InputStage::requestRestart() noexcept
{
    // Never cuts what sounds: audible (the level up and the current player or a
    // crossfade sounding) -> the 10 ms fade-out first; else at once.
    const bool xfading = old_.buf != nullptr && xf_ < kXf;
    if(level_ > 0.0f && (cur_.playable() || xfading))
    {
        restartFade_ = true;
    }
    else
    {
        startFresh();
        restartsDone_.fetch_add(1, std::memory_order_relaxed);
    }
}

const float* InputStage::pull(std::size_t frames) noexcept
{
    if(capacity_ == 0 || !xfTable_)
        return nullptr;
    if(frames > capacity_)
        frames = capacity_;

    // 1. Requests. The generation is read before the source (both acquire): the UI
    //    thread stores the source, then increments the generation. The source is
    //    read before the exchange: the UI thread hands over a buffer before it
    //    changes the source, so a source seen here comes with every buffer offered
    //    before it.
    const std::uint32_t gen  = sourceGen_.load(std::memory_order_acquire);
    const auto          src  = static_cast<InputSource>(source_.load(std::memory_order_acquire));
    const float         gt   = gainTarget_.load(std::memory_order_relaxed);
    const auto          mode = static_cast<InputPlayMode>(mode_.load(std::memory_order_relaxed));
    const std::uint32_t rq   = restartReq_.load(std::memory_order_relaxed);

    // 2a. An old buffer held because the retire queue was full.
    if(old_.buf != nullptr && xf_ >= kXf && retired_.push(old_.buf))
        old_ = Player{};

    // 2b. A file that ended in ONCE mode starts again: when the source is chosen
    //     again (OFF -> FILE), and when the mode is LOOP (ONCE -> LOOP).
    //     Both go through requestRestart(): if the old file of a crossfade still
    //     sounds (the new one, shorter than the crossfade, has ended), the
    //     restart fades out first instead of cutting it (M2b-A2 review, B1).
    if(gen != genSeen_)
    {
        genSeen_ = gen;
        if(src == InputSource::File && cur_.ended && !restartFade_)
            requestRestart();
    }
    if(mode == InputPlayMode::Loop && cur_.ended && !restartFade_)
        requestRestart();

    // 2c. restart(): fade out first if it sounds, else at once.
    if(rq != restartSeen_)
    {
        restartSeen_ = rq;
        if(!restartFade_)
            requestRestart();
    }

    // 2d. A new buffer (one exchange), unless the stage is busy switching.
    if(next_ == nullptr && old_.buf == nullptr)
        next_ = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if(next_ != nullptr)
    {
        const bool empty = next_->samples.empty();
        if(!empty && level_ > 0.0f && cur_.playable())
        {
            // Audible: crossfade from the current buffer to the new one.
            old_         = cur_;
            cur_         = Player{};
            cur_.buf     = next_;
            next_        = nullptr;
            xf_          = 0;
            restartFade_ = false; // the new buffer starts at 0 anyway
            playingFrames_.store(cur_.buf->samples.size(), std::memory_order_relaxed);
            switches_.fetch_add(1, std::memory_order_relaxed);
        }
        else if(!empty || !(level_ > 0.0f))
        {
            // Nothing audible (or an empty buffer once the fade-out is over):
            // switch, if the buffer it replaces can be retired.
            if(cur_.buf == nullptr || retired_.push(cur_.buf))
            {
                cur_     = Player{};
                cur_.buf = next_;
                next_    = nullptr;
                startFresh(); // the output was silent: the new one fades in from 0
                playingFrames_.store(cur_.buf->samples.size(), std::memory_order_relaxed);
                switches_.fetch_add(1, std::memory_order_relaxed);
            }
            // else: the retire queue is full; stay silent and try again next call.
        }
        // else: an empty buffer while the current one sounds: fade out first.
    }

    auto targetNow = [&]() noexcept {
        const bool xfading = old_.buf != nullptr && xf_ < kXf;
        return (next_ == nullptr && !restartFade_ && src == InputSource::File && (cur_.playable() || xfading)) ? 1.0f
                                                                                                              : 0.0f;
    };
    float target = targetNow();

    // 3. Nothing audible and nothing fading: no input at all.
    if(!(level_ > 0.0f) && !(target > 0.0f))
    {
        if(old_.buf != nullptr && xf_ < kXf)
            finishCrossfade(); // never return nullptr with a crossfade half-way
        level_ = 0.0f;
        gain_  = gt;
        if(restartFade_)
        {
            cur_.rewind();
            restartFade_ = false;
            restartsDone_.fetch_add(1, std::memory_order_relaxed);
        }
        active_.store(false, std::memory_order_relaxed);
        position_.store(cur_.pos, std::memory_order_relaxed);
        ended_.store(cur_.ended, std::memory_order_relaxed);
        crossfading_.store(old_.buf != nullptr && xf_ < kXf, std::memory_order_relaxed); // false: ended above
        holdsOld_.store(old_.buf != nullptr || next_ != nullptr, std::memory_order_relaxed);
        if(old_.buf == nullptr && next_ == nullptr)
            settled_.store(switches_.load(std::memory_order_relaxed), std::memory_order_release);
        return nullptr;
    }
    if(!(level_ > 0.0f))
        cur_.rewind(); // a fresh start plays the file from its beginning

    // 4. Per sample: fade, smoothed gain, the player(s).
    constexpr float kStep = 1.0f / static_cast<float>(kInputFadeFrames);
    const float*    tab   = xfTable_.get();
    float*          out   = out_.get();
    float           peak  = 0.0f;
    for(std::size_t i = 0; i < frames; ++i)
    {
        if(target > level_)
            level_ = level_ + kStep < 1.0f ? level_ + kStep : 1.0f;
        else if(target < level_)
            level_ = level_ - kStep > 0.0f ? level_ - kStep : 0.0f;

        const float d    = gt - gain_;
        const float next = gain_ + gainCoef_ * d;
        // Snap once the step no longer changes the float (or is negligible), so
        // the target is reached exactly instead of stalling a few ULPs short.
        gain_ = (std::fabs(d) < kGainSnap || next == gain_) ? gt : next;

        float v = 0.0f;
        if(level_ > 0.0f)
        {
            float s;
            if(old_.buf != nullptr && xf_ < kXf)
            {
                s = cur_.next(mode) * tab[xf_] + old_.next(mode) * tab[kXf - 1 - xf_];
                if(++xf_ == kXf)
                {
                    finishCrossfade();
                    target = targetNow();
                }
            }
            else
            {
                s = cur_.next(mode);
            }
            v = s * gain_ * level_;
            if(cur_.ended && target > 0.0f)
                target = targetNow(); // ONCE: the end reached (silence from here)
        }
        else
        {
            // The level has reached 0 (source OFF, a restart, an empty buffer).
            if(old_.buf != nullptr && xf_ < kXf)
            {
                finishCrossfade();
                target = targetNow();
            }
            if(restartFade_)
            {
                cur_.rewind();
                restartFade_ = false;
                restartsDone_.fetch_add(1, std::memory_order_relaxed);
                target = targetNow();
            }
        }
        out[i]        = v;
        const float a = std::fabs(v);
        peak          = a > peak ? a : peak;
    }

    // 5. Meters and status.
    peak_.offer(peak);
    active_.store(true, std::memory_order_relaxed);
    position_.store(cur_.pos, std::memory_order_relaxed);
    ended_.store(cur_.ended, std::memory_order_relaxed);
    crossfading_.store(old_.buf != nullptr && xf_ < kXf, std::memory_order_relaxed);
    holdsOld_.store(old_.buf != nullptr || next_ != nullptr, std::memory_order_relaxed);
    if(old_.buf == nullptr && next_ == nullptr)
        settled_.store(switches_.load(std::memory_order_relaxed), std::memory_order_release);
    return out;
}

} // namespace audreybrick
