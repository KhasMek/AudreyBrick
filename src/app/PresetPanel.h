// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PresetPanel: the UI-thread model of the PRESET tab and of Start + A
// (PRESET-B, docs/briefs/PRESET.md). It owns the tab's cursor, the mark, the
// holds and the messages, and drives the preset store (preset/PresetStore.h)
// and the capture / apply seam (preset/PresetState.h). No SDL. Time is passed
// in (seconds, monotonic), as in ControlModel and ModPanel: `t` is the clock
// the button events carry (the self-test's audio clock), `wall` the wall clock
// (for the save wait and the messages, which must run out even when audio does
// not advance). Like the INPUT and MOD panels it binds no knob: InputRouter
// sends it every button event after the ControlModel (which binds nothing on
// PRESET), the INPUT panel and the MOD panel.
//
// The list: item 0 is SAVE NEW, item i + 1 is the store's entry i (sorted by
// name, PresetStore.h). The store is scanned when the tab is entered, after
// every action, and once at start (scanAtStart), never every frame.
//
// Buttons (presses made while the PRESET tab is current act; a press made on
// another tab never does):
//   - d-pad up / down: the cursor, one item per press, wrapping at both ends
//     (up from SAVE NEW goes to the last entry in the list). Held,
//     a direction repeats from tuning::kHoldDelaySeconds after its press at
//     kCursorRepeatHz (8 per second, as the INPUT tab's lists), at most one
//     step per tick (a stalled UI drops the backlog); a repeat stops at either
//     end instead of wrapping (only a new press wraps). Both held: no
//     movement, no backlog. An up / down press during a hold cancels it.
//   - d-pad right (no modifier): on SAVE NEW saves a new preset; on a preset
//     loads it. Acts on the press; holding right does nothing more.
//   - X held + right held kHoldSeconds (1 s) on a preset: overwrites it with
//     the current state. Y held + right held 1 s: deletes it. The hold starts
//     when right is pressed while X (or Y) is already held; its progress is
//     shown; at 1 s the action runs once and right becomes inert until
//     released. Releasing right early, pressing or releasing X or Y, an up /
//     down press, L1 / R1 or releaseAll() cancel it and nothing happens.
//     X + right and Y + right on SAVE NEW do nothing; with X and Y both held
//     right does nothing; plain right never acts while X or Y is held.
//   - d-pad left, Select, B, A: nothing here (A and B keep their functions in
//     the model; Select has no meaning on this tab).
//   - Modifier rules (as MOD-D): X or Y pressed on another tab does not count:
//     right does nothing until it is released and pressed again here. L1 / R1
//     and releaseAll() drop the X and Y states and make every d-pad direction
//     that is down inert until released; so does pressing or releasing X or Y.
//   - Start + A (InputRouter, the chord in app/RecordChord.h) on any tab:
//     requestSaveNew(); it also cancels a hold in progress (right stays inert
//     until released).
//
// Actions run in tick(), one at a time, synchronously on the UI thread (a
// preset is a few hundred bytes; the slowest card write measured on the Brick
// was 28 ms: a frame or two, never audio). While one is pending (a save
// waiting for the audio side) further actions (a completed hold, a load, a
// second save) are ignored and logged, and the next tick shows BUSY, TRY AGAIN
// (a failure, kBadMessageSeconds) unless that tick's action failed (its own
// failure explains it). If the waiting save is still waiting then, its later
// success does not replace BUSY (its failure does). A save that completes
// (or fails) cancels an overwrite / delete hold in progress (the highlight may
// have moved; right is inert until released).
//   - Load: PresetStore::load(), then applyPreset() (ControlModel::setAll and
//     ModPanel::setSlot: the same paths a user's change takes). A file that
//     fails changes nothing. The audio input is never touched.
//   - Save new / overwrite: the values must be the ones the audio side has
//     applied. The panel notes ControlModel::commandsDelivered() when the
//     action is asked for and waits until ButtonControlSource::
//     commandsProcessed() has reached it (normally one audio buffer), then
//     captures (capturePreset) and writes. If that has not happened within
//     kSaveWaitSeconds of wall clock (counted from the first tick after the
//     request, so a stalled frame does not eat the wait), the save fails with
//     SAVE FAILED: AUDIO NOT RUNNING and nothing is written (never stale
//     values). The UI thread never blocks: tick() checks once per frame.
//   - Delete: PresetStore::remove().
// After a load or save the preset is marked (by file name) until another is
// loaded or saved; deleting the marked file clears the mark. After a save the
// new (or overwritten) preset is highlighted; after a delete the next entry is
// (the previous at the end, SAVE NEW when none is left). The cursor otherwise
// stays on the same file across rescans (or the same place if it has gone).
//
// Messages (upper case): LOADED <NAME>, SAVED <NAME>, OVERWROTE <NAME>,
// DELETED <NAME> (kGoodMessageSeconds, 2 s); LOAD FAILED: <REASON>, SAVE
// FAILED: <REASON>, OVERWRITE FAILED: <REASON>, DELETE FAILED: <REASON>
// (kBadMessageSeconds, 4 s). The same text goes to the tab's message line and
// to the header (shown on the other tabs; presetHeaderWins() ranks it against
// the recorder's readout).
//
// Log (takeLog(), for the app's diagnostic log): the folder and its count at
// start; one line per load, save, overwrite and delete with the file, and per
// failure with the file and the reason and its detail; a request ignored
// while busy. Save, overwrite and delete lines (and their store failures) end
// with "; file work N.N ms": the store call's duration on the UI thread
// (steady clock), so a device run shows what the write and fsync cost.
//
// No allocation except in the store's operations, the scans and the log lines
// (all in tick() / onTabEntered() / scanAtStart(), each guarded: an
// allocation failure is reported as a failed action). UI thread only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app/Buttons.h"
#include "app/ControlModel.h"
#include "app/ModPanel.h"
#include "app/PresetView.h"
#include "control/ButtonControlSource.h"
#include "preset/PresetStore.h"
#include "record/RecordView.h"

