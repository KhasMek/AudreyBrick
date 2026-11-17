// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// wavdiff: sample-by-sample comparison of two 32-bit float WAV files (task M1-D).
// Compiled by scripts/build_linux_arm64.sh inside the test container; not part of
// the CMake build and never part of the app.
//
//   wavdiff --header
//   wavdiff <name> <test.wav> <reference.wav> [--profile <file> [--window <s>]]
//
// Prints one table row for <name>, comparing <test> (the Linux render) with
// <reference> (the macOS render), over all channels, interleaved:
//   frames      length in frames (both files must agree)
//   max|d|      largest absolute sample difference
//   rms(d)      RMS of the difference
//   rms(ref)    RMS of the reference itself
//   t>1e-6      time (s) of the first frame where any channel differs by more than
//               1e-6, or "-" if none
//   t>1e-3      the same for 1e-3 (where the difference becomes audible-scale)
//   max|d|<1s   largest absolute difference within the first second
//   bits        "identical" if every sample is bit-identical, else the count of
//               samples whose bits differ (non-finite samples included; the other
//               columns leave them out)
// --profile writes, per window (default 1 s), the start time, max|d|, rms(d) and
// rms(ref) of that window to <file>, so the growth of a difference can be read.
//
// Exit 0 if both files were read and compared (whatever the difference), 1 if a
// file holds NaN or inf, 2 on bad usage, an unreadable file or a format mismatch.
// Self-contained: no dr_wav, reads only the RIFF "fmt " and "data" chunks of a
// 32-bit IEEE float file (format tag 3, or EXTENSIBLE with the float subformat).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct Wav
{
    std::uint32_t      rate     = 0;
    std::uint16_t      channels = 0;
    std::vector<float> samples;
};

std::uint32_t le32(const unsigned char* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
           | (std::uint32_t(p[3]) << 24);
}
std::uint16_t le16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }

bool readWav(const char* path, Wav& w, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
    {
        err = std::string(path) + ": cannot open";
        return false;
    }
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) != 0 || std::memcmp(&b[8], "WAVE", 4) != 0)
    {
        err = std::string(path) + ": not a RIFF WAVE file";
        return false;
    }
    bool        haveFmt = false;
    std::size_t pos     = 12;
    while(pos + 8 <= b.size())
    {
        const std::uint32_t size = le32(&b[pos + 4]);
        const std::size_t   body = pos + 8;
        if(body + size > b.size())
        {
            err = std::string(path) + ": truncated chunk";
            return false;
        }
        if(std::memcmp(&b[pos], "fmt ", 4) == 0 && size >= 16)
        {
            std::uint16_t tag = le16(&b[body]);
            w.channels        = le16(&b[body + 2]);
            w.rate            = le32(&b[body + 4]);
            const std::uint16_t bits = le16(&b[body + 14]);
            if(tag == 0xFFFE && size >= 26)
                tag = le16(&b[body + 24]); // first two bytes of the subformat GUID
            if(tag != 3 || bits != 32 || w.channels == 0)
            {
                err = std::string(path) + ": not 32-bit IEEE float";
                return false;
            }
            haveFmt = true;
        }
        else if(std::memcmp(&b[pos], "data", 4) == 0)
        {
            if(!haveFmt)
            {
                err = std::string(path) + ": data before fmt";
                return false;
            }
            w.samples.resize(size / 4);
            std::memcpy(w.samples.data(), &b[body], w.samples.size() * 4);
            return true;
        }
        pos = body + size + (size & 1u);
    }
    err = std::string(path) + ": no data chunk";
    return false;
}

void timeField(char* buf, std::size_t n, long long frame, std::uint32_t rate)
{
    if(frame < 0)
        std::snprintf(buf, n, "-");
    else
        std::snprintf(buf, n, "%.5f", double(frame) / rate);
}
} // namespace

