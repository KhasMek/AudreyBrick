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
// switch is off (PRESET-A: unless setStartState() gave another start).
//
// Modulation lines (MOD-A, docs/briefs/MOD.md), one setting of one slot each:
//
//   <time_seconds> mod<1-4> shape  <sine|triangle|square|rampup|rampdown|expup|
//                                   expdown|samplehold|envelope>
//   <time_seconds> mod<1-4> target <none|knob-name|InputGain|DelaySwitch>
//   <time_seconds> mod<1-4> rate   <0..1>          (rateNorm; for envelope: release)
//   <time_seconds> mod<1-4> hz     <0.01..2000>    (the same control, given in Hz)
//   <time_seconds> mod<1-4> depth  <-1..1>
//   <time_seconds> mod<1-4> polarity <bipolar|unipolar>   (MOD-C)
//
//   Names are case-insensitive. No ramp. Times are quantised like knob events.
//   They are kept apart from the knob and switch events (modEvents(), not
//   events()), and read() hands each one, at its block and before the frame
//   is written, to the sink given to setModSink() (ModControlSource), or
//   ignores it without a sink. A script without mod lines behaves exactly as
//   before.
//
// load*() allocate and must be called before audio starts. read() does not
// allocate, lock, log, do I/O or wait.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/AudreyVoice.h"
#include "core/ControlSource.h"
#include "mod/ModTypes.h"

namespace audreybrick
{

// MOD-A: one modulation line of a script.
enum class ScriptModField : std::uint8_t
{
    Shape,
    Target,
    Rate,  // rateNorm 0..1
    Hz,    // the rate in Hz (the sink converts with modRateNormFromHz)
    Depth,
    Polarity, // MOD-C
};

struct ScriptModEvent
{
    int            line;        // 1-based script line
    double         timeSeconds;
    std::int64_t   startBlock;  // control block the event applies at
    std::uint8_t   slot;        // 0..kModSlots-1 (mod1 is slot 0)
    ScriptModField field;
    ModShape       shape;       // Shape
    ModTarget      target;      // Target
    ModPolarity    polarity;    // Polarity (MOD-C)
    float          value;       // Rate, Hz, Depth
};

// Receives the modulation events on the audio thread, from inside read().
class ScriptModSink
{
  public:
    virtual void onScriptModEvent(const ScriptModEvent& ev) noexcept = 0;

  protected:
    ~ScriptModSink() = default;
};

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

    // PRESET-A: the initial state rewind() (and a load) returns to: every knob's
    // position and the switch before any event. Without a call: the ParamTable
    // default positions and the switch off. Values are clamped to 0..1 (NaN ->
    // 0). Rewinds. A ramp starts from the value the knob has at its event, so a
    // ramp on a knob the start state set starts from that value. For
    // audrey-render --preset.
    void setStartState(const float (&knobs)[kKnobCount], bool sw) noexcept;

    void read(ControlFrame& out) noexcept override;

    // The parsed events, sorted by startBlock (stable, so file order within a block).
    const std::vector<Event>& events() const noexcept { return events_; }
    // MOD-A: the parsed modulation lines, sorted the same way.
    const std::vector<ScriptModEvent>& modEvents() const noexcept { return modEvents_; }
    // MOD-A: where read() delivers the modulation lines (nullptr: dropped).
    // Call before audio starts.
    void setModSink(ScriptModSink* sink) noexcept { modSink_ = sink; }

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
    std::vector<ScriptModEvent> modEvents_;
    ScriptModSink*     modSink_ = nullptr;
    std::size_t        nextMod_ = 0;
    std::size_t        next_  = 0;
    std::int64_t       block_ = 0;
    float              value_[kKnobCount];
    Ramp               ramp_[kKnobCount];
    bool               switch_ = false;
    float              start_[kKnobCount];   // PRESET-A: the initial state (setStartState)
    bool               startSwitch_ = false;
};

} // namespace audreybrick
