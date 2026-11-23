// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "mod/ModControlSource.h"

#include <algorithm>

#include "input/InputTypes.h"

namespace audreybrick
{

ModControlSource::ModControlSource(ControlSource& inner, std::size_t queueCapacity)
: inner_(inner), queue_(queueCapacity)
{
    for(auto& a : slotOut_)
        a.store(0.0f, std::memory_order_relaxed);
    for(auto& a : targetOff_)
        a.store(0.0f, std::memory_order_relaxed);
}

void ModControlSource::prepare(std::size_t frames)
{
    buf_      = std::make_unique<float[]>(frames > 0 ? frames : 1);
    capacity_ = frames;
}

void ModControlSource::reset() noexcept
{
    matrix_.reset();
    in_          = nullptr;
    pre_         = nullptr;
    out_         = nullptr;
    frames_      = 0;
    cursor_      = 0;
    done_        = 0;
    blockSample_ = 0;
    pFrom_       = 1.0f;
    pTo_         = 1.0f;
    modFrom_     = false;
    modTo_       = false;
    ratioNow_    = 1.0f;
    taper_       = 0.0f;
}

bool ModControlSource::setModSlot(std::size_t index, const ModSlotSettings& s) noexcept
{
    if(index >= kModSlots)
        return false;
    return queue_.push(SlotCommand{static_cast<std::uint8_t>(index), sanitizeModSlot(s)});
}

void ModControlSource::attachScript(ScriptControlSource& script) noexcept
{
    script.setModSink(this);
    copyAlways_ = true;
}

void ModControlSource::onScriptModEvent(const ScriptModEvent& ev) noexcept
{
    if(ev.slot >= kModSlots)
        return;
    ModSlotSettings s = matrix_.slot(ev.slot);
    switch(ev.field)
    {
        case ScriptModField::Shape: s.shape = ev.shape; break;
        case ScriptModField::Target: s.target = ev.target; break;
        case ScriptModField::Rate: s.rateNorm = ev.value; break;
        case ScriptModField::Hz: s.rateNorm = modRateNormFromHz(static_cast<double>(ev.value)); break;
        case ScriptModField::Depth: s.depth = ev.value; break;
        case ScriptModField::Polarity: s.polarity = ev.polarity; break;
    }
    matrix_.setSlot(ev.slot, s);
}

bool ModControlSource::inputGainWeighted() const noexcept
{
    for(std::size_t i = 0; i < kModSlots; ++i)
        if(matrix_.weight(i, ModTarget::InputGain) != 0.0f)
            return true;
    return false;
}

float ModControlSource::targetFor(float offset, float gs, bool& modulated) noexcept
{
    // gs: the gain the stage's sample at this block's start carries; n: the
    // input gain knob position it stands for (the stage's own law, inverted).
    // The taper restarts from 0 only when the stage's gain is 0 or no slot has
    // weight on InputGain (MOD-B review): an offset that is exactly 0 for a
    // while (an envelope after digital silence, a sine's zero crossing) keeps
    // it, so the next attack is modulated at once.
    if(!(gs > 0.0f) || !inputGainWeighted())
    {
        // A gain of exactly 0 (or nothing on InputGain) is left alone: a
        // closed input stays silent, and a modulator never opens it. The
        // effective gain is the stage's own.
        modulated = false;
        taper_    = 0.0f;
        return gs;
    }
    const float n = inputNormFromGain(gs);

    // The taper weight: its goal is 0 at gain 0 and rises quadratically to 1 at
    // knob kInputGainTaperNorm; the weight follows a falling goal at once (a
    // continuous function of gs, so closing is continuous) and a rising one at
    // most kInputGainTaperRise per block (an opening input reaches its
    // modulated gain over about 20 ms instead of jumping to it).
    const float r    = n / kInputGainTaperNorm;
    const float goal = n >= kInputGainTaperNorm ? 1.0f : r * r;
    taper_           = goal < taper_ ? goal : (goal < taper_ + kInputGainTaperRise ? goal : taper_ + kInputGainTaperRise);

    // An offset of exactly 0: the stage's own gain (the samples are copied
    // unchanged); the taper has moved all the same.
    modulated = offset != 0.0f;
    if(!modulated)
        return gs;
    // The stage's law at the moved knob position (inputGainFromNorm clamps
    // n + o' to 0..1).
    return inputGainFromNorm(n + offset * taper_);
}

void ModControlSource::consume(std::size_t from, std::size_t to) noexcept
{
    if(out_ == nullptr && !matrix_.hasEnvelope())
    {
        // Nothing to write (no input, or the input passed through) and no
        // follower to feed: only keep the position.
        if(to > from)
        {
            const std::size_t n = to - from;
            const std::size_t last = static_cast<std::size_t>(AudreyVoice::kControlBlock - 1);
            blockSample_ = static_cast<int>(std::min(last, static_cast<std::size_t>(blockSample_) + n));
        }
        done_ = to > done_ ? to : done_;
        return;
    }
    const bool env  = matrix_.hasEnvelope();
    const bool copy = !modFrom_ && !modTo_;
    for(std::size_t k = from; k < to; ++k)
    {
        const float x = in_ != nullptr ? in_[k] : 0.0f;
        if(env)
            matrix_.feed(pre_ != nullptr ? pre_[k] : x); // MOD-C: before the stage's gain
        if(out_ != nullptr)
        {
            if(copy)
            {
                out_[k] = x; // exact copy: the unmodulated input
            }
            else
            {
                // The effective gain, interpolated across the block, replaces
                // the gain the sample carries (divided out per sample, so the
                // stage's own smoothing within the block does not add a lag).
                const float t = static_cast<float>(blockSample_ + 1) * 0.25f;
                const float p = pFrom_ + (pTo_ - pFrom_) * t;
                const float g = gainTrace_ != nullptr ? gainTrace_[k] : gainConst_;
                out_[k]       = g > 0.0f ? x * (p / g) : x;
            }
        }
        if(blockSample_ < AudreyVoice::kControlBlock - 1)
            ++blockSample_;
    }
    done_ = to > done_ ? to : done_;
}

const float* ModControlSource::beginChunk(const float* in, std::size_t frames, const float* gainTrace,
                                          float gain, const float* pre) noexcept
{
    // Settings from the UI, once per chunk, at most the queue's capacity (a
    // producer that keeps pushing cannot hold the audio thread here).
    SlotCommand c;
    for(std::size_t k = 0; k < queue_.capacity() && queue_.pop(c); ++k)
    {
        matrix_.setSlot(c.index, c.settings);
        ++appliedLocal_;
    }

    in_         = in;
    pre_        = in != nullptr ? pre : nullptr;
    frames_     = frames;
    done_       = 0;
    gainTrace_  = gainTrace;
    gainConst_  = gain;
    // Pass-through (the app's idle path): with nothing modulating and no
    // InputGain ramp, the voice reads `in` itself, uncopied. Only settings
    // applied above can change that, so it holds for the whole chunk; a script
    // sink (attachScript) can change settings mid-chunk, so it always copies.
    const bool passThrough = !copyAlways_ && matrix_.idle() && !modFrom_ && !modTo_;
    out_ = (in != nullptr && !passThrough && buf_ != nullptr && frames <= capacity_) ? buf_.get() : nullptr;

    // The rest of the block begun in the previous chunk (none at a block start).
    consume(0, cursor_ < frames ? cursor_ : frames);

    if(in == nullptr)
        return nullptr;
    return out_ != nullptr ? out_ : in;
}

void ModControlSource::read(ControlFrame& out) noexcept
{
    const std::size_t end = cursor_ + static_cast<std::size_t>(AudreyVoice::kControlBlock);
    const bool        quiet = out_ == nullptr && !matrix_.hasEnvelope(); // no sample to write or feed

    // Samples before this block start not yet consumed (only if the schedule was
    // broken; normally none), so the envelope sees every sample once and in order.
    if(!quiet)
        consume(done_, cursor_ < frames_ ? cursor_ : frames_);

    inner_.read(out); // a script's mod lines for this block arrive here
    matrix_.tick();

    if(quiet && matrix_.idle() && !modTo_ && !modFrom_)
    {
        // Nothing modulates and nothing to write: the wrapped source's zeros
        // (knobMod 0, delaySwitchMod false) stand as they are. No slot has
        // weight on InputGain, so the taper starts again from 0.
        taper_       = 0.0f;
        blockSample_ = 0;
        done_        = end < frames_ ? end : frames_;
        cursor_      = end;
        return;
    }

    for(std::size_t k = 0; k < kKnobCount; ++k)
        out.knobMod[k] = matrix_.offset(modTargetFromKnob(static_cast<Knob>(k)));
    out.delaySwitchMod = matrix_.switchRequest();

    const float gs = (gainTrace_ != nullptr && cursor_ < frames_) ? gainTrace_[cursor_] : gainConst_;
    pFrom_         = pTo_;
    modFrom_       = modTo_;
    pTo_           = targetFor(matrix_.offset(ModTarget::InputGain), gs, modTo_);
    ratioNow_      = modTo_ ? pTo_ / gs : 1.0f;
    blockSample_   = 0;

    consume(done_, end < frames_ ? end : frames_);
    cursor_ = end;
}

void ModControlSource::endChunk() noexcept
{
    consume(done_, frames_); // normally nothing left: the last block took its samples
    cursor_ = cursor_ >= frames_ ? cursor_ - frames_ : 0;
    in_     = nullptr;
    pre_    = nullptr;
    out_    = nullptr;
    frames_ = 0;
    done_   = 0;

    for(std::size_t i = 0; i < kModSlots; ++i)
        slotOut_[i].store(matrix_.slotValue(i), std::memory_order_relaxed);
    for(std::size_t t = 0; t < kModTargetCount; ++t)
        targetOff_[t].store(matrix_.offset(static_cast<ModTarget>(t)), std::memory_order_relaxed);
    switchReq_.store(matrix_.switchRequest(), std::memory_order_relaxed);
    ratioPub_.store(ratioNow_, std::memory_order_relaxed);
    applied_.store(appliedLocal_, std::memory_order_relaxed);
}

} // namespace audreybrick
