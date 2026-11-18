// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// CpuFreqMonitor (M3-B): the CPU frequency situation for the diagnostic log's
// statistics line, read from Linux sysfs on the UI thread only (never on the
// audio thread).
//
//   discover(base)   finds the cpufreq policies: <base>/cpufreq/policy<N> holding
//                    scaling_cur_freq, sorted by N; if there are none, the per-CPU
//                    directories <base>/cpu<N>/cpufreq holding scaling_cur_freq.
//                    At most kMaxPolicies. <base> is normally
//                    /sys/devices/system/cpu; the environment variable
//                    AUDREY_SYSFS_CPU overrides it (a testing aid only: tests point
//                    it at a fake tree). Allocates (paths); call at start.
//   sample()         reads every policy's scaling_cur_freq once and folds the value
//                    into the current window's minimum and maximum. The app calls
//                    it about once a second. No allocation: open/read/close of a
//                    small file into a fixed buffer.
//   formatAndReset() samples once more (the "now" value) and formats
//                    "policy0 1416 MHz (window 408..1416 MHz, 11 samples)" per policy,
//                    joined by "; ", then starts a new window. A policy whose file
//                    could not be read or held no valid number: "policy0 unreadable"
//                    (now) and/or "no valid sample" (window).
//   governors()      "policy0 schedutil; policy4 performance" (scaling_governor of
//                    each policy, "?" if unreadable).
//
// scaling_cur_freq holds kHz as a decimal number and a newline; anything else
// (empty, negative, zero, trailing garbage, too long) is not a valid sample.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace audreybrick
{

// The default sysfs base and the variable that overrides it (testing aid).
constexpr const char* kSysfsCpuDefault = "/sys/devices/system/cpu";
constexpr const char* kSysfsCpuEnv     = "AUDREY_SYSFS_CPU";

// Parses a sysfs frequency file's text: a positive decimal number of kHz, optional
// surrounding whitespace, nothing else. False for anything else.
bool parseKhz(const char* text, long& khz) noexcept;

// Reads at most size - 1 bytes of `path` into `buf` and NUL-terminates. False if
// it cannot be opened or read (or `buf` is null or size is 0). No allocation.
bool readSmallFile(const char* path, char* buf, std::size_t size) noexcept;

// The text of a one-word sysfs file (e.g. scaling_governor) without trailing
// whitespace, or "?" if it cannot be read or is empty.
std::string readWord(const std::string& path);

class CpuFreqMonitor
{
  public:
    static constexpr std::size_t kMaxPolicies = 8;

    // Returns the number of policies found (0: no cpufreq information).
    std::size_t discover(const std::string& base);

    std::size_t count() const noexcept { return count_; }
    bool        available() const noexcept { return count_ > 0; }
    // "policy0" or "cpu0".
    const char* name(std::size_t i) const noexcept { return i < count_ ? policies_[i].name : ""; }
    // The directory of policy i ("<base>/cpufreq/policy0").
    const std::string& directory(std::size_t i) const noexcept;

    void        sample() noexcept;
    std::size_t formatAndReset(char* out, std::size_t size) noexcept;
    std::string governors() const;

    // Successful and failed reads since discover() (for tests and the log).
    std::uint64_t goodReads() const noexcept { return goodReads_; }
    std::uint64_t badReads() const noexcept { return badReads_; }

  private:
    struct Policy
    {
        char        name[24] = {};
        std::string dir;
        std::string curPath;
        long        last    = 0;   // last valid kHz (0: none yet)
        bool        lastOk  = false;
        long        winMin  = 0;
        long        winMax  = 0;
        unsigned    samples = 0;   // valid samples in the window
    };

    bool read(Policy& p) noexcept;

    Policy        policies_[kMaxPolicies];
    std::size_t   count_     = 0;
    std::uint64_t goodReads_ = 0;
    std::uint64_t badReads_  = 0;
};

} // namespace audreybrick
