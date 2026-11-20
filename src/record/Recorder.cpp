// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "record/Recorder.h"

#include <pthread.h>
#include <signal.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <utility>

namespace audreybrick
{
namespace
{
double steadyNow() noexcept
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void atomicMax(std::atomic<std::uint64_t>& a, std::uint64_t v) noexcept
{
    std::uint64_t cur = a.load(std::memory_order_relaxed);
    while(v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
    {
    }
}

double mib(std::uint64_t bytes) noexcept
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}
} // namespace

const char* recordStopReasonName(RecordStopReason r) noexcept
{
    switch(r)
    {
        case RecordStopReason::None: return "none";
        case RecordStopReason::Button: return "button (Start + Y)";
        case RecordStopReason::Quit: return "quit";
        case RecordStopReason::FileLimit: return "file limit";
        case RecordStopReason::CardFull: return "card full";
        case RecordStopReason::WriteError: return "write error";
    }
    return "?";
}

Recorder::Recorder(RecordTap& tap, RecordFs& fs, RecorderConfig cfg) : tap_(tap), fs_(fs), cfg_(std::move(cfg))
{
    if(cfg_.chunkFrames < 1)
        cfg_.chunkFrames = 1;
    if(cfg_.sampleRate < 1)
        cfg_.sampleRate = 48000;
}

Recorder::~Recorder()
{
    shutdown();
}

bool Recorder::start(std::string& error) noexcept
{
    if(started_)
        return true;
    try
    {
        fbuf_.reset(new float[cfg_.chunkFrames * 2]);
        sbuf_.reset(new std::int16_t[cfg_.chunkFrames * 2]);
        results_.reserve(8);
        log_.reserve(16);
    }
    catch(...)
    {
        error = "out of memory for the recording buffers";
        return false;
    }
    // The thread inherits this thread's signal mask: block everything while it is
    // created so stop signals are never delivered to the writer.
    sigset_t all, old;
    sigfillset(&all);
    const bool masked = pthread_sigmask(SIG_SETMASK, &all, &old) == 0;
    try
    {
        thread_  = std::thread([this] { run(); });
        started_ = true;
    }
    catch(...)
    {
        error = "cannot start the writer thread";
    }
    if(masked)
        pthread_sigmask(SIG_SETMASK, &old, nullptr);
    return started_;
}

bool Recorder::running() const noexcept
{
    if(stopPending_.load() || quit_.load())
        return false;
    const RecordState s = state();
    return startPending_.load() || s == RecordState::Starting || s == RecordState::Recording;
}

bool Recorder::finalising() const noexcept
{
    const RecordState s = state();
    if(s == RecordState::Finalising)
        return true;
    return (stopPending_.load() || quit_.load()) && (s == RecordState::Starting || s == RecordState::Recording);
}

Recorder::Toggle Recorder::toggle() noexcept
{
    if(!started_ || joined_)
        return Toggle::Unavailable;
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(quit_.load())
            return Toggle::Unavailable;
        const RecordState s = state();
        if(stopPending_.load() || s == RecordState::Finalising)
            return Toggle::Busy;
        if(startPending_.load() || s == RecordState::Starting || s == RecordState::Recording)
        {
            stopPending_.store(true);
            stopReason_ = RecordStopReason::Button;
            tap_.disarm(); // the UI clears the flag; the writer drains what is left
            wake_.notify_one();
            return Toggle::Stopping;
        }
        startPending_.store(true);
        wake_.notify_one();
        return Toggle::Starting;
    }
    catch(...)
    {
        return Toggle::Unavailable; // a lock that failed (std::system_error)
    }
}

void Recorder::shutdown() noexcept
{
    if(!started_ || joined_)
        return;
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        quit_.store(true);
        const RecordState s = state();
        if((s == RecordState::Starting || s == RecordState::Recording) && !stopPending_.load())
        {
            stopPending_.store(true);
            stopReason_ = RecordStopReason::Quit;
        }
        tap_.disarm();
        wake_.notify_one();
    }
    catch(...)
    {
        quit_.store(true);
        tap_.disarm();
    }
    try
    {
        if(thread_.joinable())
            thread_.join();
    }
    catch(...)
    {
    }
    joined_ = true;
}

