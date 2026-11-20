// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// App: the application shell (docs/briefs/M2.md, M2-B). It owns the window, the
// audio device, the voice, the button control source and model, and the input
// map, and runs the main loop:
//
//   poll SDL events -> InputMap -> ControlModel::onButton(event, time)
//   ControlModel::tick(time)
//   fill a UiSnapshot -> render(snapshot)        (ui::Renderer draws the screen, M2-C)
//
// Time is monotonic seconds since audio started, from SDL_GetPerformanceCounter.
//
// Start-up order: parse options; load the self-test script (if any); SDL_Init;
// window and renderer; AudreyVoice::init(), control source, model; open the audio
// device (paused) and allocate the render buffers; print the obtained spec and the
// keyboard binding; open the first controller; present a few frames; unpause
// audio; loop.
// Shutdown order: pause and close the audio device; destroy the model, the
// control source and the voice; close the controller; release the screen's glyph
// atlas (it belongs to the renderer); destroy the renderer and the window;
// SDL_Quit.
//
// Self-test mode (--selftest <script> --seconds <n>):
//   - The button script is parsed before audio starts.
//   - Script times and the model's clock are on the audio timeline (frames
//     rendered / 48000). Each scripted event is delivered on the UI thread through
//     ControlModel (and from there the command queue and the held word to the
//     audio thread) and, since M2b-A, the INPUT tab's panel (InputRouter::deliver)
//     at the first loop iteration at which that much audio has been rendered,
//     stamped with that time. The final values therefore do not depend
//     on whether the driver keeps real-time pace (SDL's dummy driver does not).
//   - The UI loop polls about every millisecond (presenting at most 60 times a
//     second, without vsync) so delivery lags the script by at most about one
//     audio buffer.
//   - The run ends when n seconds of audio have been rendered, plus at most two
//     buffers so the commands sent last are processed. A wall-clock timeout of
//     max(3 n, n + 10) seconds stops a run whose audio does not advance.
//   - Keyboard and controller input are ignored (only quit is honoured; Menu +
//     Start is not watched).
//   - The final state is printed. The exit code is 0 only if the run reached n
//     seconds of audio before the timeout, delivered every scripted event at or
//     before n seconds, every reported value is finite, and every --expect-*
//     expectation holds; otherwise 3, with the reasons printed.
//
// Device behaviour (docs/briefs/M3.md, M3-A):
//   - --fullscreen: an SDL_WINDOW_FULLSCREEN_DESKTOP window (on the Brick's Mali
//     framebuffer driver the window is the whole screen anyway), cursor hidden,
//     logical size 1024 x 768 with integer scaling, controller events also while
//     the window has no focus. Without it the window is as in M2.
//   - The renderer: accelerated with vsync (not in self-test mode), then
//     accelerated without vsync, then the software renderer; every attempt is
//     logged. Without vsync the frame rate is capped at 60 (FramePacer).
//   - Quit paths: SDL_QUIT, window close, Escape, Alt+F4 (InputMap), the stop
//     signals, and Menu + Start on a pad (M3-C, app/MenuQuit.h: Start pressed while
//     Menu is held, or Menu while Start is held, quits at once). Menu (SDL 'guide')
//     reaches only MenuQuit; Start also goes to the model, which does nothing with it.
//     Every event's InputMap action goes through InputRouter (app/InputRouter.h),
//     which feeds the model and MenuQuit and reports the quit.
//   - SDL_RENDER_TARGETS_RESET / SDL_RENDER_DEVICE_RESET rebuild the glyph atlas.
//   - --exit-with-parent (Linux; the launcher passes it): PR_SET_PDEATHSIG, so the
//     app gets SIGTERM (SDL turns it into SDL_QUIT, a clean exit) if the process
//     that started it dies, even by SIGKILL. Elsewhere it is accepted and logged
//     as unsupported.
//   - --remove-on-exit <file>: that one file is deleted when the app exits, on
//     every exit path that runs (the launcher passes its keep-awake pause file, so
//     it goes away even when the launcher was killed with SIGKILL).
//   - --restore-on-exit <file> <content> <expected> (M3-B, repeatable): <content>
//     is written to that existing file on the same exit paths, only if it still
//     holds <expected> (app/RestoreOnExit.h). The launcher passes, per policy, the
//     CPU governor it replaced and the one it set. Each result is
//     logged; a failure is never fatal. Not run if the app crashes or is killed
//     with SIGKILL (the launcher then restores, if it is still there).
//   - --swap-ab, --swap-xy (M3-B): game-controller A and B, X and Y swapped
//     (platform/InputMap.h). The log states at start whether each swap is on.
//   - Stop signals (M3-B review): SIGTERM, SIGINT and SIGHUP are handled by the
//     app's own handler from before main() (app/Signals.h); SDL installs none
//     (SDL_HINT_NO_SIGNAL_HANDLERS). Start-up checks for a stop signal after each
//     stage and the main loop every iteration; either way the app leaves through
//     the normal shutdown with its exit actions. With --exit-with-parent the
//     parent is checked first thing: if it is already gone (or the app's parent
//     is init), the app stops the same way.
//   - --version (M3-B): prints "AudreyBrick <version> (built <date>, <compiler>)"
//     and exits (the launcher puts it in stdout.log).
//   - CPU frequency (M3-B, app/CpuFreq.h): at start the policies and their
//     governors are logged (or "cpu frequency: not available" once); about once a
//     second the UI thread reads each policy's scaling_cur_freq; every statistics
//     line ends with the frequency now and the window's minimum and maximum. The
//     governors are logged again at exit, before and after restore-on-exit.
//     AUDREY_SYSFS_CPU overrides the sysfs base (a testing aid only).
//   - The statistics line calls the callback figure "callback load" and adds its
//     p50 and p95 and the per-window histogram (platform/Meters.h).
//   - --no-video (self-test only): SDL_INIT_VIDEO, the window, the renderer and all
//     drawing are skipped; audio, events, controllers, the model, the quit logic,
//     the loop (paced by SDL_Delay(1) as in any self-test) and the log run as usual.
//     For the device's own libraries in a container, whose SDL has only the Mali
//     video driver. A video failure without it is fatal (no automatic fallback),
//     with every attempt's SDL error in the log and on stderr.
//   - --log <file>: the diagnostic log (app/DiagLog.h, app/SdlDiagnostics.h),
//     written on this (the UI) thread only, from values the audio thread already
//     publishes through atomics. Start-up facts, controllers, first-seen inputs,
//     unmapped keys, focus changes, a statistics line every 10 seconds and the
//     reason for exit. A log that cannot be opened is reported once on stderr and
//     the app runs without it.
//
// Audio input (docs/briefs/M2b.md, M2b-A):
//   - --input-dir <dir>: the folder the FILE source lists (the launcher passes
//     audreybrick/data/input). Missing, empty or unreadable is not fatal: the
//     INPUT tab says so. Without the option there is no folder.
//   - The InputStage is attached to the AudioRender before the device opens (and
//     prepared with it); the InputController (the INPUT tab's model) gets every
//     button event through InputRouter::deliver(), and in self-test mode the
//     scripted events too, so a script can drive the INPUT tab. Its tick() runs
//     every loop iteration after the model's; its log lines go to the diagnostic
//     log. The source is OFF at every start. M2b-A2: MemAvailable (from
//     /proc/meminfo, where it exists) is logged at start-up and after each load.
//   - Shutdown: the audio device is closed first, then the controller (which
//     joins the loader thread), then the render and the stage (which frees the
//     buffers it still holds).
//   - Self-test: the report prints the input state; --expect-input-peak-min <v>
//     checks that the input meter (after gain) went above v during the run.
//
// Live input (docs/briefs/M2b.md, M2b-B):
//   - --live-target-buffers <n> (1..32, default 2; the launcher passes
//     AUDREY_LIVE_TARGET): the live input's target fill in output buffers
//     (InputStage::setLiveTargetBuffers), before the output device opens.
//   - A LiveInput (the capture ring) is attached to the stage before the output
//     device opens. After it opens, the capture system is created with the
//     output's buffer size and handed to the InputController: SDL's
//     (platform/CaptureHost) normally; in self-test mode always the simulated one
//     (input/LiveSim), with the devices the script declares (none: an empty
//     list), so a self-test never opens a capture device. The script's timed
//     capture plug / unplug lines are applied on the audio timeline like the
//     button events.
//   - Every SDL event goes to CaptureHost::onEvent() (capture devices added or
//     removed: the controller lists them again; the open one removed: DEVICE
//     LOST). The event logger logs them as before.
//   - The statistics line carries the live part (formatLiveStats(): fill and its
//     range, underruns, overfills, frames captured and read, the capture clock
//     against the output, pre- and post-gain peaks) while the source is LIVE.
//   - Shutdown: the capture device is closed first, then the output, then the
//     controller, the capture system, the render, the stage and the ring.
//     M2b-B2: closing the capture device also restores the built-in mic's mixer
//     values (CaptureHost / input/CaptureRoute.h); this is the path a normal
//     quit and a SIGTERM / SIGINT / SIGHUP (the launcher's signal handling) take.
//   - The self-test report adds the live state; --expect-live-underruns-max <n>
//     --expect-live-underruns-min <n>, --expect-live-repeats-min <n> and
//     --expect-live-overfills-min <n> check
//     the ring's counters (for the
//     clock-drift self-tests); --expect-live-status none|failed|lost|open checks
//     the INPUT tab's live status at the end (open: a device is open).

