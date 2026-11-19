// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M2b-A2: the loader's peak memory for one load (loadInputFile, the function the
// loader thread runs). operator new and delete are replaced by versions that
// keep a size header, so the live bytes and their peak are known exactly. (dr_wav
// allocates through malloc, not operator new; for PCM files without metadata it
// allocates nothing.)
//   - A 10-minute file: the peak is the 48 kHz output (28,800,000 floats,
//     115.2 MB) plus at most a few MB (the block buffers and, when converting, the
//     resampler's table and history): no second copy of the output at any time.
//   - A file far longer than the cap (20 and 15 minutes) costs no more than a file
//     exactly at the cap, converted (8 kHz) and not converted (48 kHz).
// The numbers are printed for the report.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "input/InputFiles.h"
#include "input/InputTypes.h"
#include "input_test_common.h"

namespace
{
constexpr std::size_t    kHeader = 16; // keeps malloc's 16-byte alignment
std::atomic<long long>   g_live{0};
std::atomic<long long>   g_peak{0};

void* trackedAlloc(std::size_t size)
{
    void* raw = std::malloc(size + kHeader);
    if(raw == nullptr)
        throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = size;
    const long long now             = g_live.fetch_add(static_cast<long long>(size)) + static_cast<long long>(size);
    long long       p               = g_peak.load();
    while(now > p && !g_peak.compare_exchange_weak(p, now))
    {
    }
    return static_cast<char*>(raw) + kHeader;
}
void trackedFree(void* p) noexcept
{
    if(p == nullptr)
        return;
    void* raw = static_cast<char*>(p) - kHeader;
    g_live.fetch_sub(static_cast<long long>(*static_cast<std::size_t*>(raw)));
    std::free(raw);
}
} // namespace

void* operator new(std::size_t size) { return trackedAlloc(size); }
void* operator new[](std::size_t size) { return trackedAlloc(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return trackedAlloc(size);
    }
    catch(...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept { return operator new(size, t); }
void  operator delete(void* p) noexcept { trackedFree(p); }
void  operator delete[](void* p) noexcept { trackedFree(p); }
void  operator delete(void* p, std::size_t) noexcept { trackedFree(p); }
void  operator delete[](void* p, std::size_t) noexcept { trackedFree(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { trackedFree(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { trackedFree(p); }

namespace
{
using namespace audreybrick;
using itest::check;

struct Measured
{
    bool        ok        = false;
    std::size_t frames    = 0;
    bool        cut       = false;
    long long   peakExtra = 0; // peak live bytes during the load, above the live bytes before it
};

Measured measure(const itest::TempDir& d, const std::string& name)
{
    Measured    m;
    LoadedInput out;
    std::string reason, detail;
    const long long before = g_live.load();
    g_peak.store(before);
    m.ok        = loadInputFile(d.path(), name, nullptr, out, reason, detail);
    m.peakExtra = g_peak.load() - before;
    if(m.ok)
    {
        m.frames = out.buffer->samples.size();
        m.cut    = out.info.cut;
    }
    return m; // `out` (the buffer) is freed here
}

double mb(long long b)
{
    return static_cast<double>(b) / 1.0e6;
}
} // namespace

int main()
{
    itest::TempDir    d("memory");
    const long long   outBytes = static_cast<long long>(kInputMaxFrames * sizeof(float));
    const auto        quiet    = [](std::size_t i, int) { return ((i * 7919) % 200) / 1000.0; };
    struct Case
    {
        const char*   name;
        std::uint32_t rate;
        itest::Fmt    fmt;
        double        seconds;
    };
    const Case cases[] = {
        {"exact8k.wav", 8000, itest::Fmt::Pcm8, 600.0},  {"long8k.wav", 8000, itest::Fmt::Pcm8, 1200.0},
        {"exact48k.wav", 48000, itest::Fmt::Pcm16, 600.0}, {"long48k.wav", 48000, itest::Fmt::Pcm16, 900.0},
    };
    Measured m[4];
    for(int i = 0; i < 4; ++i)
    {
        const Case& c = cases[i];
        itest::writeWav(d.file(c.name), c.rate, 1, c.fmt, static_cast<std::size_t>(c.seconds * c.rate), quiet);
        m[i] = measure(d, c.name);
        std::remove(d.file(c.name).c_str());
        std::printf("%-13s %5u Hz %6.0f s: %s, %zu frames%s; peak during the load %.3f MB = output %.3f MB + %.3f MB\n",
                    c.name, c.rate, c.seconds, m[i].ok ? "loaded" : "FAILED", m[i].frames, m[i].cut ? " (cut)" : "",
                    mb(m[i].peakExtra), mb(outBytes), mb(m[i].peakExtra - outBytes));
    }
    for(int i = 0; i < 4; ++i)
        check(m[i].ok && m[i].frames == kInputMaxFrames && m[i].cut == (i % 2 == 1), std::string(cases[i].name) +
                                                                                       ": 28,800,000 frames at 48 kHz" +
                                                                                       (i % 2 == 1 ? ", cut" : ", not cut"));
    check(m[0].peakExtra >= outBytes && m[0].peakExtra <= outBytes + 6000000 && m[2].peakExtra >= outBytes &&
              m[2].peakExtra <= outBytes + 1000000,
          "a 10-minute load peaks at the output (115.2 MB) plus at most 6 MB converted, 1 MB not converted: never two "
          "copies of the output");
    check(m[1].peakExtra <= m[0].peakExtra + 65536 && m[3].peakExtra <= m[2].peakExtra + 65536,
          "a file far longer than the cap (20 min at 8 kHz, 15 min at 48 kHz) costs no more than one exactly at the cap "
          "(within 64 kB)");
    return itest::finish();
}