namespace audreybrick
{

// PRESET-B: whether the preset message (`preset`, appeared at `presetSince`)
// is shown in the header instead of the recorder's readout (`record`, its
// message appeared at `recordSince`; wall clock). The rule:
//   1. no preset message: no;
//   2. the recorder shows nothing, or only its running REC m:ss / REC SAVING
//      (the red border still says that a recording runs): yes;
//   3. a failure beats a success (the recorder's messages other than SAVED
//      are failures or REC BUSY, counted as failures);
//   4. two of the same kind: the newer one (a tie: the recorder's).
bool presetHeaderWins(RecordShow record, double recordSince, PresetMessageView preset, double presetSince) noexcept;

class PresetPanel
{
  public:
    static constexpr double kHoldSeconds        = 1.0;
    static constexpr double kCursorRepeatHz     = 8.0;
    static constexpr double kSaveWaitSeconds    = 0.5;
    static constexpr double kGoodMessageSeconds = 2.0;
    static constexpr double kBadMessageSeconds  = 4.0;

    enum class Action : std::uint8_t
    {
        None,
        Load,
        SaveNew,
        Overwrite,
        Delete,
    };
    // Where a save new came from (for the log).
    enum class Origin : std::uint8_t
    {
        Tab,   // right on SAVE NEW
        Chord, // Start + A
        Hook,  // the app (tests)
    };

    PresetPanel(PresetStore& store, ControlModel& model, const ButtonControlSource& source, ModPanel& mod) noexcept;

    // A button event, after the ControlModel, the INPUT and the MOD panel.
    // `onPresetTab`: the PRESET tab is current (after the model saw the event).
    void onButton(const ButtonEvent& e, double t, bool onPresetTab) noexcept;
    // The PRESET tab became current: the folder is scanned again.
    void onTabEntered() noexcept;
    // As if every button had been released (focus loss, controller removed).
    void releaseAll() noexcept;
    // Start + A: save a new preset (from any tab).
    void requestSaveNew(Origin origin = Origin::Chord) noexcept;
    // Every UI frame: the cursor repeat, a hold reaching its time, the pending
    // action (the save wait), message expiry.
    void tick(double t, double wall) noexcept;
    // The first scan, with its log line (the app calls it once at start).
    void scanAtStart() noexcept;