// Recording (docs/briefs/REC.md):
//   - --record-dir <dir>: the recordings folder (the launcher passes
//     audreybrick/data/recordings); default data/recordings, relative to the
//     working directory like the launcher's data/ folder. Created by the writer
//     when a recording starts (mkdir -p), not before.
//   - After the output device opens (paused): the RecordTap with a ring of 4 s
//     at the obtained rate is attached to the AudioRender, and the Recorder's
//     writer thread is started (record/Recorder.h). Both before audio starts.
//   - Start + Y (app/RecordChord.h, through InputRouter; in self-test mode the
//     scripted events go through the same chord) toggles recording. While
//     recording or finalising, the UiSnapshot's record view asks for the red
//     border; the readout shows REC m:ss (frames queued / rate) and DROP n, then
//     SAVED <name> m:ss for 2 s, or a refusal or failure for 4 s, or REC BUSY
//     for 2 s when Start + Y comes while the previous file is being finalised.
//   - The writer's log lines go to the diagnostic log every iteration; every
//     statistics window while recording adds a "record:" line (frames written,
//     ring fill maximum, dropped frames, longest write).
//   - Quit (every path that reaches the normal shutdown: Menu + Start, Escape,
//     window close, SIGTERM / SIGINT / SIGHUP, the parent dying): after the
//     output device is closed, Recorder::shutdown() finishes the file and joins
//     the writer; then the render (which points at the tap) and the tap go.
//   - Self-test: the report lists every recording (name, frames, seconds,
//     dropped, outcome); --expect-recordings <n> checks how many were saved.

