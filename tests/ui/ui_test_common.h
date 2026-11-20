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
#include "input/InputTypes.h"
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

// --- M2b-A: the INPUT tab and the IN indicator ---------------------------------------
inline void setInputName(UiSnapshot& s, const char* name)
{
    std::snprintf(s.input.fileName, sizeof s.input.fileName, "%s", name);
}

inline void setInputGain(UiSnapshot& s, float norm)
{
    s.input.gainNorm = norm;
    s.input.gainDb   = inputGainDb(norm);
}

// M2b-A2: the browser window as InputController::fill() makes it: `names` are the
// whole list (only the first kInputListRows around the cursor are copied).
inline void setInputList(UiSnapshot& s, const std::vector<std::string>& names, int cursor, int loadedIndex)
{
    UiInputView& v  = s.input;
    const int    n  = static_cast<int>(names.size());
    const int    rows = n < kInputListRows ? n : kInputListRows;
    int          first = cursor - kInputListRows / 2;
    if(first > n - rows)
        first = n - rows;
    if(first < 0)
        first = 0;
    v.fileCount   = n;
    v.cursor      = n > 0 ? cursor : -1;
    v.listFirst   = first;
    v.listRows    = rows;
    v.loadedIndex = loadedIndex;
    for(int i = 0; i < kInputListRows; ++i)
        std::snprintf(v.listNames[i], sizeof v.listNames[i], "%s",
                      i < rows ? names[static_cast<std::size_t>(first + i)].c_str() : "");
}

inline std::vector<std::string> fourFiles()
{
    return {"cello_drone.wav", "field_hum.wav", "notes.wav", "rain_on_roof.wav"};
}

// INPUT, source OFF, unity gain, four files listed, nothing loaded.
inline UiSnapshot inputOffSnapshot()
{
    UiSnapshot s     = playingSnapshot(Tab::Input);
    s.input.source   = InputSource::Off;
    setInputGain(s, kInputGainUnityNorm);
    s.input.fileCount = 4;
    s.input.fileIndex = -1;
    s.input.state     = InputLoadState::Idle;
    setInputList(s, fourFiles(), 0, -1);
    return s;
}

// INPUT, FILE playing: the second of four files, 1:23.4 long, gain +4.1 dB, input at -10 dB.
inline UiSnapshot inputPlayingSnapshot()
{
    UiSnapshot s     = inputOffSnapshot();
    s.input.source   = InputSource::File;
    setInputGain(s, 0.62f);
    setInputName(s, "cello_drone.wav");
    std::snprintf(s.input.playingName, sizeof s.input.playingName, "%s", "cello_drone.wav");
    s.input.fileIndex     = 1;
    s.input.loaded        = true;
    s.input.shownIsLoaded = true;
    s.input.loadedSeconds = 83.4f;
    s.input.state         = InputLoadState::Ready;
    s.input.peak          = 0.316f;
    setInputList(s, fourFiles(), 0, 0);
    s.input.positionSeconds = 37.2f;
    return s;
}

// FILE playing with X held and d-pad right down (the next file is asked for).
inline UiSnapshot inputXHeldSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    s.xHeld      = true;
    setButton(s, Button::X, true);
    setButton(s, Button::DpadRight, true);
    setInputList(s, fourFiles(), 1, 0); // M2b-A3: X + right stepped the cursor on, the first file plays
    s.input.choosing = true;
    return s;
}

// FILE, the third file loading while the second still plays.
inline UiSnapshot inputLoadingSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    setInputName(s, "rain_on_roof.wav");
    s.input.fileIndex     = 3;
    s.input.shownIsLoaded = false;
    s.input.state         = InputLoadState::Loading;
    setInputList(s, fourFiles(), 3, 0);
    return s;
}

// FILE, the fourth file failed (not a WAV file); the gain at 0 ("-inf dB").
inline UiSnapshot inputErrorSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    setInputName(s, "notes.wav");
    s.input.fileIndex     = 3;
    s.input.shownIsLoaded = false;
    s.input.state         = InputLoadState::Failed;
    std::snprintf(s.input.reason, sizeof s.input.reason, "%s", "NOT A WAV FILE");
    setInputGain(s, 0.0f);
    s.input.peak = 0.0f;
    s.input.fileIndex = 2;
    setInputList(s, fourFiles(), 2, 0);
    return s;
}