    void fill(UiPresetView& v, double wall) const noexcept;
    // Log lines since the last call (appended to `out`; may throw std::bad_alloc).
    void takeLog(std::vector<std::string>& out);

    // --- State (tests, the self-test report) -----------------------------------
    int                cursor() const noexcept { return cursor_; } // 0 SAVE NEW, i + 1 entry i
    const std::string& marked() const noexcept { return marked_; } // file name, "" if none
    bool               pending() const noexcept { return pending_.action != Action::None; }
    PresetHoldView     hold() const noexcept { return holdKind_; }
    const std::string& lastMessage() const noexcept { return message_; }
    bool               lastMessageBad() const noexcept { return messageBad_; }
    const PresetStore& store() const noexcept { return store_; }
    // Completed actions and failures (every kind) since start.
    std::uint64_t loads() const noexcept { return loads_; }
    std::uint64_t saves() const noexcept { return saves_; } // new presets
    std::uint64_t overwrites() const noexcept { return overwrites_; }
    std::uint64_t deletes() const noexcept { return deletes_; }
    std::uint64_t failures() const noexcept { return failures_; }
    std::uint64_t ignoredBusy() const noexcept { return ignored_; }

  private:
    struct Press
    {
        bool   down    = false;
        bool   inert   = false;
        double time    = 0.0;
        long   repeats = 0; // up / down: repeats made since the press
    };
    struct Pending
    {
        Action      action = Action::None;
        Origin      origin = Origin::Tab;
        std::string file;               // Load, Overwrite, Delete
        std::uint64_t target = 0;       // SaveNew, Overwrite: commands delivered at the request
        bool        deadlineSet = false;
        double      deadline    = 0.0;  // wall clock
    };
    static std::size_t idx(Button b) noexcept { return static_cast<std::size_t>(b); }
    bool               down(Button b) const noexcept { return press_[idx(b)].down; }
    bool               xHeld() const noexcept { return down(Button::X) && xArmed_; }
    bool               yHeld() const noexcept { return down(Button::Y) && yArmed_; }
    int                itemCount() const noexcept;
    std::string        fileAt(int item) const; // "" for SAVE NEW / out of range

    void makeDpadInert() noexcept;
    void cancelHold() noexcept;
    void moveCursor(int dir, bool wrap) noexcept;
    void onRight(double t) noexcept;
    void queue(Action a, Origin o, const std::string& file) noexcept;
    void run(double wall) noexcept;
    void runLoad(double wall);
    void runSave(double wall);
    void runDelete(double wall);
    void rescanKeepCursor(const std::string& file, int fallback);
    void setMessage(bool bad, const std::string& text, double wall) noexcept;
    void log(const std::string& line) noexcept;

    PresetStore&               store_;
    ControlModel&              model_;
    const ButtonControlSource& source_;
    ModPanel&                  mod_;

    Press          press_[kButtonCount];
    bool           xArmed_ = false; // X held, pressed on the PRESET tab
    bool           yArmed_ = false;
    int            cursor_ = 0;
    std::string    marked_;
    PresetHoldView holdKind_  = PresetHoldView::None;
    double         holdStart_ = 0.0;
    std::string    holdFile_;
    char           holdName_[kPresetNameBytes] = {}; // its display name, upper case (for fill())
    Pending        pending_;
    double         now_      = 0.0; // the latest `t` seen
    double         lastTick_ = 0.0;
    bool           ticked_   = false;
    bool           busyDrop_ = false; // an action was dropped while busy: BUSY at the next tick
    bool           busyHold_ = false; // BUSY shown while the save still waits: its success keeps BUSY

    std::string message_;
    bool        messageBad_   = false;
    double      messageSince_ = 0.0;
    double      messageUntil_ = -1.0;

    std::uint64_t loads_ = 0, saves_ = 0, overwrites_ = 0, deletes_ = 0, failures_ = 0, ignored_ = 0;
    std::vector<std::string> log_;
};

} // namespace audreybrick
