// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// LiveInput: the live (capture) end of the instrument's input (docs/briefs/M2b.md,
// M2b-B). SDL-free. It holds:
//
//   - The capture ring: a preallocated single-producer, single-consumer ring of
//     kLiveRingFrames mono float samples at 48 kHz. The writer is the capture
//     callback (platform/CaptureHost, or the self-test's simulated device, or a
//     test); the reader is InputStage::pull() on the output callback.
//   - What the writer publishes: frames captured, frames dropped because the ring
//     was full, callbacks, and the pre-gain peak (prePeak(), a MaxHold).
//   - What the reader publishes (InputStage calls the reader functions): the
//     fill at the start of each chunk, its minimum and maximum since the UI last
//     took them, underruns, overfills (skips), repeats and frames read.
//   - A session counter the UI increments whenever it opens or closes a device:
//     the stage then stops reading the ring and fades out on the last sample it
//     read (it never reads a sample of a newly opened device before it has faded
//     to silence and buffered again; see InputStage.h, "Live").
//
// The writer's mix (writeInterleaved): 1 channel as it is; 2 channels L + R (the
// human's decision, docs/RISKS.md 2a, as for files); more than 2: the first two,
// L + R. Non-finite samples become 0; the mono sample is clamped to
// +/-kLiveSampleLimit (8), as file samples are. What does not fit in the ring is
// dropped (counted), never blocks.
//
// Ring indices are 64-bit counters that only grow (they never wrap in practice:
// 2^64 samples at 48 kHz is millions of years); the slot is index & (size - 1).
// The writer stores its index with release after writing the slots; the reader
// loads it with acquire before reading them, and stores its own index with
// release after reading; the writer loads that with acquire before reusing slots.
// The fill (write - read) is never above kLiveRingFrames. The stage keeps its
// published index a repeat's length behind what it reads, so the samples a
// repeat jumps back to are never overwritten. All shared state is
// scalar atomics (no shared structs: ThreadSanitizer on Apple arm64 misses races
// in struct copies of 32 bytes or more, see docs/STATUS.md).
//
// No allocation, lock, logging, I/O or wait in the writer or reader functions
// (the constructor allocates the ring).
//
// LiveBackend: what the INPUT tab's model (InputController) needs from a capture
// system: list the devices, open one into a LiveInput, close it, report that it
// was lost, and count list changes. platform/CaptureHost implements it with
// SDL; the app's self-test has a simulated one (input/LiveSim.h); the tests
// have their own. Called on the UI thread only.

#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "input/InputTypes.h"
#include "platform/Meters.h"

namespace audreybrick
{

class LiveInput
{
  public:
    static constexpr std::size_t kCapacity = kLiveRingFrames;
    static_assert((kCapacity & (kCapacity - 1)) == 0, "the ring size is a power of two");

    LiveInput();
    LiveInput(const LiveInput&)            = delete;
    LiveInput& operator=(const LiveInput&) = delete;

    // One mono sample as the ring stores it: non-finite -> 0, clamped to +/-8.
    static float clean(float v) noexcept
    {
        if(!std::isfinite(v))
            return 0.0f;
        return v > kLiveSampleLimit ? kLiveSampleLimit : (v < -kLiveSampleLimit ? -kLiveSampleLimit : v);
    }
    // The mono mix of one interleaved frame (see the header comment).
    static float mixFrame(const float* frame, int channels) noexcept
    {
        if(channels <= 1)
            return clean(frame[0]);
        return clean(clean(frame[0]) + clean(frame[1]));
    }

    // --- Writer (one capture thread at a time) ----------------------------------------
    // Mixes `frames` interleaved frames of `channels` channels to mono and writes
    // what fits. Returns the frames written. Counts the callback.
    std::size_t writeInterleaved(const float* interleaved, std::size_t frames, int channels) noexcept;
    // Writes `n` mono samples (cleaned as above). Returns the samples written.
    std::size_t write(const float* mono, std::size_t n) noexcept;

