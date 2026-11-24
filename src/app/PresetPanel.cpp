// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/PresetPanel.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

#include "control/ControlTuning.h"
#include "input/InputView.h"
#include "preset/PresetState.h"

namespace audreybrick
{
namespace
{
char upper(char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// A file's display name for a message: without ".json", printable ASCII (the
// font's), upper case.
std::string shownName(const std::string& file)
{
    const std::string name = presetDisplayName(file);
    std::string       out(name.size() + 1, '\0');
    copyDisplayName(&out[0], out.size(), name.c_str());
    out.resize(std::char_traits<char>::length(out.c_str()));
    for(char& c : out)
        c = upper(c);
    return out;
}

std::string upperText(const std::string& s)
{
    std::string out = s;
    for(char& c : out)
        c = upper(c);
    return out;
}

void copyText(char* dst, std::size_t size, const char* src) noexcept
{
    if(size == 0)
        return;
    std::size_t i = 0;
    for(; src != nullptr && src[i] != '\0' && i + 1 < size; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

const char* originText(PresetPanel::Origin o) noexcept
{
    switch(o)
    {
        case PresetPanel::Origin::Tab: return "SAVE NEW on the PRESET tab";
        case PresetPanel::Origin::Chord: return "Start + A";
        case PresetPanel::Origin::Hook: return "the app";
    }
    return "?";
}

const char* failPrefix(PresetPanel::Action a) noexcept
{
    switch(a)
    {
        case PresetPanel::Action::Load: return "LOAD FAILED: ";
        case PresetPanel::Action::SaveNew: return "SAVE FAILED: ";
        case PresetPanel::Action::Overwrite: return "OVERWRITE FAILED: ";
        case PresetPanel::Action::Delete: return "DELETE FAILED: ";
        case PresetPanel::Action::None: break;
    }
    return "FAILED: ";
}

const char* actionVerb(PresetPanel::Action a) noexcept
{
    switch(a)
    {
        case PresetPanel::Action::Load: return "load";
        case PresetPanel::Action::SaveNew: return "save";
        case PresetPanel::Action::Overwrite: return "overwrite";
        case PresetPanel::Action::Delete: return "delete";
        case PresetPanel::Action::None: break;
    }
    return "?";
}

constexpr const char* kBusyMessage = "BUSY, TRY AGAIN";

using Clock = std::chrono::steady_clock;

// "; file work 12.3 ms": the store call's duration (item 11 of the PRESET-B review).
std::string fileWork(Clock::time_point start)
{
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    char         buf[48];
    std::snprintf(buf, sizeof buf, "; file work %.1f ms", ms);
    return buf;
}
} // namespace

bool presetHeaderWins(RecordShow record, double recordSince, PresetMessageView preset, double presetSince) noexcept
{
    if(preset == PresetMessageView::None)
        return false;
    switch(record)
    {
        case RecordShow::None:
        case RecordShow::Recording:
        case RecordShow::Saving: return true;
        case RecordShow::Saved:
        case RecordShow::Message: break;
    }
    const bool recordBad = record == RecordShow::Message;
    const bool presetBad = preset == PresetMessageView::Bad;
    if(recordBad != presetBad)
        return presetBad;
    return presetSince > recordSince;
}

PresetPanel::PresetPanel(PresetStore& store, ControlModel& model, const ButtonControlSource& source,
                         ModPanel& mod) noexcept
    : store_(store), model_(model), source_(source), mod_(mod)
{
}

int PresetPanel::itemCount() const noexcept
{
    return static_cast<int>(store_.entries().size()) + 1;
}

std::string PresetPanel::fileAt(int item) const
{
    if(item < 1 || item >= itemCount())
        return std::string();
    return store_.entries()[static_cast<std::size_t>(item - 1)].file;
}

void PresetPanel::log(const std::string& line) noexcept
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

void PresetPanel::takeLog(std::vector<std::string>& out)
{
    for(std::string& l : log_)
        out.push_back(std::move(l));
    log_.clear();
}

void PresetPanel::setMessage(bool bad, const std::string& text, double wall) noexcept
{
    if(!bad && busyHold_)
        return; // BUSY, TRY AGAIN stays: the dropped action has to be done again
    try
    {
        message_ = text;
    }
    catch(...)
    {
        message_.clear();
    }
    messageBad_   = bad;
    messageSince_ = wall;
    messageUntil_ = wall + (bad ? kBadMessageSeconds : kGoodMessageSeconds);
}

void PresetPanel::makeDpadInert() noexcept
{
    for(Button b : {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight})
        if(press_[idx(b)].down)
            press_[idx(b)].inert = true;
}

void PresetPanel::cancelHold() noexcept
{
    if(holdKind_ == PresetHoldView::None)
        return;
    holdKind_ = PresetHoldView::None;
    holdFile_.clear();
    if(press_[idx(Button::DpadRight)].down)
        press_[idx(Button::DpadRight)].inert = true;
}

void PresetPanel::moveCursor(int dir, bool wrap) noexcept
{
    const int n    = itemCount();
    int       next = cursor_ + dir;
    if(next < 0)
        next = wrap ? n - 1 : 0;
    else if(next >= n)
        next = wrap ? 0 : n - 1;
    cursor_ = next;
}

void PresetPanel::queue(Action a, Origin o, const std::string& file) noexcept
{
    if(pending_.action != Action::None)
    {
        ++ignored_;
        busyDrop_ = true; // BUSY, TRY AGAIN at the next tick
        log(std::string("preset: ") + actionVerb(a) + " ignored: the previous " + actionVerb(pending_.action) +
            " is still waiting");
        return;
    }
    try
    {
        pending_.file = file;
    }
    catch(...)
    {
        ++failures_;
        log(std::string("preset: ") + actionVerb(a) + " failed: out of memory");
        return;
    }
    pending_.action      = a;
    pending_.origin      = o;
    pending_.target      = model_.commandsDelivered();
    pending_.deadlineSet = false;
    pending_.deadline    = 0.0;
}

void PresetPanel::requestSaveNew(Origin origin) noexcept
{
    // Start + A during an overwrite / delete hold: the hold is cancelled (the
    // save moves the highlight to the new preset).
    cancelHold();
    queue(Action::SaveNew, origin, std::string());
}

void PresetPanel::onRight(double t) noexcept
{
    const bool xd = down(Button::X);
    const bool yd = down(Button::Y);
    if(xd && yd)
        return; // both modifiers: nothing
    if(xd || yd)
    {
        if((xd && !xArmed_) || (yd && !yArmed_))
            return; // a modifier pressed on another tab does not count
        if(cursor_ == 0)
            return; // X / Y + right on SAVE NEW: nothing
        try
        {
            holdFile_ = fileAt(cursor_);
        }
        catch(...)
        {
            holdFile_.clear();
        }
        if(holdFile_.empty())
            return;
        try
        {
            copyText(holdName_, sizeof holdName_, shownName(holdFile_).c_str());
        }
        catch(...)
        {
            holdName_[0] = '\0';
        }
        holdKind_  = xd ? PresetHoldView::Overwrite : PresetHoldView::Delete;
        holdStart_ = t;
        return;
    }
    if(cursor_ == 0)
    {
        queue(Action::SaveNew, Origin::Tab, std::string());
        return;
    }
    try
    {
        queue(Action::Load, Origin::Tab, fileAt(cursor_));
    }
    catch(...)
    {
        ++failures_;
    }
}

void PresetPanel::onButton(const ButtonEvent& e, double t, bool onPresetTab) noexcept
{
    now_     = t;
    Press& p = press_[idx(e.button)];
    if(e.pressed)
    {
        if(p.down)
            return; // a repeated down
        p.down    = true;
        p.inert   = false;
        p.time    = t;
        p.repeats = 0;
    }
    else
    {
        if(!p.down)
            return;
        p.down  = false;
        p.inert = false;
    }

    switch(e.button)
    {
        case Button::L1:
        case Button::R1:
            if(e.pressed)
            {
                cancelHold();
                makeDpadInert();
                xArmed_ = false;
                yArmed_ = false;
            }
            return;
        case Button::X:
            xArmed_ = e.pressed && onPresetTab;
            cancelHold();
            makeDpadInert();
            return;
        case Button::Y:
            yArmed_ = e.pressed && onPresetTab;
            cancelHold();
            makeDpadInert();
            return;
        case Button::DpadUp:
        case Button::DpadDown:
        {
            if(!e.pressed)
                return;
            if(!onPresetTab)
            {
                p.inert = true;
                return;
            }
            cancelHold();
            const Button other = e.button == Button::DpadUp ? Button::DpadDown : Button::DpadUp;
            if(down(other))
                return; // both held: the second press does nothing
            moveCursor(e.button == Button::DpadUp ? -1 : 1, true);
            return;
        }
        case Button::DpadRight:
            if(!e.pressed)
            {
                cancelHold(); // released early: nothing happens
                return;
            }
            if(!onPresetTab)
            {
                p.inert = true;
                return;
            }
            onRight(t);
            return;
        case Button::DpadLeft:
            if(e.pressed && !onPresetTab)
                p.inert = true;
            return;
        default: return;
    }
}

void PresetPanel::releaseAll() noexcept
{
    cancelHold();
    for(Press& p : press_)
        p = Press{};
    xArmed_ = false;
    yArmed_ = false;
}

void PresetPanel::tick(double t, double wall) noexcept
{
    now_ = t;
    // Up / down repeats (at most one step per tick; the backlog is dropped).
    const bool upDown = down(Button::DpadUp) && down(Button::DpadDown);
    for(Button b : {Button::DpadUp, Button::DpadDown})
    {
        Press& p = press_[idx(b)];
        if(!p.down || p.inert)
            continue;
        const double since = t - p.time - tuning::kHoldDelaySeconds;
        if(since < -tuning::kTimeEpsilon)
            continue;
        // Repeats due by now: the n-th at press + delay + (n - 1) / rate.
        const long due = static_cast<long>(std::floor(since * kCursorRepeatHz + tuning::kTimeEpsilon)) + 1;
        if(due <= p.repeats)
            continue;
        if(!upDown)
            moveCursor(b == Button::DpadUp ? -1 : 1, false);
        p.repeats = due;
    }
    // A hold reaching its time.
    if(holdKind_ != PresetHoldView::None && t - holdStart_ >= kHoldSeconds - tuning::kTimeEpsilon)
    {
        const Action a = holdKind_ == PresetHoldView::Overwrite ? Action::Overwrite : Action::Delete;
        try
        {
            queue(a, Origin::Tab, holdFile_);
        }
        catch(...)
        {
            ++failures_;
        }
        holdKind_ = PresetHoldView::None;
        holdFile_.clear();
        press_[idx(Button::DpadRight)].inert = true; // until released
    }
    const std::uint64_t failedBefore = failures_;
    if(pending_.action != Action::None)
        run(wall);
    if(busyDrop_)
    {
        busyDrop_ = false;
        if(failures_ == failedBefore) // a failure this tick explains itself
        {
            setMessage(true, kBusyMessage, wall);
            busyHold_ = pending_.action != Action::None;
        }
    }
    lastTick_ = t;
    ticked_   = true;
}

void PresetPanel::run(double wall) noexcept
{
    const Action a = pending_.action;
    try
    {
        switch(a)
        {
            case Action::Load: runLoad(wall); break;
            case Action::Delete: runDelete(wall); break;
            case Action::SaveNew:
            case Action::Overwrite:
                if(!pending_.deadlineSet)
                {
                    pending_.deadlineSet = true;
                    pending_.deadline    = wall + kSaveWaitSeconds;
                }
                if(source_.commandsProcessed() < pending_.target)
                {
                    if(wall < pending_.deadline)
                        return; // wait for the audio side (next tick)
                    ++failures_;
                    setMessage(true, std::string(failPrefix(a)) + "AUDIO NOT RUNNING", wall);
                    log(std::string("preset: ") + actionVerb(a) + (a == Action::Overwrite ? " " + pending_.file : "") +
                        " failed: audio not running (the audio side did not apply the last change within " +
                        std::to_string(static_cast<int>(kSaveWaitSeconds * 1000.0)) + " ms; nothing written)");
                    break;
                }
                runSave(wall);
                break;
            case Action::None: break;
        }
    }
    catch(...)
    {
        ++failures_;
        setMessage(true, std::string(failPrefix(a)) + "OUT OF MEMORY", wall);
        log(std::string("preset: ") + actionVerb(a) + " failed: out of memory");
    }
    // A save that completed or failed cancels an overwrite / delete hold in
    // progress: the highlight may have moved to the new preset, and the hold
    // must not go on acting on its old target (right inert until released).
    if(a == Action::SaveNew || a == Action::Overwrite)
        cancelHold();
    busyHold_       = false;
    pending_.action = Action::None;
    pending_.file.clear();
}

void PresetPanel::rescanKeepCursor(const std::string& file, int fallback)
{
    store_.scan();
    const long i = file.empty() ? -1 : store_.indexOf(file);
    if(i >= 0)
    {
        cursor_ = static_cast<int>(i) + 1;
        return;
    }
    const int n = itemCount();
    cursor_     = fallback < 0 ? 0 : (fallback >= n ? n - 1 : fallback);
}

void PresetPanel::runLoad(double wall)
{
    const std::string file = pending_.file;
    PresetData        p;
    const PresetResult r = store_.load(file, p);
    if(!r.ok)
    {
        ++failures_;
        setMessage(true, "LOAD FAILED: " + upperText(r.error.reason), wall);
        log("preset: load " + file + " failed: " + r.error.reason + " (" + r.error.detail + ")");
        rescanKeepCursor(file, cursor_); // it may have gone (renamed or deleted on a computer)
        return;
    }
    if(!applyPreset(p, model_, mod_, now_))
    {
        ++failures_;
        setMessage(true, "LOAD FAILED: TRY AGAIN", wall);
        log("preset: load " + file + " failed: the control queue is full (nothing applied; try again)");
        return;
    }
    marked_ = file;
    ++loads_;
    setMessage(false, "LOADED " + shownName(file), wall);
    log("preset: loaded " + file);
}

void PresetPanel::runSave(double wall)
{
    const Action      a        = pending_.action;
    const std::string atCursor = fileAt(cursor_);
    PresetData        p;
    (void)capturePreset(model_, source_, mod_, p); // applied: commandsProcessed() reached the request's count
    const Clock::time_point start = Clock::now();
    const PresetResult r = a == Action::SaveNew ? store_.saveNew(p) : store_.overwrite(pending_.file, p);
    const std::string   took = fileWork(start);
    if(!r.ok)
    {
        ++failures_;
        setMessage(true, std::string(failPrefix(a)) + upperText(r.error.reason), wall);
        log(std::string("preset: ") + actionVerb(a) + (r.file.empty() ? "" : " " + r.file) + " failed: " +
            r.error.reason + " (" + r.error.detail + ")" + took);
        // The store scanned again; keep the cursor on its file.
        rescanKeepCursor(atCursor, cursor_);
        return;
    }
    marked_     = r.file;
    const long i = store_.indexOf(r.file);
    if(i >= 0)
        cursor_ = static_cast<int>(i) + 1;
    else if(cursor_ >= itemCount())
        cursor_ = itemCount() - 1;
    if(a == Action::SaveNew)
    {
        ++saves_;
        setMessage(false, "SAVED " + shownName(r.file), wall);
        log("preset: saved " + r.file + " (" + originText(pending_.origin) + "); " +
            std::to_string(store_.totalFound()) + " preset(s) in the folder" + took);
    }
    else
    {
        ++overwrites_;
        setMessage(false, "OVERWROTE " + shownName(r.file), wall);
        log("preset: overwrote " + r.file + " (X + right held)" + took);
    }
}

void PresetPanel::runDelete(double wall)
{
    const std::string file = pending_.file;
    const long        at   = store_.indexOf(file);
    const int         item = at >= 0 ? static_cast<int>(at) + 1 : cursor_;
    const Clock::time_point start = Clock::now();
    const PresetResult r   = store_.remove(file);
    const std::string   took = fileWork(start);
    if(!r.ok)
    {
        ++failures_;
        setMessage(true, "DELETE FAILED: " + upperText(r.error.reason), wall);
        log("preset: delete " + file + " failed: " + r.error.reason + " (" + r.error.detail + ")" + took);
        rescanKeepCursor(file, cursor_);
        return;
    }
    if(marked_ == file)
        marked_.clear();
    // The next entry has slid into the deleted one's place; at the end, the
    // previous one (SAVE NEW when none is left).
    const int n = itemCount();
    cursor_     = item < n ? item : n - 1;
    ++deletes_;
    setMessage(false, "DELETED " + shownName(file), wall);
    log("preset: deleted " + file + " (Y + right held); " + std::to_string(store_.totalFound()) +
        " preset(s) left" + took);
}

void PresetPanel::onTabEntered() noexcept
{
    try
    {
        const std::string file = fileAt(cursor_);
        rescanKeepCursor(file, cursor_);
    }
    catch(...)
    {
        log("preset: scan failed: out of memory");
    }
}

void PresetPanel::scanAtStart() noexcept
{
    try
    {
        store_.scan();
        cursor_ = 0;
        std::string line;
        switch(store_.state())
        {
            case PresetFolderState::NotSet:
                line = "preset: no presets folder (no --preset-dir): SAVE NEW and Start + A report it";
                break;
            case PresetFolderState::Ok:
                line = "preset: folder " + store_.dir() + ": " + std::to_string(store_.entries().size()) +
                       " preset(s) listed";
                if(store_.truncated())
                    line += " of " + std::to_string(store_.totalFound()) + " found (the list shows the first " +
                            std::to_string(PresetStore::kMaxListed) + ")";
                line += "; next new preset " + presetFileName(store_.nextNumber());
                if(store_.staleRemoved() > 0)
                    line += "; removed " + std::to_string(store_.staleRemoved()) + " stale temporary file(s)";
                break;
            case PresetFolderState::Missing:
                line = "preset: folder " + store_.dir() + " does not exist yet (saving creates it): " +
                       store_.stateDetail();
                break;
            case PresetFolderState::NotAFolder:
            case PresetFolderState::Unreadable:
                line = "preset: folder " + store_.dir() + " cannot be listed: " + store_.stateDetail();
                break;
        }
        log(line);
    }
    catch(...)
    {
        log("preset: scan failed: out of memory");
    }
}

void PresetPanel::fill(UiPresetView& v, double wall) const noexcept
{
    switch(store_.state())
    {
        case PresetFolderState::NotSet: v.folder = PresetFolderView::NotSet; break;
        case PresetFolderState::Ok: v.folder = PresetFolderView::Ok; break;
        case PresetFolderState::Missing: v.folder = PresetFolderView::Missing; break;
        case PresetFolderState::NotAFolder:
        case PresetFolderState::Unreadable: v.folder = PresetFolderView::Unreadable; break;
    }
    const std::vector<PresetEntry>& entries = store_.entries();
    v.count   = static_cast<int>(entries.size());
    v.total   = static_cast<int>(store_.totalFound());
    const int n = v.count + 1;
    v.cursor  = cursor_ < 0 ? 0 : (cursor_ >= n ? n - 1 : cursor_);
    v.rows    = n < kPresetListRows ? n : kPresetListRows;
    int first = v.cursor - kPresetListRows / 2;
    if(first > n - v.rows)
        first = n - v.rows;
    if(first < 0)
        first = 0;
    v.first = first;
    for(int i = 0; i < kPresetListRows; ++i)
    {
        const int item = first + i;
        if(i < v.rows && item >= 1)
            copyDisplayName(v.names[i], sizeof v.names[i], entries[static_cast<std::size_t>(item - 1)].name.c_str());
        else
            v.names[i][0] = '\0';
    }
    v.marked = -1;
    if(!marked_.empty())
        for(std::size_t i = 0; i < entries.size(); ++i)
            if(entries[i].file == marked_)
            {
                v.marked = static_cast<int>(i) + 1;
                break;
            }
    v.busy = pending_.action == Action::SaveNew || pending_.action == Action::Overwrite;

    v.hold         = holdKind_;
    v.holdProgress = 0.0f;
    v.holdName[0]  = '\0';
    if(holdKind_ != PresetHoldView::None)
    {
        double f = (now_ - holdStart_) / kHoldSeconds;
        f        = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        v.holdProgress = static_cast<float>(f);
        copyText(v.holdName, sizeof v.holdName, holdName_);
    }

    const bool fresh = !message_.empty() && wall < messageUntil_;
    v.message        = fresh ? (messageBad_ ? PresetMessageView::Bad : PresetMessageView::Good) : PresetMessageView::None;
    copyText(v.messageText, sizeof v.messageText, fresh ? message_.c_str() : "");
    v.header      = v.message;
    copyText(v.headerText, sizeof v.headerText, v.messageText);
    v.headerSince = messageSince_;
    v.headerWins  = false; // the app decides (presetHeaderWins)
}

} // namespace audreybrick