// FILE selected, but the folder has no WAV files.
inline UiSnapshot inputNoFilesSnapshot()
{
    UiSnapshot s     = inputOffSnapshot();
    s.input.source   = InputSource::File;
    s.input.fileCount = 0;
    s.input.fileIndex = -1;
    s.input.state     = InputLoadState::NoFiles;
    setInputList(s, {}, -1, -1);
    return s;
}

// FILE with the longest name the view holds, cut at ten minutes, 256 files
// listed of more, the gain at +12 dB; long names in the list too.
inline UiSnapshot inputLongNameSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    setInputName(s, "an_extremely_long_field_recording_name_from_the_harbour_at_night_with_ships_and_gulls.wav");
    s.input.fileCount     = 256;
    s.input.fileIndex     = 199;
    s.input.fileListCut   = true;
    s.input.loadedSeconds = 600.0f;
    s.input.loadedCut     = true;
    setInputGain(s, 1.0f);
    std::vector<std::string> names;
    for(int i = 0; i < 256; ++i)
        names.push_back(i == 199 ? std::string(s.input.fileName)
                                 : "recording_" + std::to_string(1000 + i) + "_with_a_rather_long_descriptive_name.wav");
    setInputList(s, names, 199, 199);
    std::snprintf(s.input.playingName, sizeof s.input.playingName, "%s", s.input.fileName);
    s.input.positionSeconds = 599.9f;
    return s;
}

// FILE playing hot: the input clips (peak 1.4 after +12 dB of gain).
inline UiSnapshot inputClipSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    setInputGain(s, 1.0f);
    s.input.peak = 1.4f;
    return s;
}

// FILE, a load failed with nothing loaded before it.
inline UiSnapshot inputErrorNothingSnapshot()
{
    UiSnapshot s = inputErrorSnapshot();
    setInputName(s, "broken_recording.wav");
    s.input.loaded         = false;
    s.input.playingName[0] = '\0';
    setInputList(s, fourFiles(), 2, -1);
    std::snprintf(s.input.reason, sizeof s.input.reason, "%s", "UNSUPPORTED FORMAT");
    setInputGain(s, 0.5f);
    return s;
}

// FILE, the folder cannot be read (unreadable, or the scan failed); the file
// loaded before keeps playing.
inline UiSnapshot inputReadErrorSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    s.input.fileName[0]   = '\0';
    s.input.fileIndex     = -1;
    s.input.fileCount     = 0;
    s.input.shownIsLoaded = false;
    s.input.folderMissing = true;
    std::snprintf(s.input.folderError, sizeof s.input.folderError, "%s", "CANNOT READ FOLDER");
    s.input.state = InputLoadState::NoFiles;
    setInputList(s, {}, -1, -1);
    return s;
}

// FILE, the source OFF meanwhile, a UTF-8 name being loaded while another (also
// UTF-8) is loaded: raw bytes in the view, shown as "?".
inline UiSnapshot inputUtf8Snapshot()
{
    UiSnapshot s = inputLoadingSnapshot();
    s.input.source = InputSource::Off;
    setInputName(s, "caf\xc3\xa9_\xe2\x99\xab_\xf0\x9f\x8e\xb5_night.wav");
    std::snprintf(s.input.playingName, sizeof s.input.playingName, "%s", "\xc3\xbc\xc3\xb6\xc3\xa4_drone.wav");
    return s;
}

// LOOP with FILE playing: the IN indicator in the meter panel.
inline UiSnapshot inIndicatorSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    s.tab        = Tab::Loop;
    return s;
}

// DELAY with the input clipping: the IN indicator's label in inverse video.
inline UiSnapshot inIndicatorClipSnapshot()
{
    UiSnapshot s = inputClipSnapshot();
    s.tab        = Tab::Delay;
    return s;
}

// M2b-A2: ONCE, played to its end.
inline UiSnapshot inputOnceEndedSnapshot()
{
    UiSnapshot s           = inputPlayingSnapshot();
    s.input.playMode       = InputPlayMode::Once;
    s.input.ended          = true;
    s.input.positionSeconds = 83.4f;
    s.input.peak           = 0.0f;
    return s;
}

