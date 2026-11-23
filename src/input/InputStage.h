// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputStage: the audio-thread end of the instrument's input (docs/briefs/M2b.md,
// M2b-A, and since M2b-A2 the crossfade between files and the play modes, since
// M2b-B the live input: see "Live" below).
// AudioRender calls pull() once per chunk, just before AudreyVoice::process, and
// passes what it returns as `inMono`: either a pointer to `frames` input
// samples, or nullptr.
//
// What pull() does, and nothing else:
//   1. Reads the source generation, the source, the gain target, the play mode
//      and the restart counter (atomic loads; see "Memory order" below).
//   2. Retries retiring a buffer it has finished with (if the retire queue was
//      full), handles a restart() request, and picks up a newly handed-over
//      buffer (one atomic exchange) unless it is busy with one (see below).
//   3. If nothing is audible and nothing has to fade (source OFF, or no buffer,
//      or the buffer has ended in ONCE mode, and the fade level is 0): returns
//      nullptr. The voice then runs exactly as without an input (off is
//      bit-identical).
//   4. Otherwise, per sample: the fade level moves linearly towards its target
//      (1/kInputFadeFrames per sample), the gain follows its target through a
//      one-pole (time constant kInputGainSmoothingSeconds), the sample is read
//      from the current buffer (and, during a crossfade between files, from the
//      old one too) and written as sample * gain * level into the stage's own
//      preallocated buffer. Two records go along, neither of which changes the
//      output: the gain each sample carries (gainTrace(), MOD-A) and the
//      sample without the gain, sample * level (preGain(), MOD-C: the envelope
//      followers' input). At gain 0 (-inf dB) the stage runs exactly as at any
//      other gain: the source is read and advances, the output is the sample
//      times 0, and preGain() holds the input.
//   5. Takes the peak |in| of the chunk into peak() (a MaxHold, like the output
//      meters) and publishes a few status atomics.
// No allocation, free, lock, file access, logging, trigonometry or wait.
//
// Reading a buffer of N samples (one "player": a buffer, a position, and two
// flags). The tail region is its last X samples, X = kInputLoopCrossfadeFrames
// or N/2 for a file of N < 2X samples (N = 1: no tail). The play mode decides,
// when the position enters the tail region, how this pass ends:
//   - LOOP: the tail is crossfaded linearly with the first X samples,
//     x = tail * (1 - w) + head * w with w = (k + 1) / (X + 1) for k = 0..X-1,
//     and playback then continues at sample X. So the loop period is N - X and
//     the first pass plays the file from sample 0 unaltered.
//   - ONCE: the tail fades out with the same weights, x = tail * (1 - w), so the
//     last sample is at 1/(X + 1) and the next would be 0; then the player has
//     ended (ended() is true) and gives silence. Before the tail the samples are
//     the file's exactly. (N = 1: the one sample, then the end.)
//   The mode is read when the tail is entered and holds until the end of that
//   pass; a mode change inside the tail applies from the next pass. This is how
//   LOOP -> ONCE inside the tail stays click-free: the loop crossfade already
//   under way completes, playback continues at X, and the file stops at the end
//   of the next pass. LOOP -> ONCE before the tail: this pass is the last.
//   ONCE -> LOOP while the file plays: the tail it reaches loops; while it has
//   ended: as restart() (start at 0 with the fade-in). ONCE -> LOOP inside a
//   ONCE tail: the file ends (the fade-out completes) and then restarts.
//   These automatic restarts, like restart(), never cut a sound: if the old
//   file of a crossfade still sounds (a new file shorter than the crossfade has
//   already ended), they fade out over 10 ms first, which also ends the
//   crossfade and retires the old buffer. pull() never returns nullptr while a
//   crossfade is half-way: it is ended (and the old buffer retired) first.
//
// Fades (no clicks):
//   - Source OFF -> FILE (or a first buffer arriving while FILE): fade in from 0
//     over 10 ms, from the start of the file if the level had reached 0. A file
//     that had ended in ONCE mode starts again from 0 when the source is
//     switched OFF and back to FILE (a source generation counter makes sure a
//     quick OFF/FILE between two pulls is not missed).
//   - FILE -> OFF: fade out over 10 ms, then nullptr.
//   - A new non-empty buffer while the current one is audible (level > 0 and not
//     ended): an equal-power crossfade of kInputFileCrossfadeFrames (100 ms).
//     The old player goes on from where it is (still looping or ending by the
//     mode), the new one starts at sample 0; the output is
//     new * T[k] + old * T[F - 1 - k], k = 0..F-1, F = kInputFileCrossfadeFrames,
//     T[k] = sin(pi/2 * (k + 1) / (F + 1)), so T[F - 1 - k] = cos of the same
//     angle and T[k]^2 + T[F-1-k]^2 = 1 (the two files are unrelated material).
//     T is a table built in prepare(). After F samples the new buffer plays alone
//     and the old one is retired. The fade level (source fade, restart fade)
//     multiplies the sum as it multiplies plain playback. If the level reaches 0
//     during the crossfade (source OFF, a restart), the crossfade ends there and
//     the old buffer is retired.
//   - A new non-empty buffer while nothing is audible (level 0, no buffer, the
//     current one ended): it simply starts at 0 with the 10 ms fade-in.
//   - An empty buffer (0 frames) means "nothing": the current buffer fades out
//     over 10 ms and is retired; the stage then returns nullptr.
//   - restart() (UI thread, an atomic counter): if the file is audible, a 10 ms
//     fade-out, then position 0 and the fade-in; if it has ended or is silent, it
//     starts at 0 with the fade-in (or, with the source OFF, will start at 0).
//     ended() clears. A new buffer arriving during the restart's fade-out
//     replaces the restart (it starts at 0 anyway).
//   - Gain: every change goes through the one-pole; the largest step between two
//     consecutive gain values is coefficient * |target - current| <= 4 * a, with
//     a = 1 - exp(-1 / (0.010 * 48000)) = 0.00208, so at most 0.0083 of linear gain
//     per sample (the full 0 -> 4 jump), and 0.0021 per sample for 0 -> 1.
//   - GAIN (2026-11-20; the UI keeps one gain per source): while one source
//     fades out because another is chosen (FILE <-> LIVE, either -> OFF), the
//     gain is held where it is, so the old source fades out at its own gain;
//     when the level reaches 0 and the chosen source takes over, the gain snaps
//     to its target (silent: the level is 0) and the new source fades in at its
//     own gain. So a change of gain between sources never sounds on either.
//   - The crossfade's largest step for two full-scale DC files of opposite sign
//     (+1 new, -1 old) is (pi/2)/(F+1) * sqrt(2) = 4.6e-4 per sample.
//
// Live (M2b-B; the source LIVE reads the capture ring, input/LiveInput.h):
//   - The fade level belongs to one source at a time, the one that sounds
//     (FILE or LIVE): while it is above 0 that source is read; once it is 0 the
//     chosen source takes over. So FILE -> LIVE and LIVE -> FILE are the 10 ms
//     fade-out, the switch, and the fade-in; the two never sound together. A
//     file that takes over starts from its beginning; a file handed over while
//     the live input sounds is simply switched to (nothing of it is audible).
//   - The target: setLiveTargetBuffers() output buffers (1..32, default 2; the
//     buffer size comes from prepare()). Start-up: silence (nullptr) until the
//     ring holds the target; then the read position jumps to that many samples
//     behind the writer (older ones are dropped), the gain snaps to its target
//     (read again at that moment; the level is 0: no click; a gain set to 0
//     just before a device opened is exactly 0 for its first sample) and the
//     level fades in over 10 ms. GAIN: the first start of a session fades in
//     over setLiveStartFadeFrames() samples instead (the UI asks for
//     kLiveRestoreFadeFrames, 100 ms, when the open restores a remembered gain
//     above zero); a start finds the session unchanged after reading the gain,
//     or waits for the next pull.
//   - Underrun, i.e. the ring cannot supply the chunk (a stall, a lost device,
//     or bursts larger than the target): counted (only while the source is
//     LIVE); what is there is played, then the last output value is held while
//     the level fades out over 10 ms (no hard edge); then silence until the ring
//     holds the target again, then the start above. Two underruns within a
//     second (of output), or one underrun after the settled minimum left no
//     room for a repeat (see Repeat), double the target, up to kLiveMaxAutoBuffers (never a
//     configured target above that); liveTargetRaises() counts the raises. It
//     never lowers by itself; a new session resets it to the configured target.
//   - Overfill (capture fast; more than target + margin, the margin being four
//     output buffers or twice the target, whichever is more): counted; the read
//     position skips to the target behind the writer, with a linear crossfade
//     of min(kLiveSkipCrossfadeFrames, target) samples from the old position to
//     the new one, x = old * (1 - w) + new * w, w = (k + 1) / (L + 1).
//   - Repeat (capture slow): after a start or a skip the stage records the
//     smallest fill over kLiveBaselineFrames (the settled minimum, which
//     includes the device's burstiness). Once the fill sinks half a repeat
//     below it (and below the chunk size plus a quarter repeat at most), the
//     read position jumps back by the repeat length R = min(output buffer,
//     kLiveMaxRepeatFrames) with the same linear crossfade (min(480, R)
//     samples), adding R to the fill before the ring runs dry. Counted
//     separately (LiveInput::repeats()). At most one per kLiveBaselineFrames of
//     output, only while the writer advances (a stalled device runs dry, one
//     underrun, instead of looping), never reaching before the first sample of
//     the session (or of the last discard, below), and never when half a
//     repeat below the settled minimum is at or under the chunk size (then a
//     slow device underruns and the target is raised). Repeats never raise
//     the target.
//   - The index published to the writer stays R behind the earliest position
//     in use (and never moves back), so the samples a repeat goes back to and
//     both regions of a crossfade are never overwritten.
//   - Leaving LIVE: once the level has reached 0 with another source chosen,
//     the stage stops reading and keeps skipping whatever is written (a
//     discard every chunk, and once more in the chunk where LIVE is chosen
//     again), so a return to LIVE always buffers afresh from the writer's
//     position at that moment and snaps the gain; nothing written before is read.
//     The live input stops running at the sample where its fade-out ends, so
//     this holds also when LIVE is chosen again in the very next pull() (M2b-B2).
//   - A new session (the UI opened or closed a device): reading stops at once,
//     everything written so far is skipped, and the level fades out on the
//     held last output value; nothing of the new device is read before the
//     level is 0 and the ring has buffered to the target again. Not counted as
//     an underrun.
//   - The hold is the last value after the gain (M2b-B review, S2): it is
//     multiplied by the falling level only, so a hold fade can only decay, even
//     if the gain is raised meanwhile.
//   - Step bound: with a steady gain every output step is at most the signal's
//     own step plus |x| / 480 (the level) plus, during a skip or repeat,
//     |new - old| / (L + 1); tests/input/input_live_test.cpp checks it.
//   - Published (LiveInput's reader statistics): the fill at the start of each
//     chunk while LIVE is chosen or sounds (and its range), underruns, skips,
//     repeats, frames read, whether the ring is being played; here the target,
//     margin and raises.
//   - Still no allocation, lock, I/O, logging or wait; per sample one ring read
//     (two during a crossfade).
//   Memory order (M2b-B review 2, S3): the writer's index (acquire) is read
//   first, then the session (acquire); when the session is new, the writer's
//   index is read again and everything up to it is skipped. A session advanced
//   between the two loads is either seen (and the skip covers everything
//   written so far) or not seen yet, in which case only samples written before
//   the first load can be read, and the UI advances the session before it
//   opens a device, so none of those belongs to the new device. A start reads
//   the gain target after these loads. The UI sets the gain to zero before it
//   closes the old device and before it advances the session (a release), so a
//   start after a new session sees that gain (or a newer one).
//   GAIN: the UI now raises the gain right after an open (a remembered gain),
//   so a newer gain can belong to a device whose session the stage has not seen
//   yet. The gain is therefore stored with release and pull() loads it with
//   acquire first, before the source and the session: a gain stored after a
//   session advance (or a source change) comes with that session (and source),
//   so the old device's samples are skipped (and the old source held at its own
//   gain) before that gain applies. A start loads the gain again (acquire) and
//   then the session once more; if the session changed meanwhile it does not
//   start. The raise count
//   is incremented with release after the new target is stored; a reader that
//   loads the count with acquire, then the target, sees the raised target.
//
// Buffer lifetime (the hand-over):
//   - The UI thread owns a buffer until it calls offer(). offer() swaps it into
//     the single pending slot (an atomic exchange). If the slot already held a
//     buffer that the audio thread had not picked up, the exchange returns it to
//     the UI thread, which frees it: the audio thread never saw it.
//   - The audio thread takes the pending buffer with an atomic exchange (it then
//     owns it). It does not take one while it is switching: while a crossfade
//     runs, while an old buffer waits to be retired, or while it waits to switch
//     to a buffer it took (an empty one, or any one while the retire queue is
//     full). A buffer offered meanwhile waits in the pending slot (and can still
//     be superseded there by the UI thread).
//   - When it is done with a buffer (after the crossfade, or after the fade-out
//     for an empty one) it pushes it onto the retire queue (the project's SPSC
//     queue, audio -> UI) and never touches it again.
//   - The retire queue full (the UI has not reclaimed for 8 switches): "waits"
//     means: a crossfade still runs and completes; the old buffer is then held,
//     no longer read, and pushed as soon as there is room, and no new buffer is
//     picked up meanwhile. A switch to a buffer while nothing is audible waits,
//     silent, until there is room. Nothing is lost and the audio thread never
//     frees. At most four buffers exist outside the UI thread: the current one,
//     the old one (crossfading or held), one taken and waiting, and the pending one.
//   - The UI thread calls reclaim() every frame and frees what comes back.
//   - At shutdown: close the audio device first (pull() no longer runs), then
//     destroy the stage; its destructor frees every buffer it holds, the pending
//     one and anything still in the retire queue.
//   Memory order: offer() is an acq_rel exchange (release publishes the buffer's
//   contents written by the loader, which handed it to the UI thread under a
//   mutex); the audio thread's exchange is acq_rel too (acquire sees them). The
//   retire queue releases on push and acquires on pop. The UI thread offers a
//   buffer before it changes the source (InputController does), and pull() reads
//   the source (acquire) before its exchange, so a source change is never seen
//   without the buffers offered before it. setSource() stores the source and
//   then increments the source generation (release); pull() reads the
//   generation (acquire) before the source, so a new generation comes with the
//   source that made it. The play mode and the restart counter are independent
//   requests (relaxed).
//
// Threads: the constructor, prepare(), the destructor and the UI calls on the UI
// thread (prepare() before audio starts); pull() on the audio thread only (one
// thread at a time); peak() and the status getters from any thread.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "control/SpscQueue.h"
#include "input/InputTypes.h"
#include "input/LiveInput.h"
#include "platform/Meters.h"

