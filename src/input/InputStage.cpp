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

#include "input/LiveInput.h"

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

void InputStage::prepare(std::size_t frames, std::size_t blockFrames)
{
    capacity_ = frames > 0 ? frames : 1;
    // M2b-B: the output buffer (the live target, margin and repeat length are
    // counted in it), the configured target, and the target itself.
    liveBlock_  = blockFrames > 0 ? blockFrames : kLiveDefaultTargetFrames / kLiveTargetBuffers;
    liveRepeat_ = std::min<std::size_t>(liveBlock_, kLiveMaxRepeatFrames);
    setLiveBuffers(liveConfBuffers_);
    out_      = std::make_unique<float[]>(capacity_);
    if(!xfTable_)
    {
        xfTable_ = std::make_unique<float[]>(kXf);
        constexpr double kHalfPi = 1.57079632679489661923;
        for(std::size_t k = 0; k < kXf; ++k)
            xfTable_[k] = static_cast<float>(std::sin(kHalfPi * static_cast<double>(k + 1) / static_cast<double>(kXf + 1)));
    }
}

void InputStage::setLiveTargetBuffers(std::size_t n) noexcept
{
    liveConfBuffers_ = n < kLiveMinTargetBuffers ? kLiveMinTargetBuffers : (n > kLiveMaxTargetBuffers ? kLiveMaxTargetBuffers : n);
    setLiveBuffers(liveConfBuffers_);
}

void InputStage::setLiveBuffers(std::size_t buffers) noexcept
{
    // The target, and the overfill margin (four buffers, or twice the target if
    // that is more); both fit in half the ring with the repeat's keep.
    liveBuffers_ = buffers;
    liveTarget_  = buffers * liveBlock_;
    liveMargin_  = std::max(kLiveOverfillMarginBuffers * liveBlock_, 2 * liveTarget_);
    const std::size_t most = LiveInput::kCapacity / 2 - kLiveMaxRepeatFrames;
    if(liveTarget_ + liveMargin_ > most)
    {
        liveTarget_ = std::min(liveTarget_, most / 3);
        liveMargin_ = most - liveTarget_;
    }
    liveTargetPub_.store(liveTarget_, std::memory_order_relaxed);
    liveMarginPub_.store(liveMargin_, std::memory_order_relaxed);
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
    // Only called while the file is silent: level 0, or nothing of the file that
    // sounds (the current player ended or empty, no crossfade running), or the
    // level belongs to the live input (M2b-B: then it is left alone; the file
    // starts from 0 when it becomes the sounding source). A crossfade still under
    // way is ended first, so it can never be left frozen.
    if(old_.buf != nullptr && xf_ < kXf)
        finishCrossfade();
    if(sounding_ != InputSource::Live)
        level_ = 0.0f;
    cur_.rewind();
    restartFade_ = false;
}

void InputStage::requestRestart() noexcept
{
    // Never cuts what sounds: audible (the level up and the current player or a
    // crossfade sounding) -> the 10 ms fade-out first; else at once.
    const bool xfading = old_.buf != nullptr && xf_ < kXf;
    if(sounding_ == InputSource::File && level_ > 0.0f && (cur_.playable() || xfading))
    {
        restartFade_ = true;
    }
    else
    {
        startFresh();
        restartsDone_.fetch_add(1, std::memory_order_relaxed);
    }
}

// --- Live (M2b-B) ---------------------------------------------------------------------------
void InputStage::livePublishRead() noexcept
{
    // The slots from `keep` (the repeat length) behind the earliest position in
    // use stay unwritten, so a repeat can read them; never moves back.
    const std::uint64_t pos  = liveXf_ < liveXfLen_ ? std::min(liveOld_, liveRead_) : liveRead_;
    const std::uint64_t cand = pos >= liveRepeat_ ? pos - liveRepeat_ : 0;
    if(cand > livePub_)
        livePub_ = cand;
    live_->setReadIndex(livePub_);
}

void InputStage::liveDiscard() noexcept
{
    // Everything written so far is skipped; nothing before this point is ever
    // read again (a repeat may not reach before liveStart_).
    liveRunning_ = false;
    liveXf_      = 0;
    liveXfLen_   = 0;
    liveRead_    = liveW_;
    liveStart_   = liveW_;
    livePublishRead();
}

void InputStage::liveResetBaseline() noexcept
{
    liveNoHeadroom_  = false;
    liveRepeatClock_ = liveClock_;
    liveBaseReady_ = false;
    liveBaseMin_   = SIZE_MAX;
    liveBaseStart_ = liveClock_;
}

