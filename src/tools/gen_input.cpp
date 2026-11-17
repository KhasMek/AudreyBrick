// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// audrey-gen-input: writes the input WAVs used by the input scripts in scripts/.
//
//   audrey-gen-input <output_dir>
//
// All files are 48 kHz, mono, 32-bit float, 20 s long, and fully deterministic
// (no clock, no system random source):
//
//   sine_burst.wav   220 Hz sine bursts with 20 ms raised-cosine fades:
//                      2.0 to  6.0 s at amplitude 0.1 (-20 dBFS)
//                     10.0 to 14.0 s at amplitude 0.7 (-3.1 dBFS)
//                    silence elsewhere.
//   noise_burst.wav  white noise, uniform in +-0.25, from a fixed xorshift32 seed,
//                    2.0 to 3.0 s with 5 ms raised-cosine fades; silence elsewhere.
//   hot_burst.wav    220 Hz sine bursts at amplitude 1.8 (+5.1 dBFS, above full scale,
//                    as a stereo L + R sum of two near-full-scale channels can be),
//                    2.0 to 4.0 s and 12.0 to 14.0 s, 20 ms raised-cosine fades.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "tools/WavIO.h"

namespace
{
constexpr double      kSr     = 48000.0;
constexpr double      kPi     = 3.14159265358979323846;
constexpr std::size_t kFrames = static_cast<std::size_t>(20.0 * kSr);

// Raised-cosine envelope for a burst [start, end) seconds with `fade` seconds at each end.
double envelope(double t, double start, double end, double fade)
{
    if(t < start || t >= end)
        return 0.0;
    const double in  = (t - start) / fade;
    const double out = (end - t) / fade;
    const double x   = std::fmin(1.0, std::fmin(in, out));
    return 0.5 - 0.5 * std::cos(kPi * x);
}

std::vector<float> sineBurst()
{
    std::vector<float> s(kFrames);
    for(std::size_t n = 0; n < kFrames; ++n)
    {
        const double t   = n / kSr;
        const double amp = 0.1 * envelope(t, 2.0, 6.0, 0.02) + 0.7 * envelope(t, 10.0, 14.0, 0.02);
        s[n]             = static_cast<float>(amp * std::sin(2.0 * kPi * 220.0 * t));
    }
    return s;
}

std::vector<float> hotBurst()
{
    std::vector<float> s(kFrames);
    for(std::size_t n = 0; n < kFrames; ++n)
    {
        const double t   = n / kSr;
        const double amp = 1.8 * (envelope(t, 2.0, 4.0, 0.02) + envelope(t, 12.0, 14.0, 0.02));
        s[n]             = static_cast<float>(amp * std::sin(2.0 * kPi * 220.0 * t));
    }
    return s;
}

std::vector<float> noiseBurst()
{
    std::vector<float> s(kFrames);
    std::uint32_t      x = 0x12345678u;
    for(std::size_t n = 0; n < kFrames; ++n)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const double u = (static_cast<double>(x) / 4294967295.0) * 2.0 - 1.0; // -1..1
        const double t = n / kSr;
        s[n]           = static_cast<float>(0.25 * envelope(t, 2.0, 3.0, 0.005) * u);
    }
    return s;
}
} // namespace

int main(int argc, char** argv)
{
    if(argc != 2)
    {
        std::fprintf(stderr, "usage: audrey-gen-input <output_dir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);
    if(ec)
    {
        std::fprintf(stderr, "audrey-gen-input: error: cannot create %s: %s\n", dir.c_str(), ec.message().c_str());
        return 1;
    }
    const struct
    {
        const char*        name;
        std::vector<float> data;
    } files[] = {{"sine_burst.wav", sineBurst()}, {"noise_burst.wav", noiseBurst()}, {"hot_burst.wav", hotBurst()}};

    for(const auto& f : files)
    {
        const std::string path = dir + "/" + f.name;
        std::string       err;
        if(!audreybrick::tools::writeWavF32(path, 48000, 1, f.data.data(), f.data.size(), err))
        {
            std::fprintf(stderr, "audrey-gen-input: error: %s\n", err.c_str());
            return 1;
        }
        std::printf("audrey-gen-input: wrote %s\n", path.c_str());
    }
    return 0;
}