namespace audreybrick
{

// A mono buffer at 48 kHz, immutable once handed over.
struct InputBuffer
{
    std::vector<float> samples;
};

class InputStage
{
  public:
    // Capacity of the retire queue (buffers waiting for the UI thread to free them).
    static constexpr std::size_t kRetireCapacity = 8;

    InputStage();
    ~InputStage();
    InputStage(const InputStage&)            = delete;
    InputStage& operator=(const InputStage&) = delete;

    // Allocates the output buffer for `frames` frames and the crossfade table.
    // `blockFrames` (M2b-B): the output device's buffer size, which sets the live
    // target (kLiveTargetBuffers x) and overfill margin (kLiveOverfillMarginBuffers
    // x); 0 = kLiveDefaultTargetFrames and its margin. Call before audio starts,
    // never while pull() may run. pull() before prepare() returns nullptr.
    void        prepare(std::size_t frames, std::size_t blockFrames = 0);
    std::size_t capacity() const noexcept { return capacity_; }
    // M2b-B: the live input's ring (nullptr: none, LIVE then stays silent). Call
    // before audio starts, never while pull() may run. It must outlive pull().
    void        attachLive(LiveInput* live) noexcept { live_ = live; }
    LiveInput*  live() const noexcept { return live_; }
    // The configured live target in output buffers (1..32, clamped; default 2).
    // UI thread, before audio starts (or while nothing reads the ring).
    void        setLiveTargetBuffers(std::size_t n) noexcept;
    std::size_t liveTargetBuffersConfigured() const noexcept { return liveConfBuffers_; }
    // The live target and overfill margin now (frames; any thread), and how often
    // underruns have doubled the target since start (two within a second, or a
    // single one when the settled minimum left no room for a repeat).
    std::size_t   liveTargetFrames() const noexcept { return liveTargetPub_.load(std::memory_order_relaxed); }
    std::size_t   liveMarginFrames() const noexcept { return liveMarginPub_.load(std::memory_order_relaxed); }
    std::uint64_t liveTargetRaises() const noexcept { return liveRaises_.load(std::memory_order_acquire); }
    std::size_t   liveRepeatFrames() const noexcept { return liveRepeat_; }

