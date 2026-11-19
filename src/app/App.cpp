// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/App.h"

#include <SDL.h>

#include <sys/resource.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <chrono>
#include <csignal>
#include <ctime>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/BuildInfo.h"
#include "app/CpuFreq.h"
#include "app/DisplayPolicy.h"
#include "app/InputRouter.h"
#include "app/RestoreOnExit.h"
#include "app/Signals.h"
#include "core/ParamTable.h"
#include "platform/Denormals.h"
#include "platform/Meters.h"

namespace audreybrick
{
namespace
{
constexpr int kLogicalWidth  = 1024;
constexpr int kLogicalHeight = 768;

// Frame pacing: app/FramePacer.h.

// Frames presented before audio starts (see App::run).
constexpr int kWarmUpFrames = 3;

// One-second window for the displayed CPU maximum.
constexpr double kCpuWindowSeconds = 1.0;

// The diagnostic log's statistics line, every this many seconds (wall clock).
constexpr double kStatsSeconds = 10.0;

// CPU frequency samples (M3-B), about this often (wall clock, UI thread).
constexpr double kFreqSampleSeconds = 1.0;

static_assert(kStatsLoadBuckets == kLoadBuckets, "the log's histogram matches the meter's");

double steadySeconds() noexcept
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void copyString(char* dst, std::size_t size, const char* src) noexcept
{
    if(size == 0)
        return;
    std::size_t i = 0;
    for(; src != nullptr && src[i] != '\0' && i + 1 < size; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

bool parseInt(const char* s, int& out) noexcept
{
    if(s == nullptr || *s == '\0')
        return false;
    char* end = nullptr;
    errno     = 0;
    const long v = std::strtol(s, &end, 10);
    if(errno != 0 || end == s || *end != '\0' || v < INT32_MIN || v > INT32_MAX)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool parseNumber(const char* s, double& out) noexcept
{
    if(s == nullptr || *s == '\0')
        return false;
    char* end = nullptr;
    errno     = 0;
    const double v = std::strtod(s, &end);
    if(errno != 0 || end == s || *end != '\0' || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

std::uint64_t performanceCounter()
{
    return SDL_GetPerformanceCounter();
}
} // namespace

std::string keyboardHelpText()
{
    return "Keyboard (keys matched by position, so the layout does not matter):\n"
           "  arrow keys     d-pad: move the current tab's parameters\n"
           "  Q / W          L1 / R1: previous / next tab\n"
           "  A key / S key  L2 / R2: feedback down / up (hold to ramp)\n"
           "                 NOTE: the A key is L2, not the A button\n"
           "  Space          A button: feedback to maximum while held\n"
           "  B              B button: delay switch while held; Left Shift + B latches it\n"
           "  X              X button: hold to shift the d-pad to the tab's other parameters\n"
           "  Y              Y button: hold + a d-pad direction resets that parameter\n"
           "  Left Shift     Select: fine adjust while held\n"
           "  Return         Start (no function by itself)\n"
           "  Escape, Alt+F4 quit (on a pad: Menu + Start)\n";
}

std::string usageText(const char* program)
{
    std::string p = program != nullptr ? program : "audreybrick";
    return "usage: " + p +
           " [options]\n"
           "\n"
           "  --buffer <frames>        audio buffer size to request (default 256, " +
           std::to_string(kMinBufferFrames) + ".." + std::to_string(kMaxBufferFrames) +
           ")\n"
           "  --audio-driver <name>    SDL audio driver to use (sets SDL_AUDIODRIVER)\n"
           "  --fullscreen             fullscreen window, cursor hidden, 1024x768 with integer scaling\n"
           "                           (the device)\n"
           "  --log <file>             write a diagnostic log (its directory is created if missing)\n"
           "  --exit-with-parent       quit (cleanly, as on SIGTERM) when the parent process dies (Linux)\n"
           "  --remove-on-exit <file>  delete this one file when the app exits (the launcher's\n"
           "                           keep-awake file, in case the launcher itself was killed)\n"
           "  --restore-on-exit <file> <content> <expected>\n"
           "                           when the app exits, write <content> to this existing file\n"
           "                           (absolute path) if it still holds <expected>; never creates it;\n"
           "                           a failure is logged, not fatal. <content> and <expected>: 1 to 32\n"
           "                           characters a-z, 0-9, _. Repeatable. The launcher passes the CPU\n"
           "                           governors it changed (found, set).\n"
           "  --swap-ab                game controller A and B swapped: SDL 'a' acts as B, 'b' as A\n"
           "                           (KNULLI on the TrimUI Brick); keyboard and other buttons unaffected\n"
           "  --swap-xy                game controller X and Y swapped: SDL 'x' acts as Y, 'y' as X\n"
           "  --input-dir <dir>        the folder the INPUT tab's FILE source lists (*.wav); a missing\n"
           "                           or empty folder is not an error (the launcher passes data/input)\n"
           "  --version                print the version and exit\n"
           "  --no-video               self-test only: no window, renderer or drawing (for a system\n"
           "                           without a usable video driver, e.g. the device's libraries in a\n"
           "                           container); audio, events, controls and the log run as usual\n"
           "  --list-audio             print the audio drivers and their output devices, then exit;\n"
           "                           with --audio-driver, list only that driver\n"
           "  --selftest <script>      self-test: play a button script\n"
           "                           (<time_s> <button> <down|up> per line), then report and check\n"
           "  --seconds <n>            self-test length in seconds of audio (required with --selftest)\n"
           "  --expect-knob <Name> <min> <max>\n"
           "                           self-test: the knob's final normalised value must be within\n"
           "                           [min, max]; FeedbackGain is the stored feedback. Repeatable.\n"
           "  --expect-peak-min <v>    self-test: each channel's output peak must be above v\n"
           "  --expect-peak-max <v>    self-test: each channel's output peak must be below v\n"
           "  --expect-input-peak-min <v>\n"
           "                           self-test: the input meter (after the input gain) must go above v\n"
           "  --help                   print this text and exit\n"
           "\n" +
           keyboardHelpText();
}

bool parseOptions(int argc, const char* const* argv, AppOptions& out, std::string& error)
{
    AppOptions o;
    bool       secondsGiven = false;
    for(int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i] != nullptr ? argv[i] : "";
        auto value = [&](const char*& v) -> bool {
            if(i + 1 >= argc || argv[i + 1] == nullptr)
            {
                error = "option " + a + " needs a value";
                return false;
            }
            v = argv[++i];
            return true;
        };
        const char* v = nullptr;
        if(a == "--help" || a == "-h")
            o.help = true;
        else if(a == "--list-audio")
            o.listAudio = true;
        else if(a == "--fullscreen")
            o.fullscreen = true;
        else if(a == "--no-video")
            o.noVideo = true;
        else if(a == "--exit-with-parent")
            o.exitWithParent = true;
        else if(a == "--swap-ab")
            o.swapAB = true;
        else if(a == "--swap-xy")
            o.swapXY = true;
        else if(a == "--version")
            o.version = true;
        else if(a == "--restore-on-exit")
        {
            if(i + 3 >= argc || argv[i + 1] == nullptr || argv[i + 2] == nullptr || argv[i + 3] == nullptr)
            {
                error = "--restore-on-exit needs <file> <content> <expected>";
                return false;
            }
            RestoreAction r;
            r.file     = argv[++i];
            r.content  = argv[++i];
            r.expected = argv[++i];
            if(r.file.empty() || r.file[0] != '/')
            {
                error = "--restore-on-exit: the file must be an absolute path, got '" + r.file + "'";
                return false;
            }
            if(!isRestoreValue(r.content) || !isRestoreValue(r.expected))
            {
                error = "--restore-on-exit " + r.file + ": <content> and <expected> must be 1 to 32 characters "
                        "a-z, 0-9 or _, got '" + r.content + "' and '" + r.expected + "'";
                return false;
            }
            o.restoreOnExit.push_back(std::move(r));
        }
        else if(a == "--remove-on-exit")
        {
            if(!value(v))
                return false;
            if(*v == '\0')
            {
                error = "--remove-on-exit: empty file name";
                return false;
            }
            o.removeOnExit = v;
        }
        else if(a == "--log")
        {
            if(!value(v))
                return false;
            if(*v == '\0')
            {
                error = "--log: empty file name";
                return false;
            }
            o.logPath = v;
        }
        else if(a == "--buffer")
        {
            if(!value(v))
                return false;
            if(!parseInt(v, o.bufferFrames) || o.bufferFrames < kMinBufferFrames || o.bufferFrames > kMaxBufferFrames)
            {
                error = std::string("--buffer: expected a whole number of frames from ") +
                        std::to_string(kMinBufferFrames) + " to " + std::to_string(kMaxBufferFrames) + ", got '" + v +
                        "'";
                return false;
            }
        }
        else if(a == "--input-dir")
        {
            if(!value(v))
                return false;
            if(*v == '\0')
            {
                error = "--input-dir: empty folder name";
                return false;
            }
            o.inputDir = v;
        }
        else if(a == "--expect-input-peak-min")
        {
            if(!value(v))
                return false;
            if(!parseNumber(v, o.inputPeakMin))
            {
                error = a + ": expected a number, got '" + v + "'";
                return false;
            }
            o.hasInputPeakMin = true;
        }
        else if(a == "--audio-driver")
        {
            if(!value(v))
                return false;
            if(*v == '\0')
            {
                error = "--audio-driver: empty driver name";
                return false;
            }
            o.audioDriver = v;
        }
        else if(a == "--selftest")
        {
            if(!value(v))
                return false;
            if(*v == '\0')
            {
                error = "--selftest: empty script path";
                return false;
            }
            o.selftestScript = v;
        }
        else if(a == "--expect-knob")
        {
            if(i + 3 >= argc)
            {
                error = "--expect-knob needs <Name> <min> <max>";
                return false;
            }
            const char*     name = argv[++i];
            const char*     lo   = argv[++i];
            const char*     hi   = argv[++i];
            KnobExpectation ex{};
            if(name == nullptr || !knobFromId(name, ex.knob))
            {
                error = std::string("--expect-knob: unknown knob '") + (name ? name : "") +
                        "' (use a ParamTable id such as FeedbackGain or OutputVolume)";
                return false;
            }
            if(!parseNumber(lo, ex.min) || !parseNumber(hi, ex.max) || ex.min > ex.max)
            {
                error = std::string("--expect-knob ") + name + ": expected two numbers <min> <= <max>";
                return false;
            }
            o.expectKnobs.push_back(ex);
        }
        else if(a == "--expect-peak-min" || a == "--expect-peak-max")
        {
            if(!value(v))
                return false;
            double x = 0.0;
            if(!parseNumber(v, x))
            {
                error = a + ": expected a number, got '" + v + "'";
                return false;
            }
            if(a == "--expect-peak-min")
            {
                o.hasPeakMin = true;
                o.peakMin    = x;
            }
            else
            {
                o.hasPeakMax = true;
                o.peakMax    = x;
            }
        }
        else if(a == "--seconds")
        {
            if(!value(v))
                return false;
            if(!parseNumber(v, o.selftestSeconds) || o.selftestSeconds <= 0.0 || o.selftestSeconds > 86400.0)
            {
                error = std::string("--seconds: expected a number of seconds above 0 (at most 86400), got '") + v + "'";
                return false;
            }
            secondsGiven = true;
        }
        else
        {
            error = "unknown option '" + a + "'";
            return false;
        }
    }
    if(o.selftest() && !secondsGiven)
    {
        error = "--selftest needs --seconds <n>";
        return false;
    }
    if(!o.selftest() && o.noVideo)
    {
        error = "--no-video is only used with --selftest";
        return false;
    }
    if(!o.selftest() && secondsGiven)
    {
        error = "--seconds is only used with --selftest";
        return false;
    }
    if(!o.selftest() && (!o.expectKnobs.empty() || o.hasPeakMin || o.hasPeakMax || o.hasInputPeakMin))
    {
        error = "--expect-knob, --expect-peak-min, --expect-peak-max and --expect-input-peak-min are only used with "
                "--selftest";
        return false;
    }
    out = std::move(o);
    return true;
}

void fillControlSnapshot(UiSnapshot& s, const ControlModel& model, const ButtonControlSource& source) noexcept
{
    s.tab   = model.currentTab();
    s.fine  = model.fine();
    s.xHeld = model.xHeld();
    s.yHeld = model.yHeld();
    for(std::size_t b = 0; b < kButtonCount; ++b)
        s.buttonDown[b] = model.isDown(static_cast<Button>(b));

    s.switchOn    = source.publishedSwitch();
    s.switchLatch = source.publishedSwitchLatch();
    s.switchHeld  = model.switchHeld();
    s.feedbackMax = source.publishedFeedbackMax();

    for(std::size_t k = 0; k < kKnobCount; ++k)
    {
        const Knob  knob = static_cast<Knob>(k);
        const float n    = source.publishedKnob(knob);
        s.knob[k].norm   = n;
        s.knob[k].engine = knobToEngine(knob, n, s.switchOn);
        s.knob[k].unit   = paramSpec(knob).unit;
    }
    s.storedFeedback       = source.publishedStoredFeedback();
    s.storedFeedbackEngine = knobToEngine(Knob::FeedbackGain, s.storedFeedback, false);
    s.commandsDropped      = model.commandsDropped();
}

App::App(AppOptions options) : opts_(std::move(options)) {}

App::~App()
{
    shutDown();
}

double App::now() const noexcept
{
    return static_cast<double>(SDL_GetPerformanceCounter() - t0_) / counterFreq_;
}

double App::audioSeconds() const noexcept
{
    return static_cast<double>(audio_->framesRendered()) / AudreyVoice::kSampleRate;
}

double App::logTime() const noexcept
{
    return steadySeconds() - runStart_;
}

bool App::createWindowAndRenderer(std::string& error)
{
    const double lt = logTime();
    // The window. --fullscreen: FULLSCREEN_DESKTOP keeps the display's own mode
    // (no mode switch). The Mali framebuffer driver has one display, no window
    // manager, and sizes the window to the whole screen whatever is asked; if it
    // refuses the fullscreen flag, a plain window is tried (it is fullscreen there
    // anyway). [guess, to be confirmed by the first device run's log]
    const Uint32 flags = opts_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
    window_ = SDL_CreateWindow("AudreyBrick", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kLogicalWidth,
                               kLogicalHeight, flags);
    if(window_ == nullptr)
        videoFailure(lt, !opts_.fullscreen, "window: %s window failed: %s",
                     opts_.fullscreen ? "fullscreen-desktop" : "resizable", SDL_GetError());
    if(window_ == nullptr && opts_.fullscreen)
    {
        log_.line(lt, "window: trying a plain window");
        window_ = SDL_CreateWindow("AudreyBrick", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kLogicalWidth,
                                   kLogicalHeight, 0);
        if(window_ == nullptr)
            videoFailure(logTime(), true, "window: plain window failed: %s", SDL_GetError());
    }
    if(window_ == nullptr)
    {
        error = std::string("cannot open the window: ") + SDL_GetError();
        logVideoDisplays(log_, logTime(), stderr);
        return false;
    }

    // The renderer: accelerated with vsync (not in self-test mode: presenting must
    // never block its ~1 ms polling loop), accelerated without, then software.
    struct Attempt
    {
        Uint32      flags;
        const char* what;
    };
    const Attempt attempts[] = {
        {SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC, "accelerated with vsync"},
        {SDL_RENDERER_ACCELERATED, "accelerated without vsync"},
        {SDL_RENDERER_SOFTWARE, "software"},
    };
    for(const Attempt& a : attempts)
    {
        if(opts_.selftest() && (a.flags & SDL_RENDERER_PRESENTVSYNC) != 0)
            continue;
        renderer_ = SDL_CreateRenderer(window_, -1, a.flags);
        if(renderer_ != nullptr)
        {
            log_.line(logTime(), "renderer: created %s", a.what);
            break;
        }
        videoFailure(logTime(), false, "renderer: %s failed: %s", a.what, SDL_GetError());
    }
    if(renderer_ == nullptr)
    {
        error = std::string("cannot create a renderer: ") + SDL_GetError();
        videoFailure(logTime(), true, "renderer: every attempt failed");
        logVideoInfo(log_, logTime(), window_, nullptr, stderr);
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, kLogicalWidth, kLogicalHeight);
    if(opts_.fullscreen)
    {
        int ow = 0;
        int oh = 0;
        if(SDL_GetRendererOutputSize(renderer_, &ow, &oh) != 0)
            ow = oh = 0;
        const IntegerScaleDecision d = integerScaleFor(ow, oh, kLogicalWidth, kLogicalHeight);
        SDL_RenderSetIntegerScale(renderer_, d.on ? SDL_TRUE : SDL_FALSE);
        log_.line(logTime(), "renderer: output %dx%d, integer scaling %s", ow, oh, d.why);
        SDL_ShowCursor(SDL_DISABLE);
    }
    SDL_RendererInfo info;
    SDL_zero(info);
    if(SDL_GetRendererInfo(renderer_, &info) == 0)
        vsync_ = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    std::printf("video: driver %s, renderer %s, vsync %s\n", SDL_GetCurrentVideoDriver(),
                info.name ? info.name : "(unknown)", vsync_ ? "on" : "off (60 Hz cap)");
    logVideoInfo(log_, logTime(), window_, renderer_, stdout);
    return true;
}

void App::exitActions()
{
    if(exitActionsDone_)
        return;
    exitActionsDone_ = true;
    if(!opts_.removeOnExit.empty())
    {
        if(std::remove(opts_.removeOnExit.c_str()) == 0)
            log_.line(logTime(), "remove-on-exit: removed %s", opts_.removeOnExit.c_str());
        else
            log_.line(logTime(), "remove-on-exit: %s: %s", opts_.removeOnExit.c_str(), std::strerror(errno));
    }
    if(!opts_.restoreOnExit.empty())
    {
        logGovernors("at exit, before restore-on-exit");
        for(const RestoreAction& r : opts_.restoreOnExit)
        {
            std::string msg;
            writeRestore(r, msg);
            log_.line(logTime(), "%s", msg.c_str());
            std::printf("audreybrick: %s\n", msg.c_str()); // also in stdout.log
        }
        std::fflush(stdout);
        logGovernors("after restore-on-exit");
    }
    else
    {
        logGovernors("at exit");
    }
}

void App::logGovernors(const char* when)
{
    if(cpuFreq_.available())
        log_.line(logTime(), "cpu governor %s: %s", when, cpuFreq_.governors().c_str());
}

void App::drainInputLog()
{
    try
    {
        inputLog_.clear();
        inputCtl_->takeLog(inputLog_);
    }
    catch(...)
    {
        // Out of memory: these log lines are lost, nothing else.
    }
    for(const std::string& l : inputLog_)
        log_.line(logTime(), "%s", l.c_str());
}

void App::videoFailure(double t, bool fatal, const char* fmt, ...)
{
    char         buf[512];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    // Fatal: the app stops. Otherwise one attempt of several failed (the next is tried).
    const char* label = fatal ? "VIDEO FAILURE" : "video attempt failed (trying the next)";
    log_.line(t, "%s: %s", label, buf);
    std::fprintf(stderr, "audreybrick: %s: %s\n", label, buf);
    std::fflush(stderr);
}

bool App::startUp(std::string& error)
{
    if(!opts_.audioDriver.empty())
        SDL_setenv("SDL_AUDIODRIVER", opts_.audioDriver.c_str(), 1);
    if(opts_.fullscreen)
    {
        // The device: one fullscreen window that should never lose focus; if it
        // does, controller events still arrive. Never minimise on focus loss.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    }

    // Video facts first (SDL_VIDEODRIVER, the drivers compiled in, /dev/fb0), also
    // on stdout, so they are in the log and stdout.log even if video fails.
    logVideoEnvironment(log_, logTime(), stdout);

    // Video separately from the rest, so that a video failure is reported as one,
    // and so that --no-video (self-test only) runs audio, events and controllers
    // without it. A video failure is fatal: there is no automatic fallback.
    // The app's own stop-signal handlers stay in place (app/Signals.h); SDL must
    // not install its own.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if(stopAt("before SDL_Init", error))
        return false;
    if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
    {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    sdlInitialised_ = true;
    counterFreq_    = static_cast<double>(SDL_GetPerformanceFrequency());
    logSdlInfo(log_, logTime());

    if(opts_.noVideo)
    {
        log_.line(logTime(), "video: none (--no-video): no window, renderer or drawing in this self-test");
        std::printf("video: none (--no-video)\n");
    }
    else
    {
        if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        {
            error = std::string("SDL_Init(SDL_INIT_VIDEO) failed: ") + SDL_GetError();
            videoFailure(logTime(), true, "%s", error.c_str());
            return false;
        }
        if(stopAt("after SDL_Init", error))
            return false;
        if(!createWindowAndRenderer(error))
            return false;
    }
    if(stopAt("after the video set-up", error))
        return false;

    // The instrument. All allocation happens here, before audio is unpaused.
    voice_ = std::make_unique<AudreyVoice>();
    voice_->init();
    source_ = std::make_unique<ButtonControlSource>();
    model_  = std::make_unique<ControlModel>(*source_);
    audio_  = std::make_unique<AudioRender>(*voice_, *source_, AudioRender::FlushToZero::Enable, &performanceCounter,
                                           counterFreq_);
    // M2b-A: the input stage, prepared with the render buffers when the device opens.
    stage_ = std::make_unique<InputStage>();
    audio_->attachInput(stage_.get());
    inputCtl_ = std::make_unique<InputController>(*stage_, opts_.inputDir);
    log_.line(logTime(), "input: source OFF at start; folder %s",
              opts_.inputDir.empty() ? "none (no --input-dir)" : opts_.inputDir.c_str());
    inputCtl_->onTabEntered(); // a first scan, for the log
    inputCtl_->logMemory("at start-up"); // M2b-A2: where /proc/meminfo exists
    drainInputLog();
    host_   = std::make_unique<AudioHost>(*audio_);
    if(!host_->open(opts_.bufferFrames, error))
        return false;
    if(stopAt("after opening the audio device", error))
        return false;

    const ObtainedAudioSpec& a = host_->obtained();
    std::printf("audio: driver %s, device \"%s\", %d Hz, %d channels, float32, buffer %d frames (requested %d), "
                "render capacity %zu frames\n",
                a.driver.c_str(), a.device.c_str(), a.sampleRate, a.channels, a.bufferFrames, opts_.bufferFrames,
                audio_->capacity());
    log_.line(logTime(),
              "audio: driver %s, device \"%s\", %d Hz, %d channels, float32, buffer %d frames obtained (%d requested, "
              "%.2f ms), render capacity %zu frames",
              a.driver.c_str(), a.device.c_str(), a.sampleRate, a.channels, a.bufferFrames, opts_.bufferFrames,
              1000.0 * a.bufferFrames / (a.sampleRate > 0 ? a.sampleRate : 48000), audio_->capacity());
    snap_.audio.opened       = true;
    snap_.audio.sampleRate   = a.sampleRate;
    snap_.audio.bufferFrames = a.bufferFrames;
    snap_.audio.channels     = a.channels;
    copyString(snap_.audio.driver, sizeof snap_.audio.driver, a.driver.c_str());
    copyString(snap_.audio.device, sizeof snap_.audio.device, a.device.c_str());
    snap_.flushToZeroSupported = kFlushToZeroSupported;

    logAudioDevices(log_, logTime());
    logJoysticks(log_, logTime());
    input_.setSwapAB(opts_.swapAB);
    input_.setSwapXY(opts_.swapXY);
    if(input_.openFirstController())
        std::printf("controller: %s\n", input_.controllerName().c_str());
    else
        std::printf("controller: none (keyboard only)\n");
    log_.line(logTime(), "active controller at start: %s",
              input_.controllerConnected() ? input_.controllerName().c_str() : "none (keyboard only)");
    std::printf("%s", keyboardHelpText().c_str());
    std::fflush(stdout);
    return !stopAt("after the controllers", error);
}

bool App::stopAt(const char* stage, std::string& error)
{
    if(!stopRequested())
        return false;
    stoppedInStartUp_ = true;
    error = std::string("stopped by ") + stopSignalName(stopSignal()) + " during start-up (" + stage + ")";
    return true;
}

void App::shutDown() noexcept
{
    // 1. Audio first: after close() the callback no longer runs.
    if(host_)
        host_->close();
    host_.reset();
    // 2. The input controller (joins the loader thread), then the instrument; the
    //    stage last (it frees the buffers it still holds; the render points at it).
    if(inputCtl_)
        drainInputLog();
    inputCtl_.reset();
    audio_.reset();
    stage_.reset();
    model_.reset();
    source_.reset();
    voice_.reset();
    // 3. Input, window, SDL.
    input_.closeController();
    ui_.release(); // the glyph atlas belongs to renderer_
    if(renderer_ != nullptr)
        SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if(window_ != nullptr)
        SDL_DestroyWindow(window_);
    window_ = nullptr;
    if(sdlInitialised_)
        SDL_Quit();
    sdlInitialised_ = false;
}

void App::fillSnapshot(double t)
{
    fillControlSnapshot(snap_, *model_, *source_);
    snap_.timeSeconds = t;
    ++snap_.frameIndex;

    snap_.peakL = audio_->peakL().take();
    snap_.peakR = audio_->peakR().take();
    peakSessionMax_[0] = std::max(peakSessionMax_[0], snap_.peakL);
    peakSessionMax_[1] = std::max(peakSessionMax_[1], snap_.peakR);
    statsPeak_[0]      = std::max(statsPeak_[0], snap_.peakL);
    statsPeak_[1]      = std::max(statsPeak_[1], snap_.peakR);

    snap_.cpuLoad    = audio_->cpu().smoothed();
    const float cmax = audio_->cpu().takeMax();
    cpuWindowMax_    = std::max(cpuWindowMax_, cmax);
    cpuSessionMax_   = std::max(cpuSessionMax_, cmax);
    statsCpuMax_     = std::max(statsCpuMax_, cmax);
    if(t >= 1.0)
        cpuMaxAfterFirstSecond_ = std::max(cpuMaxAfterFirstSecond_, cmax);
    if(t - cpuWindowStart_ >= kCpuWindowSeconds)
    {
        snap_.cpuMax    = cpuWindowMax_;
        cpuWindowMax_   = 0.0f;
        cpuWindowStart_ = t;
    }

    const AudioRender::FtzState ftz = audio_->ftzState();
    snap_.flushToZeroKnown          = ftz != AudioRender::FtzState::NotYetKnown;
    snap_.flushToZero               = ftz == AudioRender::FtzState::On;

    snap_.controllerConnected = input_.controllerConnected();
    copyString(snap_.controllerName, sizeof snap_.controllerName, input_.controllerName().c_str());

    // M2b-A: the input.
    inputCtl_->fill(snap_.input);
    snap_.input.peak     = stage_->peak().take();
    inputPeakSessionMax_ = std::max(inputPeakSessionMax_, snap_.input.peak);
    statsInputPeak_      = std::max(statsInputPeak_, snap_.input.peak);

}

void App::render(const UiSnapshot& s)
{
    // M2-C: the screen (ui::Renderer clears and draws; the glyph atlas is created
    // on the first call).
    ui_.draw(renderer_, s);
    SDL_RenderPresent(renderer_);
    fps_.frame();
    logAtlasState();
}

void App::logAtlasState()
{
    if(ui_.atlasFailures() != atlasFailuresLogged_)
    {
        atlasFailuresLogged_ = ui_.atlasFailures();
        log_.line(logTime(), "glyph atlas: creation failed (%u so far): %s; the screen is drawn without text",
                  atlasFailuresLogged_, ui_.lastAtlasError());
    }
    if(ui_.atlasBuilds() != atlasBuildsLogged_)
    {
        atlasBuildsLogged_ = ui_.atlasBuilds();
        log_.line(logTime(), "glyph atlas: built (%u so far)", atlasBuildsLogged_);
    }
}

void App::logStats(double wall, const char* label)
{
    PeriodicStats st;
    st.windowSeconds   = wall - statsStart_;
    st.cpuSmoothed     = audio_->cpu().smoothed();
    st.cpuMax          = statsCpuMax_;
    st.largestCallback = audio_->largestCallback();
    const std::uint64_t cb = audio_->callbacks();
    const std::uint64_t fr = audio_->framesRendered();
    st.callbacks       = cb - statsCallbacks_;
    const std::uint64_t ov = audio_->overruns();
    st.overruns        = ov - statsOverruns_;
    st.audioFrames     = fr - statsAudioFrames_;
    st.sampleRate      = AudreyVoice::kSampleRate;
    st.haveHistogram   = true;
    std::uint64_t buckets[kStatsLoadBuckets];
    for(std::size_t i = 0; i < kStatsLoadBuckets; ++i)
    {
        buckets[i]          = audio_->cpu().bucketCount(i);
        st.loadHistogram[i] = buckets[i] - statsBuckets_[i];
    }
    char freq[512] = "";
    if(cpuFreq_.available())
    {
        cpuFreq_.formatAndReset(freq, sizeof freq);
        st.cpuFreq = freq;
    }
    st.peakL           = statsPeak_[0];
    st.peakR           = statsPeak_[1];
    st.dropped         = model_->commandsDropped();
    st.framesDrawn     = fps_.frames();
    st.keysSuppressed  = events_.takeKeysSuppressed();
    st.inputSource     = inputCtl_->source() != InputSource::Off ? inputSourceName(inputCtl_->source()) : nullptr;
    st.inputPeak       = statsInputPeak_;
    char buf[1200];
    formatStats(buf, sizeof buf, st);
    log_.line(logTime(), "%s%s", label, buf);

    statsStart_       = wall;
    statsCpuMax_      = 0.0f;
    statsPeak_[0]     = 0.0f;
    statsPeak_[1]     = 0.0f;
    statsInputPeak_   = 0.0f;
    statsCallbacks_   = cb;
    statsOverruns_    = ov;
    statsAudioFrames_ = fr;
    for(std::size_t i = 0; i < kStatsLoadBuckets; ++i)
        statsBuckets_[i] = buckets[i];
    fps_.take(st.windowSeconds);
}

int App::run()
{
    runStart_  = steadySeconds();
    parentPid_ = static_cast<long>(getppid());
    // --exit-with-parent first, before anything slow (M3-B review, fix 4): if the
    // parent has already gone, or the app was re-parented to init, stop cleanly.
    bool parentGoneAtStart = false;
    int  pdeathErrno       = 0;
    bool pdeathSet         = false;
    if(opts_.exitWithParent)
    {
#if defined(__linux__)
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == 0)
            pdeathSet = true;
        else
            pdeathErrno = errno;
#endif
        parentGoneAtStart = parentPid_ <= 1 || static_cast<long>(getppid()) != parentPid_;
    }
    if(!opts_.logPath.empty())
    {
        std::string err;
        if(!log_.open(opts_.logPath, err))
            std::fprintf(stderr, "audreybrick: log: %s (continuing without a log)\n", err.c_str());
    }
    {
        char              when[64] = "?";
        const std::time_t now      = std::time(nullptr);
        std::tm           tmv{};
        if(localtime_r(&now, &tmv) != nullptr)
            std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S %z", &tmv);
        char cwd[512] = "?";
        if(getcwd(cwd, sizeof cwd) == nullptr)
            copyString(cwd, sizeof cwd, "(unknown)");
        log_.line(0.0, "AudreyBrick %s, built %s, %s", buildVersion(), buildDate(), buildCompiler());
        log_.line(0.0, "log started %s (times in this log: seconds since start), working directory %s", when, cwd);
        log_.line(0.0, "options: buffer %d frames requested, fullscreen %s, video %s, audio driver %s, self-test %s",
                  opts_.bufferFrames, opts_.fullscreen ? "yes" : "no", opts_.noVideo ? "none (--no-video)" : "yes",
                  opts_.audioDriver.empty() ? "(SDL's choice)" : opts_.audioDriver.c_str(),
                  opts_.selftest() ? opts_.selftestScript.c_str() : "no");
        if(opts_.swapAB)
            log_.line(0.0, "input: A/B swap ON (--swap-ab): controller button 'a' acts as B and 'b' as A");
        else
            log_.line(0.0, "input: A/B swap off (SDL's naming: controller 'a' is A, 'b' is B)");
        if(opts_.swapXY)
            log_.line(0.0, "input: X/Y swap ON (--swap-xy): controller button 'x' acts as Y and 'y' as X");
        else
            log_.line(0.0, "input: X/Y swap off (SDL's naming: controller 'x' is X, 'y' is Y)");
        log_.line(0.0, "input: the other buttons and the keyboard are never swapped");
        for(const RestoreAction& r : opts_.restoreOnExit)
            log_.line(0.0, "restore-on-exit requested: '%s' -> %s if it still holds '%s'", r.content.c_str(),
                      r.file.c_str(), r.expected.c_str());
        errno            = 0;
        const int niceV  = getpriority(PRIO_PROCESS, 0);
        log_.line(0.0, "process: pid %ld, parent %ld, nice %d", static_cast<long>(getpid()), parentPid_,
                  errno == 0 ? niceV : 0);
    }
    {
        // CPU frequency (M3-B). AUDREY_SYSFS_CPU is a testing aid only.
        const char*       env  = std::getenv(kSysfsCpuEnv);
        const std::string base = (env != nullptr && env[0] != '\0') ? env : kSysfsCpuDefault;
        if(env != nullptr && env[0] != '\0')
            log_.line(logTime(), "cpu frequency: sysfs base %s (from %s, a testing aid)", base.c_str(), kSysfsCpuEnv);
        if(cpuFreq_.discover(base) == 0)
        {
            log_.line(logTime(), "cpu frequency: not available (no cpufreq policy under %s)", base.c_str());
        }
        else
        {
            std::string names;
            for(std::size_t i = 0; i < cpuFreq_.count(); ++i)
                names += std::string(i == 0 ? "" : ", ") + cpuFreq_.directory(i);
            log_.line(logTime(), "cpu frequency: %zu policy(ies): %s; sampled about every %.0f s on the UI thread",
                      cpuFreq_.count(), names.c_str(), kFreqSampleSeconds);
            logGovernors("at start");
            cpuFreq_.sample();
        }
    }
    if(opts_.exitWithParent)
    {
#if defined(__linux__)
        if(pdeathSet)
            log_.line(logTime(), "exit-with-parent: SIGTERM is delivered if the parent process (%ld) dies", parentPid_);
        else
            log_.line(logTime(), "exit-with-parent: prctl failed (%s)", std::strerror(pdeathErrno));
#else
        (void)pdeathSet;
        (void)pdeathErrno;
        log_.line(logTime(), "exit-with-parent: not supported on this system (only the start-up check is done)");
#endif
        if(parentGoneAtStart || (opts_.exitWithParent && static_cast<long>(getppid()) != parentPid_))
        {
            exitReason_ = "the launcher (parent process) was already gone at start";
            return stopEarly("at start");
        }
    }
    if(stopRequested())
        return stopEarly("at start");

    if(opts_.selftest())
    {
        std::string err;
        if(!loadButtonScriptFile(opts_.selftestScript, script_, err))
        {
            std::fprintf(stderr, "audreybrick: self-test script %s: %s\n", opts_.selftestScript.c_str(), err.c_str());
            log_.line(logTime(), "self-test script %s: %s", opts_.selftestScript.c_str(), err.c_str());
            log_.line(logTime(), "exit: bad self-test script, exit code 2");
            exitActions();
            return 2;
        }
        std::printf("selftest: script %s, %zu events, %.3f s\n", opts_.selftestScript.c_str(), script_.size(),
                    opts_.selftestSeconds);
    }
    if(stopRequested())
        return stopEarly("after loading the self-test script");

    std::string error;
    if(!startUp(error))
    {
        if(stoppedInStartUp_)
            return stopEarly(error.c_str());
        std::fprintf(stderr, "audreybrick: %s\n", error.c_str());
        log_.line(logTime(), "start-up failed: %s", error.c_str());
        log_.line(logTime(), "exit: start-up failure, exit code 1");
        exitActions();
        shutDown();
        return 1;
    }

    // Show the window before audio starts: on macOS the first presents take a few
    // hundred milliseconds (measured about 350 ms with the Metal renderer), which
    // would otherwise delay the first input events.
    for(int i = 0; i < kWarmUpFrames && !opts_.noVideo; ++i)
    {
        SDL_PumpEvents();
        render(snap_);
    }

    if(stopRequested())
        return stopEarly("before starting audio");
    t0_ = SDL_GetPerformanceCounter();
    host_->start();
    log_.line(logTime(), "audio started; statistics every %.0f s", kStatsSeconds);
    statsStart_ = 0.0;

    const bool   selftest     = opts_.selftest();
    const double n            = opts_.selftestSeconds;
    const double timeout      = std::max(3.0 * n, n + 10.0);
    bool         timedOut     = false;
    bool         draining     = false;
    std::uint64_t drainTarget = 0;
    double       lastPresent  = -1.0;

    InputRouter router(*model_, quit_, selftest, inputCtl_.get());
    bool        running = true;
    while(running)
    {
        const double frameStart = now();
        // The model's clock: wall-clock time normally; in self-test mode the audio
        // clock, so scripted hold durations are measured on the same timeline as
        // the audio-side ramps (see App.h).
        const double t = selftest ? audioSeconds() : frameStart;

        if(running && stopRequested())
        {
            exitReason_ = std::string(stopSignalName(stopSignal())) +
                          " (a stop signal: from the launcher, the system, or --exit-with-parent)";
            if(static_cast<long>(getppid()) != parentPid_)
                exitReason_ += " - the launcher (parent process) is gone";
            running = false;
        }

        SDL_Event e;
        while(running && SDL_PollEvent(&e))
        {
            ButtonEvent                 be{};
            const InputMap::Action action = input_.handle(e, be);
            events_.onEvent(e, action, be, input_, logTime());
            if(e.type == SDL_RENDER_TARGETS_RESET || e.type == SDL_RENDER_DEVICE_RESET)
                ui_.onRenderReset(); // the atlas is rebuilt on the next draw
            // M3-C: the model, MenuQuit and self-test filtering in one place
            // (app/InputRouter.h), the same code the router test drives.
            const InputRouter::Result routed = router.route(action, be, t);
            if(routed == InputRouter::Result::Quit)
            {
                if(running)
                {
                    char desc[256];
                    describeEvent(desc, sizeof desc, e);
                    exitReason_ = std::string(quitReason(e)) + " [" + desc + "]";
                    if(e.type == SDL_QUIT && static_cast<long>(getppid()) != parentPid_)
                        exitReason_ += " - the launcher (parent process) is gone";
                }
                running = false;
            }
            else if(routed == InputRouter::Result::MenuStart && running)
            {
                running     = false;
                exitReason_ = "Menu + Start";
            }
        }

        if(selftest)
        {
            while(nextEvent_ < script_.size() && script_[nextEvent_].timeSeconds <= t &&
                  script_[nextEvent_].timeSeconds <= n)
            {
                const TimedButtonEvent& ev = script_[nextEvent_++];
                maxEventLateness_          = std::max(maxEventLateness_, t - ev.timeSeconds);
                std::printf("selftest: t=%.3f s (audio) %s %s (script %.3f s)\n", t, buttonName(ev.event.button),
                            ev.event.pressed ? "down" : "up", ev.timeSeconds);
                router.deliver(ev.event, t); // the model and the INPUT tab's panel (M2b-A)
            }
        }

        model_->tick(t);
        inputCtl_->tick(t); // M2b-A: gain ramp, loader poll, buffers to free
        drainInputLog();

        if(selftest)
        {
            // n seconds of audio reached: let the audio thread render two more
            // buffers so the commands and held word sent last are applied.
            if(!draining && t >= n)
            {
                draining    = true;
                drainTarget = audio_->framesRendered() + 2u * static_cast<std::uint64_t>(host_->obtained().bufferFrames);
            }
            if(draining && audio_->framesRendered() >= drainTarget && running)
            {
                running     = false;
                exitReason_ = "self-test reached its length";
            }
            if(frameStart >= timeout && running)
            {
                timedOut    = true;
                running     = false;
                exitReason_ = "self-test timed out";
            }
        }

        // Present at most 60 times a second in self-test mode; every iteration
        // otherwise (paced below).
        if(!selftest || frameStart - lastPresent >= FramePacer::kFrameSecondsNoVsync)
        {
            fillSnapshot(t);
            if(!opts_.noVideo)
                render(snap_);
            lastPresent = frameStart;
        }
        if(cpuFreq_.available() && frameStart - lastFreqSample_ >= kFreqSampleSeconds)
        {
            cpuFreq_.sample(); // a few small sysfs reads, UI thread only
            lastFreqSample_ = frameStart;
        }
        if(frameStart - statsStart_ >= kStatsSeconds)
            logStats(frameStart, "");

        const double elapsed = now() - frameStart;
        longestFrame_        = std::max(longestFrame_, elapsed);
        if(selftest)
        {
            SDL_Delay(1);
        }
        else
        {
            const int ms = FramePacer::delayMilliseconds(elapsed, vsync_);
            if(ms > 0)
                SDL_Delay(static_cast<Uint32>(ms));
        }
    }

    const double wall = now();
    logStats(wall, "final (partial window) ");
    // Shutdown step 1 happens here so the report reads the final, settled values.
    host_->close();
    const int code = selftest ? finishSelftest(wall, timedOut) : 0;
    log_.line(logTime(), "exit: %s, exit code %d", exitReason_.c_str(), code);
    shutDown();
    exitActions();
    log_.line(logTime(), "shut down cleanly");
    log_.close();
    return code;
}

int App::stopEarly(const char* stage)
{
    if(exitReason_ == "unknown")
    {
        exitReason_ = std::string(stopSignalName(stopSignal())) + " during start-up";
        if(static_cast<long>(getppid()) != parentPid_)
            exitReason_ += " - the launcher (parent process) is gone";
    }
    const int code = opts_.selftest() ? 3 : 0;
    log_.line(logTime(), "stopping before the main loop (%s): %s", stage, exitReason_.c_str());
    std::printf("audreybrick: stopped during start-up (%s): %s\n", stage, exitReason_.c_str());
    std::fflush(stdout);
    if(host_)
        host_->close();
    log_.line(logTime(), "exit: %s, exit code %d", exitReason_.c_str(), code);
    shutDown();
    exitActions();
    log_.line(logTime(), "shut down cleanly");
    log_.close();
    return code;
}

namespace
{
bool finite(double v) noexcept
{
    return std::isfinite(v);
}
} // namespace

int App::finishSelftest(double wallSeconds, bool timedOut)
{
    int problems = 0;
    auto fail    = [&](const std::string& msg) {
        std::printf("selftest: FAIL: %s\n", msg.c_str());
        ++problems;
    };

    std::printf("selftest: delivered %zu of %zu events, latest %.1f ms after its script time; longest loop "
                "iteration %.1f ms\n",
                nextEvent_, script_.size(), 1000.0 * maxEventLateness_, 1000.0 * longestFrame_);
    std::printf("final knob values (normalised, engine):\n");
    const bool sw       = source_->publishedSwitch();
    bool       allFinite = true;
    for(std::size_t k = 0; k < kKnobCount; ++k)
    {
        const Knob       knob = static_cast<Knob>(k);
        const ParamSpec& p    = paramSpec(knob);
        const float      v    = source_->publishedKnob(knob);
        const float      eng  = knobToEngine(knob, v, sw);
        allFinite             = allFinite && finite(v) && finite(eng);
        std::printf("  %-18s %.6f  %12.4f %s\n", p.id, static_cast<double>(v), static_cast<double>(eng), p.unit);
    }
    const float fb    = source_->publishedStoredFeedback();
    const float fbEng = knobToEngine(Knob::FeedbackGain, fb, false);
    allFinite         = allFinite && finite(fb) && finite(fbEng);
    std::printf("stored feedback: %.6f (%.3f dB), maximum active: %s\n", static_cast<double>(fb),
                static_cast<double>(fbEng), source_->publishedFeedbackMax() ? "yes" : "no");
    std::printf("switch: %s (latch %s)\n", sw ? "on" : "off", source_->publishedSwitchLatch() ? "on" : "off");
    const float lastMax = audio_->cpu().takeMax();
    const float cpuMax  = std::max(cpuSessionMax_, lastMax);
    const float cpuSm   = audio_->cpu().smoothed();
    allFinite           = allFinite && finite(cpuMax) && finite(cpuSm);
    std::printf("callback load: max %.2f %% of the buffer time (after the first second %.2f %%), smoothed at end %.2f %%\n",
                100.0 * static_cast<double>(cpuMax), 100.0 * static_cast<double>(std::max(cpuMaxAfterFirstSecond_, lastMax)),
                100.0 * static_cast<double>(cpuSm));
    const float peak[2] = {std::max(peakSessionMax_[0], audio_->peakL().take()),
                           std::max(peakSessionMax_[1], audio_->peakR().take())};
    allFinite           = allFinite && finite(peak[0]) && finite(peak[1]);
    std::printf("output peak: L %.4f  R %.4f\n", static_cast<double>(peak[0]), static_cast<double>(peak[1]));
    const ObtainedAudioSpec& a = host_->obtained();
    std::printf("audio spec: driver %s, device \"%s\", %d Hz, %d channels, float32, buffer %d frames\n",
                a.driver.c_str(), a.device.c_str(), a.sampleRate, a.channels, a.bufferFrames);
    const double audioSecs = audioSeconds();
    std::printf("callbacks: %llu, largest %llu frames; rendered %.3f s of audio in %.3f s wall clock (ratio %.4f)\n",
                static_cast<unsigned long long>(audio_->callbacks()),
                static_cast<unsigned long long>(audio_->largestCallback()), audioSecs, wallSeconds,
                wallSeconds > 0.0 ? audioSecs / wallSeconds : 0.0);
    const AudioRender::FtzState ftz = audio_->ftzState();
    std::printf("flush-to-zero on the audio thread: %s%s\n",
                ftz == AudioRender::FtzState::On ? "on" : (ftz == AudioRender::FtzState::Off ? "off" : "unknown"),
                kFlushToZeroSupported ? "" : " (not supported on this architecture)");
    std::printf("commands: sent %llu, dropped %llu, processed %llu\n",
                static_cast<unsigned long long>(model_->commandsSent()),
                static_cast<unsigned long long>(model_->commandsDropped()),
                static_cast<unsigned long long>(source_->commandsProcessed()));

    // M2b-A: the input.
    const float inPeak = std::max(inputPeakSessionMax_, stage_->peak().take());
    allFinite          = allFinite && finite(inPeak);
    {
        const InputController& in = *inputCtl_;
        std::printf("input: source %s, gain %.3f, folder %s, %zu file(s) listed, cursor %d, mode %s, state %s, loaded %s",
                    inputSourceName(in.source()), static_cast<double>(in.gainNorm()),
                    in.folder().empty() ? "(none)" : in.folder().c_str(), in.files().size(), in.cursor(),
                    inputPlayModeName(in.playMode()), inputLoadStateName(in.state()),
                    in.loaded() ? in.loadedInfo().name.c_str() : "(nothing)");
        if(in.loaded())
            std::printf(" (%.3f s at 48 kHz)", in.loadedInfo().seconds);
        if(in.state() == InputLoadState::Failed)
            std::printf(", last failure: %s", in.reason());
        std::printf("; input peak max %.4f (after gain)\n", static_cast<double>(inPeak));
    }

    // --- Checks ------------------------------------------------------------------
    const double n = opts_.selftestSeconds;
    char         buf[256];
    if(timedOut)
    {
        std::snprintf(buf, sizeof buf,
                      "timed out after %.3f s wall clock with %.3f s of audio rendered (needed %.3f s)", wallSeconds,
                      audioSecs, n);
        fail(buf);
    }
    std::size_t due = 0;
    for(const TimedButtonEvent& ev : script_)
        if(ev.timeSeconds <= n)
            ++due;
    if(nextEvent_ < due)
    {
        const TimedButtonEvent& ev = script_[nextEvent_];
        std::snprintf(buf, sizeof buf, "%zu of %zu scripted events at or before %.3f s were not delivered (first: line %d, %.3f s %s %s)",
                      due - nextEvent_, due, n, ev.line, ev.timeSeconds, buttonName(ev.event.button),
                      ev.event.pressed ? "down" : "up");
        fail(buf);
    }
    if(script_.size() > due)
        std::printf("selftest: note: %zu scripted event(s) after %.3f s were not played\n", script_.size() - due, n);
    if(!allFinite)
        fail("a reported value or peak is not finite");
    for(const KnobExpectation& ex : opts_.expectKnobs)
    {
        const double actual = ex.knob == Knob::FeedbackGain ? fb : source_->publishedKnob(ex.knob);
        const bool   ok     = actual >= ex.min && actual <= ex.max;
        std::snprintf(buf, sizeof buf, "%s%s: expected %.6f .. %.6f, actual %.6f", paramSpec(ex.knob).id,
                      ex.knob == Knob::FeedbackGain ? " (stored)" : "", ex.min, ex.max, actual);
        if(ok)
            std::printf("selftest: ok: %s\n", buf);
        else
            fail(buf);
    }
    for(int c = 0; c < 2; ++c)
    {
        const char* ch = c == 0 ? "L" : "R";
        if(opts_.hasPeakMin)
        {
            std::snprintf(buf, sizeof buf, "output peak %s: expected above %.6f, actual %.6f", ch, opts_.peakMin,
                          static_cast<double>(peak[c]));
            if(peak[c] > opts_.peakMin)
                std::printf("selftest: ok: %s\n", buf);
            else
                fail(buf);
        }
        if(opts_.hasPeakMax)
        {
            std::snprintf(buf, sizeof buf, "output peak %s: expected below %.6f, actual %.6f", ch, opts_.peakMax,
                          static_cast<double>(peak[c]));
            if(peak[c] < opts_.peakMax)
                std::printf("selftest: ok: %s\n", buf);
            else
                fail(buf);
        }
    }
    if(opts_.hasInputPeakMin)
    {
        std::snprintf(buf, sizeof buf, "input peak: expected above %.6f, actual %.6f", opts_.inputPeakMin,
                      static_cast<double>(inPeak));
        if(inPeak > opts_.inputPeakMin)
            std::printf("selftest: ok: %s\n", buf);
        else
            fail(buf);
    }
    if(problems == 0)
        std::printf("selftest: PASSED\n");
    else
        std::printf("selftest: FAILED (%d problem(s))\n", problems);
    log_.line(logTime(), "self-test: %s (%d problem(s)); delivered %zu of %zu events; peak L %.4f R %.4f; callback load max %.2f %%",
              problems == 0 ? "PASSED" : "FAILED", problems, nextEvent_, script_.size(), static_cast<double>(peak[0]),
              static_cast<double>(peak[1]), 100.0 * static_cast<double>(cpuMax));
    std::fflush(stdout);
    return problems == 0 ? 0 : 3;
}

} // namespace audreybrick
