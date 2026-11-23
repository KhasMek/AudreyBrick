// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ModControlSource: the modulation's place in the audio path (MOD-A,
// docs/briefs/MOD.md). A ControlSource decorator: it wraps the instrument's
// control source (ButtonControlSource in the app, ScriptControlSource in the
// render tool) and, once per control block, adds the modulation fields of the
// ControlFrame (knobMod[], delaySwitchMod) that AudreyVoice applies after the
// original's parameter smoother. It also owns the InputGain modulation and feeds
// the envelope followers with the input.
//
// Per audio chunk (AudioRender::render, or the render tool):
//
//   const float* in2 = mod.beginChunk(in, n, stage.gainTrace(), 1.0f, stage.preGain());
//   voice.process(mod, in2, outL, outR, n);
//   mod.endChunk();
//
//   beginChunk(): applies the settings queued by the UI (setModSlot) once, and
//     returns the pointer the voice must read: nullptr if `in` is nullptr;
//     `in` itself while nothing modulates (ModMatrix::idle() and no InputGain
//     ramp; not with attachScript()); otherwise the decorator's own buffer,
//     which holds `in` (exact copies) with the InputGain modulation applied. It fills the first samples of that
//     buffer that belong to a control block begun in the previous chunk.
//   read() (called by the voice at each control block start): the wrapped
//     source's read() (a script's mod lines reach onScriptModEvent() from there,
//     so they apply at their own block, whatever the chunk size), one
//     ModMatrix::tick(), the modulation fields, and then the block's input
//     samples: each is fed raw (before InputGain modulation) to the envelope
//     followers (MOD-C: the pre-gain sample, see below) and written to the
//     buffer times the InputGain ramp.
//   endChunk(): publishes the read-back atomics once.
//
// The block schedule: the decorator counts samples itself and expects read()
// at every 4th sample of the stream, as AudreyVoice does when both start
// together (a fresh or reset() decorator with a fresh or init()ed voice). The
// voice reads input sample k only after read() for k's block, so writing the
// block's samples in read() is in time. Nothing here depends on how the stream
// is cut into chunks (tests: mod_source_test, chunkIndependence and the
// InputGain tests in mod_inputgain_test).
//
// The envelope therefore sees, at block start G, the input up to sample G-1:
// it is computed per sample. MOD-C: on the input BEFORE the stage's gain (and
// before the InputGain modulation): the `pre` record beginChunk() is given
// (InputStage::preGain(): the source's samples after the stage's fades and
// crossfades, without the gain), so the envelope follows the input at any
// gain, -inf dB included, while the voice hears nothing of it. Without a
// `pre` record (the render tool and tests, at a constant gain) it follows `in`
// itself. A chunk with no input (nullptr: the source OFF, or nothing to play)
// feeds zeros.
//
// InputGain: the stage's samples already carry its smoothed gain (and its fade
// level). The stage records, per sample, the gain each sample carries
// (InputStage::gainTrace()). Per control block, with g_s the gain at the
// block's first sample and o the summed InputGain offset:
//   n  = inputNormFromGain(g_s)          (the gain knob position g_s stands for)
//   o' = o * w,  w -> min(1, (n / kInputGainTaperNorm)^2), following a fall at
//        once and a rise at most kInputGainTaperRise per block (20 ms to 1);
//        w restarts from 0 only in a block where the stage's gain is 0 or no
//        slot has a (smoothed) weight on InputGain; an offset that is exactly 0
//        for a while (an envelope after silence) keeps w, so the next attack
//        is modulated at once (MOD-B review)
//   P  = inputGainFromNorm(clamp(n + o', 0, 1))   (the effective input gain)
// P is interpolated linearly per sample across the block (sample j of 4 gets
// P_prev + (P - P_prev) * (j + 1) / 4), and each sample x_i is written as
// x_i * P_i / g_i with g_i the gain that sample carries, so the effective gain
// is exactly the interpolated P (times the stage's fade level): it follows the
// stage's smoothing, fades and gain changes sample by sample, not the stage's
// target. The taper makes it go to 0 continuously as the stage's gain smooths
// to 0 (closing an input, a device open); its limited rise makes an opening
// input reach its modulated gain over about 20 ms (a positive offset near knob
// 0 would otherwise be a jump from silence to up to +12 dB). Above knob 0.25
// (-12 dB), once risen, the offset applies in full. Only the taper weight is
// rate-limited, never the offset: audio-rate InputGain modulation stays
// audio-rate. A gain of exactly 0 is left alone: a closed input stays silent
// and a modulator never opens it. A block without InputGain modulation has
// P = g_s; with no modulation at either end of a block the samples are copied
// unchanged, so with no slot on InputGain the voice reads exactly `in`.
//
// Threads:
//   - UI thread (one producer): setModSlot().
//   - Audio thread: beginChunk(), read(), endChunk(), onScriptModEvent(),
//     setSlotNow().
//   - Any thread: the read-back getters (relaxed atomics written by endChunk()).
//   - Main thread before audio starts / while audio is stopped: prepare(), reset().
// No allocation, lock, I/O, logging or waiting on the audio thread.
//
// API for MOD-B (the MOD tab), on the UI thread, through ModUiPort (below),
// which exposes only these calls:
//   - setModSlot(index, settings): queues the slot's complete settings; applied
//     at the next audio chunk. Returns false (and nothing changes) if the queue
//     is full or the index is not 0..kModSlots-1; the UI keeps its own copy of
//     the settings and may simply send them again.
//   - slotOutput(i): the slot's current value before depth (-1..+1, envelope
//     0..1), for an activity display.
//   - targetOffset(t): the summed offset now applied to target t (normalised
//     knob travel, before the voice's clamp). The moving value of knob k is
//     clamp(knob + targetOffset(modTargetFromKnob(k)), 0, 1).
//   - switchRequested(): the modulated delay switch request.
//   - inputGainRatio(): the InputGain ratio of the last block.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "control/ScriptControlSource.h"
#include "control/SpscQueue.h"
#include "core/ControlSource.h"
#include "mod/ModMatrix.h"
#include "mod/ModTypes.h"