    // --- UI thread ---------------------------------------------------------------
    void setSource(InputSource s) noexcept;
    // Knob position 0..1 (clamped); gain = (2n)^2. Stored with release (GAIN, see
    // "Memory order" under Live).
    void setGainNorm(float n) noexcept;
    // GAIN: the fade-in length (samples, 1..kLiveRestoreFadeFrames, clamped) of
    // the first start of the next live session. The UI sets it before it advances
    // the ring's session (LiveInput::newSession() releases it; the stage latches
    // it when it sees the session). kInputFadeFrames is the default.
    void setLiveStartFadeFrames(std::size_t frames) noexcept;
    // LOOP or ONCE (M2b-A2); LOOP at start.
    void setPlayMode(InputPlayMode m) noexcept;
    // Plays the current buffer again from its start (M2b-A2; see "Fades").
    void restart() noexcept;
    // Hands a buffer to the audio thread (ownership passes). Returns the buffer
    // that was still waiting in the pending slot, if any (never seen by the audio
    // thread; the caller frees it), else nullptr. nullptr is not allowed (use an
    // empty buffer for "nothing").
    std::unique_ptr<InputBuffer> offer(std::unique_ptr<InputBuffer> buffer) noexcept;
    // One buffer the audio thread has finished with, or nullptr. Call until it
    // returns nullptr, every UI frame; the caller frees them.
    std::unique_ptr<InputBuffer> reclaim() noexcept;

