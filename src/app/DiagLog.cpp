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

std::size_t formatStats(char* out, std::size_t size, const PeriodicStats& s) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    const double expected = s.windowSeconds * static_cast<double>(s.sampleRate);
    const double ratio    = expected > 0.0 ? static_cast<double>(s.audioFrames) / expected : 0.0;
    const double fps      = s.windowSeconds > 0.0 ? static_cast<double>(s.framesDrawn) / s.windowSeconds : 0.0;
    // The optional tail is formatted first into its own buffer, then the whole line
    // in one call, so the result is cut at `size` in one place.
    char tail[64] = "";
    if(s.keysSuppressed > 0)
        std::snprintf(tail, sizeof tail, ", unmapped-key lines suppressed %llu",
                      static_cast<unsigned long long>(s.keysSuppressed));
    const int n = std::snprintf(
        out, size,
        "stats: window %.1f s, cpu %.1f %% (max %.1f %%), callbacks %llu, overruns %llu, largest %llu frames, audio clock %.3fx "
        "real time, peak L %.3f R %.3f, dropped %llu, frames %llu (%.1f fps)%s",
        s.windowSeconds, 100.0 * static_cast<double>(s.cpuSmoothed), 100.0 * static_cast<double>(s.cpuMax),
        static_cast<unsigned long long>(s.callbacks), static_cast<unsigned long long>(s.overruns),
        static_cast<unsigned long long>(s.largestCallback), ratio,
        static_cast<double>(s.peakL), static_cast<double>(s.peakR), static_cast<unsigned long long>(s.dropped),
        static_cast<unsigned long long>(s.framesDrawn), fps, tail);
    return n < 0 ? 0 : (static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1);
}

} // namespace audreybrick
