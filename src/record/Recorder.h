// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Recorder: the recording's writer thread and its state (docs/briefs/REC.md).
// SDL-free. It drains the tap's ring (record/RecordTap.h), converts to 16-bit and
// appends to rec-NNNN.wav through a RecordFs (record/RecordFs.h).
//
// Thread: one, started with the app (start()), before audio runs, and idle
// (blocked on a condition variable, no wake-ups) until the first Start + Y. It
// is created with every signal blocked, so the app's stop signals go to the main
// thread and never interrupt a write. Started at start-up rather than on first
// use so that Start + Y never creates a thread or allocates: the ring, the
// conversion buffers and the thread all exist before audio starts.
//
// UI thread (toggle(), shutdown(), the getters, takeResult(), takeLog()): only
// sets flags under a mutex held for a few assignments (never during file work)
// and reads atomics. It never waits for the card, except in shutdown(), which
// joins the thread after the last file is finished (bounded: at most the ring's
// contents, about 4 s of audio, plus one sync).
//
// Start (toggle() while idle): the writer creates the folder if missing, checks
// the free space (below cfg.minFreeToStart: refused, REC: CARD FULL), picks the
// next number (record/RecordFormat.h; a name taken meanwhile is skipped, the
// file is created with O_EXCL so nothing is ever overwritten), writes a header
// for 0 bytes and arms the tap. The next whole callback block is the first one
// recorded.
//
// While recording: every cfg.pollSeconds (20 ms) it drains the ring in chunks
// of cfg.chunkFrames, converts (floatToPcm16) and appends. Every
// cfg.updateSeconds (2 s) it rewrites the header's RIFF and data sizes with
// pwrite (the append position is not moved, so nothing needs to seek back),
// syncs the file once (fdatasync on Linux, fsync elsewhere), and checks the free
// space (below cfg.minFreeToContinue: stop, REC STOPPED: CARD FULL). A file cut
// off by SIGKILL or a flat battery is therefore a valid WAV up to the last
// update. A file that would pass cfg.dataLimit (4,000,000,000 data bytes) is
// written up to the limit and stopped (REC STOPPED: FILE LIMIT). A failed or
// short write stops the recording at once (REC: WRITE ERROR): the header is
// finished for what was written, and nothing is retried.
//
// Stop (toggle() while recording, or shutdown()): the UI clears the armed flag;
// the writer waits until no block is being pushed (RecordTap::waitIdle), drains
// the ring to the end (the last block recorded is a whole block), writes the
// final header, syncs and closes, then reports a RecordResult (frames, bytes,
// dropped frames, largest ring fill, time to finalise) and log lines.
//
// Start + Y while the previous file is still being finalised is ignored
// (toggle() returns Busy; the app shows REC BUSY): nothing is queued, so no
// press can lose or overwrite a file.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "record/RecordFormat.h"
#include "record/RecordFs.h"
#include "record/RecordTap.h"

namespace audreybrick
{

enum class RecordState : int
{
    Idle       = 0,
    Starting   = 1, // creating the file
    Recording  = 2,
    Finalising = 3, // draining, writing the final header, closing
};

enum class RecordStopReason : int
{
    None = 0,
    Button,     // Start + Y
    Quit,       // the app is quitting
    FileLimit,  // the data limit was reached
    CardFull,   // free space fell below the minimum
    WriteError, // a write, header update or sync failed
};
const char* recordStopReasonName(RecordStopReason r) noexcept;

enum class RecordOutcome : int
{
    Saved,           // stopped by the button or by quitting
    StoppedFileLimit,
    StoppedCardFull,
    WriteError,      // stopped by a failed write (the file holds what was written)
    RefusedCardFull, // not started: too little free space
    CannotCreate,    // not started: the folder or the file could not be created
};

struct RecordResult
{
    RecordOutcome    outcome = RecordOutcome::Saved;
    RecordStopReason reason  = RecordStopReason::None;
    std::string      name;   // file name, "" if none was created
    std::string      path;   // full path, "" if none was created
    std::uint64_t    frames    = 0;
    std::uint64_t    dataBytes = 0;
    std::uint64_t    dropped   = 0;   // frames dropped because the ring was full
    std::size_t      maxFill   = 0;   // largest ring fill seen, frames
    double           seconds   = 0.0; // frames / rate
    double           finaliseMs = 0.0;
    std::string      detail; // error text for the log
};

struct RecorderConfig
{
    std::string   dir;                              // the recordings folder
    std::uint32_t sampleRate        = 48000;        // the output device's rate
    std::uint64_t dataLimit         = kRecordDataLimit;
    std::uint64_t minFreeToStart    = 200ull * 1024 * 1024; // 200 MiB
    std::uint64_t minFreeToContinue = 100ull * 1024 * 1024; // 100 MiB
    double        updateSeconds     = 2.0;  // header update + sync + free-space check
    double        pollSeconds       = 0.02; // ring drained this often while recording
    std::size_t   chunkFrames       = 4096; // frames per write call
};

class Recorder
{
  public:
    enum class Toggle
    {
        Starting,    // a recording will start
        Stopping,    // the recording will stop and be saved
        Busy,        // the previous file is still being finalised: ignored
        Unavailable, // no writer thread
    };