    // --- Audio thread ----------------------------------------------------------------
    // `frames` at most capacity() (more is clamped to capacity(); AudioRender never
    // asks for more). Returns the stage's buffer of `frames` samples, or nullptr.
    const float* pull(std::size_t frames) noexcept;

    // --- Any thread ----------------------------------------------------------------------
    // The largest |in| (after gain and fades) since the last take().
    MaxHold& peak() noexcept { return peak_; }
    // MOD-A, audio thread only: per sample of the last pull() that returned a
    // buffer (`frames` values), the smoothed linear gain that sample carries:
    // the gain it was computed with, or for a held live value the gain the
    // held value was taken with. The fade level is separate. A record only:
    // writing it changes no output. Valid until the next pull(); nullptr
    // before prepare().
    const float* gainTrace() const noexcept { return gainTrace_.get(); }
    // MOD-C, audio thread only: per sample of the last pull() that returned a
    // buffer (`frames` values), the input BEFORE the gain: the source's sample
    // after the fades and crossfades (file: the player's sample, or the
    // crossfade of two, times the fade level; live: the captured sample, or the
    // skip/repeat crossfade, times the fade level; a held live value: the last
    // sample read, times the falling level), i.e. the output sample with the
    // gain left out. The envelope followers read it (ModControlSource), so they
    // follow the input at any gain, 0 (-inf dB) included. A record only: writing
    // it changes no output, no file position and no ring read. The gain never
    // decides whether pull() returns a buffer; it returns nullptr (nothing
    // recorded; the pre-gain input then counts as silence) with the source OFF,
    // with nothing to play, and for LIVE while the ring fills to its target
    // (after a device opens, after an underrun, when LIVE is chosen again).
    // Valid until the next pull(); nullptr before prepare().
    const float* preGain() const noexcept { return preGain_.get(); }
    // True while pull() returned a buffer in its last call (a source is audible
    // or fading).
    bool active() const noexcept { return active_.load(std::memory_order_relaxed); }
    // Frames of the current buffer (the one playing, or the new one of a
    // crossfade; 0 if none).
    std::size_t playingFrames() const noexcept { return playingFrames_.load(std::memory_order_relaxed); }
    // Read position in the current buffer (in LOOP mode it runs from X to N after
    // the first pass; N once ended).
    std::size_t position() const noexcept { return position_.load(std::memory_order_relaxed); }
    // The current buffer has ended in ONCE mode (M2b-A2).
    bool ended() const noexcept { return ended_.load(std::memory_order_relaxed); }
    // A crossfade between two files is running (M2b-A2).
    bool crossfading() const noexcept { return crossfading_.load(std::memory_order_relaxed); }
    // The stage holds a buffer besides the current one: the old one of a
    // crossfade (running, or held for a full retire queue) or one taken and
    // waiting to be switched to. As of the last pull() (M2b-A2 review, S2).
    bool holdsOther() const noexcept { return holdsOld_.load(std::memory_order_relaxed); }
    // The switch count as of the last pull() that ended holding nothing but the
    // current buffer (no crossfade, no held old buffer, nothing taken and
    // waiting). Stored with release after switches() was incremented, so a
    // reader that loads this (acquire) and then switches() and finds them equal
    // knows the stage has settled on its latest buffer (M2b-A2 review, S2 and M1).
    std::uint64_t settledSwitches() const noexcept { return settled_.load(std::memory_order_acquire); }
    // Buffers the audio thread has switched to (or started crossfading to) since start.
    std::uint64_t switches() const noexcept { return switches_.load(std::memory_order_relaxed); }
    // Restarts carried out: restart() requests and the automatic ones (ONCE ->
    // LOOP or OFF -> FILE with an ended file) (tests).
    std::uint64_t restarts() const noexcept { return restartsDone_.load(std::memory_order_relaxed); }