bool Recorder::takeResult(RecordResult& out) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(results_.empty())
            return false;
        out = std::move(results_.front());
        results_.erase(results_.begin());
        return true;
    }
    catch(...)
    {
        return false;
    }
}

void Recorder::takeLog(std::vector<std::string>& out) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for(std::string& l : log_)
            out.push_back(std::move(l));
        log_.clear();
    }
    catch(...)
    {
    }
}

void Recorder::log(const char* fmt, ...) noexcept
{
    char         buf[640];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        log_.emplace_back(buf);
    }
    catch(...)
    {
    }
}

void Recorder::report(RecordResult&& r) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        results_.push_back(std::move(r));
    }
    catch(...)
    {
    }
}

void Recorder::setState(RecordState s) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_.store(static_cast<int>(s), std::memory_order_release);
        if(s == RecordState::Idle)
            stopPending_.store(false); // a stop asked for meanwhile has been done
    }
    catch(...)
    {
        state_.store(static_cast<int>(s), std::memory_order_release);
    }
}

void Recorder::noteFill() noexcept
{
    const std::size_t f = tap_.fill();
    maxFill_            = std::max(maxFill_, f);
    atomicMax(windowFill_, f);
}

void Recorder::run() noexcept
{
    for(;;)
    {
        bool             doStart = false, doStop = false, quit = false;
        RecordStopReason reason  = RecordStopReason::None;
        const bool       recording = state() == RecordState::Recording;
        try
        {
            std::unique_lock<std::mutex> lk(mutex_);
            auto ready = [this] { return quit_.load() || startPending_.load() || stopPending_.load(); };
            if(recording)
                wake_.wait_for(lk, std::chrono::duration<double>(cfg_.pollSeconds),
                               [this] { return quit_.load() || stopPending_.load(); });
            else
                wake_.wait(lk, ready);
            quit    = quit_.load();
            doStart = startPending_.load();
            doStop  = stopPending_.load();
            reason  = stopReason_;
        }
        catch(...)
        {
            // A failed lock or wait: nap and try again.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if(!recording)
        {
            if(quit)
                return;
            if(doStart)
            {
                startRecording();
                continue;
            }
            if(doStop)
                setState(RecordState::Idle); // a stale stop: clears it
            continue;
        }

        if(quit && !doStop)
        {
            doStop = true;
            reason = RecordStopReason::Quit;
        }
        if(doStop)
        {
            finish(reason == RecordStopReason::None ? RecordStopReason::Button : reason);
            if(quit)
                return;
            continue;
        }
        noteFill();
        if(!drain(false))
        {
            finish(writeFailed_ ? RecordStopReason::WriteError : RecordStopReason::FileLimit);
            continue;
        }
        periodic();
    }
}

void Recorder::startRecording() noexcept
{
    try
    {
        std::lock_guard<std::mutex> lk(mutex_);
        startPending_.store(false);
        state_.store(static_cast<int>(RecordState::Starting), std::memory_order_release);
    }
    catch(...)
    {
        return;
    }
    RecordResult fail;
    fail.outcome = RecordOutcome::CannotCreate;
    auto refuse  = [&](RecordOutcome o, const std::string& detail) {
        fail.outcome = o;
        fail.detail  = detail;
        log("record: %s: %s", o == RecordOutcome::RefusedCardFull ? "not started (card full)" : "cannot create the file",
            detail.c_str());
        setState(RecordState::Idle);
        report(std::move(fail));
    };

    try
    {
        std::string err;
        if(!fs_.makeDirs(cfg_.dir, err))
            return refuse(RecordOutcome::CannotCreate, err);
        std::uint64_t freeB    = 0;
        bool          haveFree = fs_.freeBytes(cfg_.dir, freeB, err);
        if(!haveFree)
            log("record: free space unknown (%s); starting anyway", err.c_str());
        else if(freeB < cfg_.minFreeToStart)
        {
            char d[160];
            std::snprintf(d, sizeof d, "%.1f MiB free on %s, at least %.0f MiB needed to start", mib(freeB),
                          cfg_.dir.c_str(), mib(cfg_.minFreeToStart));
            return refuse(RecordOutcome::RefusedCardFull, d);
        }
        std::vector<std::string> names;
        if(!fs_.list(cfg_.dir, names, err))
            return refuse(RecordOutcome::CannotCreate, err);
        long number = nextRecordingNumber(names);
        int  fd     = -1;
        for(int attempt = 0; attempt < 100 && fd < 0; ++attempt, ++number)
        {
            bool exists = false;
            name_       = recordingFileName(number);
            path_       = cfg_.dir + (cfg_.dir.back() == '/' ? "" : "/") + name_;
            fd          = fs_.createNew(path_, exists, err);
            if(fd < 0 && !exists)
                return refuse(RecordOutcome::CannotCreate, err);
        }
        if(fd < 0)
            return refuse(RecordOutcome::CannotCreate, "100 names in a row were taken");
        std::uint8_t hdr[kWavHeaderBytes];
        makeWavHeader(hdr, cfg_.sampleRate, 0);
        const long w = fs_.write(fd, hdr, sizeof hdr, err);
        if(w != static_cast<long>(sizeof hdr))
        {
            std::string e2;
            fs_.close(fd, e2);
            fs_.remove(path_, e2);
            return refuse(RecordOutcome::CannotCreate, "header: " + err);
        }
        fd_          = fd;
        dataBytes_   = 0;
        maxFill_     = 0;
        limitHit_    = false;
        writeFailed_ = false;
        writeError_.clear();
        written_.store(0, std::memory_order_relaxed);
        lastUpdate_ = steadyNow();
        tap_.ring().discard(); // nothing old (the ring is empty after every stop anyway)
        tap_.arm();
        setState(RecordState::Recording);
        if(haveFree)
            log("record: started %s; %.1f MiB free; ring %zu frames (%.2f s at %u Hz); 16-bit stereo",
                path_.c_str(), mib(freeB), tap_.capacity(),
                static_cast<double>(tap_.capacity()) / cfg_.sampleRate, cfg_.sampleRate);
        else
            log("record: started %s; free space unknown; ring %zu frames (%.2f s at %u Hz); 16-bit stereo",
                path_.c_str(), tap_.capacity(), static_cast<double>(tap_.capacity()) / cfg_.sampleRate,
                cfg_.sampleRate);
    }
    catch(...)
    {
        refuse(RecordOutcome::CannotCreate, "out of memory");
    }
}

bool Recorder::writeFrames(std::size_t n) noexcept
{
    const std::uint64_t roomFrames = (cfg_.dataLimit - std::min(cfg_.dataLimit, dataBytes_)) / kRecordFrameBytes;
    const std::size_t   m          = static_cast<std::size_t>(std::min<std::uint64_t>(n, roomFrames));
    if(m > 0)
    {
        floatToPcm16(fbuf_.get(), sbuf_.get(), m);
        const std::size_t bytes = m * kRecordFrameBytes;
        std::string       err;
        const double      t0 = steadyNow();
        const long        w  = fs_.write(fd_, sbuf_.get(), bytes, err);
        const double      dt = steadyNow() - t0;
        atomicMax(windowWriteUs_, static_cast<std::uint64_t>(dt * 1e6));
        if(w > 0)
        {
            const std::uint64_t whole = static_cast<std::uint64_t>(w) / kRecordFrameBytes * kRecordFrameBytes;
            dataBytes_ += whole;
            written_.fetch_add(whole / kRecordFrameBytes, std::memory_order_relaxed);
        }
        if(w != static_cast<long>(bytes))
        {
            writeFailed_ = true;
            writeError_  = err.empty() ? std::string("short write") : err;
            return false;
        }
    }
    if(m < n || dataBytes_ >= cfg_.dataLimit)
    {
        limitHit_ = true;
        return false;
    }
    return true;
}

bool Recorder::drain(bool toEnd) noexcept
{
    // While recording, one pass reads what is there now (at most the ring's
    // capacity); toEnd reads until the ring is empty (the tap is disarmed then).
    std::size_t budget = toEnd ? static_cast<std::size_t>(-1) : tap_.capacity();
    while(budget > 0)
    {
        const std::size_t n = tap_.ring().read(fbuf_.get(), std::min(cfg_.chunkFrames, budget));
        if(n == 0)
            return true;
        budget -= std::min(budget, n);
        if(!writeFrames(n))
            return false;
    }
    return true;
}

bool Recorder::updateHeader(std::string& error) noexcept
{
    std::uint8_t hdr[kWavHeaderBytes];
    makeWavHeader(hdr, cfg_.sampleRate, static_cast<std::uint32_t>(dataBytes_));
    if(!fs_.writeAt(fd_, hdr, sizeof hdr, 0, error))
        return false;
    return fs_.sync(fd_, error);
}

void Recorder::periodic() noexcept
{
    const double now = steadyNow();
    if(now - lastUpdate_ < cfg_.updateSeconds)
        return;
    lastUpdate_ = now;
    try
    {
        std::string err;
        if(!updateHeader(err))
        {
            writeFailed_ = true;
            writeError_  = "header update: " + err;
            finish(RecordStopReason::WriteError);
            return;
        }
        std::uint64_t freeB = 0;
        if(!fs_.freeBytes(cfg_.dir, freeB, err))
        {
            if(!freeUnknownLogged_)
                log("record: free space unknown (%s); recording on", err.c_str());
            freeUnknownLogged_ = true;
        }
        else if(freeB < cfg_.minFreeToContinue)
        {
            log("record: %.1f MiB free, below %.0f MiB: stopping", mib(freeB), mib(cfg_.minFreeToContinue));
            finish(RecordStopReason::CardFull);
        }
    }
    catch(...)
    {
        writeFailed_ = true;
        writeError_  = "out of memory";
        finish(RecordStopReason::WriteError);
    }
}

void Recorder::finish(RecordStopReason reason) noexcept
{
    const double t0 = steadyNow();
    setState(RecordState::Finalising);
    tap_.disarm();
    const bool idle = tap_.waitIdle(0.5);
    noteFill();
    if(reason != RecordStopReason::WriteError && reason != RecordStopReason::FileLimit && !writeFailed_ && !limitHit_)
    {
        if(!drain(true))
            reason = writeFailed_ ? RecordStopReason::WriteError : RecordStopReason::FileLimit;
    }
    tap_.ring().discard(); // whatever is left after a limit or an error
    if(writeFailed_)
        reason = RecordStopReason::WriteError;

    RecordResult r;
    r.reason  = reason;
    r.name    = name_;
    r.path    = path_;
    r.dropped = tap_.framesDropped();
    try
    {
        std::string err;
        bool        ok = updateHeader(err);
        std::string closeErr;
        if(!fs_.close(fd_, closeErr) && ok)
        {
            ok  = false;
            err = closeErr;
        }
        fd_ = -1;
        if(!ok)
        {
            writeError_ = writeError_.empty() ? "final header: " + err : writeError_ + "; final header: " + err;
            reason      = RecordStopReason::WriteError;
            r.reason    = reason;
        }
        r.detail = writeError_;
    }
    catch(...)
    {
        r.detail = "out of memory";
    }
    switch(reason)
    {
        case RecordStopReason::FileLimit: r.outcome = RecordOutcome::StoppedFileLimit; break;
        case RecordStopReason::CardFull: r.outcome = RecordOutcome::StoppedCardFull; break;
        case RecordStopReason::WriteError: r.outcome = RecordOutcome::WriteError; break;
        default: r.outcome = RecordOutcome::Saved; break;
    }
    r.frames     = dataBytes_ / kRecordFrameBytes;
    r.dataBytes  = dataBytes_;
    r.maxFill    = maxFill_;
    r.seconds    = static_cast<double>(r.frames) / cfg_.sampleRate;
    r.finaliseMs = 1000.0 * (steadyNow() - t0);
    log("record: %s %s: %llu frames (%.3f s), %llu data bytes, dropped %llu frames, largest ring fill %zu frames "
        "(%.2f s), finalised in %.1f ms; reason: %s%s%s%s",
        r.outcome == RecordOutcome::Saved ? "saved" : "stopped, saved", r.path.c_str(),
        static_cast<unsigned long long>(r.frames), r.seconds, static_cast<unsigned long long>(r.dataBytes),
        static_cast<unsigned long long>(r.dropped), r.maxFill, static_cast<double>(r.maxFill) / cfg_.sampleRate,
        r.finaliseMs, recordStopReasonName(reason), r.detail.empty() ? "" : " (", r.detail.c_str(),
        r.detail.empty() ? "" : ")");
    if(!idle)
        log("record: warning: the audio thread was still in a block after 0.5 s; the last block may be missing");
    report(std::move(r));
    setState(RecordState::Idle);
}

} // namespace audreybrick
