// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputController: the UI-thread model of the INPUT tab (docs/briefs/M2b.md,
// M2b-A and M2b-A2, and docs/CONTROL_SCHEME_PROPOSAL.md section 000). Button
// events and time go in; the source, gain, play mode and restarts go to the
// InputStage through its atomics, file loads go to the InputLoader, finished
// buffers are handed to the stage and old ones freed here. No SDL. Time is
// passed in by the caller (seconds, monotonic).
//
// Buttons (only presses made while the INPUT tab is current act; InputRouter
// sends every button event here after the ControlModel has seen it):
//   - d-pad left / right: the source, OFF <-> FILE, one step per press, no wrap,
//     no repeat while held.
//   - d-pad up / down: the input gain. A press steps it by tuning::kTapStep (1/128
//     of travel; tuning::kTapStepFine with Select held); held longer than
//     tuning::kHoldDelaySeconds it ramps like the other parameters, starting at
//     tuning::kRampStartRate and rising linearly to tuning::kRampEndRate over
//     tuning::kRampAccelSeconds (both times tuning::kFineRampScale with Select).
//     The ramp is integrated here, exactly for any frame rate; the stage's 10 ms
//     gain smoothing removes the 60 Hz steps.
//   - Choosing a file by holding X (M2b-A3; INPUT tab, source FILE only):
//       - pressing X puts the cursor on the loaded file (if it is listed; else
//         it stays where it was, or goes to the first file);
//       - X + left / right: the cursor to the previous / next file, wrapping,
//         one step per press; nothing loads;
//       - X + up / down: the first press opens the pop-up list without moving
//         the cursor and does not count as a choice (X, up, release only
//         looks), except when nothing is loaded or the loaded file is not
//         listed (then X, up, release loads the file under the cursor); while
//         it is open up / down move the cursor (up towards the first file),
//         wrapping; with both held nothing moves and no repeats pile up for
//         when one is released; held, a direction repeats: from
//         tuning::kHoldDelaySeconds after its press at kInputCursorRepeatHz (8
//         files per second), the n-th repeat at press + delay + (n - 1) / 8 s,
//         exactly for any frame rate (the opening press repeats too, so holding
//         it scrolls); left / right still step;
//       - releasing X closes the pop-up. If a d-pad direction was pressed during
//         this hold (left/right, or up/down moving the cursor) and the cursor is
//         not on the loaded file, that file loads (through the memory guard
//         below; the stage crossfades to it). On the loaded file: a load asked
//         for before and still pending or waiting is cancelled (the last choice
//         wins), and a file that has ENDED in ONCE mode plays again. A hold without a d-pad press never loads or restarts. (If the
//         last rescan saw the loaded file with another size or modification
//         time than the load did, it counts as another file: it is reloaded.)
//       - X + Y: LOOP <-> ONCE (to the stage at once; logged). X + Select: play
//         the loaded file again from its start (restart(); a load asked for
//         since is cancelled in the loader with InputLoader::cancel()). Neither
//         counts as a d-pad press. ControlModel treats Y and Select presses as
//         state only; on INPUT no knob is bound, so Y + d-pad resets nothing
//         there, and while X is held the d-pad belongs to the choice here (not
//         the gain reset). Select held still means FINE for L2 / R2.
//       - L1 / R1 while X is held, or releaseAll(): the choice is cancelled, the
//         pop-up closes, nothing loads.
//     With the source OFF (or X pressed on another tab) X + any direction does
//     nothing here.
//   - Y + d-pad up / down: the gain back to unity (n = 0.5, 0 dB). Y + left/right
//     does nothing.
//   - Both directions of an axis down: no movement (the second press does nothing).
//   - L1 / R1 (a tab change) and pressing or releasing X make every d-pad
//     direction that is down inert until it is released, as in ControlModel (so
//     releasing X also stops a cursor repeat).
//
// The source and files:
//   - The source is OFF at start, the play mode LOOP (nothing is remembered
//     between launches).
//   - Selecting FILE rescans the folder; entering the INPUT tab rescans too.
//     After a rescan the cursor stays on the same name if it is still listed,
//     else on the nearest valid position (its old index, clamped to the list);
//     the first file when there was none.
//   - No automatic load (M2b-A2): selecting FILE with nothing loaded plays
//     nothing (the screen says HOLD X: CHOOSE FILE); selecting FILE with a file
//     loaded plays it from the start. A loaded file that is no longer listed
//     (renamed, deleted, or the folder emptied) keeps playing and is still shown
//     as the one playing; only another load replaces it. (M2b-A dropped the
//     loaded file when FILE was selected on an empty folder; with loading now a
//     deliberate press that rule went, so the rescan rule holds everywhere.)
//   - While a load is pending the loaded file keeps playing; choosing another file
//     replaces the pending request (the loader cancels the superseded one).
//   - Memory guard (M2b-A2 review, S2): no load starts while the stage holds more
//     than its playing buffer (a crossfade running, an old buffer held for a full
//     retire queue, or a buffer handed over and not yet picked up). The request
//     is kept as "wanted" (shown as LOADING <name>; a newer choice replaces it
//     and cancels a load in progress) and tick() starts it once the stage has
//     settled (InputStage::settledSwitches()). Every load start (from tick() and
//     from a button) first reads whether the stage has settled, then drains
//     and frees the retire queue, then starts: the stage pushes a retired buffer
//     before it publishes the settled count, so a settled stage's old buffer is
//     always freed before the load begins.
//     So at most two 10-minute buffers (115.2 MB each) are alive at any time:
//     the playing one and either the one loading or, during the 100 ms
//     crossfade, the old one. (The stage must be pulled for this to settle; the
//     app's audio callback does that.)
//     When the new file is ready it is handed over and the stage crossfades to it
//     (or starts it, if nothing sounds); a new buffer always starts at its start.
//   - A load that fails leaves everything as it was (the loaded file keeps
//     playing) and shows the reason until the next load request, a successful
//     load, or a rescan that no longer lists the failed file.
//   - Each finished load is handed to the stage at once (also if the source was
//     switched to OFF meanwhile; it then waits silently).
//
// tick() (every UI frame): the gain ramp, the cursor repeat, one poll of the
// loader, reclaiming and freeing the buffers the stage has finished with, and
// the gain to the stage.
//
// Log lines (source changes, play mode changes, restarts, every scan with its
// time and entry count, each load with its details, failures, and MemAvailable
// from /proc/meminfo after every finished load where that file exists) are
// collected for the app's diagnostic log; takeLog() hands them over.
//
// Exceptions (M2b-A review): no public function throws. Each catches everything
// at its boundary: a failed folder scan (for example out of memory) leaves the
// list empty with "CANNOT READ FOLDER"; a request the loader cannot take (the
// thread would not start, out of memory) is a load failure ("CANNOT START
// LOADER"); anything else is a failure "OUT OF MEMORY". The state stays
// consistent and visible, the stage keeps playing what it has, and a log line is
// written if that is still possible. For tests the folder scanner and the
// loader's thread start can be replaced.
//
// UI thread only (the loader thread is internal).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app/Buttons.h"
#include "input/InputFiles.h"
#include "input/InputLoader.h"
#include "input/InputStage.h"
#include "input/InputTypes.h"
#include "input/InputView.h"

