// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Shared helpers for the M2-C screen tests: a PASS/FAIL checker and the named
// snapshots the layout and screenshot tests use. Engine values are computed with
// ParamTable exactly as App::fillControlSnapshot does.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app/Buttons.h"
#include "app/Tabs.h"
#include "app/UiSnapshot.h"
#include "core/ControlSource.h"
#include "core/ParamTable.h"
#include "ui/DrawList.h"
#include "ui/Layout.h"
#include "ui/MeterBallistics.h"

namespace uitest
{
using namespace audreybrick;

inline int g_failures = 0;

inline void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if(!ok)
        ++g_failures;
}

inline int finish()
{
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

inline void setButton(UiSnapshot& s, Button b, bool down)
{
    s.buttonDown[static_cast<std::size_t>(b)] = down;
}

inline void setKnob(UiSnapshot& s, Knob k, float norm)
{
    UiKnobView& v = s.knob[static_cast<std::size_t>(k)];
    v.norm        = norm;
    v.engine      = knobToEngine(k, norm, s.switchOn);
    v.unit        = paramSpec(k).unit;
}

// Stored feedback; the frame value is 1.0 while maximum is active (as in App).
inline void setFeedback(UiSnapshot& s, float storedNorm, bool max)
{
    s.storedFeedback       = storedNorm;
    s.storedFeedbackEngine = knobToEngine(Knob::FeedbackGain, storedNorm, false);
    s.feedbackMax          = max;
    setKnob(s, Knob::FeedbackGain, max ? 1.0f : storedNorm);
}

// Recomputes every engine value (after changing the switch).
inline void refreshEngine(UiSnapshot& s)
{
    for(std::size_t k = 0; k < kKnobCount; ++k)
        if(static_cast<Knob>(k) != Knob::FeedbackGain)
            setKnob(s, static_cast<Knob>(k), s.knob[k].norm);
}

inline void setAudio(UiSnapshot& s)
{
    s.audio.opened       = true;
    s.audio.sampleRate   = 48000;
    s.audio.bufferFrames = 256;
    s.audio.channels     = 2;
    std::snprintf(s.audio.driver, sizeof s.audio.driver, "coreaudio");
    std::snprintf(s.audio.device, sizeof s.audio.device, "default");
}

// Start-up: every knob at its default, switch off, tab LOOP, silence.
inline UiSnapshot startupSnapshot()
{
    UiSnapshot s;
    for(std::size_t k = 0; k < kKnobCount; ++k)
        setKnob(s, static_cast<Knob>(k), paramSpec(static_cast<Knob>(k)).defaultNorm);
    setFeedback(s, paramSpec(Knob::FeedbackGain).defaultNorm, false);
    setAudio(s);
    s.timeSeconds         = 0.0;
    s.frameIndex          = 1;
    s.controllerConnected = false;
    return s;
}

// dB to the normalised feedback position (-60..+12 dB, linear).
inline float feedbackNorm(float db)
{
    return (db + 60.0f) / 72.0f;
}

// A drone going: feedback -10 dB, peaks 0.2 to 0.3, CPU a few percent.
inline UiSnapshot playingSnapshot(Tab tab)
{
    UiSnapshot s = startupSnapshot();
    s.tab        = tab;
    setKnob(s, Knob::Frequency, (43.3f - 16.0f) / 56.0f); // G2 +30 cents
    setKnob(s, Knob::FeedbackBody, 0.35f);
    setKnob(s, Knob::FeedbackLPFCutoff, 0.72f);
    setKnob(s, Knob::FeedbackHPFCutoff, 0.31f);
    setKnob(s, Knob::ReverbMix, 0.42f);
    setKnob(s, Knob::ReverbDecay, 0.5f);
    setKnob(s, Knob::EchoDelaySend, 0.55f);
    setKnob(s, Knob::EchoDelayTime, 0.45f);
    setKnob(s, Knob::EchoDelayFeedback, 0.6f);
    setKnob(s, Knob::OutputVolume, 0.70710678f);
    setFeedback(s, feedbackNorm(-10.0f), false);
    s.peakL               = 0.22f;
    s.peakR               = 0.28f;
    s.cpuLoad             = 0.034f;
    s.cpuMax              = 0.052f;
    s.timeSeconds         = 42.0;
    s.frameIndex          = 2520;
    s.controllerConnected = true;
    std::snprintf(s.controllerName, sizeof s.controllerName, "Xbox Wireless Controller");
    return s;
}

// DELAY with X held and the switch latched (on).
inline UiSnapshot delayXSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Delay);
    s.xHeld      = true;
    setButton(s, Button::X, true);
    s.switchLatch = true;
    s.switchHeld  = false;
    s.switchOn    = true;
    refreshEngine(s);
    return s;
}

