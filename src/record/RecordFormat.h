// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The recording's file format and names (docs/briefs/REC.md). SDL-free, no I/O.
//
//   - floatToPcm16(): NaN -> 0; clamp to [-1, 1]; scale by 32767; round to the
//     nearest integer, halves away from zero (independent of the FPU's rounding
//     mode). No dither. So +1 -> 32767, -1 -> -32767 (-32768 is never written),
//     +inf -> 32767, -inf -> -32767.
//   - The WAV header: the canonical 44-byte PCM header (RIFF, "fmt " chunk of 16
//     bytes, format 1, then "data"), little-endian, written by us (no dr_wav).
//     The RIFF size is 36 + data bytes; the data size is the bytes of samples.
//   - The data limit: 4,000,000,000 bytes (1,000,000,000 stereo 16-bit frames,
//     5 h 47 min at 48 kHz), well below the 32-bit size fields' 0xFFFFFFFF - 36.
//   - File names: rec-0001.wav, rec-0002.wav, ... (%04d, so rec-10000.wav after
//     rec-9999.wav). The next number is one more than the highest rec-<digits>.wav
//     in the folder (matched without regard to case; other names are ignored).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace audreybrick
{

constexpr std::size_t   kWavHeaderBytes      = 44;
constexpr std::uint16_t kRecordChannels      = 2;
constexpr std::uint16_t kRecordBitsPerSample = 16;
constexpr std::size_t   kRecordFrameBytes    = kRecordChannels * kRecordBitsPerSample / 8; // 4
constexpr std::uint64_t kRecordDataLimit     = 4000000000ull;
static_assert(kRecordDataLimit + 36 <= 0xFFFFFFFFull, "the RIFF size field must hold the largest file");
static_assert(kRecordDataLimit % kRecordFrameBytes == 0, "the limit is a whole number of frames");

std::int16_t floatToPcm16(float x) noexcept;

// Converts `frames` interleaved stereo frames.
void floatToPcm16(const float* in, std::int16_t* out, std::size_t frames) noexcept;

// Writes the 44-byte header for `dataBytes` bytes of 16-bit stereo samples at
// `sampleRate` into `out`.
void makeWavHeader(std::uint8_t out[kWavHeaderBytes], std::uint32_t sampleRate, std::uint32_t dataBytes) noexcept;

// What a header says (for the tests and the self-test's check). False if the
// 44 bytes are not a canonical PCM header.
struct WavHeaderInfo
{
    std::uint32_t riffSize      = 0;
    std::uint16_t format        = 0;
    std::uint16_t channels      = 0;
    std::uint32_t sampleRate    = 0;
    std::uint32_t byteRate      = 0;
    std::uint16_t blockAlign    = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t dataBytes     = 0;
};
bool parseWavHeader(const std::uint8_t* in, std::size_t size, WavHeaderInfo& out) noexcept;

// The number in "rec-<digits>.wav" (case-insensitive), or -1 for any other name.
long recordingNumber(const std::string& name) noexcept;
// One more than the highest recording number among `names` (1 if none).
long nextRecordingNumber(const std::vector<std::string>& names) noexcept;
// "rec-0001.wav" for 1.
std::string recordingFileName(long number);

} // namespace audreybrick