namespace audreybrick
{

class ModControlSource final : public ControlSource, public ScriptModSink
{
  public:
    static constexpr std::size_t kQueueCapacity = 64;
    // InputGain: below this input gain knob position the offset is tapered
    // (quadratically) to 0, so the effective gain closes continuously.
    static constexpr float kInputGainTaperNorm = 0.25f;
    // ... and the taper weight rises by at most this per control block (from 0
    // to 1 in 960 samples, 20 ms); it falls with the stage's gain at once.
    static constexpr float kInputGainTaperRise = 4.0f / 960.0f;

    // Allocates the settings queue. Every slot starts with target None.
    explicit ModControlSource(ControlSource& inner, std::size_t queueCapacity = kQueueCapacity);
    ModControlSource(const ModControlSource&)            = delete;
    ModControlSource& operator=(const ModControlSource&) = delete;

    ControlSource& inner() noexcept { return inner_; }

    // Allocates the input buffer for chunks of up to `frames` frames. Call before
    // audio starts. A chunk longer than this (or any chunk before prepare())
    // passes the input through unmodified (no InputGain modulation; the
    // envelope still sees it).
    void        prepare(std::size_t frames);
    std::size_t capacity() const noexcept { return capacity_; }

    // Back to the start: ModMatrix::reset(), the block schedule at sample 0, no
    // InputGain ramp. Not while audio runs; queued settings stay queued.
    void reset() noexcept;

    // Makes this decorator the sink of `script`'s mod lines (render tool,
    // tests). Script lines can change slots in the middle of a chunk, so the
    // input is then always copied (never passed through). Before audio starts.
    void attachScript(ScriptControlSource& script) noexcept;

    // --- UI thread -----------------------------------------------------------------
    bool setModSlot(std::size_t index, const ModSlotSettings& s) noexcept;

    // --- Audio thread -------------------------------------------------------------
    // `gainTrace`: the input stage's smoothed gain per sample of this chunk
    // (InputStage::gainTrace()); nullptr: the constant `gain` (the render tool
    // and tests: 1, unity). MOD-C, `pre`: the envelope followers' input for this
    // chunk, `frames` samples (InputStage::preGain()); nullptr: `in` itself.
    // Ignored when `in` is nullptr (the followers then see silence).
    const float* beginChunk(const float* in, std::size_t frames, const float* gainTrace, float gain = 1.0f,
                            const float* pre = nullptr) noexcept;
    void         endChunk() noexcept;
    void         read(ControlFrame& out) noexcept override;
    void         onScriptModEvent(const ScriptModEvent& ev) noexcept override;
    // Applies settings at once (tests and tools; the app uses setModSlot()).
    void setSlotNow(std::size_t index, const ModSlotSettings& s) noexcept { matrix_.setSlot(index, s); }
    const ModMatrix& matrix() const noexcept { return matrix_; }

