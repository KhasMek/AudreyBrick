// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// record_wav_check: a helper (not a test) for the recording self-test.
//
//   record_wav_check <file.wav> <min_seconds> <max_seconds> <min_peak>
//
// Checks that the file is a canonical 16-bit stereo PCM WAV at 48 kHz whose
// header sizes match the file, whose length is within [min, max] seconds, and
// whose peak (of |sample| / 32767) is above min_peak (not silent). Prints one
// line; exit status 0 if all hold, 1 otherwise.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "record/RecordFormat.h"

using namespace audreybrick;

int main(int argc, char** argv)
{
    if(argc != 5)
    {
        std::fprintf(stderr, "usage: record_wav_check <file.wav> <min_seconds> <max_seconds> <min_peak>\n");
        return 2;
    }
    std::ifstream                   in(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const double                    minS = std::atof(argv[2]), maxS = std::atof(argv[3]), minPeak = std::atof(argv[4]);
    WavHeaderInfo                   h;
    if(!parseWavHeader(f.data(), f.size(), h))
    {
        std::printf("record_wav_check: FAIL: %s: no canonical WAV header (%zu bytes)\n", argv[1], f.size());
        return 1;
    }
    // Integer peak (a plain loop: GCC 12 at -O3 crashed vectorising a float
    // max reduction here).
    int maxAbs = 0;
    for(std::size_t i = kWavHeaderBytes; i + 1 < f.size(); i += 2)
    {
        const int v = static_cast<std::int16_t>(f[i] | (f[i + 1] << 8));
        const int a = v < 0 ? -v : v;
        if(a > maxAbs)
            maxAbs = a;
    }
    const double peak = maxAbs / 32767.0;
    const double secs = h.dataBytes / 4.0 / (h.sampleRate > 0 ? h.sampleRate : 1);
    const bool   fmt  = h.format == 1 && h.channels == 2 && h.sampleRate == 48000 && h.bitsPerSample == 16 &&
                     h.blockAlign == 4 && h.byteRate == 192000;
    const bool   size = h.riffSize == 36 + h.dataBytes && f.size() == kWavHeaderBytes + h.dataBytes;
    const bool   len  = secs >= minS && secs <= maxS;
    const bool   loud = peak > minPeak;
    std::printf("record_wav_check: %s: %s, %u Hz, %u ch, %u bit, %.3f s, peak %.4f, header sizes %s\n",
                fmt && size && len && loud ? "ok" : "FAIL", argv[1], h.sampleRate, h.channels, h.bitsPerSample, secs,
                peak, size ? "match the file" : "do NOT match the file");
    return fmt && size && len && loud ? 0 : 1;
}
