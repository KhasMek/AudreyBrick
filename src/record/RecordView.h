// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// UiRecordView: what the screen shows about recording (docs/briefs/REC.md).
// Plain data in the UiSnapshot, filled by App once per frame; no allocation.
//
//   border  the 4-pixel red frame round the whole screen: while recording or
//           finalising.
//   show    what the header's readout says (ui/Layout.cpp formats it):
//             None        nothing
//             Recording   REC m:ss (h:mm:ss from an hour), from `frames` / `sampleRate`,
//                         and DROP <n> after it when `dropped` > 0
//             Saving      REC SAVING (the file is being finalised)
//             Saved       SAVED <NAME> m:ss, `name` and `seconds` (2 s after a stop)
//             Message     `message`, a static string (REC: CARD FULL, REC BUSY, ...)

#pragma once

#include <cstdint>

namespace audreybrick
{

enum class RecordShow : std::uint8_t
{
    None,
    Recording,
    Saving,
    Saved,
    Message,
};

struct UiRecordView
{
    bool          border     = false;
    RecordShow    show       = RecordShow::None;
    std::uint64_t frames     = 0; // Recording: frames recorded so far
    std::uint64_t dropped    = 0; // Recording: frames dropped (ring full)
    int           sampleRate = 48000;
    char          name[24]   = {}; // Saved: the file name, e.g. "rec-0003.wav"
    double        seconds    = 0.0; // Saved: its length
    const char*   message    = "";  // Message: a static string, never null
};

} // namespace audreybrick
