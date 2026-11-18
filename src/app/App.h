// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// App: the application shell (docs/briefs/M2.md, M2-B). It owns the window, the
// audio device, the voice, the button control source and model, and the input
// map, and runs the main loop:
//
//   poll SDL events -> InputMap -> ControlModel::onButton(event, time)
//   ControlModel::tick(time)
//   fill a UiSnapshot -> render(snapshot)        (M2-B: only clears the window)
//
// Time is monotonic seconds since audio started, from SDL_GetPerformanceCounter.
//
// Start-up order: parse options; load the self-test script (if any); SDL_Init;
// window and renderer; AudreyVoice::init(), control source, model; open the audio
// device (paused) and allocate the render buffers; print the obtained spec and the
// keyboard binding; open the first controller; present a few frames; unpause
// audio; loop.
// Shutdown order: pause and close the audio device; destroy the model, the
// control source and the voice; destroy the renderer and the window; SDL_Quit.
//
// Self-test mode (--selftest <script> --seconds <n>):
//   - The button script is parsed before audio starts.
//   - Script times and the model's clock are on the audio timeline (frames
//     rendered / 48000). Each scripted event is delivered on the UI thread through
//     ControlModel (and from there the command queue and the held word to the
//     audio thread) at the first loop iteration at which that much audio has been
//     rendered, stamped with that time. The final values therefore do not depend
//     on whether the driver keeps real-time pace (SDL's dummy driver does not).
//   - The UI loop polls about every millisecond (presenting at most 60 times a
//     second, without vsync) so delivery lags the script by at most about one
//     audio buffer.
//   - The run ends when n seconds of audio have been rendered, plus at most two
//     buffers so the commands sent last are processed. A wall-clock timeout of
//     max(3 n, n + 10) seconds stops a run whose audio does not advance.
//   - Keyboard and controller input are ignored (only quit is honoured).
//   - The final state is printed. The exit code is 0 only if the run reached n
//     seconds of audio before the timeout, delivered every scripted event at or
//     before n seconds, every reported value is finite, and every --expect-*
//     expectation holds; otherwise 3, with the reasons printed.

#pragma once

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/ButtonScript.h"
#include "app/ControlModel.h"
#include "app/UiSnapshot.h"
#include "control/ButtonControlSource.h"
#include "core/AudreyVoice.h"
#include "core/ControlSource.h"
#include "platform/AudioHost.h"
#include "platform/AudioRender.h"
#include "platform/InputMap.h"

namespace audreybrick
{

struct KnobExpectation
{
    Knob   knob;
    double min; // inclusive, normalised
    double max; // inclusive, normalised
};

struct AppOptions
{
    int         bufferFrames = 256;  // --buffer
    std::string audioDriver;         // --audio-driver (sets SDL_AUDIODRIVER); empty = SDL's choice
    bool        listAudio = false;   // --list-audio (with --audio-driver: that driver only)
    bool        help      = false;   // --help
    std::string selftestScript;      // --selftest; empty = normal mode
    double      selftestSeconds = 0; // --seconds (required with --selftest), seconds of audio

    // Self-test expectations (only with --selftest).
    std::vector<KnobExpectation> expectKnobs; // --expect-knob <Name> <min> <max>, repeatable
    bool                         hasPeakMin = false;
    double                       peakMin    = 0.0; // --expect-peak-min: each channel's peak above this
    bool                         hasPeakMax = false;
    double                       peakMax    = 0.0; // --expect-peak-max: each channel's peak below this

    bool selftest() const noexcept { return !selftestScript.empty(); }
};

// Smallest and largest accepted --buffer values (frames). SDL's buffer size field
// is 16 bits.
constexpr int kMinBufferFrames = 16;
constexpr int kMaxBufferFrames = 32768;

// Parses the command line. Returns false with `error` set for an unknown option, a
// missing or bad value, or an inconsistent combination. Does not touch SDL.
bool        parseOptions(int argc, const char* const* argv, AppOptions& out, std::string& error);
std::string usageText(const char* program);
// The keyboard binding, as printed by --help and at start-up.
std::string keyboardHelpText();

// Fills the control part of a snapshot (tab, modifiers, buttons, knobs, feedback,
// switch, dropped commands) from the model and the source's published values.
// Does not allocate.
void fillControlSnapshot(UiSnapshot& s, const ControlModel& model, const ButtonControlSource& source) noexcept;

class App
{
  public:
    explicit App(AppOptions options);
    ~App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Runs until quit (or the end of the self-test). Returns the process exit
    // code: 0 on a clean exit (or a passing self-test), 1 with a message on stderr
    // if SDL, the window or the audio device could not be opened, 2 for a bad
    // self-test script, 3 for a failed self-test.
    int run();

  private:
    bool   startUp(std::string& error);
    void   shutDown() noexcept;
    double now() const noexcept;          // wall clock, seconds since audio started
    double audioSeconds() const noexcept; // audio rendered so far, seconds
    void   fillSnapshot(double t);
    void   render(const UiSnapshot& s);
    // Prints the self-test report, checks it and returns the exit code.
    int finishSelftest(double wallSeconds, bool timedOut);

    AppOptions opts_;

    // SDL resources.
    bool          sdlInitialised_ = false;
    SDL_Window*   window_         = nullptr;
    SDL_Renderer* renderer_       = nullptr;
    bool          vsync_          = false;
    std::uint64_t t0_             = 0;
    double        counterFreq_    = 1.0;

    // Instrument (destroyed after the audio device is closed).
    std::unique_ptr<AudreyVoice>         voice_;
    std::unique_ptr<ButtonControlSource> source_;
    std::unique_ptr<ControlModel>        model_;
    std::unique_ptr<AudioRender>         audio_;
    std::unique_ptr<AudioHost>           host_;
    InputMap                             input_;

    // Self-test.
    std::vector<TimedButtonEvent> script_;
    std::size_t                   nextEvent_        = 0;
    double                        maxEventLateness_ = 0.0; // seconds (audio clock)
    double                        longestFrame_     = 0.0; // seconds of work in one loop iteration

    // UI-side meter bookkeeping.
    UiSnapshot snap_;
    float      cpuWindowMax_           = 0.0f; // current one-second window
    double     cpuWindowStart_         = 0.0;
    float      cpuSessionMax_          = 0.0f;
    float      cpuMaxAfterFirstSecond_ = 0.0f; // self-test report: excludes start-up
    float      peakSessionMax_[2]      = {0.0f, 0.0f};
};

} // namespace audreybrick
