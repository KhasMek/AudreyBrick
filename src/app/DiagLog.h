// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The diagnostic log's SDL-free parts (docs/briefs/M3.md, M3-A, C.3).
//
// DiagLog: a text file, one event per line, each line
//     [    12.345] <text>
// with the app's clock in seconds. Every line is flushed as it is written (the
// stream is also line-buffered), so the log survives a crash or a power-off up to
// the last complete line. open() creates the file's directory (and its parents)
// if missing. A failure to open is reported once by the caller and is not fatal:
// line() on a closed log does nothing.
//
// THREADS: the UI thread only. Never call any of this from the audio callback;
// values from the audio side reach the log through the atomics it already
// publishes (AudioRender, ButtonControlSource), read on the UI thread.
//
// RateLimiter: at most N lines per window of W seconds, counting what it
// suppressed (for unmapped key events, whose rate the app does not control).
// SeenSet: remembers which of N things have been seen (first-seen lines).
// PeriodicStats / formatStats: the line written every 10 seconds.
//
// No allocation after open(): lines are formatted into a fixed buffer.

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace audreybrick
{

#if defined(__GNUC__) || defined(__clang__)
#define AB_PRINTF_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define AB_PRINTF_FORMAT(fmt, args)
#endif

// Longest line written, without the time prefix; longer text is cut and ends in "...".
constexpr std::size_t kLogMaxText = 2000;

// Formats one log line: "[%10.3f] " + text + "\n". Control characters in `text`
// (newlines, tabs, ...) become spaces, so every entry is one line. Writes at most
// `size` bytes including the NUL; returns the length written.
std::size_t formatLogLine(char* out, std::size_t size, double t, const char* text) noexcept;

// Creates the directory that will hold `path` (all missing parents, like mkdir -p).
// A path without a directory part needs nothing. Returns false with `error` set.
bool makeParentDirectories(const std::string& path, std::string& error);

class DiagLog
{
  public:
    DiagLog() = default;
    ~DiagLog() { close(); }
    DiagLog(const DiagLog&)            = delete;
    DiagLog& operator=(const DiagLog&) = delete;

    // Creates the directory if needed and opens `path` for writing (truncating
    // it). Returns false with `error` set on failure; the log stays closed.
    bool open(const std::string& path, std::string& error);
    void close() noexcept;
    bool isOpen() const noexcept { return file_ != nullptr; }

    // Writes one line at time t (printf-style). Does nothing when closed.
    void line(double t, const char* fmt, ...) noexcept AB_PRINTF_FORMAT(3, 4);
    void vline(double t, const char* fmt, std::va_list args) noexcept;

    // Lines written since open().
    std::uint64_t lines() const noexcept { return lines_; }

  private:
    std::FILE*    file_  = nullptr;
    std::uint64_t lines_ = 0;
    char          text_[kLogMaxText + 1]      = {};
    char          buf_[kLogMaxText + 64]      = {};
};

// At most `perWindow` allowed events per window of `windowSeconds`; a window
// starts at the first event after the previous window ended.
class RateLimiter
{
  public:
    RateLimiter(int perWindow, double windowSeconds) noexcept : perWindow_(perWindow), window_(windowSeconds) {}

    // True if an event at time t may be logged; otherwise counts it as suppressed.
    bool allow(double t) noexcept;
    // Events suppressed since the last call, and resets the count.
    std::uint64_t takeSuppressed() noexcept;

  private:
    int           perWindow_;
    double        window_;
    double        start_      = 0.0;
    bool          started_    = false;
    int           used_       = 0;
    std::uint64_t suppressed_ = 0;
};

// Which of N indices have been seen. first(i) is true exactly once per index;
// indices out of range are never "first".
template<std::size_t N>
class SeenSet
{
  public:
    bool first(std::size_t i) noexcept
    {
        if(i >= N || seen_[i])
            return false;
        seen_[i] = true;
        return true;
    }
    bool seen(std::size_t i) const noexcept { return i < N && seen_[i]; }

  private:
    bool seen_[N] = {};
};

// One periodic statistics line.
struct PeriodicStats
{
    double        windowSeconds   = 0.0; // wall-clock length of the window
    float         cpuSmoothed     = 0.0f; // callback load at the end of the window (1.0 = 100 %)
    float         cpuMax          = 0.0f; // largest callback load in the window
    std::uint64_t largestCallback = 0;    // largest callback buffer since start, frames
    std::uint64_t callbacks       = 0;    // callbacks in the window
    std::uint64_t overruns        = 0;    // callbacks in the window that took longer than their buffer
    std::uint64_t audioFrames     = 0;    // frames rendered in the window
    int           sampleRate      = 0;    // for the audio clock ratio
    float         peakL           = 0.0f; // largest output peak in the window
    float         peakR           = 0.0f;
    std::uint64_t dropped         = 0;    // UI commands dropped since start
    std::uint64_t framesDrawn     = 0;    // UI frames presented in the window
    std::uint64_t keysSuppressed  = 0;    // unmapped-key lines suppressed in the window
};

// "stats: window 10.0 s, cpu 3.4 % (max 5.2 %), callbacks 1875, overruns 0, largest 256 frames,
//  audio clock 1.000x real time, peak L 0.123 R 0.130, dropped 0, frames 600 (60.0 fps)"
std::size_t formatStats(char* out, std::size_t size, const PeriodicStats& s) noexcept;

} // namespace audreybrick