    // The crossfade weight T[k] (tests; k < kInputFileCrossfadeFrames, after prepare()).
    float crossfadeWeight(std::size_t k) const noexcept { return xfTable_ ? xfTable_[k] : 0.0f; }

  private:
    struct Player
    {
        InputBuffer* buf      = nullptr;
        std::size_t  pos      = 0;
        bool         ended    = false; // ONCE: reached the end
        bool         tailOnce = false; // the mode read when the current pass entered its tail
        float        next(InputPlayMode mode) noexcept;
        bool         playable() const noexcept { return buf != nullptr && !buf->samples.empty() && !ended; }
        void         rewind() noexcept
        {
            pos      = 0;
            ended    = false;
            tailOnce = false;
        }
    };

    void  liveChunkStart(InputSource src, float& gt, std::size_t frames) noexcept; // M2b-B
    float liveNext(bool& held) noexcept;
    void  liveChunkEnd(std::size_t frames) noexcept;
    void  livePublishRead() noexcept;
    void  liveDiscard() noexcept; // stop reading, skip all written so far, mark the new start
    void  liveResetBaseline() noexcept;
    void  setLiveBuffers(std::size_t buffers) noexcept;
    void finishCrossfade() noexcept; // the old player stops being read; retire it (or hold it)
    void startFresh() noexcept;      // level 0, the current player at 0 (it will fade in)
    void requestRestart() noexcept;  // a fade-out first if it sounds, else startFresh()

