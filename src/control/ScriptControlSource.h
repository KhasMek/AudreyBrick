// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// ScriptControlSource: a ControlSource driven by a preloaded, timed parameter
// script, for tests and offline renders. Deterministic: it advances one control
// block per read(), independent of wall-clock time.
//
// Script format, one event per line:
//
//   <time_seconds> <knob-name|switch> <value> [ramp_seconds]
//
//   - '#' starts a comment that runs to the end of the line. Blank lines are ignored.
//   - knob-name is a ParamTable id (the firmware enum name, e.g. FeedbackGain),
//     matched case-insensitively. "switch" is the delay switch.
//   - A knob value is a normalised knob position in 0..1 (the firmware's 1 - adc).
//     Values outside 0..1 are rejected at load time.
//   - A switch value is 0 (off) or 1 (on). A switch event takes no ramp.
//   - Without ramp_seconds, or with 0, the knob jumps to the value. Otherwise it
//     moves linearly from the value it has at the event's block to the new value
//     over ramp_seconds, like a hand turning a knob. A later event on the same
//     knob replaces a ramp still in progress, starting from the knob's value at
//     that block.
//   - Times are quantised to the control block (4 samples at 48 kHz, 12000 blocks
//     per second): an event takes effect at the first block that starts at or after
//     its time. Ramp lengths are rounded to the nearest whole block. Events at the
//     same block apply in file order.
//   - Numbers are plain decimals: [+-]digits[.digits][(e|E)[+-]digits], for
//     example 0.5, 1, -0, 2.5e-1. Not accepted: ".5", "5.", "1,5", hex, inf,
//     nan. Parsing ignores the C and C++ locale (a '.' is always the decimal
//     point, even after setlocale()).
//
// Before any event, every knob sits at its ParamTable default position and the
// switch is off.
//
// load*() allocate and must be called before audio starts. read() does not
// allocate, lock, log, do I/O or wait.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/AudreyVoice.h"
#include "core/ControlSource.h"

namespace audreybrick
{

class ScriptControlSource final : public ControlSource
{
  public:
    static constexpr double kBlocksPerSecond =
        static_cast<double>(AudreyVoice::kSampleRate) / AudreyVoice::kControlBlock; // 12000

    struct Event
    {
        int           line;        // 1-based script line
        double        timeSeconds;
        std::int64_t  startBlock;  // first control block the event applies to
        bool          isSwitch;
        Knob          knob;        // valid when !isSwitch
        float         value;       // knob position 0..1, or 0/1 for the switch
        double        rampSeconds;
        std::int64_t  rampBlocks;  // 0 = jump
    };

    ScriptControlSource();

    // Parse a script. On success, replaces the current script, rewinds and returns
    // true. On failure, leaves the current script unchanged, sets `error` to a
    // message with the line number, and returns false.
    bool loadString(const std::string& text, std::string& error);
    bool loadFile(const std::string& path, std::string& error);

    // Back to block 0 and the initial state. Does not allocate.
    void rewind() noexcept;

    void read(ControlFrame& out) noexcept override;

    // The parsed events, sorted by startBlock (stable, so file order within a block).
    const std::vector<Event>& events() const noexcept { return events_; }

    // Number of read() calls since the last load or rewind.
    std::int64_t blockIndex() const noexcept { return block_; }

  private:
    struct Ramp
    {
        bool         active = false;
        std::int64_t start  = 0;
        std::int64_t length = 0;
        float        from   = 0.0f;
        float        to     = 0.0f;
    };

    std::vector<Event> events_;
    std::size_t        next_  = 0;
    std::int64_t       block_ = 0;
    float              value_[kKnobCount];
    Ramp               ramp_[kKnobCount];
    bool               switch_ = false;
};

} // namespace audreybrick
