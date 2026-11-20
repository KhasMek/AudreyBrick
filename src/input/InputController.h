// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// InputController: the UI-thread model of the INPUT tab (docs/briefs/M2b.md,
// M2b-A to M2b-A3 and M2b-B (the live input, below), and
// docs/CONTROL_SCHEME_PROPOSAL.md section 000). Button
// events and time go in; the source, gain, play mode and restarts go to the
// InputStage through its atomics, file loads go to the InputLoader, finished
// buffers are handed to the stage and old ones freed here. No SDL. Time is
// passed in by the caller (seconds, monotonic).
//
// Buttons (only presses made while the INPUT tab is current act; InputRouter
// sends every button event here after the ControlModel has seen it):
//   - d-pad left / right: the source, OFF, FILE, LIVE (M2b-B), one step per press, no wrap,
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
//     With the source LIVE the same gestures choose a capture device (see "The
//     live input" below). With the source OFF (or X pressed on another tab) X + any direction does
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
// The live input (M2b-B; source LIVE, docs/briefs/M2b.md "M2b-B"):
//   - setLive() gives the controller the capture ring (LiveInput) and a capture
//     system (LiveBackend: SDL's in the app, a simulated one in its self-test, a
//     fake in the tests). Without them LIVE lists only SYSTEM DEFAULT and every
//     open fails with NO CAPTURE SUPPORT.
//   - The device list is SYSTEM DEFAULT (the backend's default device, SDL's
//     NULL) followed by every name the backend lists (at most kLiveMaxDevices
//     entries in all). M2b-B2: SYSTEM DEFAULT only if the backend offers it
//     (LiveBackend::offersDefaultDevice(); not with SDL's alsa driver, see
//     input/CaptureRoute.h); then the list is the listed names only, possibly
//     empty (X then chooses nothing). setLive() logs which rule applies. It is listed again when LIVE is selected, when the INPUT
//     tab is entered, when X is pressed with the source LIVE, and in tick()
//     whenever the backend's listChanges() has grown (SDL_AUDIODEVICEADDED /
//     REMOVED for capture devices). A list that changed is logged in full. After
//     a listing the cursor stays on the same name, else on the nearest position.
//   - Choosing a device: exactly as choosing a file (hold X, left/right step,
//     up/down open the pop-up and move, release X opens the device under the
//     cursor if a d-pad direction was pressed and it is not the open device; with
//     no device open X, up, release opens the cursor's device). X + Y and X +
//     Select do nothing with the source LIVE. Nothing opens by itself.
//   - Opening (howl safeguard): the input gain is set to 0 (-inf dB) first, then
//     the old device is closed and the ring's session advanced, then the backend
//     opens the device. So the stage never plays a sample of the new device
//     before it has faded to silence and buffered, and its gain then snaps to
//     the target. Every open (a first choice, another choice, or the reopen
//     below) does this. The first open of a device in a run stays at 0: the
//     player raises the gain with up/down (or Y + up/down, unity).
//   - Gain memory (GAIN, 2026-11-20; this run only, nothing is saved): the gain
//     is kept per source. FILE has one slot (start-up: unity); each capture
//     device one, keyed by its name as listed (exact comparison), at most
//     kLiveGainMemorySlots names (8; a ninth forgets the least recently opened).
//     A device gets its slot, at 0, on its first successful open. A later
//     successful open of the same name (after OFF or FILE, after another device,
//     after DEVICE LOST and a new choice) restores its gain once the open has
//     succeeded, after the zero above, and asks the stage for a 100 ms fade-in
//     (kLiveRestoreFadeFrames) for that session's start; logged as "gain
//     restored to ...". A failed open leaves the gain at 0 and no slot changed.
//     Every change the player makes (taps, ramps, Y + up/down) goes to the
//     current source's slot at once: FILE's, or the open device's. With the
//     source LIVE and no device open, or OFF, changes are not remembered.
//     Selecting FILE applies FILE's slot; selecting OFF leaves the gain as it
//     was; selecting LIVE with no device to reopen leaves it as it was too.
//     The stage holds the gain while the old source fades out, so the change
//     between sources is never heard (InputStage, "Fades").
//   - The device stays open while the source is LIVE. Leaving LIVE closes it (so
//     the mic is never open unseen); selecting LIVE again in the same run
//     reopens the same device (gain to 0 first, then its remembered gain) if it
//     is still listed.
//   - A failed open: CANNOT OPEN <name>: <the backend's reason> until the next
//     open or a source change; no device is chosen.
//   - A lost device (the backend's lost(): SDL removed or stopped it; or no frame
//     captured for kLiveStallSeconds): it is closed, DEVICE LOST shows, and no
//     device is chosen. What is heard: the capture callback stops writing as
//     soon as SDL marks the device stopped (platform/CaptureHost.h), so the ring
//     runs dry and the stage fades out over 10 ms from the last sample; if the
//     system delivers silence before SDL notices, that silence plays (the sound
//     can cut at the moment of unplugging); the close then fades out what is left.
//   - The stage doubles its live target after two underruns within a second,
//     or after one underrun when the settled fill had left no room for a repeat
//     (InputStage, "Live"); tick() logs each raise with the new target.
//   - Log lines: every listing that changed (all names), every open (the name
//     asked for, the spec asked for and obtained, the route, the time taken, or
//     the backend's error), every close with its reason, every gain reset to
//     zero, every gain restored (a device's, or FILE's when it differs), and
//     every loss; and (M2b-B2) the backend's own lines after each open
//     and close (LiveBackend::takeLog(): the route chosen, the mixer set and
//     restored). The open device's route shows on the screen as DIRECT when it
//     is the direct one (UiInputView::deviceDirect).
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
#include "input/LiveInput.h"

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
    // Stops the loader thread (joins it) and closes the live device. The
    // destructor calls it.
    void stop() noexcept;

    // M2b-B: the live input's ring and capture system (either may be nullptr).
    // Call before the first button event; both must outlive the controller (or
    // call stop() first, which closes the device).
    void setLive(LiveInput* live, LiveBackend* backend) noexcept;

    // Everything but the peaks.
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
    // M2b-B, the live input.
    const std::vector<std::string>& devices() const noexcept { return devices_; } // [0] SYSTEM DEFAULT (if offered)
    bool               offersDefault() const noexcept { return hasDefault_; }     // M2b-B2
    int                deviceCursor() const noexcept { return devCursor_; }
    bool               deviceOpen() const noexcept { return devOpen_; }
    const std::string& deviceOpenName() const noexcept { return devOpenName_; } // as listed
    int                deviceOpenIndex() const noexcept { return devOpenIndex_; }
    LiveStatus         liveStatus() const noexcept { return liveStatus_; }
    const char*        liveReason() const noexcept { return liveReason_; }
    std::uint64_t      deviceOpens() const noexcept { return opens_; } // open attempts
    const LiveOpenResult& lastOpen() const noexcept { return lastOpen_; }
    // GAIN: the gains remembered for this session. FILE's slot; and a device's
    // (by its name as listed): true and its gain if one is remembered.
    float       fileGainNorm() const noexcept { return fileGain_; }
    bool        rememberedGain(const std::string& name, float& gainNorm) const noexcept;
    std::size_t rememberedDevices() const noexcept; // names in the table (at most kLiveGainMemorySlots)

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

    struct ListRef
    {
        const std::vector<std::string>& names;
        int&                            cursor;
        std::string&                    cursorName;
    };
    ListRef activeList() noexcept; // the devices with the source LIVE, else the files

    void handleButton(const ButtonEvent& e, double t, bool onInputTab);
    // M2b-B.
    void listDevices() noexcept;
    void selectLiveSource();
    void beginDeviceChoice();
    void endDeviceChoice();
    void openDevice(int index);
    void closeDevice(const char* reason) noexcept;
    void deviceLost(const char* why) noexcept;
    void checkLive(double t) noexcept;
    int  indexOfDevice(const std::string& name) const noexcept; // among the system's names (1..)
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
    void setGain(float n) noexcept;     // the gain to the stage; remembers nothing
    void setUserGain(float n) noexcept; // a player's change: also the current source's slot (GAIN)
    int  findGainSlot(const std::string& name) const noexcept; // -1 if not remembered
    int  indexOf(const std::string& name) const noexcept;
    void log(const std::string& line) noexcept;
    void log(const char* line) noexcept;
    void logf(const char* fmt, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    void setReason(const char* r) noexcept;
    void setReasonLive(const char* r) noexcept;
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

    // M2b-B, the live input.
    LiveInput*               live_    = nullptr;
    LiveBackend*             backend_ = nullptr;
    std::vector<std::string> devices_;               // [0] SYSTEM DEFAULT (if offered), then the system's names
    bool                     devicesListed_  = false;
    bool                     devicesOk_      = true; // the last listing worked
    std::uint64_t            listSeen_       = 0;    // backend listChanges() at the last listing
    int                      devCursor_      = -1;
    std::string              devCursorName_;
    bool                     devOpen_        = false;
    bool                     hasDefault_     = true;  // M2b-B2: the list starts with SYSTEM DEFAULT
    bool                     devOpenDefault_ = false; // the open (or chosen) device is SYSTEM DEFAULT
    std::string              devOpenName_;            // as listed ("SYSTEM DEFAULT" for the default)
    int                      devOpenIndex_   = -1;
    bool                     devChosen_      = false; // reopen it when LIVE is selected again
    bool                     devChosenDefault_ = false;
    std::string              devChosenName_;
    LiveStatus               liveStatus_     = LiveStatus::None;
    std::string              liveFailName_;
    char                     liveReason_[64] = {};
    LiveOpenResult           lastOpen_;
    std::uint64_t            opens_          = 0;
    std::uint64_t            lastCaptured_   = 0;   // stall detection
    double                   lastCaptureT_   = 0.0;
    bool                     stallClock_     = false; // lastCaptureT_ not set yet
    std::uint64_t            raisesSeen_     = 0;     // the stage's target raises already logged

    // GAIN (2026-11-20): the gain per source, for this run only.
    struct GainSlot
    {
        bool          used   = false;
        std::string   name;       // the device's name as listed
        float         gain   = 0.0f;
        std::uint64_t opened = 0; // openSeq_ at its last successful open (least recent is forgotten)
    };
    float         fileGain_ = kInputGainUnityNorm; // FILE's slot (the start-up default)
    GainSlot      gainSlots_[kLiveGainMemorySlots];
    int           devSlot_  = -1; // the open device's slot (-1: none open)
    std::uint64_t openSeq_  = 0;  // successful opens
};

} // namespace audreybrick