namespace audreybrick
{

class InputController
{
  public:
    using ScanFn = InputFolderScan (*)(const std::string& folder);

    // `folder`: the input folder ("" = none). Sets the stage to OFF and unity gain.
    // `scan` and `start` (tests): the folder scanner (default scanInputFolder) and
    // the loader's thread start (default std::thread).
    InputController(InputStage& stage, std::string folder, ScanFn scan = nullptr,
                    InputLoader::ThreadStart start = nullptr);
    ~InputController();
    InputController(const InputController&)            = delete;
    InputController& operator=(const InputController&) = delete;

    // A button event, after the ControlModel has handled it. `onInputTab`: the
    // INPUT tab is current (after the event).
    void onButton(const ButtonEvent& e, double t, bool onInputTab) noexcept;
    // The INPUT tab has just become current: rescan.
    void onTabEntered() noexcept;
    // As if every button had been released (focus loss, controller removed).
    void releaseAll() noexcept;
    void tick(double t) noexcept;
    // Stops the loader thread (joins it). The destructor calls it.
    void stop() noexcept;

    // Everything but the peak.
    void fill(UiInputView& v) const noexcept;
    // One log line "input: MemAvailable <n> kB (<when>)" if /proc/meminfo (or the
    // path set for tests) has it; nothing otherwise. The app calls it at start-up.
    void logMemory(const char* when) noexcept;
    void setMemInfoPath(std::string path) noexcept { memInfoPath_ = std::move(path); } // tests
    // Log lines since the last call (appended to `out`; may throw std::bad_alloc).
    void takeLog(std::vector<std::string>& out);

