// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// UiSnapshot: everything the screen shows, filled by App once per UI frame and
// handed to render(const UiSnapshot&) (docs/briefs/M2.md, M2-B and M2-C).
//
// Plain data: fixed-size, no allocation to copy or fill, no SDL types. Strings are
// fixed char arrays (always NUL-terminated) or pointers to static strings from
// ParamTable. Tab names and bindings come from the tab table: tabSpec(tab),
// tabBinding(), kTabCount in app/Tabs.h. Parameter names and formatting data come
// from paramSpec(knob) in core/ParamTable.h.
//
// Values from the audio side are independent relaxed atomics read one after the
// other (see ButtonControlSource.h): fields may come from adjacent control blocks.

#pragma once

#include <cstddef>
#include <cstdint>

#include "app/Buttons.h"
#include "app/Tabs.h"
#include "core/ControlSource.h"
#include "input/InputView.h"
#include "record/RecordView.h"

namespace audreybrick
{

struct UiKnobView
{
    // Normalised knob position 0..1 as it went into the last control frame
    // (ButtonControlSource::publishedKnob). FeedbackGain reads 1.0 while feedback
    // maximum is active; the stored feedback is in UiSnapshot::storedFeedback.
    float norm = 0.0f;
    // The engine value that position maps to (ParamTable knobToEngine), in `unit`.
    // EchoDelayTime includes the delay switch's halving when the switch is on.
    float engine = 0.0f;
    // ParamSpec::unit: "note", "dB", "s", "Hz" or "" (static string, never null).
    const char* unit = "";
};

struct UiAudioSpec
{
    bool opened       = false; // false until the device is open
    int  sampleRate   = 0;     // obtained (always 48000 when opened)
    int  bufferFrames = 0;     // obtained buffer size in frames
    int  channels     = 0;     // obtained (always 2 when opened)
    char driver[32]   = {};    // SDL audio driver, e.g. "coreaudio", "pipewire", "alsa", "dummy"
    char device[128]  = {};    // always "default": SDL's default output device (its name is not queried)
};

struct UiSnapshot
{
    // --- Time ----------------------------------------------------------------
    double        timeSeconds = 0.0; // UI clock (monotonic, seconds since start)
    std::uint64_t frameIndex  = 0;   // UI frames since start

    // --- Tabs and modifiers ------------------------------------------------------
    Tab  tab   = Tab::Loop; // current tab; see tabSpec(tab), tabBinding() and kTabCount
    bool fine  = false;     // Select held (FINE)
    bool xHeld = false;     // X held (shift to the X bindings)
    bool yHeld = false;     // Y held (reset modifier)
    // Every button as ControlModel sees it (true = down), indexed by Button.
    bool buttonDown[kButtonCount] = {};

    // --- Knobs -------------------------------------------------------------------
    UiKnobView knob[kKnobCount]; // indexed by Knob

    // --- Feedback ------------------------------------------------------------------
    float storedFeedback       = 0.0f;  // normalised; what L2/R2 move (maximum does not change it)
    float storedFeedbackEngine = 0.0f;  // the same in dB
    bool  feedbackMax          = false; // feedback maximum active in the last frame (A held)

    // --- Delay switch ----------------------------------------------------------------
    bool switchOn    = false; // effective state in the last frame (latch XOR held)
    bool switchLatch = false; // latched by Select + B
    bool switchHeld  = false; // B held (momentary)

    // --- Output and CPU meters -------------------------------------------------------
    // Peaks: the largest |sample| per channel since the previous snapshot (0 when
    // nothing was rendered in between). The output is limited by the original's
    // limiter, so 1.0 is the clip mark.
    float peakL = 0.0f;
    float peakR = 0.0f;
    // Callback load: time in the callback / buffer duration (1.0 = 100 %).
    float cpuLoad = 0.0f; // smoothed (time constant CpuMeter::kSmoothingSeconds)
    float cpuMax  = 0.0f; // maximum over the last completed one-second window

    // --- Audio device -----------------------------------------------------------------
    UiAudioSpec audio;
    bool        flushToZero          = false; // FZ set on the audio thread (known after the first callback)
    bool        flushToZeroKnown     = false; // false until the first callback has run
    bool        flushToZeroSupported = false; // false on non-aarch64 builds (no-op there)

    // --- Diagnostics ------------------------------------------------------------------
    std::uint64_t commandsDropped = 0; // UI commands lost because the queue was full

    // --- Controller -------------------------------------------------------------------
    bool controllerConnected = false;
    char controllerName[64]  = {}; // SDL GameController name, empty if none

    // --- Input (M2b-A) ------------------------------------------------------------------
    // The INPUT tab's state and the input level (input/InputView.h). input.peak is
    // the largest |in| after gain since the previous snapshot; the screen shows it
    // on the INPUT tab and, whenever the source is not OFF, as the IN bar in the
    // meter panel on every tab.
    UiInputView input;

    // --- Recording (REC) -------------------------------------------------------------------
    // The red border and the header's REC readout (record/RecordView.h).
    UiRecordView record;
};

} // namespace audreybrick