void InputStage::liveChunkStart(InputSource src, float& gt, std::size_t frames) noexcept
{
    // A new session (a device opened or closed): stop reading at once, skip
    // everything written so far, and fade out on the last output value (the
    // hold, after the gain: it can only decay). The target goes back to the
    // configured one.
    // The writer's index first, then the session (both acquire): if the session
    // is new, the index is read again, so the new start counts from after it.
    // A session seen late (the thread preempted between the two loads during a
    // whole device open) can therefore only make the stage skip more, never read
    // a new device's samples as if they were the old one's.
    liveW_                  = live_->writeIndex();
    const std::uint32_t ses = live_->session();
    if(ses != liveSessionSeen_)
    {
        liveSessionSeen_  = ses;
        liveW_            = live_->writeIndex();
        liveHoldOnly_     = true;
        liveLastUnderrun_ = kNoUnderrun;
        setLiveBuffers(liveConfBuffers_);
        liveDiscard();
    }
    else if(sounding_ != InputSource::Live && src != InputSource::Live && (liveRunning_ || liveRead_ != liveW_))
    {
        // The live input has faded out and LIVE is not chosen (M2b-B review 2,
        // S1): stop reading and skip whatever is written meanwhile, so a return
        // to LIVE always buffers afresh from the writer's position (and snaps
        // the gain); nothing from before is ever read.
        liveDiscard();
    }
    else if(src == InputSource::Live && livePrevSrc_ != InputSource::Live && !liveRunning_)
    {
        // LIVE chosen again (and not still sounding): whatever was written before
        // this moment is skipped too, so buffering starts here.
        liveDiscard();
    }
    livePrevSrc_ = src;
    if(sounding_ == InputSource::Live && liveRunning_)
    {
        const std::uint64_t fill = liveW_ - liveRead_;
        if(fill < frames)
        {
            // A real underrun (the ring cannot supply the chunk: a stall, a lost
            // device, or bursts larger than the target): play what is there, then
            // hold, while the level fades out; buffer to the target again. Two
            // within a second (while LIVE is chosen) double the target, at most
            // kLiveMaxAutoBuffers buffers.
            liveRunning_ = false;
            liveXf_      = liveXfLen_; // a running crossfade ends; what is left plays straight
            if(src == InputSource::Live)
            {
                live_->countUnderrun();
                // Also raised when the settled minimum left no room for a repeat
                // (half a repeat below it is at or under the chunk size): then
                // drift can only end in underruns, so the target must grow.
                const bool burst = liveLastUnderrun_ != kNoUnderrun && liveClock_ - liveLastUnderrun_ < kInputSampleRate;
                if((burst || liveNoHeadroom_) && liveBuffers_ < kLiveMaxAutoBuffers)
                {
                    setLiveBuffers(std::min(liveBuffers_ * 2, kLiveMaxAutoBuffers));
                    liveRaises_.fetch_add(1, std::memory_order_release); // after the new target is stored
                }
                liveLastUnderrun_ = liveClock_;
            }
        }
        else if(liveXf_ >= liveXfLen_)
        {
            if(fill > liveTarget_ + liveMargin_)
            {
                // Overfill (capture fast, or the output stalled): skip ahead to the
                // target with a linear crossfade from the old position to the new.
                live_->countOverfill();
                liveOld_   = liveRead_;
                liveRead_  = liveW_ - liveTarget_;
                liveXfLen_ = std::min(kLiveSkipCrossfadeFrames, liveTarget_);
                liveXf_    = 0;
                liveResetBaseline();
            }
            else if(liveBaseReady_)
            {
                // Capture slow: repeat the last `liveRepeat_` samples with a
                // crossfade, before the ring runs dry.
                const std::size_t low = liveBaseMin_ > liveRepeat_ / 2 ? liveBaseMin_ - liveRepeat_ / 2 : 0;
                liveNoHeadroom_       = !(low > frames);
                // Half a repeat below the settled minimum, never at it (M2b-B
                // review 2, S2). If that is at or under the chunk size, a slow
                // device underruns first and the target is raised instead. At
                // most one repeat per kLiveBaselineFrames of output, only while
                // the device delivers (a stalled device runs dry instead of
                // looping), and never reaching before the session's (or the
                // last discard's) first sample.
                if(low > frames && fill < low && liveRead_ >= livePub_ + liveRepeat_ &&
                   liveRead_ >= liveStart_ + liveRepeat_ && liveW_ != liveRepeatW_ &&
                   liveClock_ - liveRepeatClock_ >= kLiveBaselineFrames)
                {
                    liveRepeatW_     = liveW_;
                    liveRepeatClock_ = liveClock_;
                    live_->countRepeat();
                    liveOld_   = liveRead_;
                    liveRead_  = liveRead_ - liveRepeat_;
                    liveXfLen_ = std::min(kLiveSkipCrossfadeFrames, liveRepeat_);
                    liveXf_    = 0;
                }
            }
            else
            {
                // The settled minimum: the smallest fill over kLiveBaselineFrames.
                liveBaseMin_ = std::min(liveBaseMin_, static_cast<std::size_t>(fill));
                if(liveClock_ - liveBaseStart_ >= kLiveBaselineFrames)
                    liveBaseReady_ = true;
            }
        }
    }
    // Start (and restart after an underrun or a new session): only from silence,
    // the source LIVE, and the ring holding the target. The read position jumps
    // to the target behind the writer (older samples are dropped), and the gain
    // snaps to its target, read again here (after the session and the writer's
    // index): the level is 0, so nothing clicks; a gain set to zero before a
    // device opened is exactly zero for its first sample.
    if(sounding_ == InputSource::Live && src == InputSource::Live && !liveRunning_ && !(level_ > 0.0f) &&
       liveW_ - liveRead_ >= liveTarget_)
    {
        liveRead_     = liveW_ - liveTarget_;
        liveRunning_  = true;
        liveHoldOnly_ = false;
        liveXf_       = 0;
        liveXfLen_    = 0;
        liveHoldOut_  = 0.0f;
        gt            = gainTarget_.load(std::memory_order_relaxed);
        gain_         = gt;
        liveResetBaseline();
        livePublishRead();
    }
    if(src == InputSource::Live || sounding_ == InputSource::Live)
        live_->publishFill(static_cast<std::size_t>(liveW_ - liveRead_));
    liveReadCount_ = 0;
}