    // Shared.
    std::atomic<int>           source_{static_cast<int>(InputSource::Off)};
    std::atomic<std::uint32_t> sourceGen_{0};
    std::atomic<float>         gainTarget_{1.0f};
    std::atomic<std::uint32_t> liveStartFadeReq_{static_cast<std::uint32_t>(kInputFadeFrames)}; // GAIN
    std::atomic<int>           mode_{static_cast<int>(InputPlayMode::Loop)};
    std::atomic<std::uint32_t> restartReq_{0};
    std::atomic<InputBuffer*>  pending_{nullptr};
    SpscQueue<InputBuffer*>    retired_{kRetireCapacity};
    MaxHold                    peak_;
    std::atomic<bool>          active_{false};
    std::atomic<std::size_t>   playingFrames_{0};
    std::atomic<std::size_t>   position_{0};
    std::atomic<bool>          ended_{false};
    std::atomic<bool>          crossfading_{false};
    std::atomic<bool>          holdsOld_{false};
    std::atomic<std::uint64_t> switches_{0};
    std::atomic<std::uint64_t> restartsDone_{0};
    std::atomic<std::uint64_t> settled_{0};

    // Audio thread only (and the destructor).
    Player                   cur_;
    Player                   old_;              // crossfading out (xf_ < F) or held for retiring (xf_ >= F)
    std::size_t              xf_      = 0;      // crossfade position
    InputBuffer*             next_    = nullptr; // taken from pending_, waiting to be switched to
    bool                     restartFade_ = false; // a restart's fade-out is running
    std::uint32_t            restartSeen_ = 0;
    std::uint32_t            genSeen_     = 0;
    float                    level_   = 0.0f; // fade level 0..1
    float                    gain_    = 1.0f; // smoothed linear gain
    float                    gainCoef_;
    std::unique_ptr<float[]> out_;
    std::unique_ptr<float[]> gainTrace_; // MOD-A: gain_ per sample of the last pull()
    std::unique_ptr<float[]> preGain_;   // MOD-C: the sample before the gain, per sample of the last pull()
    std::unique_ptr<float[]> xfTable_; // kInputFileCrossfadeFrames weights
    std::size_t              capacity_ = 0;

