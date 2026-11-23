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

// The sources. M2b-B added LIVE (a capture device: the internal mic, a USB
// interface, whatever SDL lists); the order here is the order on the screen
// (left/right on the INPUT tab, no wrap).
enum class InputSource : std::uint8_t
{
    Off,
    File,
    Live,
    Count
};

constexpr int kInputSourceCount = static_cast<int>(InputSource::Count);

constexpr const char* inputSourceName(InputSource s) noexcept
{
    switch(s)
    {
        case InputSource::Off: return "OFF";
        case InputSource::File: return "FILE";
        case InputSource::Live: return "LIVE";
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

// The inverse of inputGainFromNorm (MOD-A): the knob position 0..1 whose gain is
// g (clamped to 0..kInputGainMax; NaN -> 0).
inline float inputNormFromGain(float g) noexcept
{
    if(!(g > 0.0f))
        return 0.0f;
    if(g >= kInputGainMax)
        return 1.0f;
    return clampInputNorm(0.5f * std::sqrt(g)); // gain = (2n)^2  =>  n = sqrt(gain) / 2
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

// --- Live input (M2b-B) ------------------------------------------------------------------
// The capture ring (LiveInput): mono float at 48 kHz, this many samples (a power
// of two, about 1.37 s).
constexpr std::size_t kLiveRingFrames = 65536;
// The stage starts reading (and re-starts after an underrun) once the ring holds
// the target: this many output buffers' worth by default (the launch option
// --live-target-buffers sets 1 to 32; InputStage::setLiveTargetBuffers()).
constexpr std::size_t kLiveTargetBuffers    = 2;
constexpr std::size_t kLiveMinTargetBuffers = 1;
constexpr std::size_t kLiveMaxTargetBuffers = 32;
// Two real underruns within one second double the target, up to this many
// buffers (never above a configured target; it never lowers by itself; a new
// session, i.e. opening or closing a device, resets it to the configured one).
constexpr std::size_t kLiveMaxAutoBuffers = 16;
// Above the target plus this many output buffers (or twice the target, if more)
// the stage skips ahead to the target (an overfill: the capture side runs fast,
// or the output stalled).
constexpr std::size_t kLiveOverfillMarginBuffers = 4;
// Slow capture: when the fill sinks below its settled minimum (the smallest
// fill over kLiveBaselineFrames after a start or a skip) by half a repeat, the
// stage repeats the last min(output buffer, kLiveMaxRepeatFrames) samples with a
// crossfade, before the ring runs dry.
constexpr std::size_t kLiveMaxRepeatFrames = 1024;
constexpr std::size_t kLiveBaselineFrames  = 12000; // 0.25 s
// The target when the output buffer size is not known (prepare() without it).
constexpr std::size_t kLiveDefaultTargetFrames = 512;
// The skip's linear crossfade between the old and the new read position: this
// many samples (10 ms), or the target if that is smaller.
constexpr std::size_t kLiveSkipCrossfadeFrames = 480;
// GAIN (2026-11-20): the fade-in of a live session whose open restored a
// remembered gain above zero (InputStage::setLiveStartFadeFrames()): linear, this
// many samples (100 ms) instead of kInputFadeFrames. Only the session's first start.
constexpr std::size_t kLiveRestoreFadeFrames = 4800;
// GAIN: the input gain is remembered per capture device for the running session,
// for at most this many device names (the least recently opened is forgotten).
constexpr std::size_t kLiveGainMemorySlots = 8;
// The UI treats an open device as lost when it has delivered nothing for this long.
constexpr double kLiveStallSeconds = 3.0;
// Capture samples are clamped to +/- this after the mono mix, as file samples are.
constexpr float kLiveSampleLimit = kInputSampleLimit;
// The capture devices listed (SYSTEM DEFAULT plus at most this many minus one names).
constexpr std::size_t kLiveMaxDevices = 32;

// What the INPUT tab says about the live device besides its name.
enum class LiveStatus : std::uint8_t
{
    None,   // nothing to report (open, or never chosen)
    Failed, // the last open failed (CANNOT OPEN <name>: <reason>)
    Lost,   // the open device went away (unplugged, stopped delivering): DEVICE LOST
};

constexpr const char* liveStatusName(LiveStatus s) noexcept
{
    switch(s)
    {
        case LiveStatus::None: return "none";
        case LiveStatus::Failed: return "failed";
        case LiveStatus::Lost: return "lost";
    }
    return "?";
}

} // namespace audreybrick