float InputStage::liveNext(bool& held) noexcept
{
    held = false;
    float s;
    if(liveXf_ < liveXfLen_)
    {
        // A skip (overfill) or a repeat (slow capture): old * (1 - w) + new * w,
        // w = (k + 1) / (L + 1). A side that has run dry contributes the last
        // value read.
        const float a = liveOld_ < liveW_ ? live_->at(liveOld_++) : liveRaw_;
        const float b = liveRead_ < liveW_ ? live_->at(liveRead_++) : liveRaw_;
        const float w = static_cast<float>(liveXf_ + 1) / static_cast<float>(liveXfLen_ + 1);
        s             = a * (1.0f - w) + b * w;
        ++liveXf_;
        ++liveReadCount_;
    }
    else if(!liveHoldOnly_ && liveRead_ < liveW_)
    {
        s = live_->at(liveRead_++);
        ++liveReadCount_;
    }
    else
    {
        held = true; // ran dry (or a new session): the caller holds the last output value
        return liveRaw_;
    }
    liveRaw_ = s;
    return s;
}

void InputStage::liveChunkEnd(std::size_t frames) noexcept
{
    livePublishRead();
    if(liveReadCount_ > 0)
        live_->addRead(liveReadCount_);
    live_->setRunning(liveRunning_);
    liveClock_ += frames;
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
    float               gt   = gainTarget_.load(std::memory_order_relaxed);
    const auto          mode = static_cast<InputPlayMode>(mode_.load(std::memory_order_relaxed));
    const std::uint32_t rq   = restartReq_.load(std::memory_order_relaxed);

    // M2b-B: the level belongs to the source that sounds (FILE or LIVE); while
    // it is 0 nothing sounds and the chosen source is the one in force.
    if(!(level_ > 0.0f))
        sounding_ = src;

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
        const bool empty   = next_->samples.empty();
        const bool audible = sounding_ == InputSource::File && level_ > 0.0f; // the level is the file's
        if(!empty && audible && cur_.playable())
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
        else if(!empty || !audible)
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

    // 2e. The live input (M2b-B): a new session, then underrun, overfill and the
    //     start, at the start of the chunk.
    if(live_ != nullptr)
        liveChunkStart(src, gt, frames);

    auto targetNow = [&]() noexcept {
        if(sounding_ == InputSource::Live)
            return (src == InputSource::Live && liveRunning_) ? 1.0f : 0.0f;
        if(sounding_ != InputSource::File)
            return 0.0f;
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
        if(live_ != nullptr)
            liveChunkEnd(frames);
        position_.store(cur_.pos, std::memory_order_relaxed);
        ended_.store(cur_.ended, std::memory_order_relaxed);
        crossfading_.store(old_.buf != nullptr && xf_ < kXf, std::memory_order_relaxed); // false: ended above
        holdsOld_.store(old_.buf != nullptr || next_ != nullptr, std::memory_order_relaxed);
        if(old_.buf == nullptr && next_ == nullptr)
            settled_.store(switches_.load(std::memory_order_relaxed), std::memory_order_release);
        return nullptr;
    }
    if(!(level_ > 0.0f) && sounding_ == InputSource::File)
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
        if(level_ > 0.0f && sounding_ == InputSource::Live)
        {
            // M2b-B. A held value is the last output value after the gain, so
            // the hold fade only decays, whatever the gain does meanwhile.
            bool        held = false;
            const float x    = liveNext(held);
            if(held)
            {
                v = liveHoldOut_ * level_;
            }
            else
            {
                liveHoldOut_ = x * gain_;
                v            = liveHoldOut_ * level_;
            }
        }
        else if(level_ > 0.0f)
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
            if(sounding_ != src)
            {
                // M2b-B: the fade-out of one source is over; the other one takes
                // over (FILE <-> LIVE, or either -> OFF). A file starts from its
                // beginning; the live input once the ring holds the target (the
                // next chunk checks).
                sounding_ = src;
                if(sounding_ == InputSource::File)
                    cur_.rewind();
                target = targetNow();
            }
        }
        out[i]        = v;
        const float a = std::fabs(v);
        peak          = a > peak ? a : peak;
    }

    // 5. Meters and status.
    if(live_ != nullptr)
        liveChunkEnd(frames);
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