// M2b-A2/A3: twelve files, the cursor on the sixth (the window scrolled), the
// twelfth playing (out of the window).
inline UiSnapshot inputTwelveFilesSnapshot()
{
    UiSnapshot               s = inputPlayingSnapshot();
    std::vector<std::string> names;
    for(int i = 1; i <= 12; ++i)
        names.push_back("take_" + std::string(i < 10 ? "0" : "") + std::to_string(i) + (i == 7 ? "_caf\xc3\xa9" : "") +
                        ".wav");
    setInputList(s, names, 5, 11); // the window shows files 2..9: the playing take_12 is out of it
    setInputName(s, "take_12.wav");
    std::snprintf(s.input.playingName, sizeof s.input.playingName, "%s", "take_12.wav");
    s.input.fileIndex       = 11;
    s.input.loadedSeconds   = 245.0f;
    s.input.positionSeconds = 83.0f;
    return s;
}

// M2b-A2: FILE selected, files listed, nothing loaded yet (X + RIGHT: LOAD).
inline UiSnapshot inputNothingLoadedSnapshot()
{
    UiSnapshot s     = inputOffSnapshot();
    s.input.source   = InputSource::File;
    setInputList(s, fourFiles(), 0, -1);
    return s;
}

// M2b-A3: the pop-up list open over a snapshot (X held after an up/down press).
inline UiSnapshot withPopup(UiSnapshot s, Button dir = Button::DpadDown)
{
    s.xHeld = true;
    setButton(s, Button::X, true);
    setButton(s, dir, true);
    s.input.choosing = true;
    s.input.popup    = true;
    return s;
}

inline UiSnapshot inputPopupFourSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    setInputList(s, fourFiles(), 2, 0);
    return withPopup(s);
}

inline UiSnapshot inputPopupTwelveSnapshot()
{
    return withPopup(inputTwelveFilesSnapshot());
}

inline UiSnapshot inputPopupLongNameSnapshot()
{
    return withPopup(inputLongNameSnapshot(), Button::DpadUp);
}

inline UiSnapshot inputPopupNothingLoadedSnapshot()
{
    UiSnapshot s = inputNothingLoadedSnapshot();
    setInputList(s, fourFiles(), 1, -1);
    return withPopup(s);
}

inline UiSnapshot inputPopupLoadingSnapshot()
{
    UiSnapshot s = inputLoadingSnapshot();
    setInputList(s, fourFiles(), 1, 0);
    return withPopup(s);
}

inline UiSnapshot inputPopupOnceEndedSnapshot()
{
    return withPopup(inputOnceEndedSnapshot());
}

// A3 review: a failed load with the pop-up open, and X held (no pop-up) while the
// failure shows: the PLAYING line names the loaded file.
inline UiSnapshot inputPopupFailedSnapshot()
{
    return withPopup(inputErrorSnapshot());
}

inline UiSnapshot inputChoosingFailedSnapshot()
{
    UiSnapshot s = inputErrorSnapshot();
    s.xHeld      = true;
    setButton(s, Button::X, true);
    setButton(s, Button::DpadLeft, true);
    s.input.choosing = true;
    return s;
}

// The loaded file not listed (renamed on the card): "- OF 4".
inline UiSnapshot inputLoadedUnlistedSnapshot()
{
    UiSnapshot s = inputPlayingSnapshot();
    setInputList(s, fourFiles(), 2, -1);
    std::snprintf(s.input.playingName, sizeof s.input.playingName, "%s", "renamed_since.wav");
    setInputName(s, "renamed_since.wav");
    s.input.fileIndex = -1;
    return s;
}

inline UiSnapshot inputPopupNoFilesSnapshot()
{
    return withPopup(inputNoFilesSnapshot());
}

// --- M2b-B: the source LIVE ----------------------------------------------------------------
// The device list as InputController::fill() makes it with the source LIVE
// (SYSTEM DEFAULT first; the Brick's built-in codec as SDL names it; a USB
// interface): the list fields hold the devices, deviceCount their number.
inline std::vector<std::string> threeDevices()
{
    return {"SYSTEM DEFAULT", "audiocodec, ", "USB Audio CODEC, USB Audio"};
}