// Everything at once: maximum held over a different stored value, Select held,
// peaks at 0.95 and 1.0, CPU 85 % (maximum 100 %), dropped commands, no controller.
inline UiSnapshot stressSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Reverb);
    setFeedback(s, feedbackNorm(-23.5f), true);
    setButton(s, Button::A, true);
    s.fine = true;
    setButton(s, Button::Select, true);
    setButton(s, Button::DpadRight, true);
    s.peakL               = 0.95f;
    s.peakR               = 1.0f;
    s.cpuLoad             = 0.85f;
    s.cpuMax              = 1.0f;
    s.commandsDropped     = 7;
    s.controllerConnected = false;
    s.controllerName[0]   = '\0';
    return s;
}

// DELAY with B held: the switch on by the momentary hold (TIME carries the B mark).
inline UiSnapshot delaySwitchHeldSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Delay);
    setButton(s, Button::B, true);
    s.switchHeld  = true;
    s.switchLatch = false;
    s.switchOn    = true;
    refreshEngine(s);
    return s;
}

// Select + B: the latch press (B is down but does not hold the switch).
inline UiSnapshot latchPressSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Loop);
    s.fine       = true;
    setButton(s, Button::Select, true);
    setButton(s, Button::B, true);
    s.switchHeld  = false;
    s.switchLatch = true;
    s.switchOn    = true;
    refreshEngine(s);
    return s;
}

// The status line at its limits: the longest driver and controller names the
// snapshot can hold and a huge dropped-command count.
inline UiSnapshot longStatusSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Filter);
    std::snprintf(s.audio.driver, sizeof s.audio.driver, "%s", "a-very-long-audio-driver-name-x");
    std::snprintf(s.controllerName, sizeof s.controllerName, "%s",
                  "An Extremely Long Game Controller Name That Goes On And On Ok.");
    s.commandsDropped = 123456789;
    return s;
}

struct NamedSnapshot
{
    const char* name;
    UiSnapshot  snap;
};

// M3-C: Select and Start held (KNULLI's screenshot combination; Select is FINE,
// Start does nothing): the normal screen.
inline UiSnapshot selectStartSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Filter);
    s.fine       = true;
    setButton(s, Button::Select, true);
    setButton(s, Button::Start, true);
    return s;
}

// M3-C: the status line with the device's real strings (as logged on the Brick).
inline UiSnapshot deviceStatusSnapshot()
{
    UiSnapshot s = playingSnapshot(Tab::Loop);
    std::snprintf(s.audio.driver, sizeof s.audio.driver, "%s", "alsa");
    std::snprintf(s.controllerName, sizeof s.controllerName, "%s", "TRIMUI Brick Controller");
    return s;
}

inline std::vector<NamedSnapshot> allSnapshots()
{
    return {
        {"tab_loop", playingSnapshot(Tab::Loop)},
        {"tab_filter", playingSnapshot(Tab::Filter)},
        {"tab_reverb", playingSnapshot(Tab::Reverb)},
        {"tab_delay", playingSnapshot(Tab::Delay)},
        {"tab_out", playingSnapshot(Tab::Out)},
        {"delay_x_latched", delayXSnapshot()},
        {"stress", stressSnapshot()},
        {"startup", startupSnapshot()},
        {"delay_switch_held", delaySwitchHeldSnapshot()},
        {"long_status", longStatusSnapshot()},
        {"select_start", selectStartSnapshot()},
        {"device_status", deviceStatusSnapshot()},
    };
}

// The meter view the renderer would show on its first frame for this snapshot.
inline ui::MeterView firstFrameMeters(const UiSnapshot& s)
{
    ui::PeakMeter   m[2];
    ui::MeterView   v;
    const float     p[2] = {s.peakL, s.peakR};
    for(int ch = 0; ch < 2; ++ch)
    {
        m[ch].update(p[ch], s.timeSeconds);
        v.level[ch] = m[ch].level();
        v.hold[ch]  = m[ch].hold();
        v.clip[ch]  = m[ch].clipped();
    }
    return v;
}

inline bool hasText(const ui::DrawList& d, const char* s)
{
    for(std::size_t i = 0; i < d.textCount; ++i)
        if(std::strcmp(d.texts[i].text, s) == 0)
            return true;
    return false;
}

} // namespace uitest
