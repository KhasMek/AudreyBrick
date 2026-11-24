// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// UiPresetView: what the screen shows about presets (PRESET-B,
// docs/briefs/PRESET.md), part of UiSnapshot (app/UiSnapshot.h). Plain data,
// fixed size, no allocation to copy or fill. Filled once per UI frame by
// PresetPanel::fill() (app/PresetPanel.h).
//
//   - The PRESET tab: the folder's state, the list window (SAVE NEW is item 0,
//     preset i is item i + 1), the cursor, the marked item (the preset loaded
//     or saved last), a hold in progress (X or Y + right held) with what it
//     will do, and the tab's message line.
//   - The header line on the other tabs (where the recorder's readout is):
//     the latest preset message while it is fresh (`header`), and when it
//     appeared (`headerSince`, wall clock) so the app can rank it against the
//     recorder's (App::fillSnapshot; the layout shows what `headerWins` says).
// Names are display names (copyDisplayName: printable ASCII, anything else
// "?"), cut at kPresetNameBytes - 1 bytes; the layout cuts them to fit with "..".

#pragma once

#include <cstddef>
#include <cstdint>

namespace audreybrick
{

constexpr int         kPresetListRows  = 5;  // list rows on the tab (SAVE NEW included)
constexpr std::size_t kPresetNameBytes = 64; // a name with its NUL
constexpr std::size_t kPresetTextBytes = 80; // a message with its NUL

enum class PresetFolderView : std::uint8_t
{
    NotSet,     // no --preset-dir
    Ok,
    Missing,    // the folder does not exist (SAVE NEW creates it)
    Unreadable, // not a folder, or it cannot be listed
};

enum class PresetHoldView : std::uint8_t
{
    None,
    Overwrite, // X + right held on a preset
    Delete,    // Y + right held on a preset
};

enum class PresetMessageView : std::uint8_t
{
    None,
    Good, // LOADED .., SAVED .., OVERWROTE .., DELETED ..
    Bad,  // LOAD FAILED: .., SAVE FAILED: .., ..
};

struct UiPresetView
{
    PresetFolderView folder = PresetFolderView::NotSet;
    int  count     = 0;     // presets listed (at most PresetStore::kMaxListed)
    int  total     = 0;     // presets found (more than count when the list is cut)
    int  cursor    = 0;     // the highlighted item: 0 SAVE NEW, i + 1 preset i
    int  first     = 0;     // the first item in the window
    int  rows      = 1;     // items in the window (1 .. kPresetListRows; SAVE NEW at least)
    int  marked    = -1;    // the marked item (loaded or saved last), -1 if none is listed
    bool busy      = false; // a save waits for the audio side to catch up (a frame or two)
    // names[i]: item first + i ("" for SAVE NEW, which the layout draws itself).
    char names[kPresetListRows][kPresetNameBytes] = {};

    // A hold in progress (X or Y with right held on a preset).
    PresetHoldView hold         = PresetHoldView::None;
    float          holdProgress = 0.0f; // 0..1 (the action fires at 1)
    char           holdName[kPresetNameBytes] = {}; // the preset it acts on, upper case

    // The tab's message line (fresh for 2 s, a failure for 4 s).
    PresetMessageView message = PresetMessageView::None;
    char              messageText[kPresetTextBytes] = {};

    // The same message for the header on the other tabs, and its start (wall
    // clock seconds). `headerWins`: set by the app when it is to be shown
    // instead of the recorder's readout (PresetPanel.h, presetHeaderWins()).
    PresetMessageView header      = PresetMessageView::None;
    char              headerText[kPresetTextBytes] = {};
    double            headerSince = 0.0;
    bool              headerWins  = false;
};

} // namespace audreybrick