inline void setLiveList(UiSnapshot& s, const std::vector<std::string>& names, int cursor, int openIndex)
{
    const int files = s.input.fileCount;
    setInputList(s, names, cursor, openIndex);
    s.input.fileCount   = files; // the file count stays the file count
    s.input.deviceCount = static_cast<int>(names.size());
}

// LIVE selected, nothing chosen yet: the gain was forced to -inf, the warning shows.
inline UiSnapshot inputLiveNoDeviceSnapshot()
{
    UiSnapshot s   = inputOffSnapshot();
    s.input.source = InputSource::Live;
    setInputGain(s, 0.0f);
    setLiveList(s, threeDevices(), 0, -1);
    return s;
}

// LIVE with the USB interface open and signal: gain -6 dB, input at -12 dB, the
// mic level before the gain at -6 dB.
inline UiSnapshot inputLiveOpenSnapshot()
{
    UiSnapshot s = inputLiveNoDeviceSnapshot();
    setInputGain(s, 0.3536f);
    setLiveList(s, threeDevices(), 2, 2);
    s.input.deviceOpen      = true;
    std::snprintf(s.input.deviceName, sizeof s.input.deviceName, "%s", "USB Audio CODEC, USB Audio");
    s.input.grantedRate     = 48000;
    s.input.grantedChannels = 2;
    s.input.grantedFrames   = 256;
    s.input.liveRunning     = true;
    s.input.peak            = 0.25f;
    s.input.prePeak         = 0.5f;
    return s;
}

// LIVE, the built-in mic open, the gain still at zero (forced on open): the
// input meter is silent but the PRE bar shows the mic is alive.
inline UiSnapshot inputLiveGainZeroSnapshot()
{
    UiSnapshot s = inputLiveOpenSnapshot();
    setInputGain(s, 0.0f);
    setLiveList(s, threeDevices(), 1, 1);
    std::snprintf(s.input.deviceName, sizeof s.input.deviceName, "%s", "audiocodec, ");
    s.input.grantedChannels = 1;
    s.input.peak            = 0.0f;
    s.input.prePeak         = 0.08f;
    return s;
}

// M2b-B2: the Brick with SDL's alsa driver: no SYSTEM DEFAULT; the built-in mic
// opened on the direct route (DIRECT), the gain still at zero, the mic alive.
inline std::vector<std::string> brickDevices()
{
    return {"audiocodec, ", "JIANT FM, USB Audio"};
}

inline UiSnapshot inputLiveDirectSnapshot()
{
    UiSnapshot s = inputLiveGainZeroSnapshot();
    setLiveList(s, brickDevices(), 0, 0);
    s.input.grantedChannels = 1;
    s.input.deviceDirect    = true;
    return s;
}

// ... with 32 devices listed (the widest "n OF m" beside DIRECT).
inline UiSnapshot inputLiveDirectManySnapshot()
{
    UiSnapshot               s = inputLiveDirectSnapshot();
    std::vector<std::string> names;
    for(int i = 0; i < 32; ++i)
        names.push_back(i == 31 ? "audiocodec, " : "Device " + std::to_string(i));
    setLiveList(s, names, 31, 31);
    return s;
}

// ... while X is held on another device: the spec line (and DIRECT) give way.
inline UiSnapshot inputLiveDirectChoosingSnapshot()
{
    UiSnapshot s = inputLiveDirectSnapshot();
    s.xHeld      = true;
    setButton(s, Button::X, true);
    setButton(s, Button::DpadRight, true);
    setLiveList(s, brickDevices(), 1, 0);
    s.input.choosing = true;
    return s;
}

// LIVE, X held with left/right: the cursor on another device than the open one.
inline UiSnapshot inputLiveChoosingSnapshot()
{
    UiSnapshot s = inputLiveOpenSnapshot();
    s.xHeld      = true;
    setButton(s, Button::X, true);
    setButton(s, Button::DpadLeft, true);
    setLiveList(s, threeDevices(), 0, 2);
    s.input.choosing = true;
    return s;
}

