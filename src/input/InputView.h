// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// UiInputView: what the screen shows about the input (the INPUT tab and the IN
// indicator), part of UiSnapshot (app/UiSnapshot.h). Plain data, fixed size, no
// allocation to copy or fill. Filled by InputController::fill() on the UI thread,
// plus the peak from the stage. M2b-A2 added the file browser (a window of
// kInputListRows names around the cursor, the loaded file's place), the play
// mode and the playback position. M2b-B added the live input: with the source
// LIVE the list fields (cursor, listFirst, listRows, listNames, loadedIndex)
// describe the capture devices instead of the files, and the live fields below
// say what is open.

#pragma once

#include <cstddef>
#include <cstdint>

#include "input/InputTypes.h"

namespace audreybrick
{

struct UiInputView
{
    InputSource source   = InputSource::Off;
    float       gainNorm = kInputGainUnityNorm; // knob position 0..1 (gain = (2n)^2)
    float       gainDb   = 0.0f;                 // -infinity at 0

    // The file list (as of the last scan).
    int  fileCount     = 0;     // files listed (at most kInputMaxFiles)
    bool fileListCut   = false; // the folder holds more usable files than that
    bool folderMissing = false; // the folder could not be listed (missing, not a folder, unreadable)
    char folderError[24] = {};  // why ("NO FOLDER", "NOT A FOLDER", "CANNOT READ FOLDER"), "" if listed

    // The file named by the load status (and by the FILE row with the source
    // OFF): the one being loaded, the one that failed, or else the one loaded;
    // "" if none. File names are display text: every byte outside printable
    // ASCII is shown as "?" (one per UTF-8 character; see copyDisplayName below),
    // since the font covers ASCII 32 to 126 only. (listNames and playingName too.)
    char fileName[96] = {};
    int  fileIndex    = -1; // its position in the list (0-based), -1 if not in it

    // M2b-A2/A3: the cursor (the file being chosen, separate from the loaded
    // one) and a window of up to kInputListRows names that scrolls with it, for
    // the pop-up list: listNames[i] is file listFirst + i, for i < listRows. The
    // cursor is always inside the window.
    int  cursor    = -1; // 0-based, -1 if the list is empty
    int  listFirst = 0;
    int  listRows  = 0;
    char listNames[kInputListRows][96] = {};
    int  loadedIndex = -1; // the loaded file's position in the list, -1 if none or not listed
    bool choosing    = false; // M2b-A3: X is held on INPUT with the source FILE (choosing a file)
    bool popup       = false; // M2b-A3: the pop-up list is open

    // The loaded file (what plays while the source is FILE).
    bool  loaded         = false;
    bool  shownIsLoaded  = false; // fileName is the loaded file
    char  playingName[96] = {};   // the loaded file's name (what sounds while the source is FILE), "" if none
    float loadedSeconds  = 0.0f;  // its length at 48 kHz (after the cut)
    bool  loadedCut      = false; // cut at kInputMaxSeconds
    bool  loadedEndedEarly = false; // the file's data ended before its header said
    // M2b-A2: playback.
    InputPlayMode playMode        = InputPlayMode::Loop;
    bool          ended           = false; // ONCE: the loaded file has played to its end
    float         positionSeconds = 0.0f;  // where the stage reads in the loaded file

    InputLoadState state      = InputLoadState::Idle;
    char           reason[32] = {}; // Failed: the short reason ("NOT A WAV FILE")

    // M2b-B: the live input (the source LIVE). listNames[i] are device names,
    // loadedIndex is the open device's position (as listed), cursor the chooser's.
    int        deviceCount      = 0;     // devices listed, SYSTEM DEFAULT included (when offered)
    bool       deviceListFailed = false; // the capture system could not list its devices
    bool       deviceOpen       = false;
    char       deviceName[96]   = {};    // the open device as listed ("SYSTEM DEFAULT"), "" if none
    int        grantedRate      = 0;     // what the open device delivers (as opened)
    int        grantedChannels  = 0;
    int        grantedFrames    = 0;
    bool       deviceDirect     = false; // M2b-B2: opened on the direct route (hw:<card>,0)
    LiveStatus liveStatus       = LiveStatus::None;
    char       liveFailName[96] = {};    // Failed: the device that could not be opened; Lost: the one lost
    char       liveReason[64]   = {};    // Failed: the capture system's reason
    bool       liveRunning      = false; // the stage plays the ring (it had buffered to its target)
    // The pre-gain level (the mono mix as captured) since the previous snapshot.
    float prePeak = 0.0f;

    // The input level: the largest |in| (after gain) since the previous snapshot.
    float peak = 0.0f;
};

// The name the device list shows for the capture system's default device.
inline constexpr const char* kLiveDefaultDeviceName = "SYSTEM DEFAULT";

// Copies `src` (`n` bytes, or up to its NUL if n is SIZE_MAX) into `dst` for
// display: printable ASCII as it is; every UTF-8 character outside it (a lead
// byte and its continuation bytes), every stray byte above 126 and every control
// character becomes one "?". Always NUL-terminated, cut at `size` - 1. No allocation.
inline void copyDisplayName(char* dst, std::size_t size, const char* src, std::size_t n = SIZE_MAX) noexcept
{
    if(dst == nullptr || size == 0)
        return;
    std::size_t o = 0;
    for(std::size_t i = 0; src != nullptr && i < n && src[i] != '\0' && o + 1 < size; ++i)
    {
        const auto c = static_cast<unsigned char>(src[i]);
        if(c >= 32 && c <= 126)
        {
            dst[o++] = static_cast<char>(c);
            continue;
        }
        dst[o++] = '?';
        if(c >= 0xC0) // a UTF-8 lead byte: skip its continuation bytes
            while(i + 1 < n && (static_cast<unsigned char>(src[i + 1]) & 0xC0u) == 0x80u)
                ++i;
    }
    dst[o] = '\0';
}

} // namespace audreybrick