    // --- Reader (the output callback: InputStage) -------------------------------------
    std::uint64_t writeIndex() const noexcept { return write_.load(std::memory_order_acquire); }
    std::uint64_t readIndex() const noexcept { return read_.load(std::memory_order_relaxed); }
    // The sample at absolute position `pos` (readIndex() <= pos < writeIndex()).
    float at(std::uint64_t pos) const noexcept { return ring_[static_cast<std::size_t>(pos) & (kCapacity - 1)]; }
    // Frees the slots before `pos` for the writer (pos never decreases, never
    // passes writeIndex()).
    void setReadIndex(std::uint64_t pos) noexcept { read_.store(pos, std::memory_order_release); }
    std::uint32_t session() const noexcept { return session_.load(std::memory_order_acquire); }
    // Statistics, reader side (relaxed; single writer).
    void publishFill(std::size_t fill) noexcept;
    void countUnderrun() noexcept { bump(underruns_); }
    void countOverfill() noexcept { bump(overfills_); }
    void countRepeat() noexcept { bump(repeats_); }
    void addRead(std::size_t n) noexcept
    {
        readFrames_.store(readFrames_.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
    }
    void setRunning(bool r) noexcept { running_.store(r, std::memory_order_relaxed); }

    // --- UI thread -------------------------------------------------------------------------
    // A device was opened or closed: the stage stops reading and fades out.
    // Call after closing the old device and before opening the new one.
    void newSession() noexcept { session_.fetch_add(1, std::memory_order_acq_rel); }

    // --- Any thread ------------------------------------------------------------------------
    MaxHold&      prePeak() noexcept { return prePeak_; } // the mono mix before the gain
    std::uint64_t framesCaptured() const noexcept { return captured_.load(std::memory_order_relaxed); }
    std::uint64_t framesDropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    std::uint64_t callbacks() const noexcept { return callbacks_.load(std::memory_order_relaxed); }
    std::uint64_t framesRead() const noexcept { return readFrames_.load(std::memory_order_relaxed); }
    std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
    std::uint64_t overfills() const noexcept { return overfills_.load(std::memory_order_relaxed); }
    std::uint64_t repeats() const noexcept { return repeats_.load(std::memory_order_relaxed); } // slow-capture repeats
    std::size_t   fill() const noexcept { return fill_.load(std::memory_order_relaxed); }
    // The stage reads the ring now (it had buffered to the target).
    bool          running() const noexcept { return running_.load(std::memory_order_relaxed); }
    // The smallest and largest fill the stage saw since the last call (the
    // current fill if it has not published since). UI thread (one caller).
    void          takeFillRange(std::size_t& minFill, std::size_t& maxFill) noexcept;

  private:
    static void bump(std::atomic<std::uint64_t>& c) noexcept
    {
        c.store(c.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); // single writer
    }

    std::unique_ptr<float[]>   ring_;
    std::atomic<std::uint64_t> write_{0};
    std::atomic<std::uint64_t> read_{0};
    std::atomic<std::uint32_t> session_{0};

    // Writer statistics.
    MaxHold                    prePeak_;
    std::atomic<std::uint64_t> captured_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> callbacks_{0};

    // Reader statistics.
    std::atomic<std::size_t>   fill_{0};
    std::atomic<std::size_t>   minFill_{0};
    std::atomic<std::size_t>   maxFill_{0};
    std::atomic<std::uint32_t> rangeReset_{0}; // UI -> reader: start a new min/max window
    std::uint32_t              rangeSeen_ = 0; // reader only
    bool                       rangeValid_ = false; // reader only
    std::size_t                minLocal_  = 0;      // reader only
    std::size_t                maxLocal_  = 0;      // reader only
    std::atomic<std::uint64_t> readFrames_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overfills_{0};
    std::atomic<std::uint64_t> repeats_{0};
    std::atomic<bool>          running_{false};

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "ring indices must be lock-free");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "statistics must be lock-free");
};

// What a capture system reports about an open (asked and obtained).
struct LiveOpenResult
{
    bool        ok = false;
    std::string error;           // the system's reason when !ok
    std::string askedName;       // "" for the system default
    int         askedRate     = 0;
    int         askedChannels = 0;
    int         askedFrames   = 0;
    std::string askedFormat;
    int         rate     = 0; // obtained
    int         channels = 0;
    int         frames   = 0;
    std::string format;
    double      seconds = 0.0; // how long the open took (wall clock)
};

class LiveBackend
{
  public:
    virtual ~LiveBackend() = default;
    // The capture devices' names in the system's order. False (names empty) if
    // the system cannot list them.
    virtual bool listDevices(std::vector<std::string>& names) = 0;
    // Opens `name` (nullptr: the system default) for capture into the LiveInput,
    // closing a device open before. Fills `r`; returns r.ok. A device opened
    // here starts delivering at once.
    virtual bool open(const char* name, LiveOpenResult& r) = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;
    // The open device has gone (removed, or stopped by the system) since it was opened.
    virtual bool lost() noexcept = 0;
    // Grows whenever the list of capture devices may have changed.
    virtual std::uint64_t listChanges() const noexcept = 0;
    // A short name for the log ("SDL alsa", "simulated").
    virtual const char* kind() const noexcept = 0;
};

} // namespace audreybrick