// The device pop-up with three devices, the USB one open, the cursor on the mic.
inline UiSnapshot inputLivePopupSnapshot()
{
    UiSnapshot s = inputLiveOpenSnapshot();
    setLiveList(s, threeDevices(), 1, 2);
    return withPopup(s);
}

// The device pop-up with nothing open yet.
inline UiSnapshot inputLivePopupNothingSnapshot()
{
    UiSnapshot s = inputLiveNoDeviceSnapshot();
    setLiveList(s, threeDevices(), 0, -1);
    return withPopup(s);
}

// An open that failed (with SDL's reason, long).
inline UiSnapshot inputLiveFailedSnapshot()
{
    UiSnapshot s = inputLiveNoDeviceSnapshot();
    setLiveList(s, threeDevices(), 2, -1);
    s.input.liveStatus = LiveStatus::Failed;
    std::snprintf(s.input.liveFailName, sizeof s.input.liveFailName, "%s", "USB Audio CODEC, USB Audio");
    std::snprintf(s.input.liveReason, sizeof s.input.liveReason, "%s",
                  "ALSA: Couldn't open audio device: Device or resource busy");
    return s;
}

inline UiSnapshot inputLivePopupFailedSnapshot()
{
    return withPopup(inputLiveFailedSnapshot());
}

// The open device was unplugged.
inline UiSnapshot inputLiveLostSnapshot()
{
    UiSnapshot s = inputLiveNoDeviceSnapshot();
    setLiveList(s, {"SYSTEM DEFAULT", "audiocodec, "}, 1, -1);
    s.input.liveStatus = LiveStatus::Lost;
    return s;
}

// A long, non-ASCII device name open; nine devices (the pop-up scrolls).
inline UiSnapshot inputLiveLongNameSnapshot()
{
    UiSnapshot               s = inputLiveOpenSnapshot();
    std::vector<std::string> names{"SYSTEM DEFAULT"};
    for(int i = 1; i < 9; ++i)
        names.push_back(i == 8 ? "Scarlett 2i2 4th Gen \xe2\x80\x93 Analogue In 1 + 2, very long interface name"
                               : "Device " + std::to_string(i));
    setLiveList(s, names, 8, 8);
    std::snprintf(s.input.deviceName, sizeof s.input.deviceName, "%s", names[8].c_str());
    s.input.prePeak = 1.2f; // clipping before the gain
    return s;
}

inline UiSnapshot inputLivePopupLongSnapshot()
{
    return withPopup(inputLiveLongNameSnapshot(), Button::DpadUp);
}

// DELAY with the source LIVE: the IN indicator as with FILE.
inline UiSnapshot inIndicatorLiveSnapshot()
{
    UiSnapshot s = inputLiveOpenSnapshot();
    s.tab        = Tab::Delay;
    return s;
}

