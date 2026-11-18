// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/App.h"

#include <SDL.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "core/ParamTable.h"
#include "platform/Denormals.h"

namespace audreybrick
{
namespace
{
constexpr int kLogicalWidth  = 1024;
constexpr int kLogicalHeight = 768;

// Frame pacing: without vsync, at most 60 frames per second. With vsync, a floor of
// 1/75 s per frame still applies, in case presenting does not block (for example a
// hidden or occluded window on macOS).
constexpr double kFrameSecondsNoVsync = 1.0 / 60.0;
constexpr double kFrameSecondsVsync   = 1.0 / 75.0;

// Frames presented before audio starts (see App::run).
constexpr int kWarmUpFrames = 3;

// One-second window for the displayed CPU maximum.
constexpr double kCpuWindowSeconds = 1.0;

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
           "  Return         Start (no function yet)\n"
           "  Escape         quit\n";
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
    if(!o.selftest() && secondsGiven)
    {
        error = "--seconds is only used with --selftest";
        return false;
    }
    if(!o.selftest() && (!o.expectKnobs.empty() || o.hasPeakMin || o.hasPeakMax))
    {
        error = "--expect-knob, --expect-peak-min and --expect-peak-max are only used with --selftest";
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

bool App::startUp(std::string& error)
{
    if(!opts_.audioDriver.empty())
        SDL_setenv("SDL_AUDIODRIVER", opts_.audioDriver.c_str(), 1);

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
    {
        error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    sdlInitialised_ = true;
    counterFreq_    = static_cast<double>(SDL_GetPerformanceFrequency());

    window_ = SDL_CreateWindow("AudreyBrick", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kLogicalWidth,
                               kLogicalHeight, SDL_WINDOW_RESIZABLE);
    if(window_ == nullptr)
    {
        error = std::string("cannot open the window: ") + SDL_GetError();
        return false;
    }
    // Self-test: no vsync, so presenting never blocks the ~1 ms polling loop.
    renderer_ = SDL_CreateRenderer(window_, -1, opts_.selftest() ? 0 : SDL_RENDERER_PRESENTVSYNC);
    if(renderer_ == nullptr)
        renderer_ = SDL_CreateRenderer(window_, -1, 0);
    if(renderer_ == nullptr)
    {
        error = std::string("cannot create a renderer: ") + SDL_GetError();
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, kLogicalWidth, kLogicalHeight);
    SDL_RendererInfo info;
    SDL_zero(info);
    if(SDL_GetRendererInfo(renderer_, &info) == 0)
        vsync_ = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    std::printf("video: driver %s, renderer %s, vsync %s\n", SDL_GetCurrentVideoDriver(),
                info.name ? info.name : "(unknown)", vsync_ ? "on" : "off (60 Hz cap)");

    // The instrument. All allocation happens here, before audio is unpaused.
    voice_ = std::make_unique<AudreyVoice>();
    voice_->init();
    source_ = std::make_unique<ButtonControlSource>();
    model_  = std::make_unique<ControlModel>(*source_);
    audio_  = std::make_unique<AudioRender>(*voice_, *source_, AudioRender::FlushToZero::Enable, &performanceCounter,
                                           counterFreq_);
    host_   = std::make_unique<AudioHost>(*audio_);
    if(!host_->open(opts_.bufferFrames, error))
        return false;

    const ObtainedAudioSpec& a = host_->obtained();
    std::printf("audio: driver %s, device \"%s\", %d Hz, %d channels, float32, buffer %d frames (requested %d), "
                "render capacity %zu frames\n",
                a.driver.c_str(), a.device.c_str(), a.sampleRate, a.channels, a.bufferFrames, opts_.bufferFrames,
                audio_->capacity());
    snap_.audio.opened       = true;
    snap_.audio.sampleRate   = a.sampleRate;
    snap_.audio.bufferFrames = a.bufferFrames;
    snap_.audio.channels     = a.channels;
    copyString(snap_.audio.driver, sizeof snap_.audio.driver, a.driver.c_str());
    copyString(snap_.audio.device, sizeof snap_.audio.device, a.device.c_str());
    snap_.flushToZeroSupported = kFlushToZeroSupported;

    if(input_.openFirstController())
        std::printf("controller: %s\n", input_.controllerName().c_str());
    else
        std::printf("controller: none (keyboard only)\n");
    std::printf("%s", keyboardHelpText().c_str());
    std::fflush(stdout);
    return true;
}

void App::shutDown() noexcept
{
    // 1. Audio first: after close() the callback no longer runs.
    if(host_)
        host_->close();
    host_.reset();
    // 2. The instrument.
    audio_.reset();
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

    snap_.cpuLoad    = audio_->cpu().smoothed();
    const float cmax = audio_->cpu().takeMax();
    cpuWindowMax_    = std::max(cpuWindowMax_, cmax);
    cpuSessionMax_   = std::max(cpuSessionMax_, cmax);
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
}

void App::render(const UiSnapshot& s)
{
    // M2-C: the screen (ui::Renderer clears and draws; the glyph atlas is created
    // on the first call).
    ui_.draw(renderer_, s);
    SDL_RenderPresent(renderer_);
}

int App::run()
{
    if(opts_.selftest())
    {
        std::string err;
        if(!loadButtonScriptFile(opts_.selftestScript, script_, err))
        {
            std::fprintf(stderr, "audreybrick: self-test script %s: %s\n", opts_.selftestScript.c_str(), err.c_str());
            return 2;
        }
        std::printf("selftest: script %s, %zu events, %.3f s\n", opts_.selftestScript.c_str(), script_.size(),
                    opts_.selftestSeconds);
    }

    std::string error;
    if(!startUp(error))
    {
        std::fprintf(stderr, "audreybrick: %s\n", error.c_str());
        shutDown();
        return 1;
    }

    // Show the window before audio starts: on macOS the first presents take a few
    // hundred milliseconds (measured about 350 ms with the Metal renderer), which
    // would otherwise delay the first input events.
    for(int i = 0; i < kWarmUpFrames; ++i)
    {
        SDL_PumpEvents();
        render(snap_);
    }

    t0_ = SDL_GetPerformanceCounter();
    host_->start();

    const bool   selftest     = opts_.selftest();
    const double n            = opts_.selftestSeconds;
    const double timeout      = std::max(3.0 * n, n + 10.0);
    bool         timedOut     = false;
    bool         draining     = false;
    std::uint64_t drainTarget = 0;
    double       lastPresent  = -1.0;

    bool running = true;
    while(running)
    {
        const double frameStart = now();
        // The model's clock: wall-clock time normally; in self-test mode the audio
        // clock, so scripted hold durations are measured on the same timeline as
        // the audio-side ramps (see App.h).
        const double t = selftest ? audioSeconds() : frameStart;

        SDL_Event e;
        while(SDL_PollEvent(&e))
        {
            ButtonEvent                 be{};
            const InputMap::Action action = input_.handle(e, be);
            if(action == InputMap::Action::Quit)
                running = false;
            else if(selftest)
                continue; // self-test: real input is ignored
            else if(action == InputMap::Action::Button)
                model_->onButton(be, t);
            else if(action == InputMap::Action::ReleaseAll)
                model_->releaseAll(t);
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
                model_->onButton(ev.event, t);
            }
        }

        model_->tick(t);

        if(selftest)
        {
            // n seconds of audio reached: let the audio thread render two more
            // buffers so the commands and held word sent last are applied.
            if(!draining && t >= n)
            {
                draining    = true;
                drainTarget = audio_->framesRendered() + 2u * static_cast<std::uint64_t>(host_->obtained().bufferFrames);
            }
            if(draining && audio_->framesRendered() >= drainTarget)
                running = false;
            if(frameStart >= timeout)
            {
                timedOut = true;
                running  = false;
            }
        }

        // Present at most 60 times a second in self-test mode; every iteration
        // otherwise (paced below).
        if(!selftest || frameStart - lastPresent >= kFrameSecondsNoVsync)
        {
            fillSnapshot(t);
            render(snap_);
            lastPresent = frameStart;
        }

        const double elapsed = now() - frameStart;
        longestFrame_        = std::max(longestFrame_, elapsed);
        if(selftest)
        {
            SDL_Delay(1);
        }
        else
        {
            const double minFrame = vsync_ ? kFrameSecondsVsync : kFrameSecondsNoVsync;
            if(elapsed < minFrame)
                SDL_Delay(static_cast<Uint32>((minFrame - elapsed) * 1000.0));
        }
    }

    const double wall = now();
    // Shutdown step 1 happens here so the report reads the final, settled values.
    host_->close();
    const int code = selftest ? finishSelftest(wall, timedOut) : 0;
    shutDown();
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
    std::printf("cpu: max %.2f %% of the buffer time (after the first second %.2f %%), smoothed at end %.2f %%\n",
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
    if(problems == 0)
        std::printf("selftest: PASSED\n");
    else
        std::printf("selftest: FAILED (%d problem(s))\n", problems);
    std::fflush(stdout);
    return problems == 0 ? 0 : 3;
}

} // namespace audreybrick