    // M2b-B, the live input (audio thread only, apart from the pointer and the
    // target, set before audio starts).
    static constexpr std::uint64_t kNoUnderrun = ~std::uint64_t{0};
    LiveInput*    live_            = nullptr;
    std::size_t   liveConfBuffers_ = kLiveTargetBuffers; // configured (setLiveTargetBuffers)
    std::size_t   liveBuffers_     = kLiveTargetBuffers; // now (raised by underruns)
    std::size_t   liveBlock_       = kLiveDefaultTargetFrames / kLiveTargetBuffers;
    std::size_t   liveTarget_      = kLiveDefaultTargetFrames;
    std::size_t   liveMargin_      = kLiveOverfillMarginBuffers * (kLiveDefaultTargetFrames / kLiveTargetBuffers);
    std::size_t   liveRepeat_      = kLiveDefaultTargetFrames / kLiveTargetBuffers;
    InputSource   sounding_        = InputSource::Off; // what level_ applies to
    bool          liveRunning_     = false;            // reading the ring (it had reached the target)
    bool          liveHoldOnly_    = false;            // a new session: fade out on the hold, read nothing
    std::uint64_t liveW_           = 0;                // the writer's index at the start of the chunk
    std::uint64_t liveRead_        = 0;                // the read position
    std::uint64_t liveOld_         = 0;                // the other read position during a skip or repeat
    std::uint64_t livePub_         = 0;                // the read index published to the writer
    InputSource   livePrevSrc_     = InputSource::Off; // the source chosen at the previous chunk
    std::uint64_t liveStart_       = 0;                // nothing before this index is read (session or discard)
    std::size_t   liveXf_          = 0;                // the crossfade: running while liveXf_ < liveXfLen_
    std::size_t   liveXfLen_       = 0;
    float         liveRaw_         = 0.0f;             // the last live sample read (before the gain)
    float         liveHoldOut_     = 0.0f;             // the last live value after the gain (held when dry)
    float         liveHoldGain_    = 0.0f;             // MOD-A: the gain liveHoldOut_ was taken with (record only)
    std::uint32_t liveSessionSeen_ = 0;
    std::uint64_t liveClock_       = 0;                // frames pulled since start
    std::uint64_t liveLastUnderrun_ = kNoUnderrun;     // liveClock_ at the last underrun
    std::size_t   liveBaseMin_     = SIZE_MAX;         // the settled minimum fill
    std::uint64_t liveBaseStart_   = 0;
    bool          liveBaseReady_   = false;
    bool          liveNoHeadroom_  = false;            // settled: half a repeat below the minimum is <= the chunk
    std::uint64_t liveRepeatClock_ = 0;                // liveClock_ at the last repeat (or start)
    std::uint64_t liveRepeatW_     = 0;                // the writer's index at the last repeat
    std::atomic<std::size_t>   liveTargetPub_{kLiveDefaultTargetFrames};
    std::atomic<std::size_t>   liveMarginPub_{kLiveOverfillMarginBuffers * (kLiveDefaultTargetFrames / kLiveTargetBuffers)};
    std::atomic<std::uint64_t> liveRaises_{0};
    std::size_t   liveReadCount_   = 0; // samples taken from the ring in this chunk
    // GAIN: the fade-in of the session's first start (latched with the session),
    // and the level's rising step while the live input sounds.
    std::uint32_t liveStartFade_   = static_cast<std::uint32_t>(kInputFadeFrames);
    float         liveFadeStep_    = 1.0f / static_cast<float>(kInputFadeFrames);

    static_assert(std::atomic<InputBuffer*>::is_always_lock_free, "the hand-over must be lock-free");
    static_assert(std::atomic<float>::is_always_lock_free, "the gain must be lock-free");
    static_assert(std::atomic<int>::is_always_lock_free, "the source must be lock-free");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the counters must be lock-free");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "status must be lock-free");
};

} // namespace audreybrick
