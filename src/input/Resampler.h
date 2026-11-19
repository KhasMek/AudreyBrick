// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Resampler: sample-rate conversion of a mono stream, for the file loader (never
// the audio thread). A windowed-sinc interpolator, our own so the input code
// stays SDL-free and testable everywhere (docs/briefs/M2b.md, M2b-A).
//
// Method:
//   - The ratio is reduced to L/M (output rate / input rate over their greatest
//     common divisor). Output sample k sits at input time t = k * M / L, tracked
//     exactly in integers: base = floor(t), phase = (k * M mod L) / L.
//   - y[k] = sum over j = -H+1 .. H of x[base + j] * h(j - phase), with
//     h(u) = 2 fc sinc(2 fc u) * kaiser(u / H), in input samples. Samples before
//     the start and after the end count as zero.
//   - Cut-off fc = 0.5 * min(1, out / in) * kPassFraction (cycles per input
//     sample): 95 % of the lower of the two Nyquist frequencies, so a downsampler
//     also removes what would alias. Kaiser beta 9.0 (about 90 dB stopband).
//   - H = kHalfTapsAtUnity / min(1, out / in), at most kMaxHalfTaps (2H taps):
//     32 (64 taps) for 44.1 kHz and anything slower, 64 (128 taps) for 96 kHz,
//     128 (256 taps) from 192 kHz up (where the transition band therefore widens).
//   - The kernel is tabulated per phase. When L is small enough (L * 2H <= 1 M
//     entries: 160 phases for 44.1 kHz, 1 for 96 kHz) every phase is exact;
//     otherwise 4096 phases with linear interpolation between neighbours. Every
//     phase's taps are normalised to sum to 1, so DC passes at exactly unity.
//   - Streaming: push() takes input in pieces and appends the outputs it fully
//     determines; finish() appends the rest. The output then has exactly
//     outputLength(inputs, in, out) = ceil(inputs * out / in) samples. The input
//     history kept is about 2H samples (trimmed in 64 k blocks).
//
// Cost: 2H multiply-adds per output sample (64 for 44.1 kHz). Measured quality and
// speed: tests/input/input_resampler_test.cpp prints them.
//
// Allocates (the table, the history, the output). Not for the audio thread.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace audreybrick
{

class Resampler
{
  public:
    static constexpr int    kHalfTapsAtUnity = 32;
    static constexpr int    kMaxHalfTaps     = 128;
    static constexpr double kPassFraction    = 0.95;
    static constexpr double kKaiserBeta      = 9.0;
    static constexpr std::size_t kMaxExactEntries = std::size_t{1} << 20; // table entries (4 MB)
    static constexpr std::size_t kInterpolatedPhases = 4096;

    // Both rates above 0.
    Resampler(std::uint32_t inRate, std::uint32_t outRate);

    void push(const float* in, std::size_t n, std::vector<float>& out);
    void finish(std::vector<float>& out);

    static std::uint64_t outputLength(std::uint64_t inFrames, std::uint32_t inRate, std::uint32_t outRate) noexcept;

    int         taps() const noexcept { return 2 * half_; }
    std::size_t phases() const noexcept { return phases_; }
    bool        exactPhases() const noexcept { return exact_; }

  private:
    void produce(std::vector<float>& out, std::int64_t available);
    void trim();

    std::uint64_t      l_ = 1; // output rate / g
    std::uint64_t      m_ = 1; // input rate / g
    int                half_ = 0;
    std::size_t        phases_ = 1;
    bool               exact_  = true;
    std::vector<float> table_;  // (phases_ + 1) rows of 2 * half_ taps
    std::vector<float> buf_;    // input history; buf_[0] is input index bufStart_
    std::int64_t       bufStart_ = 0;
    std::uint64_t      totalIn_  = 0; // real input samples pushed
    std::uint64_t      k_        = 0; // next output index
    bool               finished_ = false;
};

} // namespace audreybrick
