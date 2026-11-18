// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Every tunable number of the button control scheme, in one place so it can be
// adjusted in M4 (docs/briefs/M2.md, "Design shared by all three tasks", and
// docs/CONTROL_SCHEME_PROPOSAL.md section 0).
//
// All values are in normalised knob travel (0..1) unless stated otherwise.

#pragma once

#include <cstddef>

#include "core/ControlSource.h"

namespace audreybrick
{
namespace tuning
{

// The control rate: one ButtonControlSource::read() per 4-sample block at 48 kHz.
constexpr double kBlocksPerSecond = 12000.0;

// --- Taps (one Step command per press) -------------------------------------
constexpr double kTapStep         = 1.0 / 128.0;  // ordinary knob
constexpr double kTapStepFine     = 1.0 / 1024.0;
constexpr double kFreqTapStep     = 1.0 / 56.0;   // Frequency: one semitone (56 notes of travel)
constexpr double kFreqTapStepFine = 1.0 / 560.0;  // 10 cents
constexpr double kFbTapStep       = 0.5 / 72.0;   // FeedbackGain: 0.5 dB of the 72 dB range
constexpr double kFbTapStepFine   = 0.1 / 72.0;   // 0.1 dB

// --- Press-to-hold ------------------------------------------------------------
// A d-pad direction or L2/R2 still held this long after its press starts ramping.
// Decided on the UI side (ControlModel::tick), in seconds.
constexpr double kHoldDelaySeconds = 0.25;
// Tolerance for comparing caller-supplied times against the hold delay, so a
// tick computed as n / rate that lands a rounding error short of 0.25 s counts.
constexpr double kTimeEpsilon = 1e-9;

// --- D-pad ramp (integrated on the audio side) --------------------------------
// Speed when the hold begins, rising linearly to the end speed over
// kRampAccelSeconds, then constant. Travel per second.
constexpr double kRampStartRate    = 0.1;
constexpr double kRampEndRate      = 0.4;
constexpr double kRampAccelSeconds = 1.0;
// Fine (Select held) scales both speeds.
constexpr double kFineRampScale = 1.0 / 8.0;

// --- Feedback ramp (L2/R2 held; constant rate) --------------------------------
constexpr double kFbRampRate     = 24.0 / 72.0; // 24 dB per second
constexpr double kFbRampRateFine = 4.0 / 72.0;  // 4 dB per second

// --- Command transport ---------------------------------------------------------
// Commands drained per read() at most (bounded work per control block).
constexpr int kMaxCommandsPerBlock = 8;
// Default queue capacity (commands). A tap produces one command; the UI produces a
// few per 60 Hz frame at most.
constexpr std::size_t kCommandQueueCapacity = 256;

// Tap size for a knob, in normalised travel (always positive).
constexpr double tapStep(Knob knob, bool fine) noexcept
{
    switch(knob)
    {
        case Knob::Frequency: return fine ? kFreqTapStepFine : kFreqTapStep;
        case Knob::FeedbackGain: return fine ? kFbTapStepFine : kFbTapStep;
        default: return fine ? kTapStepFine : kTapStep;
    }
}

} // namespace tuning
} // namespace audreybrick