#pragma once

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/ButtonScript.h"
#include "app/ControlModel.h"
#include "app/CpuFreq.h"
#include "app/DiagLog.h"
#include "app/FramePacer.h"
#include "app/MenuQuit.h"
#include "app/RecordChord.h"
#include "app/RestoreOnExit.h"
#include "app/SdlDiagnostics.h"
#include "app/UiSnapshot.h"
#include "control/ButtonControlSource.h"
#include "core/AudreyVoice.h"
#include "core/ControlSource.h"
#include "input/InputController.h"
#include "input/InputStage.h"
#include "input/LiveInput.h"
#include "input/LiveSim.h"
#include "platform/CaptureHost.h"
#include "platform/AudioHost.h"
#include "platform/AudioRender.h"
#include "platform/InputMap.h"
#include "record/RecordFs.h"
#include "record/RecordTap.h"
#include "record/Recorder.h"
#include "ui/Renderer.h"

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
    bool        fullscreen = false;  // --fullscreen (the device)
    std::string logPath;             // --log <file>; empty = no diagnostic log
    bool        noVideo   = false;   // --no-video (self-test only): no window, renderer or drawing
    bool        exitWithParent = false; // --exit-with-parent (Linux): SIGTERM when the parent dies
    std::string removeOnExit;        // --remove-on-exit <file>: deleted when the app exits
    std::vector<RestoreAction> restoreOnExit; // --restore-on-exit <file> <content>, repeatable (M3-B)
    bool        swapAB    = false;   // --swap-ab: controller A and B swapped (M3-B)
    bool        swapXY    = false;   // --swap-xy: controller X and Y swapped (M3-B)
    bool        version   = false;   // --version: print the version and exit (M3-B)
    std::string inputDir;            // --input-dir <dir> (M2b-A): the FILE source's folder; empty = none
    int         liveTargetBuffers = 2; // --live-target-buffers <n> (M2b-B review, S1): 1..32
    std::string recordDir = "data/recordings"; // --record-dir <dir> (REC)
    std::string selftestScript;      // --selftest; empty = normal mode
    double      selftestSeconds = 0; // --seconds (required with --selftest), seconds of audio

    // Self-test expectations (only with --selftest).
    std::vector<KnobExpectation> expectKnobs; // --expect-knob <Name> <min> <max>, repeatable
    bool                         hasPeakMin = false;
    double                       peakMin    = 0.0; // --expect-peak-min: each channel's peak above this
    bool                         hasPeakMax = false;
    double                       peakMax    = 0.0; // --expect-peak-max: each channel's peak below this
    bool                         hasInputPeakMin = false;
    double                       inputPeakMin    = 0.0; // --expect-input-peak-min (M2b-A): the input meter went above this
    bool                         hasLiveUnderrunsMax = false;
    double                       liveUnderrunsMax    = 0.0; // --expect-live-underruns-max (M2b-B)
    bool                         hasLiveUnderrunsMin = false;
    double                       liveUnderrunsMin    = 0.0; // --expect-live-underruns-min (M2b-B)
    bool                         hasLiveRepeatsMin   = false;
    double                       liveRepeatsMin      = 0.0; // --expect-live-repeats-min (M2b-B review, S3)
    bool                         hasLiveOverfillsMin = false;
    double                       liveOverfillsMin    = 0.0; // --expect-live-overfills-min (M2b-B)
    std::string                  expectLiveStatus;          // --expect-live-status none|failed|lost|open (M2b-B)
    int                          expectRecordings = -1;     // --expect-recordings <n> (REC): files saved; -1 = no check

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
    bool   createWindowAndRenderer(std::string& error);
    double logTime() const noexcept; // seconds since run() started (the log's clock)
    void   logStats(double wall, const char* label);
    void   logAtlasState();
    void   exitActions(); // --remove-on-exit and --restore-on-exit, once per run
    void   logGovernors(const char* when);
    void   drainInputLog(); // M2b-A: the input controller's lines into the diagnostic log
    void   formatLiveWindow(char* out, std::size_t size, std::uint64_t outputFrames); // M2b-B
    void   onRecordToggle();   // REC: Start + Y
    void   drainRecorder();    // REC: the writer's log lines and results
    void   fillRecordView(double wall) noexcept; // REC: snap_.record
    void   stopRecorder();     // REC: finishes a recording and joins the writer
    // A stop signal during start-up: true (with `error` set) if one has arrived.
    bool   stopAt(const char* stage, std::string& error);
    // Leaves through the normal shutdown with the exit actions; returns the exit code.
    int    stopEarly(const char* stage);
    // A video start-up failure (fatal) or a failed attempt with a fallback (not
    // fatal): to the log and to stderr (so to stdout.log on the device).
    void videoFailure(double t, bool fatal, const char* fmt, ...) AB_PRINTF_FORMAT(4, 5);
    // Prints the self-test report, checks it and returns the exit code.
    int finishSelftest(double wallSeconds, bool timedOut);

    AppOptions opts_;

    // SDL resources.
    bool          sdlInitialised_ = false;
    SDL_Window*   window_         = nullptr;
    SDL_Renderer* renderer_       = nullptr;
    bool          vsync_          = false;
    ui::Renderer  ui_;            // the screen (M2-C); released before renderer_ is destroyed
    std::uint64_t t0_             = 0;
    double        counterFreq_    = 1.0;

    // Instrument (destroyed after the audio device is closed).
    std::unique_ptr<AudreyVoice>         voice_;
    std::unique_ptr<ButtonControlSource> source_;
    std::unique_ptr<ControlModel>        model_;
    std::unique_ptr<LiveInput>           live_;     // M2b-B: the capture ring (outlives stage_ and the capture systems)
    std::unique_ptr<InputStage>          stage_;    // M2b-A: the input (outlives audio_)
    std::unique_ptr<AudioRender>         audio_;
    std::unique_ptr<AudioHost>           host_;
    std::unique_ptr<CaptureHost>         captureHost_; // M2b-B: SDL's capture devices (not in self-test mode)
    std::unique_ptr<LiveSim>             captureSim_;  // M2b-B: the self-test's simulated capture devices
    std::unique_ptr<InputController>     inputCtl_; // M2b-A: the INPUT tab's model and the loader thread
    std::vector<std::string>             inputLog_; // its log lines, drained every iteration
    // REC: the recording. The tap outlives audio_ (the render points at it).
    std::unique_ptr<RecordTap>           recordTap_;
    PosixRecordFs                        recordFs_;
    std::unique_ptr<Recorder>            recorder_;
    RecordChord                          recordChord_;
    std::vector<std::string>             recordLog_;
    std::vector<RecordResult>            recordResults_; // every finished recording (self-test report)
    const char*                          recordMessage_      = "";   // the readout's message (static string)
    RecordShow                           recordMessageShow_  = RecordShow::None; // Saved or Message
    char                                 recordSavedName_[24] = {};
    double                               recordSavedSeconds_ = 0.0;
    double                               recordMessageUntil_ = -1.0; // wall clock (now())
    InputMap                             input_;

    // Self-test.
    std::vector<TimedButtonEvent> script_;
    CaptureScript                 captureScript_;         // M2b-B: the script's simulated capture devices and events
    std::size_t                   nextCaptureEvent_ = 0;
    std::size_t                   nextEvent_        = 0;
    double                        maxEventLateness_ = 0.0; // seconds (audio clock)
    double                        longestFrame_     = 0.0; // seconds of work in one loop iteration

    // Device behaviour (M3).
    MenuQuit      quit_;
    DiagLog       log_;
    EventLogger   events_{log_};
    FrameRate     fps_;
    double        runStart_            = 0.0; // steady clock at the start of run(), seconds
    long          parentPid_           = 0;   // getppid() at start (to explain an SDL_QUIT)
    std::string   exitReason_          = "unknown";
    double        statsStart_          = 0.0; // wall clock (now()) at the start of the statistics window
    float         statsCpuMax_         = 0.0f;
    float         statsPeak_[2]        = {0.0f, 0.0f};
    std::uint64_t statsCallbacks_      = 0; // callbacks at the start of the window
    std::uint64_t statsOverruns_       = 0; // overruns at the start of the window
    std::uint64_t statsAudioFrames_    = 0; // audio frames rendered at the start of the window
    std::uint64_t statsBuckets_[kStatsLoadBuckets] = {}; // load histogram counters at the start of the window
    CpuFreqMonitor cpuFreq_;                   // M3-B
    double        lastFreqSample_      = -1.0; // wall clock (now()) of the last frequency sample
    bool          exitActionsDone_     = false;
    bool          stoppedInStartUp_    = false;
    unsigned      atlasBuildsLogged_   = 0;
    unsigned      atlasFailuresLogged_ = 0;

    // UI-side meter bookkeeping.
    UiSnapshot snap_;
    float      cpuWindowMax_           = 0.0f; // current one-second window
    double     cpuWindowStart_         = 0.0;
    float      cpuSessionMax_          = 0.0f;
    float      cpuMaxAfterFirstSecond_ = 0.0f; // self-test report: excludes start-up
    float      peakSessionMax_[2]      = {0.0f, 0.0f};
    float      inputPeakSessionMax_    = 0.0f; // M2b-A
    float      statsInputPeak_         = 0.0f; // M2b-A: input peak in the statistics window
    // M2b-B: the live input in the statistics window (counters at its start).
    float         statsPrePeak_        = 0.0f;
    float         prePeakSessionMax_   = 0.0f;
    std::uint64_t statsLiveUnderruns_  = 0;
    std::uint64_t statsLiveRepeats_    = 0;
    std::uint64_t statsLiveOverfills_  = 0;
    std::uint64_t statsLiveCaptured_   = 0;
    std::uint64_t statsLiveRead_       = 0;
    std::uint64_t statsLiveDropped_    = 0;
    std::uint64_t statsLiveCallbacks_  = 0;
};

} // namespace audreybrick
