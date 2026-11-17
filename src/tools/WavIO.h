// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// WAV reading and writing for the offline tools and tests, on top of dr_wav.
// Not for the audio path: everything here allocates and does file I/O.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace audreybrick::tools
{

struct WavData
{
    std::uint32_t      sampleRate = 0;
    std::uint32_t      channels   = 0;
    std::uint64_t      frames     = 0;
    std::uint16_t      formatTag  = 0; // 1 = PCM, 3 = IEEE float (after resolving EXTENSIBLE)
    std::uint16_t      bitsPerSample = 0;
    std::vector<float> interleaved; // frames * channels samples, converted to float
};

// Reads any WAV that dr_wav can decode and converts it to float.
bool readWav(const std::string& path, WavData& out, std::string& error);

// Reads an input WAV for AudreyVoice: must be 48000 Hz. Mono is used as is;
// stereo is mixed as L + R (plain sum, the project's input-stage decision,
// docs/PLAN.md section 4). Any other channel count is rejected.
bool readInputWav(const std::string& path, std::vector<float>& mono, std::string& error);

// Writes a 32-bit IEEE float WAV. `interleaved` holds frames * channels samples.
bool writeWavF32(const std::string& path, std::uint32_t sampleRate, std::uint32_t channels,
                 const float* interleaved, std::uint64_t frames, std::string& error);

} // namespace audreybrick::tools