int main(int argc, char** argv)
{
    if(argc == 2 && std::strcmp(argv[1], "--header") == 0)
    {
        std::printf("%-26s %9s %11s %11s %11s %10s %10s %11s %s\n", "render", "frames", "max|d|",
                    "rms(d)", "rms(ref)", "t>1e-6", "t>1e-3", "max|d|<1s", "bits");
        return 0;
    }
    if(argc != 4 && argc != 6 && argc != 8)
    {
        std::fprintf(stderr, "usage: wavdiff <name> <test.wav> <ref.wav> [--profile <file> [--window <s>]]\n");
        return 2;
    }
    const char* profilePath = nullptr;
    double      window      = 1.0;
    for(int i = 4; i + 1 < argc; i += 2)
    {
        if(std::strcmp(argv[i], "--profile") == 0)
            profilePath = argv[i + 1];
        else if(std::strcmp(argv[i], "--window") == 0)
            window = std::strtod(argv[i + 1], nullptr);
        else
        {
            std::fprintf(stderr, "wavdiff: unknown option %s\n", argv[i]);
            return 2;
        }
    }

    Wav         t, r;
    std::string err;
    if(!readWav(argv[2], t, err) || !readWav(argv[3], r, err))
    {
        std::fprintf(stderr, "wavdiff: %s\n", err.c_str());
        return 2;
    }
    if(t.rate != r.rate || t.channels != r.channels || t.samples.size() != r.samples.size())
    {
        std::fprintf(stderr, "wavdiff: %s: format or length differs (%u Hz %u ch %zu vs %u Hz %u ch %zu)\n",
                     argv[1], t.rate, t.channels, t.samples.size(), r.rate, r.channels, r.samples.size());
        return 2;
    }

    const std::size_t ch     = t.channels;
    const std::size_t frames = t.samples.size() / ch;
    const std::size_t oneSec = std::size_t(t.rate);
    const std::size_t winLen = window > 0 ? std::size_t(window * t.rate) : oneSec;

    std::FILE* prof = nullptr;
    if(profilePath)
    {
        prof = std::fopen(profilePath, "w");
        if(!prof)
        {
            std::fprintf(stderr, "wavdiff: cannot write %s\n", profilePath);
            return 2;
        }
        std::fprintf(prof, "# %s: per %.3f s window: start_s max|d| rms(d) rms(ref)\n", argv[1], window);
    }

    double        maxAbs = 0, sumD2 = 0, sumR2 = 0, maxFirstSec = 0;
    long long     first6 = -1, first3 = -1;
    std::uint64_t bitDiffs = 0, nonFinite = 0;
    double        wMax = 0, wD2 = 0, wR2 = 0;
    std::size_t   wN = 0, wStart = 0;
    for(std::size_t f = 0; f < frames; ++f)
    {
        for(std::size_t c = 0; c < ch; ++c)
        {
            const float a = t.samples[f * ch + c];
            const float b = r.samples[f * ch + c];
            std::uint32_t ab, bb;
            std::memcpy(&ab, &a, 4);
            std::memcpy(&bb, &b, 4);
            if(ab != bb)
                ++bitDiffs; // non-finite samples included
            if(!std::isfinite(a) || !std::isfinite(b))
            {
                ++nonFinite;
                continue;
            }
            const double d  = double(a) - double(b);
            const double ad = std::fabs(d);
            if(ad > maxAbs)
                maxAbs = ad;
            if(f < oneSec && ad > maxFirstSec)
                maxFirstSec = ad;
            if(first6 < 0 && ad > 1e-6)
                first6 = (long long)f;
            if(first3 < 0 && ad > 1e-3)
                first3 = (long long)f;
            sumD2 += d * d;
            sumR2 += double(b) * double(b);
            wMax = ad > wMax ? ad : wMax;
            wD2 += d * d;
            wR2 += double(b) * double(b);
            ++wN;
        }
        if(prof && (f + 1 - wStart == winLen || f + 1 == frames))
        {
            const double n = wN ? double(wN) : 1.0;
            std::fprintf(prof, "%8.3f %.6e %.6e %.6e\n", double(wStart) / t.rate, wMax, std::sqrt(wD2 / n),
                         std::sqrt(wR2 / n));
            wMax = wD2 = wR2 = 0;
            wN               = 0;
            wStart           = f + 1;
        }
    }
    if(prof)
        std::fclose(prof);

    const double n = double(frames * ch);
    char         t6[32], t3[32], bits[32];
    timeField(t6, sizeof t6, first6, t.rate);
    timeField(t3, sizeof t3, first3, t.rate);
    if(bitDiffs == 0)
        std::snprintf(bits, sizeof bits, "identical");
    else
        std::snprintf(bits, sizeof bits, "%llu differ", (unsigned long long)bitDiffs);
    std::printf("%-26s %9zu %11.4e %11.4e %11.4e %10s %10s %11.4e %s\n", argv[1], frames, maxAbs,
                std::sqrt(sumD2 / n), std::sqrt(sumR2 / n), t6, t3, maxFirstSec, bits);
    if(nonFinite)
    {
        std::fprintf(stderr, "wavdiff: %s: %llu non-finite samples\n", argv[1], (unsigned long long)nonFinite);
        return 1;
    }
    return 0;
}