// REC: the recording views.
inline void setRecording(UiSnapshot& s, std::uint64_t seconds, std::uint64_t dropped)
{
    s.record            = UiRecordView{};
    s.record.border     = true;
    s.record.show       = RecordShow::Recording;
    s.record.sampleRate = 48000;
    s.record.frames     = seconds * 48000u + 1234u;
    s.record.dropped    = dropped;
}
inline UiSnapshot recordingSnapshot(Tab tab)
{
    UiSnapshot s = playingSnapshot(tab);
    setRecording(s, 83, 512); // REC 1:23 DROP 512
    return s;
}
inline UiSnapshot recordPopupSnapshot()
{
    UiSnapshot s = inputPopupTwelveSnapshot();
    setRecording(s, 5 * 3600 + 47 * 60 + 12, 123456); // REC 5:47:12 DROP 99999+ over the pop-up
    return s;
}
inline UiSnapshot recordSavingLiveSnapshot()
{
    UiSnapshot s     = inputLivePopupSnapshot();
    s.record         = UiRecordView{};
    s.record.border  = true;
    s.record.show    = RecordShow::Saving;
    return s;
}
inline UiSnapshot recordSavedSnapshot()
{
    UiSnapshot s     = stressSnapshot();
    s.record         = UiRecordView{};
    s.record.show    = RecordShow::Saved;
    std::snprintf(s.record.name, sizeof s.record.name, "%s", "rec-10000.wav");
    s.record.seconds = 5 * 3600 + 47 * 60 + 12.9; // SAVED REC-10000.WAV 5:47:12
    return s;
}
inline UiSnapshot recordMessageSnapshot()
{
    UiSnapshot s     = playingSnapshot(Tab::Delay);
    uitest::setFeedback(s, 1.0f, true); // the MAX plate and the stored marker too
    s.record         = UiRecordView{};
    s.record.show    = RecordShow::Message;
    s.record.message = "REC STOPPED: FILE LIMIT";
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
        {"input_off", inputOffSnapshot()},
        {"input_file_playing", inputPlayingSnapshot()},
        {"input_x_held", inputXHeldSnapshot()},
        {"input_loading", inputLoadingSnapshot()},
        {"input_error", inputErrorSnapshot()},
        {"input_no_files", inputNoFilesSnapshot()},
        {"input_long_name", inputLongNameSnapshot()},
        {"input_clip", inputClipSnapshot()},
        {"input_error_nothing_playing", inputErrorNothingSnapshot()},
        {"input_read_error", inputReadErrorSnapshot()},
        {"input_utf8_names", inputUtf8Snapshot()},
        {"input_once_ended", inputOnceEndedSnapshot()},
        {"input_twelve_files", inputTwelveFilesSnapshot()},
        {"input_nothing_loaded", inputNothingLoadedSnapshot()},
        {"input_popup_4_files", inputPopupFourSnapshot()},
        {"input_popup_12_files", inputPopupTwelveSnapshot()},
        {"input_popup_long_name", inputPopupLongNameSnapshot()},
        {"input_popup_nothing_loaded", inputPopupNothingLoadedSnapshot()},
        {"input_popup_loading", inputPopupLoadingSnapshot()},
        {"input_popup_once_ended", inputPopupOnceEndedSnapshot()},
        {"input_popup_no_files", inputPopupNoFilesSnapshot()},
        {"input_popup_failed", inputPopupFailedSnapshot()},
        {"input_choosing_failed", inputChoosingFailedSnapshot()},
        {"input_loaded_unlisted", inputLoadedUnlistedSnapshot()},
        {"in_indicator_loop", inIndicatorSnapshot()},
        {"in_indicator_clip_delay", inIndicatorClipSnapshot()},
        {"input_live_no_device", inputLiveNoDeviceSnapshot()},
        {"input_live_open", inputLiveOpenSnapshot()},
        {"input_live_gain_zero", inputLiveGainZeroSnapshot()},
        {"input_live_direct", inputLiveDirectSnapshot()},
        {"input_live_direct_32_devices", inputLiveDirectManySnapshot()},
        {"input_live_direct_choosing", inputLiveDirectChoosingSnapshot()},
        {"input_live_choosing", inputLiveChoosingSnapshot()},
        {"input_live_popup_3_devices", inputLivePopupSnapshot()},
        {"input_live_popup_nothing_open", inputLivePopupNothingSnapshot()},
        {"input_live_open_failed", inputLiveFailedSnapshot()},
        {"input_live_popup_failed", inputLivePopupFailedSnapshot()},
        {"input_live_device_lost", inputLiveLostSnapshot()},
        {"input_live_long_name", inputLiveLongNameSnapshot()},
        {"input_live_popup_long", inputLivePopupLongSnapshot()},
        {"in_indicator_live_delay", inIndicatorLiveSnapshot()},
        {"rec_loop", recordingSnapshot(Tab::Loop)},
        {"rec_popup_12_files", recordPopupSnapshot()},
        {"rec_saving_live_popup", recordSavingLiveSnapshot()},
        {"rec_saved", recordSavedSnapshot()},
        {"rec_message_max", recordMessageSnapshot()},
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
    ui::PeakMeter in; // M2b-A
    in.update(s.input.peak, s.timeSeconds);
    v.inLevel = in.level();
    v.inHold  = in.hold();
    v.inClip  = in.clipped();
    ui::PeakMeter pre; // M2b-B
    pre.update(s.input.prePeak, s.timeSeconds);
    v.preLevel = pre.level();
    v.preHold  = pre.hold();
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