    // --- State (tests, the self-test report) ---------------------------------------------
    InputSource                     source() const noexcept { return source_; }
    float                           gainNorm() const noexcept { return gain_; }
    const std::string&              folder() const noexcept { return folder_; }
    const std::vector<std::string>& files() const noexcept { return files_; }
    bool                            loaded() const noexcept { return loaded_; }
    const InputFileInfo&            loadedInfo() const noexcept { return loadedInfo_; }
    InputLoadState                  state() const noexcept;
    // The file the load status names: the one loading or failed, else the loaded one.
    const std::string&              shownName() const noexcept;
    int                             cursor() const noexcept { return cursor_; }
    bool                            choosing() const noexcept { return choosing_; } // X held, choosing
    bool                            popupOpen() const noexcept { return popup_; }
    const std::string&              cursorName() const noexcept { return cursorName_; }
    InputPlayMode                   playMode() const noexcept { return mode_; }
    bool                            loadPending() const noexcept { return pendingId_ != 0 || wanted_; }
    bool                            loadWaiting() const noexcept { return wanted_; } // for the stage to settle
    const char*                     reason() const noexcept { return reason_; }
    const char*                     folderError() const noexcept { return folderError_; }
    // Buffers freed on this (the UI) thread: returned by the stage after use, or
    // superseded in its pending slot.
    std::uint64_t buffersFreed() const noexcept { return freed_; }
    const InputLoader& loader() const noexcept { return loader_; }

  private:
    struct Press
    {
        bool        down    = false;
        bool        inert   = false;
        bool        cursor  = false; // X + up/down: repeats move the cursor
        std::size_t repeats = 0;     // cursor repeats made since the press
        double      time    = 0.0;
    };
    static std::size_t idx(Button b) noexcept { return static_cast<std::size_t>(b); }
    bool down(Button b) const noexcept { return press_[idx(b)].down; }

    void handleButton(const ButtonEvent& e, double t, bool onInputTab);
    void handleTick(double t);
    void makeDpadInert() noexcept;
    void setSource(InputSource s);
    void moveCursor(long delta);
    void beginChoice();
    void cancelChoice() noexcept;
    void endChoice();
    void playAgain();
    bool loadedEnded() const noexcept;
    void toggleMode() noexcept;
    void rescan() noexcept;
    void selectFileSource();
    void requestLoad(std::size_t index);
    void startLoad(std::string name);
    bool stageBusy() const noexcept;
    bool settledAndDrained() noexcept; // !stageBusy() read first, then reclaim() and free
    void handleResult(InputLoadResult& r);
    void freeBuffer(std::unique_ptr<InputBuffer> b) noexcept;
    void setGain(float n) noexcept;
    int  indexOf(const std::string& name) const noexcept;
    void log(const std::string& line) noexcept;
    void log(const char* line) noexcept;
    void logf(const char* fmt, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    void setReason(const char* r) noexcept;
    // An exception reached a public function: a visible failure, nothing thrown.
    void failSafe(const char* reason) noexcept;

    InputStage&  stage_;
    std::string  folder_;
    ScanFn       scan_;
    InputLoader  loader_;
    Press        press_[kButtonCount];
    double       lastTick_ = 0.0;
    bool         ticked_   = false;

    InputSource              source_ = InputSource::Off;
    float                    gain_   = kInputGainUnityNorm;
    InputPlayMode            mode_   = InputPlayMode::Loop;
    bool                     choosing_    = false; // X held on INPUT with the source FILE
    bool                     popup_       = false; // the pop-up list is open
    bool                     choiceMoved_ = false; // a d-pad press during this hold
    std::vector<std::string> files_;
    std::size_t              filesFound_   = 0;
    bool                     folderOk_     = false;
    const char*              folderError_ = ""; // static strings only
    bool                     scanned_      = false;
    int                      cursor_       = -1; // in files_
    std::string              cursorName_;
    std::uint64_t            pendingId_    = 0;  // the latest request (0 = none)
    std::string              pendingName_;       // its file
    bool                     wanted_       = false; // a load waiting for the stage to settle
    std::string              wantedName_;
    bool                     offered_      = false; // a buffer handed over ...
    std::uint64_t            expectSwitch_ = 0;     // ... becomes current at this switch count
    std::vector<InputFileStamp> stamps_;            // parallel to files_
    bool                     failed_       = false; // the last load failed (until the next request or load)
    std::string              failedName_;
    bool                     loaded_       = false;
    std::string              loadedName_;
    int                      loadedIndex_  = -1; // in files_, -1 if not listed
    InputFileInfo            loadedInfo_;
    char                     reason_[32] = {}; // fixed, so setting it never throws
    std::uint64_t            freed_        = 0;
    std::string              memInfoPath_  = "/proc/meminfo";
    std::vector<std::string> log_;
};

} // namespace audreybrick