    Recorder(RecordTap& tap, RecordFs& fs, RecorderConfig cfg);
    ~Recorder();
    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Allocates the buffers and starts the writer thread (signals blocked in it).
    // False with `error` set if that fails; recording is then unavailable.
    bool start(std::string& error) noexcept;
    // UI thread: Start + Y.
    Toggle toggle() noexcept;
    // Quits: a recording in progress is stopped (reason Quit) and finalised, then
    // the thread is joined. Idempotent.
    void shutdown() noexcept;

    // --- UI thread: state ---------------------------------------------------------------
    RecordState state() const noexcept { return static_cast<RecordState>(state_.load(std::memory_order_acquire)); }
    // Recording or about to (the border and the running time show).
    bool running() const noexcept;
    // A stop was asked for or the file is being finalised.
    bool finalising() const noexcept;
    // Either of the above: the border shows.
    bool active() const noexcept { return running() || finalising(); }
    bool available() const noexcept { return started_; }
    const RecorderConfig& config() const noexcept { return cfg_; }
    std::size_t ringFrames() const noexcept { return tap_.capacity(); }
    // Frames written to the file in this recording (or the last one).
    std::uint64_t framesWritten() const noexcept { return written_.load(std::memory_order_relaxed); }
    // The largest ring fill and the longest write call since the last take (the
    // statistics window); both reset to 0.
    std::size_t takeWindowMaxFill() noexcept { return static_cast<std::size_t>(windowFill_.exchange(0)); }
    double      takeWindowLongestWriteMs() noexcept { return static_cast<double>(windowWriteUs_.exchange(0)) / 1000.0; }

    // The next finished recording or failed start, once.
    bool takeResult(RecordResult& out) noexcept;
    // The writer's log lines since the last call (appended to `out`).
    void takeLog(std::vector<std::string>& out) noexcept;

    // The thread body (public for the thread start only).
    void run() noexcept;

  private:
    void startRecording() noexcept;
    // Drains the ring (all of it if `toEnd`); false if a write failed or the limit was reached.
    bool drain(bool toEnd) noexcept;
    bool writeFrames(std::size_t n) noexcept;
    bool updateHeader(std::string& error) noexcept;
    void periodic() noexcept;
    void finish(RecordStopReason reason) noexcept;
    void setState(RecordState s) noexcept;
    void log(const char* fmt, ...) noexcept
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    void report(RecordResult&& r) noexcept;
    void noteFill() noexcept;

    RecordTap&     tap_;
    RecordFs&      fs_;
    RecorderConfig cfg_;

    std::thread             thread_;
    bool                    started_ = false; // UI thread only
    bool                    joined_  = false; // UI thread only
    mutable std::mutex      mutex_;
    std::condition_variable wake_;
    // Guarded by mutex_ (also atomics so the UI getters read them without the lock).
    std::atomic<bool>      quit_{false};
    std::atomic<bool>      startPending_{false};
    std::atomic<bool>      stopPending_{false};
    RecordStopReason       stopReason_ = RecordStopReason::None;
    std::vector<RecordResult> results_;
    std::vector<std::string>  log_;
    std::atomic<int>       state_{static_cast<int>(RecordState::Idle)};

    // Writer thread only.
    std::unique_ptr<float[]>        fbuf_;
    std::unique_ptr<std::int16_t[]> sbuf_;
    int                             fd_        = -1;
    std::string                     path_;
    std::string                     name_;
    std::uint64_t                   dataBytes_ = 0;
    std::size_t                     maxFill_   = 0;
    bool                            limitHit_  = false;
    bool                            writeFailed_ = false;
    std::string                     writeError_;
    double                          lastUpdate_ = 0.0; // steady seconds
    bool                            freeUnknownLogged_ = false;

    // Writer writes, UI reads.
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> windowFill_{0};
    std::atomic<std::uint64_t> windowWriteUs_{0};
};

} // namespace audreybrick