    // --- Any thread: read-back, as of the last endChunk() --------------------------
    float slotOutput(std::size_t i) const noexcept
    {
        return i < kModSlots ? slotOut_[i].load(std::memory_order_relaxed) : 0.0f;
    }
    float targetOffset(ModTarget t) const noexcept
    {
        const auto i = static_cast<std::size_t>(t);
        return i < kModTargetCount ? targetOff_[i].load(std::memory_order_relaxed) : 0.0f;
    }
    bool  switchRequested() const noexcept { return switchReq_.load(std::memory_order_relaxed); }
    float inputGainRatio() const noexcept { return ratioPub_.load(std::memory_order_relaxed); }
    // Settings applied from the queue so far (tests).
    std::uint64_t settingsApplied() const noexcept { return applied_.load(std::memory_order_relaxed); }

  private:
    struct SlotCommand
    {
        std::uint8_t    index;
        ModSlotSettings settings;
    };

    void  consume(std::size_t from, std::size_t to) noexcept;
    // The effective input gain for this block (sets `modulated`); moves taper_.
    float targetFor(float offset, float stageGain, bool& modulated) noexcept;
    // Some slot has a non-zero (smoothed) weight on InputGain.
    bool inputGainWeighted() const noexcept;

    ControlSource&           inner_;
    SpscQueue<SlotCommand>   queue_;
    ModMatrix                matrix_;
    std::unique_ptr<float[]> buf_;
    std::size_t              capacity_ = 0;

    // Audio thread.
    const float* in_        = nullptr; // this chunk's input (nullptr: none)
    const float* pre_       = nullptr; // MOD-C: the followers' input (nullptr: in_)
    float*       out_       = nullptr; // where modulated samples go (nullptr: nowhere)
    std::size_t  frames_    = 0;       // this chunk's length
    std::size_t  cursor_    = 0;       // next block start, relative to this chunk
    std::size_t  done_      = 0;       // samples of this chunk consumed
    int          blockSample_ = 0;     // position within the current block (0..3)
    bool         copyAlways_  = false; // attachScript(): never pass the input through
    float        pFrom_     = 1.0f;  // effective input gain at the previous block start
    float        pTo_       = 1.0f;  // ... and at this block's start
    bool         modFrom_   = false; // the previous block's InputGain was modulated
    bool         modTo_     = false; // this block's InputGain is modulated
    float        ratioNow_  = 1.0f;  // pTo_ / gain (read-back)
    const float* gainTrace_ = nullptr; // this chunk's stage gain per sample (nullptr: gainConst_)
    float        taper_     = 0.0f;    // InputGain taper weight 0..1 (see targetFor)
    float        gainConst_ = 1.0f;
    std::uint64_t appliedLocal_ = 0;

    // Read-back.
    std::atomic<float>         slotOut_[kModSlots];
    std::atomic<float>         targetOff_[kModTargetCount];
    std::atomic<bool>          switchReq_{false};
    std::atomic<float>         ratioPub_{1.0f};
    std::atomic<std::uint64_t> applied_{0};

    static_assert(std::atomic<float>::is_always_lock_free, "read-back must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free, "read-back must be lock-free");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "read-back must be lock-free");
};

// The UI thread's view of a ModControlSource (MOD-B): setModSlot() and the
// read-back only, so the UI side cannot reach the audio-thread calls
// (setSlotNow, reset, beginChunk, read). App::modulation() returns one. A
// default-constructed port (no decorator yet) refuses every setting and reads 0.
class ModUiPort
{
  public:
    ModUiPort() noexcept = default;
    explicit ModUiPort(ModControlSource& m) noexcept : m_(&m) {}
    explicit operator bool() const noexcept { return m_ != nullptr; }
    bool setModSlot(std::size_t index, const ModSlotSettings& s) noexcept
    {
        return m_ != nullptr && m_->setModSlot(index, s);
    }
    float slotOutput(std::size_t i) const noexcept { return m_ != nullptr ? m_->slotOutput(i) : 0.0f; }
    float targetOffset(ModTarget t) const noexcept { return m_ != nullptr ? m_->targetOffset(t) : 0.0f; }
    bool  switchRequested() const noexcept { return m_ != nullptr && m_->switchRequested(); }
    float inputGainRatio() const noexcept { return m_ != nullptr ? m_->inputGainRatio() : 1.0f; }
    std::uint64_t settingsApplied() const noexcept { return m_ != nullptr ? m_->settingsApplied() : 0; }

  private:
    ModControlSource* m_ = nullptr;
};

} // namespace audreybrick
