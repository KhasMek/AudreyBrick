// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/DiagLog.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace audreybrick
{

std::size_t formatLogLine(char* out, std::size_t size, double t, const char* text) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    int n = std::snprintf(out, size, "[%10.3f] ", t);
    if(n < 0)
        n = 0;
    std::size_t at = static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1;
    for(const char* c = text != nullptr ? text : ""; *c != '\0' && at + 2 < size; ++c)
    {
        const unsigned char u = static_cast<unsigned char>(*c);
        out[at++]             = (u < 0x20 || u == 0x7F) ? ' ' : *c;
    }
    if(at + 1 < size)
        out[at++] = '\n';
    out[at] = '\0';
    return at;
}

bool makeParentDirectories(const std::string& path, std::string& error)
{
    const std::size_t slash = path.find_last_of('/');
    if(slash == std::string::npos || slash == 0)
        return true; // "file" or "/file": nothing to create
    const std::string dir = path.substr(0, slash);
    for(std::size_t i = 1; i <= dir.size(); ++i)
    {
        if(i < dir.size() && dir[i] != '/')
            continue;
        const std::string part = dir.substr(0, i);
        if(part.empty() || part == "." || part == "..")
            continue;
        if(::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
        {
            error = "cannot create directory " + part + ": " + std::strerror(errno);
            return false;
        }
    }
    struct stat st;
    if(::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    {
        error = dir + " is not a directory";
        return false;
    }
    return true;
}

bool DiagLog::open(const std::string& path, std::string& error)
{
    close();
    if(path.empty())
    {
        error = "empty log path";
        return false;
    }
    if(!makeParentDirectories(path, error))
        return false;
    std::FILE* f = std::fopen(path.c_str(), "w");
    if(f == nullptr)
    {
        error = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    std::setvbuf(f, nullptr, _IOLBF, 0); // line-buffered; line() also flushes
    file_  = f;
    lines_ = 0;
    return true;
}

void DiagLog::close() noexcept
{
    if(file_ != nullptr)
        std::fclose(file_);
    file_ = nullptr;
}

void DiagLog::line(double t, const char* fmt, ...) noexcept
{
    std::va_list args;
    va_start(args, fmt);
    vline(t, fmt, args);
    va_end(args);
}

void DiagLog::vline(double t, const char* fmt, std::va_list args) noexcept
{
    if(file_ == nullptr)
        return;
    const int n = std::vsnprintf(text_, sizeof text_, fmt, args);
    if(n < 0)
        std::snprintf(text_, sizeof text_, "(unformattable log line)");
    else if(static_cast<std::size_t>(n) >= sizeof text_)
        std::memcpy(text_ + sizeof text_ - 4, "...", 4); // cut, marked
    const std::size_t len = formatLogLine(buf_, sizeof buf_, t, text_);
    std::fwrite(buf_, 1, len, file_);
    std::fflush(file_);
    ++lines_;
}

bool RateLimiter::allow(double t) noexcept
{
    if(!started_ || t - start_ >= window_)
    {
        started_ = true;
        start_   = t;
        used_    = 0;
    }
    if(used_ < perWindow_)
    {
        ++used_;
        return true;
    }
    ++suppressed_;
    return false;
}

std::uint64_t RateLimiter::takeSuppressed() noexcept
{
    const std::uint64_t s = suppressed_;
    suppressed_           = 0;
    return s;
}

int loadPercentileBucket(const std::uint64_t (&counts)[kStatsLoadBuckets], double p) noexcept
{
    std::uint64_t total = 0;
    for(const std::uint64_t c : counts)
        total += c;
    if(total == 0)
        return -1;
    if(!(p > 0.0))
        p = 0.0;
    if(p > 1.0)
        p = 1.0;
    // The smallest bucket whose cumulative count reaches ceil(p * total) (at least 1).
    const double  want   = p * static_cast<double>(total);
    std::uint64_t needed = static_cast<std::uint64_t>(want);
    if(static_cast<double>(needed) < want)
        ++needed;
    if(needed == 0)
        needed = 1;
    std::uint64_t cum = 0;
    for(std::size_t i = 0; i < kStatsLoadBuckets; ++i)
    {
        cum += counts[i];
        if(cum >= needed)
            return static_cast<int>(i);
    }
    return static_cast<int>(kStatsLoadBuckets) - 1;
}

const char* loadBucketLabel(int bucket) noexcept
{
    static const char* const kLabels[kStatsLoadBuckets] = {"<10 %", "<25 %", "<50 %", "<75 %", "<100 %", ">=100 %"};
    return bucket >= 0 && bucket < static_cast<int>(kStatsLoadBuckets) ? kLabels[bucket] : "n/a";
}

std::size_t formatStats(char* out, std::size_t size, const PeriodicStats& s) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    const double expected = s.windowSeconds * static_cast<double>(s.sampleRate);
    const double ratio    = expected > 0.0 ? static_cast<double>(s.audioFrames) / expected : 0.0;
    const double fps      = s.windowSeconds > 0.0 ? static_cast<double>(s.framesDrawn) / s.windowSeconds : 0.0;
    // The optional parts are formatted first into their own buffers, then the whole
    // line in one call, so the result is cut at `size` in one place.
    char hist[160] = "";
    if(s.haveHistogram)
    {
        const std::uint64_t* h = s.loadHistogram;
        std::snprintf(hist, sizeof hist, ", p50 %s, p95 %s, histogram %llu/%llu/%llu/%llu/%llu/%llu",
                      loadBucketLabel(loadPercentileBucket(s.loadHistogram, 0.50)),
                      loadBucketLabel(loadPercentileBucket(s.loadHistogram, 0.95)), static_cast<unsigned long long>(h[0]),
                      static_cast<unsigned long long>(h[1]), static_cast<unsigned long long>(h[2]),
                      static_cast<unsigned long long>(h[3]), static_cast<unsigned long long>(h[4]),
                      static_cast<unsigned long long>(h[5]));
    }
    char freq[320] = "";
    if(s.cpuFreq != nullptr && s.cpuFreq[0] != '\0')
        std::snprintf(freq, sizeof freq, ", cpu freq %s", s.cpuFreq);
    char input[64] = "";
    if(s.inputSource != nullptr && s.inputSource[0] != '\0')
        std::snprintf(input, sizeof input, ", input %.16s peak %.3f", s.inputSource, static_cast<double>(s.inputPeak));
    char live[400] = "";
    if(s.live != nullptr && s.live[0] != '\0')
        std::snprintf(live, sizeof live, ", %s", s.live);
    char tail[64] = "";
    if(s.keysSuppressed > 0)
        std::snprintf(tail, sizeof tail, ", unmapped-key lines suppressed %llu",
                      static_cast<unsigned long long>(s.keysSuppressed));
    const int n = std::snprintf(
        out, size,
        "stats: window %.1f s, callback load %.1f %% (max %.1f %%%s), callbacks %llu, overruns %llu, largest %llu frames, "
        "audio clock %.3fx real time, peak L %.3f R %.3f, dropped %llu, frames %llu (%.1f fps)%s%s%s%s",
        s.windowSeconds, 100.0 * static_cast<double>(s.cpuSmoothed), 100.0 * static_cast<double>(s.cpuMax), hist,
        static_cast<unsigned long long>(s.callbacks), static_cast<unsigned long long>(s.overruns),
        static_cast<unsigned long long>(s.largestCallback), ratio,
        static_cast<double>(s.peakL), static_cast<double>(s.peakR), static_cast<unsigned long long>(s.dropped),
        static_cast<unsigned long long>(s.framesDrawn), fps, freq, input, live, tail);
    return n < 0 ? 0 : (static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1);
}

std::size_t formatLiveStats(char* out, std::size_t size, const LiveWindowStats& s) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    char dev[96];
    if(s.deviceOpen)
        std::snprintf(dev, sizeof dev, "device '%.60s' (%s)", s.device != nullptr ? s.device : "", s.running ? "running" : "buffering");
    else
        std::snprintf(dev, sizeof dev, "no device open");
    char clock[40];
    if(s.outputFrames > 0)
        std::snprintf(clock, sizeof clock, "%.4fx output",
                      static_cast<double>(s.captured) / static_cast<double>(s.outputFrames));
    else
        std::snprintf(clock, sizeof clock, "n/a");
    const int n = std::snprintf(
        out, size,
        "live: %s, fill %zu (min %zu, max %zu, target %zu, raised %llu), underruns %llu, skips %llu, repeats %llu, "
        "captured %llu, read %llu, dropped %llu, capture callbacks %llu, capture clock %s, pre peak %.3f, post peak %.3f",
        dev, s.fill, s.minFill, s.maxFill, s.target, static_cast<unsigned long long>(s.raises),
        static_cast<unsigned long long>(s.underruns), static_cast<unsigned long long>(s.overfills),
        static_cast<unsigned long long>(s.repeats), static_cast<unsigned long long>(s.captured),
        static_cast<unsigned long long>(s.read), static_cast<unsigned long long>(s.dropped),
        static_cast<unsigned long long>(s.callbacks), clock, static_cast<double>(s.prePeak),
        static_cast<double>(s.postPeak));
    return n < 0 ? 0 : (static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1);
}

} // namespace audreybrick
