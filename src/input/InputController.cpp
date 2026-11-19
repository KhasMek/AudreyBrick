// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/InputController.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "control/ControlTuning.h"

namespace audreybrick
{
namespace
{
bool isDpad(Button b) noexcept
{
    return b == Button::DpadUp || b == Button::DpadDown || b == Button::DpadLeft || b == Button::DpadRight;
}

Button opposite(Button b) noexcept
{
    switch(b)
    {
        case Button::DpadLeft: return Button::DpadRight;
        case Button::DpadRight: return Button::DpadLeft;
        case Button::DpadDown: return Button::DpadUp;
        default: return Button::DpadDown; // DpadUp
    }
}

// Ramp distance after `tau` seconds of ramping (rate rising linearly from the
// start rate to the end rate over the acceleration time, then constant), in
// travel at the normal (not fine) speed. ButtonControlSource's d-pad ramp.
double rampDistance(double tau) noexcept
{
    if(!(tau > 0.0))
        return 0.0;
    constexpr double s = tuning::kRampStartRate;
    constexpr double e = tuning::kRampEndRate;
    constexpr double a = tuning::kRampAccelSeconds;
    if(tau <= a)
        return s * tau + (e - s) * tau * tau / (2.0 * a);
    return s * a + (e - s) * a / 2.0 + e * (tau - a);
}

void copyText(char* dst, std::size_t size, const char* src) noexcept
{
    if(size == 0)
        return;
    std::size_t n = 0;
    for(; src != nullptr && src[n] != '\0' && n + 1 < size; ++n)
        dst[n] = src[n];
    dst[n] = '\0';
}
} // namespace

InputController::InputController(InputStage& stage, std::string folder, ScanFn scan, InputLoader::ThreadStart start)
: stage_(stage), folder_(std::move(folder)), scan_(scan != nullptr ? scan : &scanInputFolder), loader_(start)
{
    stage_.setSource(InputSource::Off);
    stage_.setGainNorm(gain_);
}

InputController::~InputController()
{
    stop();
}

void InputController::stop() noexcept
{
    loader_.stop();
    closeDevice("shutting down");
}

void InputController::setLive(LiveInput* live, LiveBackend* backend) noexcept
{
    live_    = live;
    backend_ = backend;
    if(backend_ != nullptr)
        logf("input: live capture through %s", backend_->kind());
    else
        log("input: no capture system: LIVE lists SYSTEM DEFAULT only and cannot open it");
    devicesListed_ = false; // the next listing is logged
    listDevices();
}

InputController::ListRef InputController::activeList() noexcept
{
    if(source_ == InputSource::Live)
        return ListRef{devices_, devCursor_, devCursorName_};
    return ListRef{files_, cursor_, cursorName_};
}

void InputController::log(const std::string& line) noexcept
{
    try
    {
        log_.push_back(line);
    }
    catch(...)
    {
        // Out of memory: the line is lost, nothing else.
    }
}

void InputController::log(const char* line) noexcept
{
    try
    {
        log_.emplace_back(line);
    }
    catch(...)
    {
    }
}

void InputController::setReason(const char* r) noexcept
{
    copyText(reason_, sizeof reason_, r);
}

void InputController::failSafe(const char* reason) noexcept
{
    pendingId_ = 0;
    failed_    = true;
    setReason(reason);
    char buf[96];
    std::snprintf(buf, sizeof buf, "input: failure handled at the INPUT panel's boundary: %s", reason);
    log(buf);
}

void InputController::takeLog(std::vector<std::string>& out)
{
    for(std::string& l : log_)
        out.push_back(std::move(l));
    log_.clear();
}

void InputController::freeBuffer(std::unique_ptr<InputBuffer> b) noexcept
{
    if(b)
    {
        b.reset();
        ++freed_;
    }
}

void InputController::setGain(float n) noexcept
{
    gain_ = clampInputNorm(n);
    stage_.setGainNorm(gain_);
}

void InputController::makeDpadInert() noexcept
{
    for(Button b : {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight})
        if(press_[idx(b)].down)
        {
            press_[idx(b)].inert  = true;
            press_[idx(b)].cursor = false;
        }
}

InputLoadState InputController::state() const noexcept
{
    if(pendingId_ != 0 || wanted_)
        return InputLoadState::Loading;
    if(failed_)
        return InputLoadState::Failed;
    if(source_ == InputSource::File && scanned_ && files_.empty())
        return InputLoadState::NoFiles;
    return loaded_ ? InputLoadState::Ready : InputLoadState::Idle;
}

const std::string& InputController::shownName() const noexcept
{
    if(wanted_)
        return wantedName_;
    if(pendingId_ != 0)
        return pendingName_;
    if(failed_)
        return failedName_;
    return loadedName_; // "" if nothing is loaded
}

void InputController::logMemory(const char* when) noexcept
{
    std::uint64_t kb = 0;
    if(readMemAvailableKb(kb, memInfoPath_.c_str()))
        logf("input: MemAvailable %llu kB (%s)", static_cast<unsigned long long>(kb), when != nullptr ? when : "");
}

void InputController::releaseAll() noexcept
{
    for(Press& p : press_)
        p = Press{};
    cancelChoice(); // focus lost or controller gone: nothing is chosen
}

int InputController::indexOf(const std::string& name) const noexcept
{
    if(name.empty())
        return -1;
    for(std::size_t i = 0; i < files_.size(); ++i)
        if(files_[i] == name)
            return static_cast<int>(i);
    return -1;
}

void InputController::onButton(const ButtonEvent& e, double t, bool onInputTab) noexcept
{
    try
    {
        handleButton(e, t, onInputTab);
    }
    catch(...)
    {
        failSafe("OUT OF MEMORY");
    }
}

void InputController::handleButton(const ButtonEvent& e, double t, bool onInputTab)
{
    const std::size_t i = idx(e.button);
    if(i >= kButtonCount)
        return;
    Press& p = press_[i];
    if(!e.pressed)
    {
        if(!p.down)
            return;
        p = Press{};
        if(e.button == Button::X)
        {
            makeDpadInert();
            if(choosing_)
                endChoice();
        }
        return;
    }
    if(p.down)
        return; // repeated down
    p.down  = true;
    p.inert = false;
    p.time  = t;

    if(e.button == Button::L1 || e.button == Button::R1)
    {
        makeDpadInert();
        cancelChoice(); // a tab change while X is held: nothing is chosen
        return;
    }
    if(e.button == Button::X)
    {
        makeDpadInert();
        if(onInputTab && source_ == InputSource::File)
            beginChoice();
        else if(onInputTab && source_ == InputSource::Live)
            beginDeviceChoice(); // M2b-B
        return;
    }
    if(choosing_ && onInputTab && e.button == Button::Y)
    {
        if(source_ == InputSource::File)
            toggleMode(); // X + Y (not a d-pad press); nothing with the source LIVE
        return;
    }
    if(choosing_ && onInputTab && e.button == Button::Select)
    {
        if(source_ == InputSource::File)
            playAgain(); // X + Select (not a d-pad press); nothing with the source LIVE
        return;
    }
    if(!isDpad(e.button))
        return;
    if(!onInputTab)
    {
        p.inert = true; // a press made on another tab never acts here
        return;
    }
    const bool leftRight = e.button == Button::DpadLeft || e.button == Button::DpadRight;
    const int  dir       = (e.button == Button::DpadRight || e.button == Button::DpadUp) ? +1 : -1;
    if(choosing_)
    {
        // Holding X: the d-pad chooses (also with Y or Select held).
        p.inert = true; // never the gain
        if(down(opposite(e.button)))
            return; // both directions down: no movement
        if(leftRight)
        {
            choiceMoved_ = true;
            moveCursor(dir); // right: the next file; one step per press
            return;
        }
        p.cursor  = true; // held while the pop-up is open: repeats (tick)
        p.repeats = 0;
        if(!popup_)
        {
            // The first up/down opens the list, the cursor stays. It does not
            // count as a choice (X, up, release only looks), except when
            // nothing is loaded (or the loaded file is not listed): then the
            // file under the cursor is the obvious choice (M2b-A3 review, M2).
            popup_ = true;
            if(source_ == InputSource::Live)
            {
                if(!devOpen_ || devOpenIndex_ < 0)
                    choiceMoved_ = true; // nothing open: X, up, release opens the cursor's device
                logf("input: device list opened (cursor %d of %zu)", devCursor_ + 1, devices_.size());
                return;
            }
            if(!loaded_ || loadedIndex_ < 0)
                choiceMoved_ = true;
            logf("input: file list opened (cursor %d of %zu)", cursor_ + 1, files_.size());
            return;
        }
        choiceMoved_ = true;
        moveCursor(-dir); // up: towards the first file
        return;
    }
    if(down(Button::X))
    {
        p.inert = true; // X held but no choice (source OFF, or X pressed elsewhere): nothing
        return;
    }
    if(down(Button::Y))
    {
        p.inert = true; // a reset press never ramps
        if(!leftRight)
            setGain(kInputGainUnityNorm);
        return;
    }
    if(down(opposite(e.button)))
    {
        p.inert = true; // both directions down: no movement
        return;
    }
    if(leftRight)
    {
        p.inert = true; // left/right never repeat
        const int s = static_cast<int>(source_) + dir;
        if(s >= 0 && s < kInputSourceCount)
            setSource(static_cast<InputSource>(s));
        return;
    }
    const bool fine = down(Button::Select);
    setGain(gain_ + static_cast<float>(dir * (fine ? tuning::kTapStepFine : tuning::kTapStep)));
}

void InputController::beginChoice()
{
    choosing_    = true;
    popup_       = false;
    choiceMoved_ = false;
    if(files_.empty())
        return;
    int at = loadedIndex_ >= 0 ? loadedIndex_ : (cursor_ >= 0 ? cursor_ : 0);
    if(at >= static_cast<int>(files_.size()))
        at = 0;
    std::string name = files_[static_cast<std::size_t>(at)]; // may throw: nothing has changed yet
    cursor_          = at;
    cursorName_.swap(name);
}

void InputController::cancelChoice() noexcept
{
    if(choosing_ && popup_)
        log(source_ == InputSource::Live ? "input: device list closed, choice cancelled"
                                         : "input: file list closed, choice cancelled");
    choosing_    = false;
    popup_       = false;
    choiceMoved_ = false;
}

void InputController::endChoice()
{
    if(source_ == InputSource::Live)
    {
        endDeviceChoice(); // M2b-B
        return;
    }
    const bool moved  = choiceMoved_;
    const bool wasPop = popup_;
    choosing_         = false;
    popup_            = false;
    choiceMoved_      = false;
    if(wasPop)
        log("input: file list closed");
    if(!moved || files_.empty() || cursor_ < 0)
        return; // a hold without a d-pad press never loads or restarts
    const auto at      = static_cast<std::size_t>(cursor_);
    const bool changed = at < stamps_.size() && stamps_[at] != loadedInfo_.stamp;
    if(loaded_ && files_[at] == loadedName_ && !changed)
    {
        // The loaded file: the last choice wins, so a load asked for before
        // (pending or waiting) is cancelled; and an ended ONCE file plays again.
        if(pendingId_ != 0 || wanted_)
        {
            if(pendingId_ != 0)
                loader_.cancel();
            logf("input: back to '%s': the load of '%s' is cancelled", loadedName_.c_str(),
                 wanted_ ? wantedName_.c_str() : pendingName_.c_str());
            pendingId_ = 0;
            pendingName_.clear();
            wanted_ = false;
            wantedName_.clear();
        }
        if(loadedEnded())
            playAgain();
        return;
    }
    requestLoad(at);
}

bool InputController::loadedEnded() const noexcept
{
    return loaded_ && offered_ && stage_.switches() >= expectSwitch_ && stage_.ended();
}

void InputController::tick(double t) noexcept
{
    try
    {
        handleTick(t);
    }
    catch(...)
    {
        failSafe("OUT OF MEMORY");
    }
}

void InputController::handleTick(double t)
{
    const double prev = ticked_ ? lastTick_ : t;
    ticked_           = true;
    lastTick_         = t;

    // 1. The gain ramp (up/down held past the hold delay, exactly one of them).
    const Press& up = press_[idx(Button::DpadUp)];
    const Press& dn = press_[idx(Button::DpadDown)];
    if(up.down != dn.down)
    {
        const Press& d   = up.down ? up : dn;
        const int    dir = up.down ? +1 : -1;
        if(!d.inert && t > prev)
        {
            const double start = d.time + tuning::kHoldDelaySeconds - tuning::kTimeEpsilon;
            const double tau1  = t - start;
            const double tau0  = prev - start;
            if(tau1 > 0.0)
            {
                double dist = rampDistance(tau1) - rampDistance(tau0);
                if(down(Button::Select))
                    dist *= tuning::kFineRampScale;
                setGain(gain_ + static_cast<float>(dir * dist));
            }
        }
    }

    // 2. The cursor repeat (X held, the pop-up open, up or down held: exactly one).
    if(up.down != dn.down && choosing_ && popup_)
    {
        Press&    d   = up.down ? press_[idx(Button::DpadUp)] : press_[idx(Button::DpadDown)];
        const int dir = up.down ? -1 : +1;
        if(d.cursor)
        {
            const double start = d.time + tuning::kHoldDelaySeconds - tuning::kTimeEpsilon;
            if(t >= start)
            {
                const auto due = static_cast<std::size_t>(std::floor((t - start) * kInputCursorRepeatHz + 1e-9)) + 1;
                if(due > d.repeats)
                {
                    moveCursor(dir * static_cast<long>(due - d.repeats));
                    d.repeats = due;
                }
            }
        }
    }

    // 2b. Both up and down held: no movement, and each held press's repeat
    //     count is kept current, so no backlog is applied when one is released.
    if(up.down && dn.down && choosing_ && popup_)
    {
        for(Button bt : {Button::DpadUp, Button::DpadDown})
        {
            Press& d = press_[idx(bt)];
            if(!d.cursor)
                continue;
            const double start = d.time + tuning::kHoldDelaySeconds - tuning::kTimeEpsilon;
            if(t >= start)
            {
                const auto due = static_cast<std::size_t>(std::floor((t - start) * kInputCursorRepeatHz + 1e-9)) + 1;
                d.repeats      = due > d.repeats ? due : d.repeats;
            }
        }
    }

    // 3. One poll of the loader.
    InputLoadResult r;
    if(loader_.poll(r))
        handleResult(r);

    // 4. Buffers the stage has finished with; and (5.) a load that waited for
    //    the stage to settle (S2). settledAndDrained() reads the stage's state
    //    first and frees what it retired after, so a load never starts while a
    //    retired buffer is still alive (M2b-A3 review, M3).
    const bool settled = settledAndDrained();
    if(wanted_ && settled)
    {
        std::string name;
        name.swap(wantedName_);
        wanted_ = false;
        startLoad(std::move(name));
    }

    // 6. M2b-B: the live device (lost, stalled) and the device list (changed).
    checkLive(t);

    stage_.setGainNorm(gain_);
}

void InputController::logf(const char* fmt, ...) noexcept
{
    char         buf[768];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    log(buf);
}

void InputController::setSource(InputSource s)
{
    if(s == source_)
        return;
    logf("input: source %s -> %s", inputSourceName(source_), inputSourceName(s));
    const InputSource was = source_;
    source_               = s;
    if(was == InputSource::Live)
    {
        closeDevice("the source left LIVE"); // the mic is never open unseen
        liveStatus_ = LiveStatus::None;
    }
    // The stage follows source_ on every exit path, also if listing or
    // reopening throws (out of memory; M2b-B review 2, M2).
    try
    {
        if(s == InputSource::File)
            selectFileSource();
        if(s == InputSource::Live)
            selectLiveSource(); // may reopen the device chosen before (gain to 0 first)
    }
    catch(...)
    {
        stage_.setSource(s);
        throw;
    }
    stage_.setSource(s);
}

void InputController::toggleMode() noexcept
{
    const InputPlayMode m = mode_ == InputPlayMode::Loop ? InputPlayMode::Once : InputPlayMode::Loop;
    logf("input: play mode %s -> %s", inputPlayModeName(mode_), inputPlayModeName(m));
    mode_ = m;
    stage_.setPlayMode(m);
}

void InputController::moveCursor(long delta)
{
    ListRef l = activeList(); // the files, or the devices with the source LIVE
    if(l.names.empty())
        return;
    const long n  = static_cast<long>(l.names.size());
    const long at = l.cursor >= 0 ? l.cursor : 0;
    const long to = ((at + delta) % n + n) % n;
    std::string name = l.names[static_cast<std::size_t>(to)]; // may throw: nothing has changed yet
    l.cursor         = static_cast<int>(to);
    l.cursorName.swap(name);
}

void InputController::playAgain()
{
    if(!loaded_)
        return;
    // Play the loaded file again from the start; no reload, and a load asked
    // for since is cancelled (S1).
    if(pendingId_ != 0)
        loader_.cancel();
    pendingId_ = 0;
    pendingName_.clear();
    wanted_ = false;
    wantedName_.clear();
    failed_ = false;
    setReason("");
    stage_.restart();
    logf("input: restart '%s' (%s)", loadedName_.c_str(), inputPlayModeName(mode_));
}

void InputController::rescan() noexcept
{
    const char* where = folder_.empty() ? "(none given; --input-dir)" : folder_.c_str();
    scanned_          = true;
    try
    {
        InputFolderScan scan = scan_(folder_);
        folderOk_            = scan.ok;
        folderError_         = scan.error != nullptr ? scan.error : "";
        filesFound_          = scan.found;
        files_.swap(scan.names);
        stamps_.swap(scan.stamps);
        // The cursor: the same name if still listed, else the nearest position.
        const int byName = indexOf(cursorName_);
        if(files_.empty())
            cursor_ = -1;
        else if(byName >= 0)
            cursor_ = byName;
        else
            cursor_ = cursor_ < 0 ? 0 : std::min(cursor_, static_cast<int>(files_.size()) - 1);
        if(cursor_ >= 0 && files_[static_cast<std::size_t>(cursor_)] != cursorName_)
        {
            std::string name = files_[static_cast<std::size_t>(cursor_)];
            cursorName_.swap(name);
        }
        else if(cursor_ < 0)
        {
            cursorName_.clear();
        }
        loadedIndex_ = loaded_ ? indexOf(loadedName_) : -1;
        if(failed_ && indexOf(failedName_) < 0)
        {
            failed_ = false; // the failed file is gone: its message too
            setReason("");
        }
        // Every scan is logged with its time and entry count (the device run tells
        // whether a scan on the UI thread is quick enough on the SD card).
        if(!folderOk_)
            logf("input: scan of %s: %s (%.2f ms)", where, folderError_, 1000.0 * scan.seconds);
        else
            logf("input: scan of %s: %zu entries, %zu WAV file(s)%s, %zu other entr%s ignored, %.2f ms", where,
                 filesFound_ + scan.ignored, filesFound_,
                 filesFound_ > files_.size() ? " (only the first 256 by name are listed)" : "", scan.ignored,
                 scan.ignored == 1 ? "y" : "ies", 1000.0 * scan.seconds);
    }
    catch(...)
    {
        // The scan itself failed (out of memory, or a test's scanner): no list.
        files_.clear();
        stamps_.clear();
        filesFound_  = 0;
        folderOk_    = false;
        folderError_ = "CANNOT READ FOLDER";
        cursor_      = -1;
        loadedIndex_ = -1;
        logf("input: scan of %s failed with an exception: CANNOT READ FOLDER", where);
    }
}

void InputController::onTabEntered() noexcept
{
    rescan();
    listDevices(); // M2b-B
}

void InputController::selectFileSource()
{
    // A rescan, nothing else: no automatic load (M2b-A2). A loaded file plays
    // from its start (the stage starts it from 0 when the source turns FILE).
    rescan();
}

bool InputController::settledAndDrained() noexcept
{
    // Order matters: first whether the stage has settled (acquire), then the
    // retire queue. The stage pushes a retired buffer before it publishes the
    // settled count, so when settled is seen here, every buffer it retired is
    // already in the queue and is freed below, before any load starts.
    const bool busy = stageBusy();
    while(auto b = stage_.reclaim())
        freeBuffer(std::move(b));
    return !busy;
}

bool InputController::stageBusy() const noexcept
{
    // Settled first (acquire), then the switch count: equal means the stage was
    // seen holding only its current buffer after its latest switch.
    const std::uint64_t settled = stage_.settledSwitches();
    const std::uint64_t target  = offered_ ? expectSwitch_ : stage_.switches();
    return settled < target;
}

void InputController::requestLoad(std::size_t index)
{
    if(index >= files_.size())
        return;
    std::string name = files_[index]; // may throw: nothing has changed yet
    failed_          = false;
    setReason("");
    if(!settledAndDrained())
    {
        // S2: wait until the stage holds only its playing buffer. A load in
        // progress is cancelled now (the last choice wins).
        if(pendingId_ != 0)
            loader_.cancel();
        pendingId_ = 0;
        pendingName_.clear();
        wanted_ = true;
        wantedName_.swap(name);
        logf("input: loading '%s' once the stage has settled (a crossfade is running)", wantedName_.c_str());
        return;
    }
    wanted_ = false;
    wantedName_.clear();
    startLoad(std::move(name));
}

void InputController::startLoad(std::string name)
{
    const std::uint64_t id = loader_.request(folder_, name);
    failed_                = false;
    setReason("");
    if(id == 0)
    {
        pendingId_ = 0;
        pendingName_.clear();
        failed_ = true;
        failedName_.swap(name);
        setReason("CANNOT START LOADER");
        logf("input: load of '%s' could not be started (the loader thread would not start, or out of memory)",
             failedName_.c_str());
        return;
    }
    pendingId_ = id;
    pendingName_.swap(name);
    logf("input: loading '%s'%s", pendingName_.c_str(), loaded_ ? " (the loaded file keeps playing)" : "");
}

void InputController::handleResult(InputLoadResult& r)
{
    if(pendingId_ == 0 || r.id != pendingId_)
        return; // superseded (freed with r)
    pendingId_ = 0;
    if(!r.ok)
    {
        std::string name = pendingName_;
        failedName_.swap(name);
        pendingName_.clear();
        failed_ = true;
        setReason(r.reason.c_str());
        logf("input: load failed: '%s': %s (%s); %s", r.name.c_str(), r.reason.c_str(), r.detail.c_str(),
             loaded_ ? "the loaded file keeps playing" : "nothing was loaded");
        logMemory("after a failed load");
        return;
    }
    // The name first (it may throw), then nothing that can.
    std::string loadedName = r.file.info.name;
    expectSwitch_          = stage_.switches() + 1; // no other offer is outstanding (S2): this one is next
    offered_               = true;
    freeBuffer(stage_.offer(std::move(r.file.buffer)));
    loaded_ = true;
    loadedName_.swap(loadedName);
    loadedIndex_ = indexOf(loadedName_);
    loadedInfo_  = std::move(r.file.info);
    pendingName_.clear();
    failed_ = false;
    setReason("");
    const InputFileInfo& f = loadedInfo_;
    char                 conv[64];
    if(f.converted)
        std::snprintf(conv, sizeof conv, "converted (%d-tap windowed sinc)", f.resamplerTaps);
    else
        std::snprintf(conv, sizeof conv, "not converted");
    char fmt[48] = "?";
    try
    {
        std::snprintf(fmt, sizeof fmt, "%s", inputFormatName(f.formatTag, f.bitsPerSample).c_str());
    }
    catch(...)
    {
    }
    logf("input: loaded '%s': %u channel(s), %u Hz, %s, %llu frames in the file; used %llu (%.3f s) -> %zu frames at "
         "48000 Hz (%.3f s, %.1f MB), %s, %s%s, %.1f ms; %s",
         f.name.c_str(), f.channels, f.sampleRate, fmt, static_cast<unsigned long long>(f.fileFrames),
         static_cast<unsigned long long>(f.usedFrames),
         static_cast<double>(f.usedFrames) / (f.sampleRate > 0 ? f.sampleRate : 1), f.frames, f.seconds,
         static_cast<double>(f.frames) * sizeof(float) / 1.0e6, conv, f.cut ? "CUT to 10 minutes" : "not cut",
         f.endedEarly ? ", data ended before the header said" : "", 1000.0 * f.loadSeconds, inputPlayModeName(mode_));
    logMemory("after a load");
}

// --- M2b-B: the live input ------------------------------------------------------------------
int InputController::indexOfDevice(const std::string& name) const noexcept
{
    for(std::size_t i = 1; i < devices_.size(); ++i)
        if(devices_[i] == name)
            return static_cast<int>(i);
    return -1;
}

void InputController::listDevices() noexcept
{
    try
    {
        std::vector<std::string> names;
        bool                     ok = true;
        if(backend_ != nullptr)
        {
            listSeen_ = backend_->listChanges(); // before listing: a change during it is seen next time
            ok        = backend_->listDevices(names);
        }
        std::vector<std::string> list;
        list.reserve(names.size() + 1);
        list.emplace_back(kLiveDefaultDeviceName);
        for(std::string& n : names)
            if(list.size() < kLiveMaxDevices)
                list.push_back(std::move(n));
        const bool changed = !devicesListed_ || list != devices_ || ok != devicesOk_;
        devices_.swap(list);
        devicesListed_ = true;
        devicesOk_     = ok;
        // The cursor: the same name if still listed, else the nearest position.
        int at = -1;
        for(std::size_t i = 0; i < devices_.size(); ++i)
            if(devices_[i] == devCursorName_)
                at = static_cast<int>(i);
        if(at < 0)
            at = devCursor_ < 0 ? 0 : std::min(devCursor_, static_cast<int>(devices_.size()) - 1);
        devCursor_ = at;
        if(devices_[static_cast<std::size_t>(at)] != devCursorName_)
        {
            std::string n = devices_[static_cast<std::size_t>(at)];
            devCursorName_.swap(n);
        }
        devOpenIndex_ = !devOpen_ ? -1 : (devOpenDefault_ ? 0 : indexOfDevice(devOpenName_));
        if(changed && backend_ != nullptr)
        {
            std::string line = "input: capture devices";
            if(!ok)
                line += " (the capture system cannot list them)";
            line += ": " + std::to_string(devices_.size() - 1) + " listed besides SYSTEM DEFAULT";
            for(std::size_t i = 1; i < devices_.size(); ++i)
                line += "; [" + std::to_string(i) + "] '" + devices_[i] + "'";
            log(line);
        }
    }
    catch(...)
    {
        // Out of memory: the list stays as it was (the cursor is valid in it).
        log("input: listing the capture devices failed (out of memory)");
    }
}

void InputController::selectLiveSource()
{
    listDevices();
    if(!devChosen_)
        return; // nothing chosen yet in this run: nothing opens by itself
    const int at = devChosenDefault_ ? 0 : indexOfDevice(devChosenName_);
    if(at < 0)
    {
        logf("input: the device chosen before, '%s', is no longer listed: nothing reopened", devChosenName_.c_str());
        devChosen_ = false;
        return;
    }
    logf("input: LIVE selected again: reopening '%s'", devices_[static_cast<std::size_t>(at)].c_str());
    openDevice(at);
}

void InputController::beginDeviceChoice()
{
    listDevices(); // X pressed: list again
    choosing_    = true;
    popup_       = false;
    choiceMoved_ = false;
    if(devices_.empty())
        return;
    int at = devOpen_ && devOpenIndex_ >= 0 ? devOpenIndex_ : (devCursor_ >= 0 ? devCursor_ : 0);
    if(at >= static_cast<int>(devices_.size()))
        at = 0;
    std::string name = devices_[static_cast<std::size_t>(at)]; // may throw: nothing has changed yet
    devCursor_       = at;
    devCursorName_.swap(name);
}

void InputController::endDeviceChoice()
{
    const bool moved  = choiceMoved_;
    const bool wasPop = popup_;
    choosing_         = false;
    popup_            = false;
    choiceMoved_      = false;
    if(wasPop)
        log("input: device list closed");
    if(!moved || devices_.empty() || devCursor_ < 0)
        return; // a hold without a d-pad press never opens anything
    if(devOpen_ && devCursor_ == devOpenIndex_)
        return; // the open device: nothing
    openDevice(devCursor_);
}

void InputController::closeDevice(const char* reason) noexcept
{
    if(!devOpen_)
        return;
    const auto t0 = std::chrono::steady_clock::now();
    if(backend_ != nullptr)
        backend_->close();
    if(live_ != nullptr)
        live_->newSession(); // the stage stops reading and fades out on its last sample
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    logf("input: capture device '%s' closed (%s), %.1f ms", devOpenName_.c_str(), reason, ms);
    devOpen_      = false;
    devOpenIndex_ = -1;
}

void InputController::openDevice(int index)
{
    if(index < 0 || static_cast<std::size_t>(index) >= devices_.size())
        return;
    const bool def = index == 0;
    // Everything that may throw first (copies), before anything changes; after
    // the open only swaps, moves and fixed-size formatting (M2b-B review, M4).
    std::string    name   = devices_[static_cast<std::size_t>(index)];
    std::string    open   = name;
    std::string    chosen = name;
    LiveOpenResult r;
    r.askedName = def ? std::string() : name;
    r.error.reserve(64);
    char asked[160];
    if(def)
        std::snprintf(asked, sizeof asked, "the system default (NULL)");
    else
        std::snprintf(asked, sizeof asked, "'%.150s'", name.c_str());
    // The howl safeguard, in this order: the gain to zero, then the old device
    // closed (which advances the ring's session), then a new session, then the
    // open. So the gain is zero before any session the stage can see.
    if(gain_ > 0.0f)
        logf("input: gain %.3f -> 0 (-inf dB) before opening a capture device", static_cast<double>(gain_));
    setGain(0.0f);
    closeDevice("another device chosen");
    if(live_ != nullptr)
        live_->newSession();
    ++opens_;
    bool ok = false;
    if(backend_ == nullptr || live_ == nullptr)
        r.error = "NO CAPTURE SUPPORT";
    else
        ok = backend_->open(def ? nullptr : name.c_str(), r);
    if(ok)
    {
        devOpen_          = true;
        devOpenDefault_   = def;
        devOpenIndex_     = index;
        devChosen_        = true;
        devChosenDefault_ = def;
        devOpenName_.swap(open);
        devChosenName_.swap(chosen);
        liveStatus_ = LiveStatus::None;
        setReasonLive("");
        lastCaptured_ = live_->framesCaptured();
        stallClock_   = true;
        raisesSeen_   = stage_.liveTargetRaises();
        logf("input: capture device opened: '%s' (asked for %s; asked %d Hz %s %d ch %d frames; obtained %d Hz %s %d ch "
             "%d frames) in %.1f ms",
             devOpenName_.c_str(), asked, r.askedRate, r.askedFormat.c_str(), r.askedChannels, r.askedFrames, r.rate,
             r.format.c_str(), r.channels, r.frames, 1000.0 * r.seconds);
    }
    else
    {
        devChosen_  = false;
        liveStatus_ = LiveStatus::Failed;
        liveFailName_.swap(name);
        setReasonLive(r.error.c_str());
        logf("input: capture device '%s' could not be opened (asked for %s): %s, %.1f ms", liveFailName_.c_str(), asked,
             r.error.c_str(), 1000.0 * r.seconds);
    }
    lastOpen_ = std::move(r);
}

void InputController::deviceLost(const char* why) noexcept
{
    logf("input: DEVICE LOST: '%s' (%s)", devOpenName_.c_str(), why);
    try
    {
        liveFailName_ = devOpenName_;
    }
    catch(...)
    {
        liveFailName_.clear();
    }
    closeDevice("lost");
    devChosen_  = false;
    liveStatus_ = LiveStatus::Lost;
    listDevices();
}

void InputController::checkLive(double t) noexcept
{
    if(backend_ != nullptr && backend_->listChanges() != listSeen_)
        listDevices(); // SDL_AUDIODEVICEADDED / REMOVED
    // The stage doubled its target after two underruns within a second.
    // The count first (acquire; the stage stores the target, then increments it
    // with release), then the target: at least the raised one (M2b-B review 2, M3).
    const std::uint64_t raises = stage_.liveTargetRaises();
    if(raises != raisesSeen_)
    {
        raisesSeen_          = raises;
        const std::size_t to = stage_.liveTargetFrames();
        logf("input: live target raised to %zu frames (two underruns within a second; %llu raise(s) since start)", to,
             static_cast<unsigned long long>(raises));
    }
    if(!devOpen_)
        return;
    if(backend_ != nullptr && backend_->lost())
    {
        deviceLost("the capture system reports it removed or stopped");
        return;
    }
    if(live_ == nullptr)
        return;
    const std::uint64_t c = live_->framesCaptured();
    if(stallClock_ || c != lastCaptured_)
    {
        stallClock_   = false;
        lastCaptured_ = c;
        lastCaptureT_ = t;
    }
    else if(t - lastCaptureT_ > kLiveStallSeconds)
    {
        char why[64];
        std::snprintf(why, sizeof why, "no audio from it for %.0f s", kLiveStallSeconds);
        deviceLost(why);
    }
}

void InputController::setReasonLive(const char* r) noexcept
{
    copyText(liveReason_, sizeof liveReason_, r);
}

void InputController::fill(UiInputView& v) const noexcept
{
    v.source        = source_;
    v.gainNorm      = gain_;
    v.gainDb        = inputGainDb(gain_);
    v.fileCount     = static_cast<int>(files_.size());
    v.fileListCut   = filesFound_ > files_.size();
    v.folderMissing = scanned_ && !folderOk_;
    copyText(v.folderError, sizeof v.folderError, scanned_ && !folderOk_ ? folderError_ : "");
    const std::string& shown = shownName();
    copyDisplayName(v.fileName, sizeof v.fileName, shown.c_str());
    const bool idle = pendingId_ == 0 && !wanted_ && !failed_;
    v.fileIndex     = shown == loadedName_ && idle ? loadedIndex_ : indexOf(shown);
    v.loaded        = loaded_;
    v.shownIsLoaded = loaded_ && idle;
    copyDisplayName(v.playingName, sizeof v.playingName, loaded_ ? loadedName_.c_str() : "");
    v.loadedSeconds    = static_cast<float>(loadedInfo_.seconds);
    v.loadedCut        = loaded_ && loadedInfo_.cut;
    v.loadedEndedEarly = loaded_ && loadedInfo_.endedEarly;
    v.state            = state();
    copyText(v.reason, sizeof v.reason, reason_);

    // The browser window: kInputListRows names with the cursor kept in view
    // (centred where the list allows). M2b-B: the capture devices with the
    // source LIVE.
    const bool                      liveList = source_ == InputSource::Live;
    const std::vector<std::string>& names    = liveList ? devices_ : files_;
    const int                       cur      = liveList ? devCursor_ : cursor_;
    const int n   = static_cast<int>(names.size());
    const int rows = n < kInputListRows ? n : kInputListRows;
    int       first = cur - kInputListRows / 2;
    if(first > n - rows)
        first = n - rows;
    if(first < 0)
        first = 0;
    v.cursor    = cur;
    v.listFirst = first;
    v.listRows  = rows;
    for(int i = 0; i < kInputListRows; ++i)
        copyDisplayName(v.listNames[i], sizeof v.listNames[i],
                        i < rows ? names[static_cast<std::size_t>(first + i)].c_str() : "");
    v.loadedIndex = liveList ? devOpenIndex_ : (loaded_ ? loadedIndex_ : -1);

    // M2b-B: the live input.
    v.deviceCount      = static_cast<int>(devices_.size());
    v.deviceListFailed = !devicesOk_;
    v.deviceOpen       = devOpen_;
    copyDisplayName(v.deviceName, sizeof v.deviceName, devOpen_ ? devOpenName_.c_str() : "");
    v.grantedRate     = devOpen_ ? lastOpen_.rate : 0;
    v.grantedChannels = devOpen_ ? lastOpen_.channels : 0;
    v.grantedFrames   = devOpen_ ? lastOpen_.frames : 0;
    v.liveStatus      = liveStatus_;
    copyDisplayName(v.liveFailName, sizeof v.liveFailName, liveStatus_ != LiveStatus::None ? liveFailName_.c_str() : "");
    copyDisplayName(v.liveReason, sizeof v.liveReason, liveStatus_ == LiveStatus::Failed ? liveReason_ : "");
    v.liveRunning = live_ != nullptr && live_->running();

    // Playback, from the stage's status atomics (for the loaded file only).
    v.playMode = mode_;
    // M1: exact: the stage has switched to the buffer last handed over.
    const bool current = loaded_ && offered_ && stage_.switches() >= expectSwitch_;
    v.ended           = current && stage_.ended();
    v.choosing        = choosing_;
    v.popup           = popup_;
    v.positionSeconds = current ? static_cast<float>(static_cast<double>(stage_.position()) / kInputSampleRate) : 0.0f;
}

} // namespace audreybrick
