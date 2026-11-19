// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared constants and small types of the input stage (docs/briefs/M2b.md,
// "Design shared by both tasks"). Header only, no SDL, no allocation.
//
// The input gain: one knob position n in 0..1 mapped by a square law to a linear
// gain of 0 to 4, gain = (2n)^2, so n = 0.5 is unity (0 dB) and n = 1 is +12 dB.
// The stage computes in = gain * mono, where mono is the source mixed to one
// channel as L + R (a mono source as it is), and hands `in` to the original's
// unity-gain input point (docs/SIGNAL_FLOW.md section 4).

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace audreybrick
{

// The sources of M2b-A. M2b-B adds the live sources (internal mic, USB) between
// OFF and FILE; the order here is the order on the screen.
enum class InputSource : std::uint8_t
{
    Off,
    File,
    Count
};

constexpr int kInputSourceCount = static_cast<int>(InputSource::Count);

constexpr const char* inputSourceName(InputSource s) noexcept
{
    switch(s)
    {
        case InputSource::Off: return "OFF";
        case InputSource::File: return "FILE";
        case InputSource::Count: break;
    }
    return "?";
}

// The engine's rate (AudreyVoice::kSampleRate); every buffer handed to the
// audio thread is at this rate.
constexpr std::uint32_t kInputSampleRate = 48000;

// File input: at most this much of a file is used; longer files are cut (10
// minutes since M2b-A2: 28,800,000 samples, 115.2 MB as float).
constexpr double      kInputMaxSeconds = 600.0;
constexpr std::size_t kInputMaxFrames  = static_cast<std::size_t>(kInputMaxSeconds * kInputSampleRate);
// At most this many WAV files are listed (the first ones by name).
constexpr std::size_t kInputMaxFiles = 256;
// Samples of a loaded file are clamped to this (per channel and after the mix);
// no real file comes near it, but a float file can hold 3e38, which would reach
// infinity after the gain.
constexpr float kInputSampleLimit = 8.0f;
// Source rates the loader accepts (anything dr_wav decodes within this range).
constexpr std::uint32_t kInputMinSourceRate = 8000;
constexpr std::uint32_t kInputMaxSourceRate = 384000;

// --- Gain -------------------------------------------------------------------------
constexpr float kInputGainUnityNorm = 0.5f; // the default, and Y + up/down
constexpr float kInputGainMax       = 4.0f; // at n = 1 (+12 dB)

constexpr float clampInputNorm(float n) noexcept
{
    return n > 0.0f ? (n < 1.0f ? n : 1.0f) : 0.0f; // NaN -> 0
}

// gain = (2n)^2: 0 at n = 0, 1.0 at n = 0.5, 4.0 at n = 1.
constexpr float inputGainFromNorm(float n) noexcept
{
    const float g = 2.0f * clampInputNorm(n);
    return g * g;
}

// The gain in dB; -infinity at n = 0.
inline float inputGainDb(float n) noexcept
{
    const float g = inputGainFromNorm(n);
    return g > 0.0f ? 20.0f * std::log10(g) : -INFINITY;
}

// --- Fades (audio thread) -----------------------------------------------------------
// Gain smoothing: a one-pole with this time constant, per sample.
constexpr double kInputGainSmoothingSeconds = 0.010;
// Start, stop and file-change fades: linear, this many samples from 0 to 1 (10 ms).
constexpr int kInputFadeFrames = 480;
// The loop point: a linear crossfade of this many samples (10 ms), or half the
// file if it is shorter than twice that.
constexpr int kInputLoopCrossfadeFrames = 480;
// M2b-A2: a new file while another one sounds: an equal-power crossfade of this
// many samples (100 ms) between the two.
constexpr int kInputFileCrossfadeFrames = 4800;

// --- Play modes (M2b-A2) -------------------------------------------------------------
enum class InputPlayMode : std::uint8_t
{
    Loop, // loops with the loop-point crossfade (the default)
    Once, // plays from the start to the end, fades out over its last samples, stops
};

constexpr const char* inputPlayModeName(InputPlayMode m) noexcept
{
    return m == InputPlayMode::Once ? "ONCE" : "LOOP";
}

// --- Browsing (M2b-A2, M2b-A3) ---------------------------------------------------------
// Up/down held in the pop-up list (X held) past tuning::kHoldDelaySeconds moves
// the cursor this many files per second.
constexpr double kInputCursorRepeatHz = 8.0;
// The pop-up file list (M2b-A3) shows this many names, a window around the cursor.
constexpr int kInputListRows = 8;

// --- Loading ----------------------------------------------------------------------------
enum class InputLoadState : std::uint8_t
{
    Idle,    // nothing requested yet (or the source was never FILE)
    Loading, // a load is running or queued
    Ready,   // the shown file is loaded (and plays while the source is FILE)
    Failed,  // the last load failed; the previous state is untouched
    NoFiles, // FILE selected, but the folder has no WAV files (or is missing)
};

constexpr const char* inputLoadStateName(InputLoadState s) noexcept
{
    switch(s)
    {
        case InputLoadState::Idle: return "idle";
        case InputLoadState::Loading: return "loading";
        case InputLoadState::Ready: return "ready";
        case InputLoadState::Failed: return "failed";
        case InputLoadState::NoFiles: return "no files";
    }
    return "?";
}

} // namespace audreybrick
