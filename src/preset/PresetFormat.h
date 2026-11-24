// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A (docs/briefs/PRESET.md): the preset file format, version 1, as text.
// The format and every reading rule are documented in docs/PRESET_FORMAT.md;
// this header only summarises. No I/O (preset/PresetStore.h reads and writes
// files), no SDL. UI thread or tools (it allocates).
//
// encodePreset(): the canonical text: LF line endings, two-space indent, the
//   keys in a fixed order, every float with "%.9g" in the classic locale (so
//   reading returns the identical float), the canonical names (shape and
//   polarity in lower case, the target as modTargetName()). The values are
//   sanitised first (sanitizePreset), so the text always reads back.
//
// decodePreset(): parse completely, then build the whole PresetData from
//   presetDefaults() plus what the file says; the caller applies it only on
//   success, so nothing is applied from a file that fails.
//   - required: "format": "audreybrick-preset" and "version": an integer 1
//     (2 or more: "made by a newer version"; 0, negative or fractional: bad);
//   - optional: "app" (a string, ignored), "knobs" (an object), "delaySwitch"
//     (a boolean), "mod" (an array of up to four slot objects; elements after
//     the fourth are ignored);
//   - a missing knob, switch, slot or slot field keeps its start-up default;
//   - unknown keys are ignored at every level;
//   - knob names are matched without regard to case (as in scripts); two keys
//     naming the same knob ("Frequency" and "frequency") are an error, and so
//     is any key repeated exactly in one object (the JSON reader's rule);
//     every other key is matched exactly;
//   - numbers are clamped: knobs and rate to 0..1, depth to -1..+1 (then the
//     slot is sanitised: a depth below 1e-6 in size is 0);
//   - shape, target and polarity accept every name ModTypes.h accepts
//     (case-insensitive, with its aliases); any other name is an error;
//   - a wrong type anywhere the format defines a key is an error;
//   - more than kMaxPresetBytes (64 KiB) of text is an error.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "preset/PresetData.h"

namespace audreybrick
{

constexpr const char* kPresetFormatMarker = "audreybrick-preset";
constexpr int         kPresetFormatVersion = 1;
constexpr std::size_t kMaxPresetBytes      = 64 * 1024;

// Why something failed: `reason` is short, for the screen ("syntax error",
// "not a preset", "made by a newer version", "bad value", "too large",
// "unreadable", "not found", "folder missing", "card full", ...); `detail` is
// the full story for the log ("line 3, column 7: expected ':'",
// "knobs.Frequency is not a number", "rename: Read-only file system").
struct PresetError
{
    std::string reason;
    std::string detail;
};

// `appVersion`: written as "app" (informational; buildVersion() in the app).
std::string encodePreset(const PresetData& p, std::string_view appVersion);

// True and `out` filled on success; false and `error` filled otherwise (`out`
// unchanged). No exception escapes.
bool decodePreset(std::string_view text, PresetData& out, PresetError& error) noexcept;

} // namespace audreybrick
